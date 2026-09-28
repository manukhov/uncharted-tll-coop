"""Conditional OS/CRT export graph, from explicit trusted files, not target IAT.

This narrow AMD64 PE reader is deliberately separate from the inert ABI reader:
system DLLs may have TLS, certificates and other directories. Those bytes are
hashed, NOT interpreted/admitted. Only export metadata and selected RX entry
RVAs are checked; machine-code jumps and provider dependencies are NOT followed.

API-set routes are exact (importer, contract) caller inputs, not discovered here.
Bases are also caller inputs. Results do not certify mapped provider bytes,
loader namespace, target identity, lifetime, API-set schema or installation.
No process APIs, dynamic lookup, loading or target writes exist in this module.
"""
from dataclasses import dataclass
from itertools import islice
import re
import struct
from types import MappingProxyType

import pefile

from p99_wrist_abi_manifest import digest, require, READ, WRITE, EXECUTE
from p99_wrist_mapped_image import Binding, _address

MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL_FILES = 128 * 1024 * 1024
MAX_METADATA = 4 * 1024 * 1024
MAX_EXPORTS, MAX_STRING, MAX_STEPS, MAX_PROVIDERS = 65536, 512, 16, 32
DISCARDABLE = 0x02000000


def module_name(name):
    require(isinstance(name, str) and name.isascii() and len(name) <= 128 and
            re.fullmatch(r'[A-Za-z0-9_-]+(?:\.dll)?', name, re.IGNORECASE),
            'simple DLL name required')
    name = name.lower()
    return name if name.endswith('.dll') else name + '.dll'


def api_contract(name):
    return name.startswith(('api-', 'ext-'))


def symbol_key(value):
    if type(value) is int:
        require(1 <= value <= 65535, 'ordinal range')
    else:
        require(isinstance(value, str) and 0 < len(value) < MAX_STRING and
                all(33 <= ord(c) <= 126 for c in value) and not value.startswith('#'),
                'export symbol syntax')
    return value


def split_forwarder(value):
    # Deliberate narrow grammar. NAME.dll.symbol is ambiguous with an export
    # named dll.symbol; neither extension guessing nor dotted symbols admitted.
    require(value.count('.') == 1, 'ambiguous forwarder syntax')
    module, separator, symbol = value.partition('.')
    require(separator, 'forwarder syntax')
    module = module_name(module)
    if symbol.startswith('#'):
        require(re.fullmatch(r'#[1-9][0-9]{0,4}', symbol), 'forwarder ordinal syntax')
        symbol = int(symbol[1:])
    return module, symbol_key(symbol)


@dataclass(frozen=True)
class Section:
    rva: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    flags: int


@dataclass(frozen=True)
class Export:
    ordinal: int
    rva: int
    forwarder: str | None


@dataclass(frozen=True)
class Provider:
    name: str
    sha256: str
    base: int
    size: int
    raw: bytes
    sections: tuple[Section, ...]
    ordinal_base: int
    exports: tuple[Export, ...]
    names: tuple[tuple[str, int], ...]

    def lookup(self, symbol):
        symbol_key(symbol)
        if isinstance(symbol, str):
            matches = [ordinal for name, ordinal in self.names if name == symbol]
            require(len(matches) == 1, f'export name missing: {self.name}!{symbol}')
            ordinal = matches[0]
        else:
            ordinal = symbol
        index = ordinal - self.ordinal_base
        require(0 <= index < len(self.exports), 'export ordinal missing')
        export = self.exports[index]
        require(export.rva, 'null export hole')
        if export.forwarder is None:
            section = _section(self.sections, export.rva, 1)
            require(section.flags & EXECUTE and not section.flags & (WRITE | DISCARDABLE),
                    'selected export is not persistent RX code')
        return export


def _section(sections, rva, count):
    require(type(rva) is int and type(count) is int and rva > 0 and count > 0,
            'export RVA range')
    matches = [s for s in sections if s.rva <= rva and
               rva+count <= s.rva+min(s.virtual_size, s.raw_size)]
    require(len(matches) == 1, 'export bytes outside unique file-backed VirtualSize')
    return matches[0]


