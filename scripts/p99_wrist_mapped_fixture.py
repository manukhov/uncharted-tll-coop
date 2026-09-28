"""Owned Windows loader/RPM exam. Fixed inert artifact only, no external PID.

No game or exported adapter is called. This loads the reviewed artifact into
this short-lived fixture process, resolves ONLY its named OS/CRT imports, and
releases owned loader references. Bindings are local fixture evidence only;
they are not a cross-process import integrity/API-set/forwarder certificate.
"""
import argparse
import ctypes as ct
import json
from pathlib import Path

import p99_wrist_abi_manifest as disk
import p99_wrist_mapped_image as mapped
from p99_wrist_mapped_windows import WindowsAPI, Reader

ARTIFACT = Path(__file__).resolve().parents[1] / 'scanner/build/tll_p99_wrist_abi.dll'


def run(trusted_sha256):
    api = WindowsAPI()
    kernel = api.kernel
    for name, result, args in (
        ('LoadLibraryExW', ct.c_void_p, [ct.c_wchar_p, ct.c_void_p, ct.c_uint32]),
        ('GetModuleHandleExW', ct.c_int, [ct.c_uint32, ct.c_wchar_p, ct.POINTER(ct.c_void_p)]),
        ('GetModuleHandleW', ct.c_void_p, [ct.c_wchar_p]),
        ('GetProcAddress', ct.c_void_p, [ct.c_void_p, ct.c_char_p]),
        ('FreeLibrary', ct.c_int, [ct.c_void_p]),
        ('CreateFileW', ct.c_void_p, [ct.c_wchar_p, ct.c_uint32, ct.c_uint32, ct.c_void_p,
                                   ct.c_uint32, ct.c_uint32, ct.c_void_p]),
        ('GetFileSizeEx', ct.c_int, [ct.c_void_p, ct.POINTER(ct.c_int64)]),
        ('ReadFile', ct.c_int, [ct.c_void_p, ct.c_void_p, ct.c_uint32, ct.POINTER(ct.c_uint32), ct.c_void_p]),
        ('CloseHandle', ct.c_int, [ct.c_void_p]),
        ('GetCurrentProcess', ct.c_void_p, [])):
        fn = getattr(kernel, name)
        fn.restype, fn.argtypes = result, args
    # Keep a deny-write/delete handle through trust check, load, comparison and
    # unload. Hash the snapshot from THIS handle, not a separately reopened path.
    file_handle = kernel.CreateFileW(str(ARTIFACT), 0x80000000, 1, None, 3, 0x80, None)
    disk.require(file_handle and file_handle != ct.c_void_p(-1).value, 'artifact lock failed')
    refs, base = {}, None
    try:
        length, actual = ct.c_int64(), ct.c_uint32()
        disk.require(kernel.GetFileSizeEx(file_handle, ct.byref(length)) and
                     0 < length.value <= disk.MAX_FILE, 'artifact length')
        buffer = (ct.c_ubyte*length.value)()
        disk.require(kernel.ReadFile(file_handle, buffer, length.value, ct.byref(actual), None) and
                     actual.value == length.value, 'artifact snapshot read failed')
        raw = bytes(buffer)
        manifest = disk.manifest(raw, trusted_sha256)
        disk.require(not kernel.GetModuleHandleW(str(ARTIFACT)), 'artifact already loaded in fixture')
        # DLL directory + system32 only; never discover a DLL via PATH/cwd.
        base = kernel.LoadLibraryExW(str(ARTIFACT), None, 0x100 | 0x800)
        disk.require(base, 'owned artifact load failed')
        bindings = []
        for row in manifest['imports']:
            name = row['dll']
            if name not in refs:
                module = ct.c_void_p()
                disk.require(kernel.GetModuleHandleExW(0, name, ct.byref(module)), 'named dependency not loaded')
                refs[name] = module.value
            address = kernel.GetProcAddress(refs[name], row['name'].encode('ascii'))
            disk.require(address, 'named dependency export missing')
            bindings.append(mapped.Binding(name, row['name'], address))
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
        return result
    finally:
        released = True
        for module in reversed(list(refs.values())):
            released = bool(kernel.FreeLibrary(module)) and released
        if base:
            released = bool(kernel.FreeLibrary(base)) and released
            released = not kernel.GetModuleHandleW(str(ARTIFACT)) and released
        released = bool(kernel.CloseHandle(file_handle)) and released
        disk.require(released, 'owned loader reference release failed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sha256', required=True)
    args = parser.parse_args()
    result = run(args.sha256)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['comparison_passed'] else 1)
