"""OWNED child-only cross-process evidence exam. No external PID or game.

The only DLL loaded is the fixed, hash-reviewed inert ABI artifact. No export
is called. Child owns full-path references; the parent never treats those local
HMODULE values or a child PIN report as its own lifetime certificate. Three
protocol actions drop the load ref, drop the extra ref, then exit normally.
"""
import argparse
import ctypes as ct
import json
from pathlib import Path
import queue
import subprocess
import sys
import threading

from p99_provider_evidence import ModuleAPI, compare_provider, native_path, PATH_CAPACITY
from p99_wrist_abi_manifest import require, manifest, digest
from p99_wrist_import_graph import MAX_TOTAL_FILES, Resolver, read_provider
from p99_wrist_import_profile import IMPORTER, read_profile
from p99_wrist_mapped_fixture import ARTIFACT, locked_snapshot, reference, import_bindings
from p99_wrist_mapped_windows import Reader
import p99_wrist_mapped_image as mapped

SYMBOL = 'TllP99WristAsyncV1'
WAIT_SECONDS = 10


def api_setup():
    api = ModuleAPI()
    for name, result, args in (
        ('LoadLibraryExW', ct.c_void_p, [ct.c_wchar_p, ct.c_void_p, ct.c_uint32]),
        ('GetModuleHandleExW', ct.c_int, [ct.c_uint32, ct.c_wchar_p, ct.POINTER(ct.c_void_p)]),
        ('FreeLibrary', ct.c_int, [ct.c_void_p]),
        ('GetModuleHandleW', ct.c_void_p, [ct.c_wchar_p]),
        ('GetModuleFileNameW', ct.c_uint32, [ct.c_void_p, ct.c_wchar_p, ct.c_uint32]),
        ('GetProcAddress', ct.c_void_p, [ct.c_void_p, ct.c_char_p]),
        ('CreateFileW', ct.c_void_p, [ct.c_wchar_p, ct.c_uint32, ct.c_uint32, ct.c_void_p,
                                   ct.c_uint32, ct.c_uint32, ct.c_void_p]),
        ('GetFileSizeEx', ct.c_int, [ct.c_void_p, ct.POINTER(ct.c_int64)]),
        ('ReadFile', ct.c_int, [ct.c_void_p, ct.c_void_p, ct.c_uint32, ct.POINTER(ct.c_uint32), ct.c_void_p]),
        ('CloseHandle', ct.c_int, [ct.c_void_p]),
        ('GetFinalPathNameByHandleW', ct.c_uint32, [ct.c_void_p, ct.c_wchar_p, ct.c_uint32, ct.c_uint32]),
        ('GetCurrentProcess', ct.c_void_p, [])):
        fn = getattr(api.kernel, name)
        fn.restype, fn.argtypes = result, args
    return api


def file_native_path(kernel, handle):
    buffer = ct.create_unicode_buffer(PATH_CAPACITY)
    count = kernel.GetFinalPathNameByHandleW(handle, buffer, len(buffer), 2)  # VOLUME_NAME_NT
    require(0 < count < len(buffer) and len(buffer.value) == count, 'independent file path failed/truncated')
    return native_path(buffer.value)


def drop(kernel, handle):
    # Caller revokes its ownership variable BEFORE calling: no retry if an API
    # ever reports false after effect. Process exit handles fixture failure.
    require(kernel.FreeLibrary(handle), 'owned FreeLibrary failed/uncertain')


def close_files(kernel, handles):
    okay = True
    for handle in reversed(handles):
        okay = bool(kernel.CloseHandle(handle)) and okay
    require(okay, 'owned file handle cleanup failed')


def cleanup_parent(child, kernel, handles):
    """Attempt every owned cleanup even if another operation throws/refuses."""
    errors = []
    if child is not None:
        try:
            if child.poll() is None:
                child.kill()  # Only this exact Popen-owned fixture child.
        except Exception as error:
            errors.append('owned child termination: '+str(error))
        try:
            child.wait(timeout=WAIT_SECONDS)
        except Exception as error:
            errors.append('owned child wait: '+str(error))
        for stream in (child.stdin, child.stdout, child.stderr):
            try:
                if stream is not None:
                    stream.close()
            except Exception as error:
                errors.append('owned stream close: '+str(error))
    for handle in reversed(handles):
        try:
            require(kernel.CloseHandle(handle), 'CloseHandle refused')
        except Exception as error:
            errors.append('owned file close: '+str(error))
    require(not errors, '; '.join(errors))