def read_provider(raw, sha256, name, base):
    """Own bytes and verify caller's hash before PE parsing. No hash auto-trust."""
    name = module_name(name)
    require(not api_contract(name), 'API contract is not a concrete provider')
    require(isinstance(sha256, str) and re.fullmatch('[0-9a-fA-F]{64}', sha256),
            'explicit trusted provider SHA256 required')
    require(0 < len(raw) <= MAX_FILE, 'provider file bound')
    raw = bytes(raw)
    require(digest(raw) == sha256.lower(), 'trusted provider SHA256 mismatch')
    pe = pefile.PE(data=raw, fast_load=True)
    h = pe.OPTIONAL_HEADER
    require(pe.FILE_HEADER.Machine == 0x8664 and h.Magic == 0x20b and
            pe.FILE_HEADER.Characteristics & 0x2002 == 0x2002, 'AMD64 PE32+ provider DLL required')
    require(pe.FILE_HEADER.SizeOfOptionalHeader == 240 and h.NumberOfRvaAndSizes == 16 and
            len(h.DATA_DIRECTORY) == 16, 'provider optional header')
    require(h.SectionAlignment == 4096 and h.FileAlignment == 512 and
            0 < h.SizeOfImage <= MAX_FILE and h.SizeOfImage % 4096 == 0,
            'provider image alignment/extent')
    require(type(base) is int and base % 65536 == 0 and _address(base, h.SizeOfImage),
            'provider selected base')
    require(0 < len(pe.sections) == pe.FILE_HEADER.NumberOfSections <= 96 and
            0 < h.SizeOfHeaders <= len(raw) and h.SizeOfHeaders % 512 == 0 and
            pe.sections[-1].get_file_offset()+40 <= h.SizeOfHeaders, 'provider headers extent')
    sections, raw_ranges = [], []
    end = (h.SizeOfHeaders+4095) & ~4095
    for s in pe.sections:
        va, vs, offset, size = s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData
        require(vs > 0 and va >= end and va % 4096 == 0 and
                va+max(vs, size) <= h.SizeOfImage, 'provider section virtual extent/overlap')
        require(s.Characteristics & READ and
                not (s.Characteristics & EXECUTE and s.Characteristics & WRITE),
                'provider section permissions')
        if size:
            require(offset >= h.SizeOfHeaders and offset % 512 == 0 and size % 512 == 0 and
                    offset+size <= len(raw), 'provider raw extent')
            raw_ranges.append((offset, offset+size))
        sections.append(Section(va, vs, offset, size, s.Characteristics))
        end = (va+max(vs, size)+4095) & ~4095
    raw_ranges.sort()
    require(end <= h.SizeOfImage and
            all(a[1] <= b[0] for a, b in zip(raw_ranges, raw_ranges[1:])), 'provider raw overlap')
    # Certificate directory uses FILE offsets. Never feed it to an RVA reader.
    cert = h.DATA_DIRECTORY[4]
    require(bool(cert.VirtualAddress) == bool(cert.Size), 'partial certificate directory')
    if cert.Size:
        require(raw_ranges and cert.VirtualAddress % 8 == 0 and cert.VirtualAddress >= max(b for a, b in raw_ranges) and
                cert.VirtualAddress+cert.Size <= len(raw), 'certificate file extent')
    directory = h.DATA_DIRECTORY[0]
    first, extent = directory.VirtualAddress, directory.Size
    require(40 <= extent <= MAX_METADATA, 'provider export directory extent')
    directory_section = _section(sections, first, extent)
    require(not directory_section.flags & (WRITE | EXECUTE | DISCARDABLE),
            'export metadata not persistent RO')
    work = 0

    def metadata(rva, count):
        nonlocal work
        require(first <= rva and rva+count <= first+extent, 'export metadata outside directory')
        s = _section(sections, rva, count)
        require(not s.flags & (WRITE | EXECUTE | DISCARDABLE), 'export metadata not persistent RO')
        work += count
        require(work <= MAX_METADATA, 'export parsing work bound')
        at = s.raw_offset+rva-s.rva
        return raw[at:at+count]

    def string(rva):
        result = bytearray()
        for i in range(MAX_STRING):
            value = metadata(rva+i, 1)[0]
            if not value:
                require(result and all(33 <= c <= 126 for c in result), 'export ASCII string')
                return result.decode('ascii')
            result.append(value)
        raise ValueError('unterminated export string')

    fields = struct.unpack('<IIHHIIIIIII', metadata(first, 40))
    flags, _, _, _, dll_name, ordinal_base, count, n_names, eat, names, ordinals = fields
    require(flags == 0 and 1 <= count <= MAX_EXPORTS and n_names <= MAX_EXPORTS and
            1 <= ordinal_base <= 65535 and ordinal_base+count <= 65536, 'export counts/ordinal base')
    require(module_name(string(dll_name)) == name, 'provider export DLL name mismatch')
    addresses = struct.unpack('<'+'I'*count, metadata(eat, count*4))
    name_rvas = struct.unpack('<'+'I'*n_names, metadata(names, n_names*4)) if n_names else ()
    indexes = struct.unpack('<'+'H'*n_names, metadata(ordinals, n_names*2)) if n_names else ()
    named, previous = [], ''
    for rva, index in zip(name_rvas, indexes):
        symbol = symbol_key(string(rva))
        require(symbol > previous and index < count, 'duplicate/unsorted name or invalid ordinal index')
        named.append((symbol, ordinal_base+index))
        previous = symbol
    exports = []
    for index, rva in enumerate(addresses):
        forwarder = string(rva) if first <= rva < first+extent else None
        if forwarder is not None:
            split_forwarder(forwarder)
        exports.append(Export(ordinal_base+index, rva, forwarder))
    return Provider(name, sha256.lower(), base, h.SizeOfImage, raw, tuple(sections),
                    ordinal_base, tuple(exports), tuple(named))


