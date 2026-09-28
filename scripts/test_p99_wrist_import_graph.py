import dataclasses
from itertools import repeat
import struct
import unittest
from unittest.mock import patch

import p99_wrist_import_graph as graph
from p99_wrist_abi_manifest import digest
from test_p99_wrist_abi_manifest import Fixture

SYMBOL = 'TllP99WristAsyncV1'
OTHER = 'TllP99WristAsyncBridgeV1'
BASE = 0x700000000
CONTRACT = 'api-ms-win-crt-heap-l1-1-0.dll'


def fixture(name='first.dll'):
    f = Fixture()
    f.write(0x2080, name.encode()+b'\0')
    return f


def provider(f=None, name='first.dll', base=BASE):
    f = fixture(name) if f is None else f
    return graph.read_provider(f.raw, digest(f.raw), name, base)


def forward(f, value, symbol=SYMBOL):
    f.write(0x2300, value.encode()+b'\0')
    f.rpack(f.eat_at[symbol], '<I', 0x2300)


class ProviderTests(unittest.TestCase):
    def rejected(self, mutate, message):
        f = fixture()
        mutate(f)
        with self.assertRaisesRegex(ValueError, message):
            provider(f)

    def test_owned_immutable_bytes_and_case_sensitive_export(self):
        f = fixture()
        p = provider(f, name='FIRST.DLL')
        self.assertEqual(p.name, 'first.dll')
        self.assertEqual(p.lookup(SYMBOL).rva, f.exports[SYMBOL])
        f.raw[:] = bytes(len(f.raw))
        self.assertEqual(p.raw[:2], b'MZ')
        with self.assertRaises(dataclasses.FrozenInstanceError):
            p.base = 1
        with self.assertRaisesRegex(ValueError, 'name missing'):
            p.lookup(SYMBOL.lower())

    def test_hash_checked_before_parse(self):
        f = fixture()
        for sha in ('', None, 'g'*64, '0'*64):
            with self.subTest(sha=sha), patch.object(graph.pefile, 'PE') as pe:
                with self.assertRaisesRegex(ValueError, 'SHA256'):
                    graph.read_provider(f.raw, sha, 'first.dll', BASE)
                pe.assert_not_called()

    def test_file_bound(self):
        with self.assertRaisesRegex(ValueError, 'file bound'):
            graph.read_provider(b'', '0'*64, 'first.dll', BASE)

    def test_base_bound(self):
        f = fixture()
        for base in (True, 0, BASE+1, 0x800000000000-0x10000):
            bad = base if base != 0x800000000000-0x10000 else 0x800000000000
            with self.subTest(base=bad), self.assertRaisesRegex(ValueError, 'selected base'):
                provider(f, base=bad)

    def test_name_is_simple_concrete_dll(self):
        for name in ('../first.dll', 'C:\\first.dll', 'first.dll\0', '', CONTRACT, 'first.dll.dll',
                     '\u212aernel32.dll', '\u0130.dll', '\u017f.dll'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                provider(name=name)

    def test_internal_name_mismatch(self):
        with self.assertRaisesRegex(ValueError, 'DLL name mismatch'):
            provider(fixture('second.dll'))

    def test_pe_arch_and_header(self):
        for at, fmt, value in ((0x84, '<H', 0x14c), (0x98, '<H', 0x10b), (0x96, '<H', 0x22),
                               (0x96, '<H', 0x2000)):
            with self.subTest(at=at), self.assertRaises(ValueError):
                f = fixture()
                f.pack(at, fmt, value)
                provider(f)

    def test_section_raw_overlap(self):
        self.rejected(lambda f: f.pack(0x188+40+20, '<I', 0x400), 'raw overlap')

    def test_section_virtual_overlap(self):
        self.rejected(lambda f: f.pack(0x188+40+12, '<I', 0x1000), 'virtual extent/overlap')

    def test_metadata_cannot_be_writable_executable_or_discardable(self):
        for flags in (0xc0000040, 0x60000040, 0x42000040):
            with self.subTest(flags=flags):
                self.rejected(lambda f: f.pack(0x188+40+36, '<I', flags), 'persistent RO')

    def test_metadata_must_fit_directory_and_virtualsize(self):
        self.rejected(lambda f: f.directory(0, 0x2000, 40), 'outside directory')
        self.rejected(lambda f: f.pack(0x188+40+8, '<I', 0x90), 'VirtualSize')

    def test_huge_export_count_refused(self):
        self.rejected(lambda f: f.rpack(0x2014, '<I', 65537), 'counts')

    def test_full_declared_directory_must_be_file_backed(self):
        self.rejected(lambda f: f.directory(0, 0x2000, 0x2000), 'VirtualSize')

    def test_metadata_work_bound(self):
        # Directory fits the cap but repeated strings/tables exhaust work.
        f = fixture()
        for i in range(8):
            f.rpack(0x20a0+i*4, '<I', 0x2300)
        f.write(0x2300, b'SECOND.'+b'x'*200+b'\0')
        with patch.object(graph, 'MAX_METADATA', 0x500), self.assertRaisesRegex(ValueError, 'work bound'):
            provider(f)

    def test_aliases_share_ordinal(self):
        f = fixture()
        f.rpack(f.ordinal_at[SYMBOL], '<H', 0)
        p = provider(f)
        self.assertEqual(p.lookup(SYMBOL), p.lookup(OTHER))

    def test_more_names_than_functions_allowed(self):
        f = fixture()
        f.rpack(0x2014, '<I', 1)
        for at in f.ordinal_at.values():
            f.rpack(at, '<H', 0)
        p = provider(f)
        self.assertEqual(p.lookup(SYMBOL), p.lookup(OTHER))

    def test_ordinal_only_and_non_one_base(self):
        f = fixture()
        f.rpack(0x2010, '<III', 9, 8, 0)
        p = provider(f)
        self.assertEqual(p.lookup(9).rva, f.exports[OTHER])
        for ordinal in (0, 8, 17, 65536, True):
            with self.subTest(ordinal=ordinal), self.assertRaises(ValueError):
                p.lookup(ordinal)

    def test_null_export_hole(self):
        f = fixture()
        f.rpack(f.eat_at[SYMBOL], '<I', 0)
        with self.assertRaisesRegex(ValueError, 'null export hole'):
            provider(f).lookup(SYMBOL)

    def test_duplicate_name_and_bad_index(self):
        self.rejected(lambda f: f.rpack(0x20c4, '<I', f.name_at[OTHER]), 'duplicate/unsorted')
        self.rejected(lambda f: f.rpack(0x20e0, '<H', 8), 'invalid ordinal')

    def test_unrequested_data_allowed_but_reached_data_rejected(self):
        p = provider()
        p.lookup(SYMBOL)
        with self.assertRaisesRegex(ValueError, 'persistent RX'):
            p.lookup('TllP99WristPreparedV1')

    def test_selected_code_not_raw_padding_zero_fill_or_discardable(self):
        for offset, value in ((8, 0x40), (16, 0), (36, 0x62000020)):
            f = fixture()
            f.pack(0x188+offset, '<I', value)
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'VirtualSize|persistent RX'):
                provider(f).lookup(SYMBOL)

    def test_forwarder_directory_end_is_not_inclusive(self):
        f = fixture()
        f.rpack(f.eat_at[SYMBOL], '<I', 0x2500)
        p = provider(f)
        self.assertIsNone(p.exports[p.lookup(OTHER).ordinal-1].forwarder)
        with self.assertRaisesRegex(ValueError, 'persistent RX'):
            p.lookup(SYMBOL)

    def test_forwarder_nul_must_fit_directory(self):
        f = fixture()
        f.write(0x24f8, b'NTDLL.XY\0')
        f.rpack(f.eat_at[SYMBOL], '<I', 0x24f8)
        with self.assertRaisesRegex(ValueError, 'outside directory'):
            provider(f)

    def test_forwarder_grammar(self):
        for value in ('NTDLL', '../x.y', 'NTDLL.', 'NTDLL.#0', 'NTDLL.#65536',
                      'NTDLL.#-1', 'NTDLL.#01', 'NTDLL.dll.X', 'NTDLL.A.B'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                graph.split_forwarder(value)
        self.assertEqual(graph.split_forwarder('NTDLL.#12'), ('ntdll.dll', 12))

    def test_certificate_overlay_hashed_not_mapped(self):
        f = fixture()
        offset = len(f.raw)
        f.raw.extend(bytes(128))
        f.directory(4, offset, 128)
        p = provider(f)
        self.assertEqual(len(p.raw), offset+128)
        old = digest(f.raw)
        f.raw[-1] = 1
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
            graph.read_provider(f.raw, old, 'first.dll', BASE)

    def test_certificate_bad_offset_and_extent(self):
        for rva, size in ((0x2000, 16), (0, 1), (0x5401, 1), (0x5400, 128)):
            with self.subTest(rva=rva):
                self.rejected(lambda f: f.directory(4, rva, size), 'certificate')

    def test_other_directories_not_treated_as_admission(self):
        f = fixture()
        f.directory(9, 0x4000, 40)
        provider(f).lookup(SYMBOL)  # Parsing exports never authorizes a load.


class ResolutionTests(unittest.TestCase):
    def resolve(self, resolver, dll='first.dll', name=SYMBOL, importer='artifact.dll'):
        return resolver.resolve(dll, name, importer=importer)

    def test_direct_exact_base_rva_and_all_limits_false(self):
        p = provider()
        r = self.resolve(graph.Resolver([p]))
        self.assertEqual(r.binding.address, BASE+p.lookup(SYMBOL).rva)
        self.assertEqual(r.provider_sha256, p.sha256)
        for key in ('provider_mapped_bytes_verified', 'api_set_schema_verified',
                    'remote_lifetime_verified', 'admitted_for_install'):
            self.assertIs(getattr(r, key), False)

    def test_forwarder_chain_uses_destination_base(self):
        f = fixture()
        forward(f, 'SECOND.'+SYMBOL)
        a, b = provider(f), provider(name='second.dll', base=BASE+0x10000)
        r = self.resolve(graph.Resolver([a, b]))
        self.assertEqual(r.binding.address, b.base+b.lookup(SYMBOL).rva)
        self.assertEqual([s.provider_sha256 for s in r.steps], [a.sha256, b.sha256])
        self.assertEqual(r.steps[-1].importer, 'first.dll')

    def test_forwarded_ordinal_only(self):
        f, g = fixture(), fixture('second.dll')
        forward(f, 'SECOND.#9')
        g.rpack(0x2010, '<III', 9, 8, 0)
        a, b = provider(f), provider(g, 'second.dll', BASE+0x10000)
        r = self.resolve(graph.Resolver([a, b]))
        self.assertEqual(r.rva, b.lookup(9).rva)

    def test_api_route_scoped_to_importer(self):
        p = provider()
        routes = [graph.ApiRoute('artifact.dll', CONTRACT, 'first.dll')]
        resolver = graph.Resolver([p], routes)
        r = self.resolve(resolver, CONTRACT)
        self.assertEqual(r.steps[0].kind, 'api-set-input')
        with self.assertRaisesRegex(ValueError, 'no exact'):
            self.resolve(resolver, CONTRACT, importer='another.dll')

    def test_api_forwarder_uses_forwarding_dll_context(self):
        f = fixture()
        forward(f, CONTRACT[:-4]+'.'+SYMBOL)
        a, b = provider(f), provider(name='second.dll', base=BASE+0x10000)
        wrong = [graph.ApiRoute('artifact.dll', CONTRACT, 'second.dll')]
        with self.assertRaisesRegex(ValueError, 'no exact'):
            self.resolve(graph.Resolver([a, b], wrong))
        right = [graph.ApiRoute('first.dll', CONTRACT, 'second.dll')]
        r = self.resolve(graph.Resolver([a, b], right))
        self.assertEqual([s.kind for s in r.steps], ['forwarder', 'api-set-input', 'export'])

    def test_missing_provider_and_exact_symbol(self):
        f = fixture()
        forward(f, 'MISSING.'+SYMBOL)
        with self.assertRaisesRegex(ValueError, 'missing provider'):
            self.resolve(graph.Resolver([provider(f)]))
        with self.assertRaisesRegex(ValueError, 'name missing'):
            self.resolve(graph.Resolver([provider()]), name=SYMBOL.lower())

    def test_forwarder_cycle_and_api_cycle(self):
        for target in ('first.'+SYMBOL, CONTRACT[:-4]+'.'+SYMBOL):
            f = fixture()
            forward(f, target)
            routes = [graph.ApiRoute('first.dll', CONTRACT, 'first.dll')]
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, 'cycle'):
                self.resolve(graph.Resolver([provider(f)], routes))

    def test_depth_bound(self):
        f = fixture()
        forward(f, 'SECOND.'+SYMBOL)
        resolver = graph.Resolver([provider(f), provider(name='second.dll', base=BASE+0x10000)])
        with patch.object(graph, 'MAX_STEPS', 1), self.assertRaisesRegex(ValueError, 'depth bound'):
            self.resolve(resolver)

    def test_exact_depth_and_one_more_step(self):
        f = fixture()
        forward(f, 'SECOND.'+SYMBOL)
        resolver = graph.Resolver([provider(f), provider(name='second.dll', base=BASE+0x10000)])
        with patch.object(graph, 'MAX_STEPS', 2):
            self.assertEqual(len(self.resolve(resolver).steps), 2)
        routes = [graph.ApiRoute('artifact.dll', CONTRACT, 'first.dll')]
        resolver = graph.Resolver(list(resolver.providers.values()), routes)
        with patch.object(graph, 'MAX_STEPS', 2), self.assertRaisesRegex(ValueError, 'depth bound'):
            self.resolve(resolver, CONTRACT)

    def test_duplicate_provider_and_overlapping_base(self):
        p = provider()
        for providers in ([p, p], [p, provider(name='second.dll')]):
            with self.subTest(providers=len(providers)), self.assertRaises(ValueError):
                graph.Resolver(providers)

    def test_duplicate_unknown_or_api_host_and_default_importer(self):
        route = graph.ApiRoute('artifact.dll', CONTRACT, 'first.dll')
        for routes in ([route, route], [graph.ApiRoute('*', CONTRACT, 'first.dll')],
                       [graph.ApiRoute('artifact.dll', CONTRACT, 'missing.dll')],
                       [graph.ApiRoute('artifact.dll', CONTRACT, CONTRACT)]):
            with self.subTest(routes=routes), self.assertRaises(ValueError):
                graph.Resolver([provider()], routes)

    def test_no_initial_ordinal_or_api_importer(self):
        resolver = graph.Resolver([provider()])
        for name, importer in ((1, 'artifact.dll'), (SYMBOL, CONTRACT)):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.resolve(resolver, name=name, importer=importer)

    def test_collections_are_owned_readonly(self):
        rows = [provider()]
        resolver = graph.Resolver(rows)
        rows.clear()
        self.resolve(resolver)
        with self.assertRaises(TypeError):
            resolver.providers['first.dll'] = None

    def test_forged_provider_fields_are_not_a_certificate(self):
        p = provider()
        for changes in ({'base': 0}, {'size': 4096}, {'sha256': '0'*64}, {'exports': ()},
                        {'raw': bytes(len(p.raw))}, {'names': ()}):
            with self.subTest(changes=list(changes)), self.assertRaises(ValueError):
                graph.Resolver([dataclasses.replace(p, **changes)])

    def test_infinite_iterables_stop_at_count_bound(self):
        with self.assertRaisesRegex(ValueError, 'provider/route count'):
            graph.Resolver(repeat(provider()))
        with self.assertRaisesRegex(ValueError, 'provider/route count'):
            graph.Resolver([provider()], repeat(graph.ApiRoute('artifact.dll', CONTRACT, 'first.dll')))

    def test_aggregate_byte_bound(self):
        with patch.object(graph, 'MAX_TOTAL_FILES', 1), self.assertRaisesRegex(ValueError, 'aggregate'):
            graph.Resolver([provider()])


if __name__ == '__main__':
    unittest.main()
