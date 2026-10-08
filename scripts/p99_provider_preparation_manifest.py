#!/usr/bin/env python3
"""Offline contract for the separately reviewed ten-export P99 preparation DLL.

No DLL loading, execution, process access, profile discovery or admission. This
is deliberately NOT the old eight-export inert-ABI schema. A caller must supply
an independently reviewed build hash; the exact root-import allowlist does not
certify implementations, transitive dependencies, API-set routes or lifetime.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from types import MappingProxyType

import p99_wrist_abi_manifest as abi

SCHEMA = 'tll-p99-provider-preparation-disk-v1'
REQUEST_VERSION, REQUEST_BYTES = 1, 133952
ENTRY = 'TllP99PrepareProvidersV1'
PROVIDERS = 'TllP99ProviderPreparedV1'
OFFER = 'TllP99WristPreparedV1'
ROLES = MappingProxyType({**abi.ROLES,
    ENTRY: 'prepare:uint32(const RequestV1*,uint32):same-build-x64-only',
    PROVIDERS: 'data:zero-RW-page4096',
})
PAGES = MappingProxyType({OFFER: '.p99prep', PROVIDERS: '.p99deps'})
DISCARDABLE = 0x02000000

# Exact reviewed direct imports of this separate build contract, not a broad
# "preparation is allowed" exemption. Compiler/dependency changes require review.
IMPORTS = MappingProxyType({
    'bcrypt.dll': frozenset(('BCryptHash', 'BCryptCloseAlgorithmProvider',
                            'BCryptOpenAlgorithmProvider', 'BCryptGenRandom')),
    'kernel32.dll': frozenset(('InitializeSListHead', 'GetSystemTimeAsFileTime',
        'K32GetMappedFileNameW', 'GetLastError', 'SetLastError', 'FlsAlloc',
        'FlsGetValue', 'FlsSetValue', 'FlsFree', 'IsThreadAFiber',
        'QueryPerformanceCounter', 'GetCurrentThreadId', 'GetTickCount64',
        'VirtualQuery', 'GetModuleHandleW', 'GetModuleHandleExW',
        'QueryPerformanceFrequency', 'GetProcessTimes', 'GetCurrentProcess',
        'GetCurrentProcessId', 'FlushInstructionCache', 'GetSystemInfo',
        'VirtualAlloc', 'VirtualProtect', 'VirtualFree', 'WaitForSingleObject',
        'GetProcessId', 'VirtualQueryEx', 'ReadProcessMemory')),
    'msvcp140.dll': frozenset(('?_Xout_of_range@std@@YAXPEBD@Z',)),
    'vcruntime140.dll': frozenset(('__std_type_info_destroy_list', '__std_terminate',
        '__std_exception_copy', '__std_exception_destroy', '_CxxThrowException',
        '__C_specific_handler', '__C_specific_handler_noexcept',
        'memcmp', 'memcpy', 'memmove', 'memset')),
    'vcruntime140_1.dll': frozenset(('__CxxFrameHandler4',)),
    'api-ms-win-crt-heap-l1-1-0.dll': frozenset(('free', 'malloc', '_callnewh')),
    'api-ms-win-crt-runtime-l1-1-0.dll': frozenset(('_seh_filter_dll',
        '_initialize_narrow_environment', '_initialize_onexit_table',
        '_register_onexit_function', '_execute_onexit_table', '_crt_atexit',
        '_cexit', '_initterm', '_initterm_e', '_configure_narrow_argv')),
})


class Image(abi.Image):
    def exports(self):
        return self._exports(ROLES)


def manifest(raw, expected_sha256):
    raw = bytes(raw)
    image = Image(raw, expected_sha256)
    exports, runtime = image.exports(), image.runtime_functions()
    functions = {r['begin']: r for r in runtime}
    roles = {}
    for name, role in ROLES.items():
        rva = exports[name]
        if name in PAGES:
            section = image.section(rva, 4096)
            abi.require(section['name'] == PAGES[name] and section['rva'] == rva and
                section['virtual_size'] == section['raw_size'] == 4096 and
                section['characteristics'] & (abi.READ | abi.WRITE | abi.EXECUTE) == abi.READ | abi.WRITE and
                not section['characteristics'] & DISCARDABLE,
                'isolated named RW publication page required')
            abi.require(not any(image.read(rva, 4096)), 'publication page must be unpublished zero bytes')
            roles[name] = {'role': role, 'rva': rva, 'size': 4096}
        else:
            abi.require(rva in functions, 'export must start at exact runtime-function boundary')
            abi.require(functions[rva]['owner'] == rva, 'export must be the primary unwind entry')
            fragments = [{'rva': r['begin'], 'size': r['end']-r['begin'], 'unwind': r['unwind'],
                          'disk_sha256': abi.digest(image.code(r['begin'], r['end']-r['begin']))}
                         for r in runtime if r['owner'] == rva]
            roles[name] = {'role': role, 'rva': rva, 'fragments': fragments}
    imports = image.imports()
    expected = {(dll, name) for dll, names in IMPORTS.items() for name in names}
    abi.require({(i['dll'].lower(), i['name']) for i in imports} == expected,
                'exact reviewed preparation import inventory required')
    relocations = image.relocations()
    entry = exports[ENTRY]
    abi.require(functions[entry]['end'] >= entry+32, 'preparation prefix exceeds primary runtime range')
    prefix = image.code(entry, 32)
    abi.require(not any(r['rva'] < entry+32 and entry < r['rva']+8 for r in relocations),
                'preparation prefix contains DIR64 fixup')
    # A zero disk publication page must also remain zero when relocated.
    abi.require(not any(r['rva'] < exports[n]+4096 and exports[n] < r['rva']+8
                        for r in relocations for n in PAGES), 'publication page relocation')
    return {'schema': SCHEMA, 'admitted_for_install': False, 'loaded_image_verified': False,
            'dependency_closure_verified': False, 'preparation_executed': False,
            'file_sha256': abi.digest(raw), 'file_size': len(raw),
            'preferred_base': image.pe.OPTIONAL_HEADER.ImageBase,
            'size_of_image': image.pe.OPTIONAL_HEADER.SizeOfImage,
            'entry_rva': image.pe.OPTIONAL_HEADER.AddressOfEntryPoint,
            'headers_sha256': abi.digest(raw[:image.pe.OPTIONAL_HEADER.SizeOfHeaders]),
            'roles': roles, 'patch_rvas': [exports[n] for n in abi.PATCH_ORDER],
            'preparation_entry': {'rva': entry, 'request_version': REQUEST_VERSION,
                'request_bytes': REQUEST_BYTES, 'prefix_size': 32,
                'prefix_hex': prefix.hex(), 'prefix_sha256': abi.digest(prefix)},
            'sections': image.sections, 'runtime_functions': runtime,
            'dir64_relocations': relocations, 'imports': imports,
            'limits': ['Disk structure/hash only; no loaded code, IAT, process identity or lifetime proof.',
                'Exact direct imports do not cover provider dependencies, API-set routes or indirect calls.',
                'Entry and page locators do not prove DllMain/entry behavior or provider retention.',
                'Unwind opcode/frame/handler semantics are not fully decoded; complete sections are hashed.',
                'No preparation, hook, transaction, ACK, arm or game-test admission.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--sha256', required=True, help='independently reviewed build hash')
    parser.add_argument('--check', type=Path, help='exact previously reviewed manifest')
    parser.add_argument('--summary', action='store_true')
    args = parser.parse_args()
    abi.require(args.dll.stat().st_size <= abi.MAX_FILE, 'file size bound')
    result = manifest(args.dll.read_bytes(), args.sha256)
    if args.check:
        abi.require(json.loads(args.check.read_text(encoding='utf-8-sig')) == result, 'manifest mismatch')
    if args.summary:
        result = {k: result[k] for k in ('schema', 'file_sha256', 'size_of_image', 'roles',
            'preparation_entry', 'admitted_for_install', 'loaded_image_verified', 'dependency_closure_verified')}
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
