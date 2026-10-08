import dataclasses
from itertools import repeat
import unittest

from p99_preparation_provider_evidence import compare_inventory
from p99_provider_preparation_profile import IMPORTER
from p99_wrist_import_graph import ApiRoute, Resolver
from p99_wrist_mapped_fixture import ImportInventory
from test_p99_wrist_import_graph import fixture, provider, forward, SYMBOL, OTHER, CONTRACT, BASE
from test_p99_provider_evidence import Remote, NATIVE, IDENTITY


class MultiAPI:
    def __init__(self, selected):
        self.images = []
        for p in selected:
            f = fixture(p.name)
            api = Remote(f)
            api.base = p.base
            api.path = NATIVE.replace('first.dll', p.name)
            self.images.append(api)
    def image(self, address):
        return next(a for a in self.images if a.base <= address < a.base+len(a.data))
    def identity(self, _):
        return IDENTITY
    def query(self, handle, address):
        return self.image(address).query(handle, address)
    def read(self, handle, address, size):
        return self.image(address).read(handle, address, size)
    def mapped_path(self, handle, base):
        return self.image(base).mapped_path(handle, base)
    @property
    def reads(self):
        return sum(a.reads for a in self.images)


def setup():
    selected = (provider(), provider(name='second.dll', base=BASE+0x10000))
    routes = (ApiRoute(IMPORTER, CONTRACT, 'first.dll'),)
    resolver = Resolver(selected, routes)
    roots = [{'dll': 'first.dll', 'name': SYMBOL}, {'dll': CONTRACT, 'name': SYMBOL},
             {'dll': 'second.dll', 'name': OTHER}]
    rows = tuple(resolver.resolve(r['dll'], r['name'], importer=IMPORTER) for r in roots)
    inventory = ImportInventory(IMPORTER, selected, routes, rows)
    paths = {p.name: NATIVE.replace('first.dll', p.name) for p in selected}
    return inventory, roots, paths, MultiAPI(selected)


