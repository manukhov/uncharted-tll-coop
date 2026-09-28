"""Read-only comparison plan derived from trusted disk bytes, never remote PE.

This is NOT installer admission. Independent import bindings are caller input,
not certified by this module. Mutable globals/publication, discardable sections,
file/page padding, dependency integrity and lifetime remain explicit boundaries.
Unknown loader overrides (including CFG/XFG) fail exact comparison, not masking.
"""
from dataclasses import dataclass
from enum import Enum
import struct

import p99_wrist_abi_manifest as disk

PAGE_READONLY, PAGE_EXECUTE_READ = 0x02, 0x20
USER_END = 0x800000000000
CHUNK = 4096
DISCARDABLE = 0x02000000


class HeaderBase(Enum):
    PREFERRED = 'preferred-disk-base'
    LOADED = 'independently-selected-loaded-base'


class _MetadataReads(disk.Image):
    def __init__(self, raw, sha256):
        self.accesses = None
        super().__init__(raw, sha256)

    def read(self, rva, size):
        result = super().read(rva, size)
        if self.accesses is not None:
            self.accesses.append((rva, size))
        return result


@dataclass(frozen=True)
class Binding:
    dll: str
    name: str
    address: int


@dataclass(frozen=True)
class Region:
    name: str
    rva: int
    expected: bytes
    protection: int


@dataclass(frozen=True)
class Exclusion:
    name: str
    rva: int
    size: int
    reason: str


@dataclass(frozen=True)
class Plan:
    sha256: str
    base: int
    size: int
    header_base: HeaderBase
    regions: tuple[Region, ...]
    exclusions: tuple[Exclusion, ...]
    imports: tuple[Binding, ...]
    relocated_qwords: int
    excluded_relocations: int


def _address(address, size=1):
    return type(address) is int and type(size) is int and 0x10000 <= address < USER_END and \
        0 < size <= USER_END-address


def _disjoint_regions(staged):
    spans = sorted((rva, rva+len(data)) for _, rva, data, _ in staged)
    disk.require(all(first < last for first, last in spans) and
                 all(a[1] <= b[0] for a, b in zip(spans, spans[1:])), 'overlapping/empty comparison regions')


