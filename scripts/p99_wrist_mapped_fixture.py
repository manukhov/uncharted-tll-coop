"""Owned Windows loader/RPM exam. Fixed inert artifact only, no external PID.

No game or exported adapter is called. This loads the reviewed artifact into
this short-lived fixture process. Expected OS/CRT bindings come from explicit
file/hash inputs and disk exports, with exact caller-supplied API-set routes.
GetProcAddress is only a separate local oracle, NEVER the expected-address source.
This is not a cross-process import integrity/API-set/forwarder certificate.
"""
import argparse
import ctypes as ct
from dataclasses import asdict, dataclass
import json
from itertools import islice
from pathlib import Path, PureWindowsPath

import p99_wrist_abi_manifest as disk
import p99_wrist_mapped_image as mapped
from p99_wrist_import_graph import MAX_TOTAL_FILES, Provider, Resolution, Resolver, api_contract, module_name, read_provider
from p99_wrist_import_profile import IMPORTER, read_profile
from p99_wrist_mapped_windows import WindowsAPI, Reader

ARTIFACT = Path(__file__).resolve().parents[1] / 'scanner/build/tll_p99_wrist_abi.dll'


def locked_snapshot(kernel, path, handles):
    """Keep file locked through load/compare/unload, snapshot from SAME handle."""
    handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x80, None)
    disk.require(handle and handle != ct.c_void_p(-1).value, 'file lock failed: '+str(path))
    handles.append(handle)
    length, actual = ct.c_int64(), ct.c_uint32()
    disk.require(kernel.GetFileSizeEx(handle, ct.byref(length)) and
                 0 < length.value <= disk.MAX_FILE, 'snapshot length')
    buffer = (ct.c_ubyte*length.value)()
    disk.require(kernel.ReadFile(handle, buffer, length.value, ct.byref(actual), None) and
                 actual.value == length.value, 'snapshot read failed')
    return bytes(buffer)


def reference(kernel, name, refs):
    module = ct.c_void_p()
    disk.require(kernel.GetModuleHandleExW(0, name, ct.byref(module)) and module.value,
                 'selected dependency not loaded: '+name)
    refs.append(module.value)
    return module.value


def import_bindings(kernel, profile, snapshots, imports, refs):
    return _import_bindings(kernel, profile, snapshots, imports, refs, importer=IMPORTER)


def _import_bindings(kernel, profile, snapshots, imports, refs, *, importer):
    inventory = _import_inventory(kernel, profile, snapshots, imports, refs, importer=importer)
    return list(inventory.bindings), [asdict(row) for row in inventory.resolutions]


@dataclass(frozen=True)
class ImportInventory:
    importer: str
    providers: tuple[Provider, ...]
    api_routes: tuple
    resolutions: tuple[Resolution, ...]

    @property
    def bindings(self):
        return tuple(row.binding for row in self.resolutions)


def _import_inventory(kernel, profile, snapshots, imports, refs, *, importer):
    # Own a bounded version before callbacks and before the two passes below.
    # Iterators must not be exhausted into a silently empty binding result.
    imports = tuple({'dll': row['dll'], 'name': row['name']} for row in islice(imports, 4097))
    disk.require(0 < len(imports) <= 4096, 'owned import inventory count')
    providers = []
    for pin in profile.providers:
        base = reference(kernel, pin.path, refs)
        path = ct.create_unicode_buffer(32768)
        count = kernel.GetModuleFileNameW(base, path, len(path))
        disk.require(0 < count < len(path) and PureWindowsPath(path.value) == PureWindowsPath(pin.path),
                     'loaded provider path differs from reviewed profile')
        providers.append(read_provider(snapshots[pin.name], pin.sha256, pin.name, base))
    resolver = Resolver(providers, profile.api_routes)
    # Resolve ALL expected addresses before using any observed export oracle.
    resolved = [resolver.resolve(row['dll'], row['name'], importer=importer) for row in imports]
    sources = {}
    for row, resolution in zip(imports, resolved):
        dll = module_name(row['dll'])
        if dll not in sources:
            sources[dll] = reference(kernel, dll, refs)
            host = resolver.routes[(importer, dll)] if api_contract(dll) else dll
            disk.require(sources[dll] == resolver.providers[host].base,
                         'local loader source differs from explicit provider/API route')
        actual = kernel.GetProcAddress(sources[dll], row['name'].encode('ascii'))
        disk.require(actual == resolution.binding.address,
                     'local loader export differs from independent disk graph: '+dll+'!'+row['name'])
    return ImportInventory(importer, tuple(resolver.providers.values()), tuple(profile.api_routes), tuple(resolved))


