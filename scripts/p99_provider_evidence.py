"""Borrowed-process provider evidence; never a loader reference or PIN proof.

Expected bytes are supplied by a hash-checked Provider. Compare DOS/NT/section
metadata (explicitly excluding ImageBase), the complete export directory, and
32-byte selected entry prefixes. These are NOT full provider-body/dependency
verification. No image discovery, target loading, writing, protection, debugger,
PIN, or target function calls. GetMappedFileName is a path observation, not a
file-object identity certificate. ABA and post-return unload remain possible.
"""
import ctypes as ct
from dataclasses import dataclass
from itertools import islice
import struct

import pefile

from p99_wrist_abi_manifest import require, digest
from p99_wrist_import_graph import Provider, _section, read_provider, EXECUTE, WRITE, DISCARDABLE
from p99_wrist_mapped_image import CHUNK, PAGE_READONLY, PAGE_EXECUTE_READ
from p99_wrist_mapped_windows import Identity, Reader, WindowsAPI

PREFIX_BYTES, MAX_ENTRIES, PATH_CAPACITY = 32, 4096, 32768


@dataclass(frozen=True)
class Span:
    label: str
    rva: int
    expected: bytes
    protection: int


def native_path(value):
    # Exact spelling, no case folding or DOS/device alias guessing. A spelling
    # mismatch refuses; even a match is NOT filesystem object identity.
    require(isinstance(value, str) and value.startswith('\\Device\\') and
            8 < len(value) < PATH_CAPACITY and '/' not in value and
            not any(ord(c) < 32 for c in value) and
            all(part not in ('', '.', '..') for part in value[1:].split('\\')),
            'explicit native mapped-file path required')
    return value


def file_native_path(kernel, handle):
    """Expected native spelling from caller-owned file, NEVER observed mapping."""
    buffer = ct.create_unicode_buffer(PATH_CAPACITY)
    count = kernel.GetFinalPathNameByHandleW(handle, buffer, len(buffer), 2)  # VOLUME_NAME_NT
    require(0 < count < len(buffer) and len(buffer.value) == count, 'independent file path failed/truncated')
    return native_path(buffer.value)


def comparison_spans(provider, entry_rvas):
    require(isinstance(provider, Provider), 'provider type')
    checked = read_provider(provider.raw, provider.sha256, provider.name, provider.base)
    require(checked == provider, 'provider record differs from trusted bytes')
    entries = tuple(islice(entry_rvas, MAX_ENTRIES+1))
    require(0 < len(entries) <= MAX_ENTRIES and all(type(rva) is int for rva in entries) and
            len(set(entries)) == len(entries), 'entry count/type/duplicate')
    pe = pefile.PE(data=checked.raw, fast_load=True)
    nt = struct.unpack_from('<I', checked.raw, 0x3c)[0]
    header_end = nt+24+240+40*pe.FILE_HEADER.NumberOfSections
    require(header_end <= pe.OPTIONAL_HEADER.SizeOfHeaders, 'provider section table extent')
    # ImageBase at NT+48..56 can be rewritten by the loader. This evidence API
    # makes NO full-header claim and never uses the observed field as expected.
    spans = [Span('DOS', 0, checked.raw[:64], PAGE_READONLY),
             Span('NT-before-ImageBase', nt, checked.raw[nt:nt+48], PAGE_READONLY),
             Span('NT-after-ImageBase-and-sections', nt+56,
                  checked.raw[nt+56:header_end], PAGE_READONLY)]

    def file_bytes(rva, size):
        section = _section(checked.sections, rva, size)
        start = section.raw_offset+rva-section.rva
        return checked.raw[start:start+size]

    exports = pe.OPTIONAL_HEADER.DATA_DIRECTORY[0]
    spans.append(Span('exports', exports.VirtualAddress,
                      file_bytes(exports.VirtualAddress, exports.Size), PAGE_READONLY))
    for rva in sorted(entries):
        require(any(e.rva == rva and e.forwarder is None and e.rva for e in checked.exports),
                'entry is not a direct export RVA')
        s = _section(checked.sections, rva, PREFIX_BYTES)
        require(s.flags & EXECUTE and not s.flags & (WRITE | DISCARDABLE), 'entry prefix requires persistent RX')
        spans.append(Span('entry-prefix', rva, file_bytes(rva, PREFIX_BYTES), PAGE_EXECUTE_READ))
    return checked, tuple(spans)


class ModuleAPI(WindowsAPI):
    def __init__(self):
        super().__init__()
        fn = self.kernel.K32GetMappedFileNameW
        fn.restype = ct.c_uint32
        fn.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_wchar_p, ct.c_uint32]

    def mapped_path(self, handle, base):
        buffer = ct.create_unicode_buffer(PATH_CAPACITY)
        count = self.kernel.K32GetMappedFileNameW(handle, base, buffer, len(buffer))
        require(0 < count < len(buffer) and len(buffer.value) == count,
                'mapped-file path failed/truncated')
        return native_path(buffer.value)


def compare_provider(borrowed_handle, identity, provider, expected_native_path, entry_rvas, *, api=None):
    """Fresh scoped evidence, not a cache/lease. Caller retains process handle."""
    require(isinstance(identity, Identity), 'process identity type')
    expected_path = native_path(expected_native_path)
    provider, spans = comparison_spans(provider, entry_rvas)
    api = ModuleAPI() if api is None else api
    reader = Reader(borrowed_handle, identity, provider.base, provider.size, api=api)

    def boundary():
        reader.boundary(provider.base, provider.size)
        require(api.mapped_path(borrowed_handle, provider.base) == expected_path,
                'mapped-file path differs from independent expected path')
        reader.boundary(provider.base, provider.size)

    boundary()
    copied = 0
    for span in spans:
        for offset in range(0, len(span.expected), CHUNK):
            expected = span.expected[offset:offset+CHUNK]
            actual = reader.read(provider.base+span.rva+offset, len(expected), span.protection)
            require(actual == expected, f'provider evidence mismatch {span.label} RVA {span.rva+offset:x}')
            copied += len(expected)
    boundary()
    return {
        'schema': 'p99-provider-evidence-v1', 'evidence_matched': True,
        'pid': identity.pid, 'creation': identity.creation,
        'provider': provider.name, 'provider_file_sha256': provider.sha256,
        'base': provider.base, 'image_size': provider.size, 'native_path': expected_path,
        'compared_bytes': copied,
        'ranges': [{'label': s.label, 'rva': s.rva, 'size': len(s.expected),
                    'expected_sha256': digest(s.expected)} for s in spans],
        'header_image_base_compared': False, 'full_provider_bytes_verified': False,
        'mapped_file_object_identity_verified': False, 'api_set_schema_verified': False,
        'dependency_closure_verified': False, 'atomic_snapshot': False,
        'module_lifetime_verified': False, 'admitted_for_install': False,
    }