def build_plan(raw, trusted_sha256, base, bindings, *, header_base=HeaderBase.PREFERRED):
    raw = bytes(raw)  # Own one immutable version for every parse and expectation.
    manifest = disk.manifest(raw, trusted_sha256)
    image = _MetadataReads(raw, trusted_sha256)
    # Require FULL metadata reads within compared VirtualSize, not merely the
    # first unwind header. Raw padding may hold a valid-looking CHAININFO tail,
    # import name or ILT that the disk parser can read but the plan excludes.
    image.accesses = []
    image.exports()
    image.runtime_functions()
    image.imports()
    size = manifest['size_of_image']
    disk.require(_address(base, size) and base % 65536 == 0, 'loaded image address/extent')
    disk.require(isinstance(header_base, HeaderBase), 'explicit header ImageBase policy required')
    supplied = {}
    for item in bindings:
        disk.require(isinstance(item, Binding) and isinstance(item.dll, str) and
                     isinstance(item.name, str) and _address(item.address), 'independent binding shape/address')
        key = (item.dll.lower(), item.name)
        disk.require(key not in supplied, 'duplicate binding')
        disk.require(not base <= item.address < base+size, 'self-image import target')
        supplied[key] = item
    required = {(row['dll'].lower(), row['name']) for row in manifest['imports']}
    disk.require(set(supplied) == required, 'incomplete/unexpected independent bindings')

    # VirtualSize defines comparison coverage. Raw/file alignment padding is
    # NOT promised mapped; notably .rsrc has nonzero PADDINGXX after VirtualSize.
    staged = [('headers', 0, bytearray(raw[:image.pe.OPTIONAL_HEADER.SizeOfHeaders]), PAGE_READONLY)]
    if header_base is HeaderBase.LOADED:
        # This host's owned loader exam rewrites only this header qword to the
        # actual base. The expected value comes from independent module selection,
        # never from the observed header. The older SEC_IMAGE_NO_EXECUTE audit
        # proved header base need NOT equal its view: that case remains rejected
        # here. No auto-detection, opaque-field or accept-either wildcard.
        at = image.pe.OPTIONAL_HEADER.get_field_absolute_offset('ImageBase')
        disk.require(at+8 <= len(staged[0][2]), 'header ImageBase extent')
        struct.pack_into('<Q', staged[0][2], at, base)
    excluded = []
    for section in image.sections:
        name, rva, count = section['name'], section['rva'], section['virtual_size']
        flags = section['characteristics']
        if flags & disk.WRITE:
            excluded.append(Exclusion(name, rva, count, 'mutable state; not verified'))
        elif flags & DISCARDABLE:
            disk.require(name == '.reloc' and not flags & disk.EXECUTE, 'unsupported discardable section')
            excluded.append(Exclusion(name, rva, count, 'discardable; relocation instructions are trusted disk only'))
        else:
            copied = min(count, section['raw_size'])
            offset = section['raw_offset']
            data = bytearray(raw[offset:offset+copied]) + bytearray(count-copied)
            staged.append((name, rva, data, PAGE_EXECUTE_READ if flags & disk.EXECUTE else PAGE_READONLY))
        if section['raw_size'] > count:
            excluded.append(Exclusion(name, rva+count, section['raw_size']-count, 'raw alignment padding'))
    _disjoint_regions(staged)
    # Include every unnamed inter-section/page gap in the report; none becomes
    # an implicit zero-byte mask or a claim to validate the entire image.
    covered = sorted([(rva, rva+len(data)) for _, rva, data, _ in staged] +
                     [(e.rva, e.rva+e.size) for e in excluded])
    cursor = 0
    for first, last in covered:
        if first > cursor:
            excluded.append(Exclusion('gap', cursor, first-cursor, 'page/inter-section padding'))
        cursor = max(cursor, last)
    if cursor < size:
        excluded.append(Exclusion('gap', cursor, size-cursor, 'page/inter-section padding'))

    def owner(rva, count):
        matches = [(at, data) for _, at, data, _ in staged if at <= rva and rva+count <= at+len(data)]
        disk.require(len(matches) <= 1, 'ambiguous comparison region')
        if matches:
            return matches[0]
        disk.require(not any(at < rva+count and rva < at+len(data) for _, at, data, _ in staged),
                     'partial immutable span')
        return None

    iat = {row['iat_rva'] for row in manifest['imports']}
    # Check overlap even for excluded bytes, so no transformation can silently
    # overwrite another expectation or excuse a corrupted relocation table.
    for fixup in manifest['dir64_relocations']:
        disk.require(not any(fixup['rva'] < at+8 and at < fixup['rva']+8 for at in iat),
                     'DIR64/IAT overlap')
    relocated, skipped = 0, 0
    for fixup in manifest['dir64_relocations']:
        rva, preferred = fixup['rva'], fixup['preferred_value']
        owned = owner(rva, 8)
        if owned is None:
            disk.require(any(e.reason.startswith('mutable') and e.rva <= rva and rva+8 <= e.rva+e.size
                             for e in excluded), 'relocation outside compared/mutable range')
            skipped += 1
            continue
        at, data = owned
        disk.require(struct.unpack_from('<Q', data, rva-at)[0] == preferred, 'disk relocation value mismatch')
        value = base + (preferred-manifest['preferred_base'])
        disk.require(_address(value), 'relocated address overflow')
        struct.pack_into('<Q', data, rva-at, value)
        relocated += 1
    for row in manifest['imports']:
        owned = owner(row['iat_rva'], 8)
        disk.require(owned is not None, 'IAT must be in compared immutable range')
        at, data = owned
        expected = supplied[(row['dll'].lower(), row['name'])].address
        struct.pack_into('<Q', data, row['iat_rva']-at, expected)
    # Every code/unwind/export directory span must actually be in covered bytes.
    for record in manifest['runtime_functions']:
        disk.require(owner(record['begin'], record['end']-record['begin']) is not None and
                     owner(record['unwind'], 4) is not None, 'runtime function outside comparison')
    for index in (0, 1, 3, 10, 12):
        directory = image.pe.OPTIONAL_HEADER.DATA_DIRECTORY[index]
        if directory.Size:
            disk.require(owner(directory.VirtualAddress, directory.Size) is not None,
                         'metadata outside comparison')
    for rva, count in image.accesses:
        disk.require(owner(rva, count) is not None, 'metadata read outside comparison')
    return Plan(manifest['file_sha256'], base, size, header_base,
                tuple(Region(name, rva, bytes(data), protection) for name, rva, data, protection in staged),
                tuple(sorted(excluded, key=lambda e: (e.rva, e.reason))),
                tuple(supplied[key] for key in sorted(supplied)), relocated, skipped)


def compare(plan, reader):
    """Reader boundary(base,size) must check its selected process before/after.

    read(address,count,protection) must check exact image/permissions before and
    after an exact read. No hashing/allocation claim for a stopped game thread:
    this Python verifier is external and is not yet wired into the installer.
    """
    reader.boundary(plan.base, plan.size)
    compared = 0
    for region in plan.regions:
        for offset in range(0, len(region.expected), CHUNK):
            expected = region.expected[offset:offset+CHUNK]
            actual = reader.read(plan.base+region.rva+offset, len(expected), region.protection)
            disk.require(isinstance(actual, bytes) and len(actual) == len(expected), 'short/nonbytes read')
            if actual != expected:
                byte = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
                raise ValueError(f'mapped mismatch {region.name} at RVA {region.rva+offset+byte:X}')
            compared += len(expected)
    reader.boundary(plan.base, plan.size)
    return {'file_sha256': plan.sha256, 'compared_bytes': compared,
            'header_base_policy': plan.header_base.value,
            'compared_regions': len(plan.regions), 'iat_bindings_compared': len(plan.imports),
            'dir64_applied': plan.relocated_qwords, 'mutable_dir64_not_compared': plan.excluded_relocations,
            'comparison_passed': True, 'atomic_snapshot': False, 'admitted_for_install': False,
            'dependency_integrity_verified': False, 'module_lifetime_verified': False}