def configure_loader(kernel):
    for name, result, args in (
        ('LoadLibraryExW', ct.c_void_p, [ct.c_wchar_p, ct.c_void_p, ct.c_uint32]),
        ('GetModuleHandleExW', ct.c_int, [ct.c_uint32, ct.c_wchar_p, ct.POINTER(ct.c_void_p)]),
        ('GetModuleHandleW', ct.c_void_p, [ct.c_wchar_p]),
        ('GetModuleFileNameW', ct.c_uint32, [ct.c_void_p, ct.c_wchar_p, ct.c_uint32]),
        ('GetProcAddress', ct.c_void_p, [ct.c_void_p, ct.c_char_p]),
        ('FreeLibrary', ct.c_int, [ct.c_void_p]),
        ('CreateFileW', ct.c_void_p, [ct.c_wchar_p, ct.c_uint32, ct.c_uint32, ct.c_void_p,
                                   ct.c_uint32, ct.c_uint32, ct.c_void_p]),
        ('GetFileSizeEx', ct.c_int, [ct.c_void_p, ct.POINTER(ct.c_int64)]),
        ('ReadFile', ct.c_int, [ct.c_void_p, ct.c_void_p, ct.c_uint32, ct.POINTER(ct.c_uint32), ct.c_void_p]),
        ('CloseHandle', ct.c_int, [ct.c_void_p]),
        ('GetFinalPathNameByHandleW', ct.c_uint32, [ct.c_void_p, ct.c_wchar_p, ct.c_uint32, ct.c_uint32]),
        ('GetCurrentProcess', ct.c_void_p, [])):
        fn = getattr(kernel, name)
        fn.restype, fn.argtypes = result, args


def run(trusted_sha256, profile):
    api = WindowsAPI()
    kernel = api.kernel
    configure_loader(kernel)
    handles, refs, base = [], [], None
    try:
        raw = locked_snapshot(kernel, ARTIFACT, handles)
        manifest = disk.manifest(raw, trusted_sha256)
        snapshots = {}
        for pin in profile.providers:
            snapshot = locked_snapshot(kernel, pin.path, handles)
            disk.require(disk.digest(snapshot) == pin.sha256, 'provider hash changed before load: '+pin.name)
            snapshots[pin.name] = snapshot
            disk.require(sum(map(len, snapshots.values())) <= MAX_TOTAL_FILES, 'provider aggregate byte bound')
        disk.require(not kernel.GetModuleHandleW(str(ARTIFACT)), 'artifact already loaded in fixture')
        # DLL directory + system32 only; never discover a DLL via PATH/cwd.
        base = kernel.LoadLibraryExW(str(ARTIFACT), None, 0x100 | 0x800)
        disk.require(base, 'owned artifact load failed')
        bindings, resolutions = import_bindings(kernel, profile, snapshots, manifest['imports'], refs)
        plan = mapped.build_plan(raw, trusted_sha256, base, bindings, header_base=mapped.HeaderBase.LOADED)
        handle = kernel.GetCurrentProcess()
        reader = Reader(handle, api.identity(handle), base, plan.size, api=api)
        try:
            result = mapped.compare(plan, reader)
        except ValueError as error:
            # Bounded diagnostic only. Never turn observed differences into
            # expected values, a wildcard mask, or installation permission.
            differences = []
            for region in plan.regions:
                for offset in range(0, len(region.expected), mapped.CHUNK):
                    expected = region.expected[offset:offset+mapped.CHUNK]
                    actual = reader.read(base+region.rva+offset, len(expected), region.protection)
                    for at in range(0, len(expected), 8):
                        if actual[at:at+8] != expected[at:at+8] and len(differences) < 32:
                            differences.append({'region': region.name, 'rva': hex(region.rva+offset+at),
                                                'expected': expected[at:at+8].hex(), 'actual': actual[at:at+8].hex()})
            result = {'comparison_passed': False, 'admitted_for_install': False,
                      'failure': str(error), 'differences': differences}
        # No GetProcAddress is used on the artifact; role RVAs are trusted disk.
        result['fixture_only'] = True
        result['base'] = hex(base)
        result['exclusions'] = [vars(e) for e in plan.exclusions]
        result['import_resolutions'] = resolutions
        result['owned_provider_refs_held_during_comparison'] = True
        result['expected_bindings_from_disk_graph'] = True
        result['api_set_routes_are_caller_inputs'] = True
        return result
    finally:
        released = True
        if base:
            released = bool(kernel.FreeLibrary(base)) and released
            released = not kernel.GetModuleHandleW(str(ARTIFACT)) and released
        for module in reversed(refs):
            released = bool(kernel.FreeLibrary(module)) and released
        for handle in reversed(handles):
            released = bool(kernel.CloseHandle(handle)) and released
        disk.require(released, 'owned loader reference release failed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--provider-profile', type=Path, required=True,
                        help='Externally reviewed local-only paths/hashes/API-set inputs; no auto-discovery')
    args = parser.parse_args()
    disk.require(args.provider_profile.stat().st_size <= 65536, 'profile file bound')
    profile = read_profile(args.provider_profile.read_text(encoding='utf-8'))
    result = run(args.sha256, profile)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['comparison_passed'] else 1)
