#!/usr/bin/env python3
"""Offline manifest of an explicitly trusted, inert P99 ABI DLL. No loading/calls.

The caller MUST supply a SHA256 obtained from an independently reviewed build.
An export is only a locator, not a signature/body certificate. All raw sections
are hashed, including complete code and readonly unwind/SEH data; import targets,
mutable state, loaded relocations and remote lifetime are NOT verified here.
This file cannot grant installation/observation admission. No process API exists.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import pefile

SCHEMA = 'tll-p99-wrist-inert-disk-abi-v1'
ROLES = {
    'TllP99WristAsyncV1': 'patch:void(void*)',
    'TllP99WristProcess0V1': 'patch:uint32(const uint32*,uint32,const void*,uint32,const void*,const void*,const void*)',
    'TllP99WristProcess1V1': 'patch:uint32(const uint32*,uint32,const void*,uint32,const void*,const void*,const void*)',
    'TllP99WristCmd16V1': 'patch:uint32*(uint32*,const void*)',
    'TllP99WristAsyncBridgeV1': 'helper:void(void*,Native,uintptr_t*)',
    'TllP99WristProcessBridgeV1': 'helper:uint32(original7,Native,uintptr_t*)',
    'TllP99WristInvokeV1': 'helper:uint32*(State&,Native,uint32*,const void*,Capture,void*)',
    'TllP99WristPreparedV1': 'data:zero-RW-page4096',
}
PATCH_ORDER = ('TllP99WristAsyncV1', 'TllP99WristAsyncV1',
               'TllP99WristProcess0V1', 'TllP99WristProcess1V1', 'TllP99WristCmd16V1')
MAX_FILE = 64 * 1024 * 1024
MAX_CHAIN_DEPTH = 16
READ, WRITE, EXECUTE = 0x40000000, 0x80000000, 0x20000000


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


class Image:
    def __init__(self, raw, expected_sha256):
        require(isinstance(expected_sha256, str) and
                re.fullmatch('[0-9a-fA-F]{64}', expected_sha256), 'explicit trusted SHA256 required')
        require(0 < len(raw) <= MAX_FILE, 'file size bound')
        self.raw = bytes(raw)
        require(digest(self.raw) == expected_sha256.lower(), 'trusted SHA256 mismatch')
        self.pe = pefile.PE(data=self.raw, fast_load=True)
        p = self.pe
        require(p.FILE_HEADER.Machine == 0x8664 and p.OPTIONAL_HEADER.Magic == 0x20B and
                p.FILE_HEADER.Characteristics & 0x2000, 'AMD64 PE32+ DLL required')
        require(p.OPTIONAL_HEADER.SectionAlignment == 4096 and
                p.OPTIONAL_HEADER.FileAlignment == 512, 'unexpected alignment')
        require(p.OPTIONAL_HEADER.DllCharacteristics & 0x140 == 0x140,
                'ASLR and NX required')
        require(p.OPTIONAL_HEADER.NumberOfRvaAndSizes == 16 and
                len(p.OPTIONAL_HEADER.DATA_DIRECTORY) == 16, 'directory count')
        require(0 < p.OPTIONAL_HEADER.SizeOfHeaders <= len(raw) and
                p.OPTIONAL_HEADER.SizeOfHeaders % 512 == 0, 'headers extent')
        require(0 < p.OPTIONAL_HEADER.SizeOfImage <= MAX_FILE and
                p.OPTIONAL_HEADER.SizeOfImage % 4096 == 0, 'image extent')
        require(0 < len(p.sections) == p.FILE_HEADER.NumberOfSections <= 96, 'section count')
        self.sections = []
        names = set()
        prev_end, raw_spans = 4096, []
        for s in p.sections:
            name = s.Name.rstrip(b'\0').decode('ascii')
            va, vs, offset, size = s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData
            extent = max(vs, size)
            require(name and name not in names, 'duplicate/empty section name')
            names.add(name)
            require(va % 4096 == 0 and va >= prev_end and vs > 0 and
                    va + extent <= p.OPTIONAL_HEADER.SizeOfImage, 'section virtual overlap/extent')
            require(s.Characteristics & READ and
                    not (s.Characteristics & EXECUTE and s.Characteristics & WRITE), 'section permission')
            if size:
                require(offset >= p.OPTIONAL_HEADER.SizeOfHeaders and offset % 512 == 0 and
                        size % 512 == 0 and offset + size <= len(raw), 'section raw extent')
                raw_spans.append((offset, offset + size))
            prev_end = (va + extent + 4095) & ~4095
            self.sections.append({'name': name, 'rva': va, 'virtual_size': vs,
                                  'raw_offset': offset, 'raw_size': size,
                                  'characteristics': s.Characteristics,
                                  'raw_sha256': digest(self.raw[offset:offset+size])})
        raw_spans.sort()
        require(prev_end == p.OPTIONAL_HEADER.SizeOfImage, 'uncovered image extent')
        require(all(a[1] <= b[0] for a, b in zip(raw_spans, raw_spans[1:])), 'section raw overlap')
        require(raw_spans and raw_spans[-1][1] == len(raw), 'overlay/signature not admitted')
        # Do not quietly accept alternate loaders, TLS initializers or delay imports.
        for index in (4, 7, 8, 9, 11, 13, 14, 15):
            d = p.OPTIONAL_HEADER.DATA_DIRECTORY[index]
            require(d.VirtualAddress == 0 and d.Size == 0, f'unsupported directory {index}')
        for index, d in enumerate(p.OPTIONAL_HEADER.DATA_DIRECTORY):
            require(bool(d.VirtualAddress) == bool(d.Size), f'partial directory {index}')
            if d.Size:
                self.read(d.VirtualAddress, d.Size)
        self.code(p.OPTIONAL_HEADER.AddressOfEntryPoint, 1)

    def section(self, rva, size):
        require(size > 0, 'empty RVA range')
        matches = [s for s in self.sections if s['rva'] <= rva and
                   rva + size <= s['rva'] + max(s['virtual_size'], s['raw_size'])]
        require(len(matches) == 1, 'unmapped/ambiguous RVA')
        return matches[0]

    def read(self, rva, size):
        s = self.section(rva, size)
        offset = rva - s['rva']
        require(offset + size <= s['raw_size'], 'RVA not fully file-backed')
        return self.raw[s['raw_offset']+offset:s['raw_offset']+offset+size]

    def cstring(self, rva):
        out = bytearray()
        for n in range(256):
            value = self.read(rva+n, 1)[0]
            if not value:
                require(out, 'empty name')
                return out.decode('ascii')
            out.append(value)
        raise ValueError('unterminated/long name')

    def readonly(self, rva, size):
        s = self.section(rva, size)
        require(not s['characteristics'] & (WRITE | EXECUTE), 'readonly metadata required')
        return self.read(rva, size)

    def code(self, rva, size):
        s = self.section(rva, size)
        require(s['characteristics'] & EXECUTE and not s['characteristics'] & WRITE, 'RX code required')
        return self.read(rva, size)

    def exports(self):
        return self._exports(ROLES)

    def _exports(self, roles):
        # Shared bounded decoder; each public artifact Image keeps its own exact
        # export contract. The old manifest never selects the preparation roles.
        d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[0]
        require(40 <= d.Size <= 65536, 'export directory extent')
        def metadata(rva, size):
            require(d.VirtualAddress <= rva and rva+size <= d.VirtualAddress+d.Size,
                    'export metadata outside directory')
            return self.readonly(rva, size)
        fields = struct.unpack('<IIHHIIIIIII', metadata(d.VirtualAddress, 40))
        _, _, _, _, _, base, count, n_names, eat, names, ordinals = fields
        require(count == n_names == len(roles) and base == 1, 'exact versioned export count required')
        addresses = struct.unpack('<'+'I'*count, metadata(eat, count*4))
        name_rvas = struct.unpack('<'+'I'*count, metadata(names, count*4))
        indexes = struct.unpack('<'+'H'*count, metadata(ordinals, count*2))
        require(sorted(indexes) == list(range(count)), 'duplicate/invalid export ordinal')
        result = {}
        previous = ''
        for name_rva, index in zip(name_rvas, indexes):
            name, address = self.cstring(name_rva), addresses[index]
            metadata(name_rva, len(name)+1)
            require(name > previous, 'unsorted/duplicate export name')
            previous = name
            require(address != 0 and not d.VirtualAddress <= address < d.VirtualAddress+d.Size,
                    'null/forwarded export')
            result[name] = address
        require(set(result) == set(roles), 'missing/unexpected versioned export')
        require(len(set(result.values())) == len(result), 'aliased ABI roles')
        return result

    def runtime_functions(self):
        d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
        require(d.Size > 0 and d.Size % 12 == 0 and d.Size <= 1024*1024, 'runtime table extent')
        table = self.readonly(d.VirtualAddress, d.Size)
        records, previous_end = [], 0
        for offset in range(0, len(table), 12):
            begin, end, unwind = struct.unpack_from('<III', table, offset)
            require(previous_end <= begin < end and unwind % 4 == 0, 'runtime range/ordering')
            self.code(begin, end-begin)
            header = self.readonly(unwind, 4)
            require(header[0] & 7 == 1, 'unsupported unwind version')
            flags, codes = header[0] >> 3, header[2]
            require(flags & ~7 == 0 and not (flags & 4 and flags & 3), 'invalid unwind flags')
            prefix_size = 4 + ((codes+1) & ~1)*2
            self.readonly(unwind, prefix_size + (12 if flags & 4 else 4 if flags & 3 else 0))
            # Handler-specific records/funclets are covered by whole section
            # hashes, NOT interpreted or claimed semantically validated here.
            record = {'begin': begin, 'end': end, 'unwind': unwind,
                      'unwind_prefix': header.hex()}
            if flags & 4:
                record['chain'] = list(struct.unpack('<III', self.readonly(unwind+prefix_size, 12)))
            records.append(record)
            previous_end = end
        by_key = {(r['begin'], r['end'], r['unwind']): r for r in records}
        for record in records:
            seen, owner = set(), record
            while 'chain' in owner:
                require(len(seen) < MAX_CHAIN_DEPTH, 'unwind chain depth bound')
                key = tuple(owner['chain'])
                require(key in by_key and key not in seen, 'missing/cyclic unwind chain')
                seen.add(key)
                owner = by_key[key]
            require(record['unwind_prefix'][6:8] == owner['unwind_prefix'][6:8],
                    'chained unwind frame descriptor mismatch')
            record['owner'] = owner['begin']
        return records

    def relocations(self):
        d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[5]
        require(d.Size > 0, 'relocation directory required')
        raw = self.readonly(d.VirtualAddress, d.Size)
        offset, out = 0, []
        while offset < len(raw):
            require(offset+8 <= len(raw), 'truncated relocation block')
            page, size = struct.unpack_from('<II', raw, offset)
            require(page % 4096 == 0 and size >= 8 and size % 4 == 0 and
                    offset+size <= len(raw), 'relocation block extent')
            for at in range(offset+8, offset+size, 2):
                entry = struct.unpack_from('<H', raw, at)[0]
                kind, relative = entry >> 12, entry & 0xFFF
                if kind == 0:
                    continue
                require(kind == 10 and relative <= 4088, 'only in-page DIR64 supported')
                rva = page+relative
                value = struct.unpack('<Q', self.read(rva, 8))[0]
                require(self.pe.OPTIONAL_HEADER.ImageBase <= value <
                        self.pe.OPTIONAL_HEADER.ImageBase+self.pe.OPTIONAL_HEADER.SizeOfImage,
                        'DIR64 value outside image')
                out.append({'rva': rva, 'preferred_value': value})
            offset += size
        require(out and len({r['rva'] for r in out}) == len(out), 'empty/duplicate relocations')
        out.sort(key=lambda r: r['rva'])
        require(all(a['rva']+8 <= b['rva'] for a, b in zip(out, out[1:])), 'overlapping relocations')
        return out

    def imports(self):
        # Parse every descriptor/thunk explicitly. A convenience parser may
        # return a partial list on malformed data; that is not a complete pin.
        d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[1]
        iat = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[12]
        require(0 < d.Size <= 65536 and d.Size % 20 == 0 and iat.Size > 0,
                'import descriptor/IAT extent')
        descriptors = self.readonly(d.VirtualAddress, d.Size)
        out, dlls, spans, terminated = [], set(), [], False
        for offset in range(0, len(descriptors), 20):
            original, timestamp, forward, dll_rva, thunk = struct.unpack_from('<IIIII', descriptors, offset)
            if (original, timestamp, forward, dll_rva, thunk) == (0, 0, 0, 0, 0):
                require(not any(descriptors[offset:]), 'data after import terminator')
                terminated = True
                break
            require(original and thunk and original % 8 == 0 and thunk % 8 == 0 and
                    timestamp == 0 and forward == 0, 'bound/partial import descriptor')
            dll = self.cstring(dll_rva)
            require(dll.lower() not in dlls, 'duplicate import DLL')
            dlls.add(dll.lower())
            names = set()
            for index in range(4096):
                value = struct.unpack('<Q', self.readonly(original+index*8, 8))[0]
                disk_iat = struct.unpack('<Q', self.readonly(thunk+index*8, 8))[0]
                require(value == disk_iat, 'prebound/mismatched disk IAT')
                require(iat.VirtualAddress <= thunk+index*8 and
                        thunk+(index+1)*8 <= iat.VirtualAddress+iat.Size, 'IAT directory coverage')
                if value == 0:
                    require(names, 'empty import DLL')
                    spans.append((thunk, thunk+(index+1)*8))
                    break
                require(value < 0x100000000, 'ordinal/invalid import RVA')
                self.readonly(value, 2)  # Hint, followed by the bounded name.
                name = self.cstring(value+2)
                require(name not in names, 'duplicate named import')
                names.add(name)
                out.append({'dll': dll, 'name': name, 'iat_rva': thunk+index*8})
            else:
                raise ValueError('unterminated/oversized import thunk array')
        require(terminated and out, 'missing import terminator/imports')
        spans.sort()
        require(spans[0][0] == iat.VirtualAddress and spans[-1][1] == iat.VirtualAddress+iat.Size and
                all(a[1] == b[0] for a, b in zip(spans, spans[1:])), 'incomplete/overlapping IAT coverage')
        return out


def manifest(raw, expected_sha256):
    image = Image(raw, expected_sha256)
    exports = image.exports()
    runtime = image.runtime_functions()
    functions = {r['begin']: r for r in runtime}
    roles = {}
    for name, role in ROLES.items():
        rva = exports[name]
        if role.startswith('data:'):
            section = image.section(rva, 4096)
            require(section['name'] == '.p99prep' and section['rva'] == rva and
                    section['virtual_size'] == section['raw_size'] == 4096 and
                    section['characteristics'] & (READ | WRITE | EXECUTE) == READ | WRITE,
                    'isolated RW prepared page required')
            require(image.read(rva, 4096) == bytes(4096), 'prepared page must be unpublished zero bytes')
            roles[name] = {'role': role, 'rva': rva, 'size': 4096}
        else:
            require(rva in functions, 'export must start at exact runtime-function boundary')
            record = functions[rva]
            require(record['owner'] == rva, 'export must be the primary unwind entry')
            # Optimized cmd16 has multiple CHAININFO fragments: the actual
            # invoke CALL is NOT in its first .pdata range. Retain every chunk.
            fragments = [{'rva': r['begin'], 'size': r['end']-r['begin'], 'unwind': r['unwind'],
                          'disk_sha256': digest(image.code(r['begin'], r['end']-r['begin']))}
                         for r in runtime if r['owner'] == rva]
            roles[name] = {'role': role, 'rva': rva, 'fragments': fragments}
    imports = image.imports()
    forbidden = {'CreateThread', 'CreateRemoteThread', 'VirtualProtect', 'VirtualProtectEx',
                 'WriteProcessMemory', 'WSAStartup', 'LoadLibraryA', 'LoadLibraryW',
                 'GetProcAddress', 'SendInput', 'FlsAlloc'}
    require(not forbidden.intersection(i['name'] for i in imports), 'installer/dynamic lookup import in inert artifact')
    return {'schema': SCHEMA, 'admitted_for_install': False, 'loaded_image_verified': False,
            'file_sha256': digest(raw), 'file_size': len(raw),
            'preferred_base': image.pe.OPTIONAL_HEADER.ImageBase,
            'size_of_image': image.pe.OPTIONAL_HEADER.SizeOfImage,
            'entry_rva': image.pe.OPTIONAL_HEADER.AddressOfEntryPoint,
            'headers_sha256': digest(raw[:image.pe.OPTIONAL_HEADER.SizeOfHeaders]),
            'roles': roles, 'patch_rvas': [exports[n] for n in PATCH_ORDER],
            'sections': image.sections, 'runtime_functions': runtime,
            'dir64_relocations': image.relocations(), 'imports': imports,
            'limits': ['No remote mapped bytes, relocation application or IAT target verification.',
                       'No import/OS/CRT lifetime or transitive indirect-call proof.',
                       'Unwind opcode/frame/handler semantics are not fully decoded; complete section bytes are hashed.',
                       'No initialization, preparation, FLS, hook, ACK, arm or game test admission.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--sha256', required=True, help='independently trusted final build hash')
    parser.add_argument('--check', type=Path, help='compare complete previously reviewed manifest')
    parser.add_argument('--summary', action='store_true')
    args = parser.parse_args()
    require(args.dll.stat().st_size <= MAX_FILE, 'file size bound')
    result = manifest(args.dll.read_bytes(), args.sha256)
    if args.check:
        require(json.loads(args.check.read_text(encoding='utf-8-sig')) == result, 'manifest mismatch')
    if args.summary:
        result = {k: result[k] for k in ('schema', 'file_sha256', 'size_of_image', 'roles',
                                       'admitted_for_install', 'loaded_image_verified')}
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