class InventoryEvidenceTests(unittest.TestCase):
    def call(self, inv, roots, paths, api):
        return compare_inventory(1, IDENTITY, inv, roots, paths, api=api)

    def test_full_root_inventory_and_deduplicated_terminal_prefixes(self):
        result = self.call(*setup())
        self.assertTrue(result['partial_evidence_matched'])
        self.assertEqual((result['provider_count'], result['root_imports'], result['entry_prefixes']), (2, 3, 2))
        self.assertEqual(result['compared_bytes'], sum(r['compared_bytes'] for r in result['providers']))
        for key in ('full_provider_bytes_verified', 'dependency_closure_verified', 'api_set_schema_verified',
                    'mapped_file_object_identity_verified', 'module_lifetime_verified', 'atomic_snapshot', 'admitted_for_install'):
            self.assertIs(result[key], False)

    def test_missing_extra_duplicate_or_forged_resolution_refuses_before_read(self):
        for fault in ('missing', 'duplicate', 'address', 'rva', 'hash', 'steps', 'owner'):
            inv, roots, paths, api = setup()
            rows = inv.resolutions
            last = rows[-1]
            if fault == 'missing':
                rows = rows[:-1]
            elif fault == 'duplicate':
                rows += rows[-1:]
            else:
                changed = {'address': {'binding': dataclasses.replace(last.binding, address=last.binding.address+1)},
                    'rva': {'rva': last.rva+1}, 'hash': {'provider_sha256': '0'*64},
                    'steps': {'steps': ()}, 'owner': {'provider': 'first.dll'}}[fault]
                rows = rows[:-1]+(dataclasses.replace(last, **changed),)
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                self.call(dataclasses.replace(inv, resolutions=rows), roots, paths, api)
            self.assertEqual(api.reads, 0)

    def test_expected_roots_are_independent_not_recovered_from_result(self):
        inv, roots, paths, api = setup()
        for bad in (roots[:-1], roots+[{'dll': 'second.dll', 'name': SYMBOL}], roots+roots[:1],
                    [], repeat(roots[0])):
            with self.assertRaises(ValueError):
                self.call(inv, bad, paths, api)
        self.assertEqual(api.reads, 0)

    def test_provider_reparse_and_profile_identity(self):
        inv, roots, paths, api = setup()
        for changed in (dataclasses.replace(inv, importer='tll_p99_wrist_abi.dll'),
                        dataclasses.replace(inv, providers=inv.providers[:-1]),
                        dataclasses.replace(inv, providers=(dataclasses.replace(inv.providers[0], size=0x1000), inv.providers[1])),
                        dataclasses.replace(inv, providers=repeat(inv.providers[0])),
                        dataclasses.replace(inv, api_routes=())):
            with self.assertRaises(ValueError):
                self.call(changed, roots, paths, api)
        self.assertEqual(api.reads, 0)

    def test_exact_independent_path_inventory_required(self):
        inv, roots, paths, api = setup()
        for changed in ({'first.dll': NATIVE}, {**paths, 'extra.dll': NATIVE},
                        {**paths, 'second.dll': NATIVE}, {**paths, 'second.dll': 'C:\\second.dll'}):
            with self.assertRaises(ValueError):
                self.call(inv, roots, changed, api)
        self.assertEqual(api.reads, 0)

    def test_unused_or_forwarder_only_provider_is_not_silently_omitted(self):
        inv, roots, paths, api = setup()
        with self.assertRaisesRegex(ValueError, 'lacks terminal'):
            self.call(dataclasses.replace(inv, resolutions=inv.resolutions[:2]), roots[:2], paths, api)
        first = fixture()
        forward(first, 'second.'+OTHER)
        selected = (provider(first), inv.providers[1])
        resolver = Resolver(selected)
        one = ({'dll': 'first.dll', 'name': SYMBOL},)
        fwd = ImportInventory(IMPORTER, selected, (),
            (resolver.resolve('first.dll', SYMBOL, importer=IMPORTER),))
        with self.assertRaisesRegex(ValueError, 'lacks terminal'):
            self.call(fwd, one, paths, api)
        self.assertEqual(api.reads, 0)

    def test_every_selected_provider_header_export_and_prefix_corruption_refuses(self):
        for index in (0, 1):
            for rva in (0x30, 0xd0, 0x2080, 0x20a0, 0x1000, 0x1080):
                inv, roots, paths, api = setup()
                # Only select the relevant terminal's prefix for this provider.
                chosen = inv.resolutions[0 if index == 0 else 2].rva
                at = chosen if rva in (0x1000, 0x1080) else rva
                api.images[index].data[at] ^= 1
                with self.subTest(index=index, rva=hex(at)), self.assertRaisesRegex(ValueError, 'evidence mismatch'):
                    self.call(inv, roots, paths, api)

    def test_last_provider_mapping_path_and_permission_faults_refuse(self):
        for fault in ('path', 'allocation', 'guard', 'short'):
            inv, roots, paths, api = setup()
            last = api.images[-1]
            if fault == 'path':
                last.path += 'x'
            elif fault == 'short':
                last.short_read = True
            else:
                last.query_override.update({'AllocationBase': BASE} if fault == 'allocation' else {'Protect': 0x102})
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                self.call(inv, roots, paths, api)

    def test_second_pass_cannot_reuse_first_provider_evidence(self):
        inv, roots, paths, api = setup()
        self.assertTrue(self.call(inv, roots, paths, api)['partial_evidence_matched'])
        api.images[0].query_override['State'] = 0x10000
        with self.assertRaisesRegex(ValueError, 'image state'):
            self.call(inv, roots, paths, api)

    def test_owned_path_snapshot_is_not_mutated_by_first_read_callback(self):
        inv, roots, paths, api = setup()
        api.images[0].after_read = lambda _: paths.update({'second.dll': 'bad'})
        self.assertTrue(self.call(inv, roots, paths, api)['partial_evidence_matched'])
        self.assertEqual(paths['second.dll'], 'bad')


if __name__ == '__main__':
    unittest.main()
