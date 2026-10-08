"""Partial provider evidence for the new, selected owned preparation inventory.

Replays exact expected roots before reading memory, rather than trusting mutable
reports or taking entry RVAs from observed exports. This is NOT full provider
code, import closure, API-set namespace, file-object identity or lifetime proof.
"""
from itertools import islice

from p99_provider_evidence import compare_provider, native_path, comparison_spans
from p99_provider_preparation_profile import IMPORTER, MAX_DEPENDENCIES
from p99_wrist_abi_manifest import require
from p99_wrist_import_graph import Resolution, Resolver, module_name
from p99_wrist_mapped_fixture import ImportInventory


def compare_inventory(handle, identity, inventory, expected_imports, expected_paths, *, api):
    require(isinstance(inventory, ImportInventory) and inventory.importer == IMPORTER,
            'preparation evidence inventory/importer')
    providers = tuple(islice(inventory.providers, MAX_DEPENDENCIES+1))
    rows = tuple(islice(inventory.resolutions, 4097))
    require(0 < len(providers) <= MAX_DEPENDENCIES and 0 < len(rows) <= 4096,
            'preparation evidence inventory bound')
    roots = tuple((module_name(r['dll']), r['name']) for r in islice(expected_imports, 4097))
    require(0 < len(roots) <= 4096 and len(set(roots)) == len(roots), 'expected root count/duplicate')
    require(all(isinstance(r, Resolution) for r in rows), 'resolution type')
    keys = tuple((r.binding.dll, r.binding.name) for r in rows)
    require(len(keys) == len(roots) and len(set(keys)) == len(keys) and set(keys) == set(roots),
            'resolution inventory differs from independent expected roots')
    resolver = Resolver(providers, inventory.api_routes)  # reparses/copies each pinned provider
    require(isinstance(expected_paths, dict) and set(expected_paths) == set(resolver.providers),
            'exact independently selected provider paths required')
    paths = {name: native_path(path) for name, path in expected_paths.items()}
    require(len(set(paths.values())) == len(paths), 'duplicate independent native path')
    entries = {name: set() for name in resolver.providers}
    for row in rows:
        replay = resolver.resolve(row.binding.dll, row.binding.name, importer=IMPORTER)
        require(row == replay, 'resolution differs from pinned export graph')
        entries[replay.provider].add(replay.rva)
    # Do not silently omit an unused or forwarder-only selected provider. This
    # narrow entry-prefix verifier requires a terminal entry for EACH selection.
    require(all(entries.values()), 'selected provider lacks terminal entry evidence')
    selected = []
    for name, provider in resolver.providers.items():
        rvas = tuple(sorted(entries[name]))
        comparison_spans(provider, rvas)  # validate ALL expectations before any process read
        selected.append((provider, paths[name], rvas))
    results = [compare_provider(handle, identity, provider, path, rvas, api=api)
               for provider, path, rvas in selected]
    return {'schema': 'p99-preparation-provider-evidence-v1', 'partial_evidence_matched': True,
            'provider_count': len(results), 'root_imports': len(roots),
            'entry_prefixes': sum(len(rvas) for _, _, rvas in selected),
            'compared_bytes': sum(r['compared_bytes'] for r in results), 'providers': results,
            'full_provider_bytes_verified': False, 'dependency_closure_verified': False,
            'api_set_schema_verified': False, 'mapped_file_object_identity_verified': False,
            'module_lifetime_verified': False, 'atomic_snapshot': False, 'admitted_for_install': False}