@dataclass(frozen=True)
class ApiRoute:
    importer: str
    contract: str
    host: str


@dataclass(frozen=True)
class Step:
    kind: str
    importer: str
    module: str
    symbol: str | int
    provider_sha256: str | None
    rva: int | None
    target_module: str | None
    target_symbol: str | int | None


@dataclass(frozen=True)
class Resolution:
    binding: Binding
    provider: str
    provider_sha256: str
    provider_base: int
    rva: int
    steps: tuple[Step, ...]
    # Conditional graph results can never grant these certificates.
    provider_mapped_bytes_verified: bool = False
    api_set_schema_verified: bool = False
    remote_lifetime_verified: bool = False
    admitted_for_install: bool = False


class Resolver:
    def __init__(self, providers, api_routes=()):
        providers = tuple(islice(providers, MAX_PROVIDERS+1))
        api_routes = tuple(islice(api_routes, 129))
        require(0 < len(providers) <= MAX_PROVIDERS and len(api_routes) <= 128, 'provider/route count')
        require(all(isinstance(p, Provider) for p in providers) and
                sum(len(p.raw) for p in providers) <= MAX_TOTAL_FILES, 'provider aggregate byte bound')
        by_name = {}
        for provider in providers:
            require(provider.name not in by_name, 'duplicate/invalid provider')
            # Frozen dataclasses can still be manually built or replaced. Do not
            # treat their type as a validation certificate. Reparse bounded owned
            # bytes, verify hash and all derived fields, retain the canonical copy.
            checked = read_provider(provider.raw, provider.sha256, provider.name, provider.base)
            require(checked == provider, 'provider record differs from trusted bytes')
            by_name[provider.name] = checked
        spans = sorted((p.base, p.base+p.size) for p in providers)
        require(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])), 'overlapping provider bases')
        routes = {}
        for row in api_routes:
            require(isinstance(row, ApiRoute), 'API route type')
            importer, contract, host = map(module_name, (row.importer, row.contract, row.host))
            require(not api_contract(importer) and api_contract(contract) and
                    not api_contract(host) and host in by_name, 'API route importer/host')
            key = importer, contract
            require(key not in routes, 'duplicate API route')
            routes[key] = host
        self.providers, self.routes = MappingProxyType(by_name), MappingProxyType(routes)

    def resolve(self, dll, name, *, importer):
        source, symbol = module_name(dll), symbol_key(name)
        require(isinstance(symbol, str), 'artifact imports must be named')
        module, consumer = source, module_name(importer)
        require(not api_contract(consumer), 'concrete importer required')
        visited, steps = set(), []
        for _ in range(MAX_STEPS):
            key = consumer, module, symbol
            require(key not in visited, 'export/API route cycle')
            visited.add(key)
            if api_contract(module):
                host = self.routes.get((consumer, module))
                require(host is not None, 'no exact importer/API-set route')
                steps.append(Step('api-set-input', consumer, module, symbol, None, None, host, symbol))
                module = host
                continue
            provider = self.providers.get(module)
            require(provider is not None, f'missing provider: {module}')
            export = provider.lookup(symbol)
            if export.forwarder is not None:
                target, target_symbol = split_forwarder(export.forwarder)
                steps.append(Step('forwarder', consumer, module, symbol, provider.sha256,
                                  export.rva, target, target_symbol))
                consumer, module, symbol = module, target, target_symbol
                continue
            steps.append(Step('export', consumer, module, symbol, provider.sha256, export.rva, None, None))
            return Resolution(Binding(source, name, provider.base+export.rva), provider.name,
                              provider.sha256, provider.base, export.rva, tuple(steps))
        raise ValueError('export/API route depth bound')