def profile_inputs(kernel, profile_path, handles):
    require(profile_path.stat().st_size <= 65536, 'profile file bound')
    profile = read_profile(profile_path.read_text(encoding='utf-8'))
    snapshots, paths = {}, {}
    for pin in profile.providers:
        raw = locked_snapshot(kernel, pin.path, handles)
        require(digest(raw) == pin.sha256, 'provider file pin mismatch: '+pin.name)
        snapshots[pin.name] = raw
        paths[pin.name] = file_native_path(kernel, handles[-1])
        require(sum(map(len, snapshots.values())) <= MAX_TOTAL_FILES, 'provider aggregate byte bound')
    return profile, snapshots, paths


def child_exam(sha256, mode, profile_path):
    api = api_setup()
    kernel = api.kernel
    handles, refs, provider_refs = [], [], []
    initial, retained = None, None
    try:
        raw = locked_snapshot(kernel, ARTIFACT, handles)
        artifact_manifest = manifest(raw, sha256)  # Inert artifact contract BEFORE any load.
        path = file_native_path(kernel, handles[0])
        profile, snapshots, _ = profile_inputs(kernel, profile_path, handles)
        require(not kernel.GetModuleHandleW(str(ARTIFACT)), 'fixture artifact already loaded')
        initial = kernel.LoadLibraryExW(str(ARTIFACT), None, 0x100 | 0x800)
        require(initial, 'owned artifact load failed')
        retained = reference(kernel, str(ARTIFACT), refs)
        require(retained == initial, 'full-path retained reference mismatch')
        # Retain OS/CRT refs in THIS child. The parent does not inherit their
        # meaning or derive expectations from any observed export pointer.
        import_bindings(kernel, profile, snapshots, artifact_manifest['imports'], provider_refs)
        bases = [{'name': pin.name, 'base': reference(kernel, pin.path, provider_refs)}
                 for pin in profile.providers]
        provider = read_provider(raw, sha256, ARTIFACT.name, initial)
        rva = provider.lookup(SYMBOL).rva
        process = kernel.GetCurrentProcess()
        identity = api.identity(process)
        # Validate after acquiring the full-path reference, BEFORE irreversible PIN.
        compare_provider(process, identity, provider, path, [rva], api=api)
        if mode == 'pin':
            pinned = ct.c_void_p()
            anchor = ct.cast(ct.c_void_p(provider.base+rva), ct.c_wchar_p)
            require(kernel.GetModuleHandleExW(5, anchor, ct.byref(pinned)) and pinned.value == initial,
                    'owned PIN failed/uncertain')
        print(json.dumps({'stage': 'held', 'pid': identity.pid, 'creation': identity.creation,
                          'base': provider.base, 'size': provider.size, 'providers': bases}), flush=True)
        for command in ('drop-load', 'drop-ref', 'exit'):
            require(sys.stdin.readline(64).rstrip('\n') == command, 'fixture protocol order')
            if command == 'drop-load':
                releasing, initial = initial, None
                drop(kernel, releasing)
            elif command == 'drop-ref':
                releasing, retained = retained, None
                drop(kernel, releasing)
            print(json.dumps({'stage': command}), flush=True)
    finally:
        # Never release a PIN handle. These are only references we actually own.
        okay = True
        if retained:
            okay = bool(kernel.FreeLibrary(retained)) and okay
        if initial:
            okay = bool(kernel.FreeLibrary(initial)) and okay
        for module in reversed(provider_refs):
            okay = bool(kernel.FreeLibrary(module)) and okay
        close_files(kernel, handles)
        require(okay, 'owned module cleanup failed')


def read_lines(stream, messages):
    try:
        while True:
            line = stream.readline(65537)
            if not line:
                messages.put(None)
                return
            require(len(line) <= 65536 and line.endswith('\n'), 'oversized fixture message')
            messages.put(json.loads(line))
    except Exception as error:
        messages.put(error)


