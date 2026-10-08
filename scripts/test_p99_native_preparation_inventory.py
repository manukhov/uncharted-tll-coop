import copy
import json
import unittest
from unittest.mock import patch

import p99_native_preparation_inventory as native
from p99_wrist_abi_manifest import digest
from test_p99_wrist_import_graph import fixture, SYMBOL, OTHER, CONTRACT, forward


def setup():
    files = {r'C:\Windows\System32\first.dll': bytes(fixture().raw),
             r'C:\Windows\System32\second.dll': bytes(fixture('second.dll').raw)}
    profile = {'schema': native.SCHEMA, 'scope': native.SCOPE,
        'providers': [{'name': path.rsplit('\\', 1)[1], 'path': path, 'sha256': digest(raw)}
                      for path, raw in files.items()],
        'api_routes': [{'importer': native.IMPORTER, 'contract': CONTRACT, 'host': 'first.dll'}]}
    artifact = {'file_sha256': 'a'*64, 'size_of_image': 0x8000,
        'preparation_entry': {'rva': 0x1100, 'prefix_hex': '11'*32},
        'roles': {native.ENTRY: {'rva': 0x1100}, native.OFFER: {'rva': 0x6000}, native.PROVIDERS: {'rva': 0x7000}},
        'imports': [{'dll': 'first.dll', 'name': SYMBOL, 'iat_rva': 0x4000},
                    {'dll': CONTRACT, 'name': SYMBOL, 'iat_rva': 0x4008},
                    {'dll': 'second.dll', 'name': OTHER, 'iat_rva': 0x4010}]}
    return files, profile, artifact


class NativeInventoryTests(unittest.TestCase):
    def call(self, files, profile, artifact):
        with patch.object(native, 'manifest', return_value=artifact) as manifest:
            result = native.build(b'artifact', 'a'*64, json.dumps(profile), files.__getitem__)
        manifest.assert_called_once_with(b'artifact', 'a'*64)
        return result

    def test_own_scope_and_exact_paths_reject_python_and_remote(self):
        _, doc, _ = setup()
        for key, value in (('schema', 'p99-preparation-owned-import-profile-v1'),
                           ('scope', 'owned-preparation-loader-fixture-only'), ('scope', 'remote')):
            changed = {**doc, key: value}
            with self.assertRaises(ValueError):
                native.read_profile(json.dumps(changed))
        for path in (r'C:\Python312\first.dll', r'C:\Windows\System32Else\first.dll',
                     r'C:\Windows\System32\sub\first.dll'):
            changed = copy.deepcopy(doc)
            changed['providers'][0]['path'] = path
            with self.assertRaisesRegex(ValueError, 'System32'):
                native.read_profile(json.dumps(changed))

    def test_artifact_reserves_first_of_32_slots(self):
        _, doc, _ = setup()
        doc['api_routes'] = []
        doc['providers'] = [{'name': f'p{i}.dll', 'path': f'C:\\Windows\\System32\\p{i}.dll', 'sha256': 'a'*64}
                            for i in range(31)]
        self.assertEqual(len(native.read_profile(json.dumps(doc)).providers), 31)
        doc['providers'].append({'name': 'last.dll', 'path': r'C:\Windows\System32\last.dll', 'sha256': 'a'*64})
        with self.assertRaises(ValueError):
            native.read_profile(json.dumps(doc))

    def test_graph_keeps_all_roots_deduplicates_prefixes_and_emits_no_synthetic_bases(self):
        data = self.call(*setup())
        self.assertEqual(len(data['pins']), 3)
        self.assertEqual(data['prefix_count'], 2)
        self.assertEqual(len(data['bindings']), 3)
        self.assertEqual(data['bindings'][0][1:], data['bindings'][1][1:])
        self.assertEqual([len([s for s in data['spans'] if s[0] == i]) for i in (1, 2)], [5, 5])
        self.assertTrue(all(rva < 0x10000 for _, _, rva in data['bindings']))
        self.assertEqual(data['pins'][0], ('', 'a'*64, 0x8000, 0x1100, '11'*32))

    def test_hash_failure_before_any_generated_expectations(self):
        for index in (0, 1):
            files, doc, artifact = setup()
            doc['providers'][index]['sha256'] = '0'*64
            with self.assertRaisesRegex(ValueError, 'SHA256'):
                self.call(files, doc, artifact)

    def test_missing_provider_or_route_refuses(self):
        for fault in ('provider', 'route', 'symbol'):
            files, doc, artifact = setup()
            if fault == 'provider':
                doc['providers'].pop()
            elif fault == 'route':
                doc['api_routes'] = []
            else:
                artifact['imports'][-1]['name'] = 'NotAnExport'
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                self.call(files, doc, artifact)

    def test_unused_or_forwarder_only_selection_refuses(self):
        files, doc, artifact = setup()
        artifact['imports'] = artifact['imports'][:2]
        with self.assertRaisesRegex(ValueError, 'lacks terminal'):
            self.call(files, doc, artifact)
        forwarded = fixture()
        forward(forwarded, 'second.'+OTHER)
        files[doc['providers'][0]['path']] = bytes(forwarded.raw)
        doc['providers'][0]['sha256'] = digest(forwarded.raw)
        with self.assertRaisesRegex(ValueError, 'lacks terminal'):
            self.call(files, doc, artifact)

    def test_duplicate_iat_rejected(self):
        files, doc, artifact = setup()
        artifact['imports'][-1]['iat_rva'] = artifact['imports'][0]['iat_rva']
        with self.assertRaisesRegex(ValueError, 'IAT bound/duplicate'):
            self.call(files, doc, artifact)

    def test_independent_artifact_hash_is_required(self):
        files, doc, _ = setup()
        with self.assertRaises(ValueError):
            native.build(b'artifact', 'a'*64, json.dumps(doc), files.__getitem__)

    def test_render_deterministic_bounded_data_not_request_wire(self):
        data = self.call(*setup())
        header = native.render(data)
        self.assertEqual(header, native.render(data))
        self.assertIn('std::array<Pin,3>', header)
        self.assertIn('std::array<Span,10>', header)
        self.assertIn('std::array<Binding,3>', header)
        self.assertNotIn('0x100000000', header)
        self.assertNotIn('GetProcAddress', header)
        self.assertNotIn('LoadLibrary', header)
        self.assertIn(r'L"\x0043\x003a', header)


if __name__ == '__main__':
    unittest.main()
