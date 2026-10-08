"""Owned local loader/RPM exam for the fixed ten-export preparation DLL.

Never calls ANY artifact export, including its explicit preparation entry. No
arbitrary PID/path, target injection, input, patching, FLS setup or PIN request.
Pre-reviewed provider snapshots and exact API routes produce expectations before
GetProcAddress is used solely as a local loader oracle. No closure/admission.
"""
import argparse
from dataclasses import asdict
import json
from pathlib import Path

import p99_provider_preparation_manifest as disk
import p99_provider_preparation_mapped as preparation
import p99_wrist_abi_manifest as abi
import p99_wrist_mapped_image as mapped
from p99_provider_preparation_profile import IMPORTER, read_profile
from p99_wrist_import_graph import MAX_TOTAL_FILES
from p99_wrist_mapped_fixture import configure_loader, locked_snapshot, _import_bindings, _import_inventory
from p99_wrist_mapped_windows import Reader
from p99_provider_evidence import ModuleAPI, file_native_path
from p99_preparation_provider_evidence import compare_inventory

ARTIFACT = Path(__file__).resolve().parents[1] / 'scanner/build/tll_p99_provider_preparation.dll'


def import_bindings(kernel, profile, snapshots, imports, refs):
    return _import_bindings(kernel, profile, snapshots, imports, refs, importer=IMPORTER)


def import_inventory(kernel, profile, snapshots, imports, refs):
    return _import_inventory(kernel, profile, snapshots, imports, refs, importer=IMPORTER)


def _zero_pages(api, handle, identity, base, manifest):
    """Dedicated pre-preparation observation, NOT a general RW/claims reader."""
    reader = Reader(handle, identity, base, manifest['size_of_image'], api=api)
    for name in disk.PAGES:
        address = base+manifest['roles'][name]['rva']
        abi.require(address % 4096 == 0 and base <= address and
                    address+4096 <= base+manifest['size_of_image'], 'unpublished page range')
        reader.boundary(base, manifest['size_of_image'])
        def query():
            q = api.query(handle, address)
            first, size = q.BaseAddress or 0, q.RegionSize
            abi.require(q.AllocationBase == base and q.State == 0x1000 and q.Type == 0x1000000 and
                q.Protect in (0x04, 0x08) and mapped._address(first, size) and first <= address and
                address+4096 <= first+size, 'unpublished page identity/RW extent')
            return first, size, q.AllocationBase, q.State, q.Type, q.Protect, q.AllocationProtect
        before = query()
        raw = api.read(handle, address, 4096)
        abi.require(isinstance(raw, bytes) and raw == bytes(4096), 'unpublished page must remain zero')
        abi.require(query() == before, 'unpublished page metadata changed during read')
        reader.boundary(base, manifest['size_of_image'])


def _release_owned(kernel, base, refs, handles):
    """Best effort, once per acquisition; never retry false/ambiguous releases."""
    failed = []
    def attempt(label, operation):
        try:
            if not operation():
                failed.append(label)
        except Exception as error:
            failed.append(label+':'+type(error).__name__)
    if base:
        attempt('artifact release', lambda: kernel.FreeLibrary(base))
        attempt('artifact unload', lambda: not kernel.GetModuleHandleW(str(ARTIFACT)))
    # Consume each ownership token BEFORE calling the OS. Repeated module values
    # intentionally remain separate acquisitions, never deduplicated or retried.
    while refs:
        module = refs.pop()
        attempt('provider release', lambda: kernel.FreeLibrary(module))
    while handles:
        handle = handles.pop()
        attempt('file close', lambda: kernel.CloseHandle(handle))
    abi.require(not failed, 'owned loader cleanup failed: '+', '.join(failed))


def run(trusted_sha256, profile_text):
    # Validate strict scope BEFORE constructing any OS API or acquiring resources.
    profile = read_profile(profile_text)
    api = ModuleAPI()
    kernel = api.kernel
    configure_loader(kernel)
    handles, refs, base = [], [], None
    try:
        raw = locked_snapshot(kernel, ARTIFACT, handles)
        manifest = disk.manifest(raw, trusted_sha256)
        snapshots, native_paths = {}, {}
        for pin in profile.providers:
            snapshot = locked_snapshot(kernel, pin.path, handles)
            abi.require(abi.digest(snapshot) == pin.sha256, 'provider hash changed before load: '+pin.name)
            snapshots[pin.name] = snapshot
            native_paths[pin.name] = file_native_path(kernel, handles[-1])
            abi.require(sum(map(len, snapshots.values())) <= MAX_TOTAL_FILES, 'provider aggregate byte bound')
        abi.require(not kernel.GetModuleHandleW(str(ARTIFACT)), 'artifact already loaded in fixture')
        base = kernel.LoadLibraryExW(str(ARTIFACT), None, 0x100 | 0x800)
        abi.require(base, 'owned artifact load failed')
        inventory = import_inventory(kernel, profile, snapshots, manifest['imports'], refs)
        plan = preparation.build_plan(raw, trusted_sha256, base, inventory.bindings, header_base=mapped.HeaderBase.LOADED)
        handle = kernel.GetCurrentProcess()
        identity = api.identity(handle)
        _zero_pages(api, handle, identity, base, manifest)
        evidence_before = compare_inventory(handle, identity, inventory, manifest['imports'], native_paths, api=api)
        result = mapped.compare(plan, Reader(handle, identity, base, plan.size, api=api))
        evidence_after = compare_inventory(handle, identity, inventory, manifest['imports'], native_paths, api=api)
        _zero_pages(api, handle, identity, base, manifest)
        result.update(schema='p99-preparation-owned-loader-exam-v1', fixture_only=True,
            pid=identity.pid, creation=identity.creation, base=hex(base),
            exclusions=[vars(e) for e in plan.exclusions], import_resolutions=[asdict(r) for r in inventory.resolutions],
            provider_files=len(profile.providers), owned_provider_refs_held_during_comparison=True,
            expected_bindings_from_disk_graph=True, api_set_routes_are_caller_inputs=True,
            both_publication_pages_observed_zero=True, preparation_entry_called=False,
            artifact_exports_called=False, dependency_closure_verified=False,
            provider_mapped_bytes_verified=False, api_set_schema_verified=False,
            partial_provider_evidence_matched=True,
            provider_evidence_bytes_per_pass=evidence_after['compared_bytes'],
            provider_entry_prefixes=evidence_after['entry_prefixes'],
            provider_evidence_before=evidence_before, provider_evidence_after=evidence_after)
    finally:
        # Only this fixed non-prepared owned fixture may release its initial ref.
        # This is NOT cleanup guidance for the process-lifetime preparation Owner.
        _release_owned(kernel, base, refs, handles)
    result['owned_cleanup_succeeded'] = True
    result['artifact_unloaded_after_comparison'] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--provider-profile', type=Path, required=True)
    parser.add_argument('--summary', action='store_true')
    args = parser.parse_args()
    abi.require(args.provider_profile.stat().st_size <= 65536, 'profile file bound')
    result = run(args.sha256, args.provider_profile.read_text(encoding='utf-8-sig'))
    if args.summary:
        result = {k: v for k, v in result.items() if k not in
                  ('exclusions', 'import_resolutions', 'provider_evidence_before', 'provider_evidence_after')}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