def parent_exam(sha256, mode, profile_path):
    require(mode in ('lease', 'pin'), 'fixture mode')
    api, handles, child = api_setup(), [], None
    kernel = api.kernel
    try:
        raw = locked_snapshot(kernel, ARTIFACT, handles)
        artifact_manifest = manifest(raw, sha256)
        expected_path = file_native_path(kernel, handles[0])
        profile, snapshots, paths = profile_inputs(kernel, profile_path, handles)
        # Fixed this-script child, not a discovered/external process. Hidden on Windows.
        child = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--child', mode,
                                  '--sha256', sha256, '--provider-profile', str(profile_path.resolve())],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True, encoding='utf-8',
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        messages = queue.Queue()
        worker = threading.Thread(target=read_lines, args=(child.stdout, messages), daemon=True)
        worker.start()

        def receive(stage):
            row = messages.get(timeout=WAIT_SECONDS)
            if row is None:
                child.wait(timeout=WAIT_SECONDS)
                raise ValueError('owned child exited before '+stage+': '+child.stderr.read(8192))
            require(isinstance(row, dict) and row.get('stage') == stage,
                    'child failed/protocol mismatch: '+str(row))
            return row

        def send(command):
            child.stdin.write(command+'\n')
            child.stdin.flush()
            return receive(command)

        row = receive('held')
        # Borrow only the handle of the child Popen actually created. No OpenProcess.
        borrowed = int(child._handle)
        identity = api.identity(borrowed)
        require(identity.pid == child.pid == row.get('pid') and identity.creation == row.get('creation'),
                'owned child process identity mismatch')
        provider = read_provider(raw, sha256, ARTIFACT.name, row['base'])
        require(provider.size == row.get('size'), 'child module extent differs from trusted file')
        rva = provider.lookup(SYMBOL).rva

        declared = row.get('providers')
        require(isinstance(declared, list) and len(declared) == len(profile.providers) and
                all(isinstance(item, dict) and set(item) == {'name', 'base'} for item in declared),
                'child provider inventory shape')
        bases = {item['name']: item['base'] for item in declared}
        require(len(bases) == len(declared) and set(bases) == {pin.name for pin in profile.providers},
                'child provider inventory identities')
        providers = [read_provider(snapshots[pin.name], pin.sha256, pin.name, bases[pin.name])
                     for pin in profile.providers]
        resolver = Resolver(providers, profile.api_routes)
        resolutions = [resolver.resolve(item['dll'], item['name'], importer=IMPORTER)
                       for item in artifact_manifest['imports']]
        dependency_evidence = []
        for selected in providers:
            entries = sorted({r.rva for r in resolutions if r.provider == selected.name})
            # This fixed five-provider fixture expects each one to have a reached
            # terminal function. Do not invent an RX anchor for a forwarder.
            require(entries, 'owned fixture profile has no selected terminal export')
            dependency_evidence.append(compare_provider(borrowed, identity, selected,
                                                        paths[selected.name], entries, api=api))
        artifact_plan = mapped.build_plan(raw, sha256, provider.base,
            [r.binding for r in resolutions], header_base=mapped.HeaderBase.LOADED)
        artifact_reader = Reader(borrowed, identity, provider.base, provider.size, api=api)
        artifact_result = mapped.compare(artifact_plan, artifact_reader)

        def evidence():
            return compare_provider(borrowed, identity, provider, expected_path, [rva], api=api)

        first = evidence()
        send('drop-load')
        retained = evidence()
        require(mapped.compare(artifact_plan, artifact_reader)['comparison_passed'],
                'artifact changed after initial reference release')
        send('drop-ref')
        if mode == 'lease':
            try:
                evidence()
            except ValueError as error:
                after = {'refused_after_final_reference': True, 'reason': str(error)}
            else:
                raise ValueError('unloaded owned module unexpectedly accepted')
            # Require actual unmapped allocation, not just an unrelated mismatch.
            region = api.query(borrowed, provider.base)
            require(region.State == 0x10000 and not region.AllocationBase, 'owned DLL did not become MEM_FREE')
        else:
            after = evidence()
        send('exit')
        require(child.wait(timeout=WAIT_SECONDS) == 0, 'child exit failed')
        try:
            evidence()
        except ValueError as error:
            require('process no longer live' in str(error), 'unexpected post-exit refusal')
        else:
            raise ValueError('exited process unexpectedly accepted')
        return {'fixture_only': True, 'mode': mode, 'initial': first, 'after_drop_load': retained,
                'remote_provider_evidence': dependency_evidence,
                'remote_artifact_comparison': artifact_result,
                'after_drop_ref': after, 'process_exit_refused': True,
                'child_exit_code': child.returncode, 'module_lifetime_verified': False,
                'admitted_for_install': False}
    finally:
        cleanup_parent(child, kernel, handles)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--provider-profile', type=Path, required=True)
    parser.add_argument('--child', choices=('lease', 'pin'), help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.child:
        child_exam(args.sha256, args.child, args.provider_profile)
    else:
        print(json.dumps([parent_exam(args.sha256, mode, args.provider_profile) for mode in ('lease', 'pin')], indent=2))
