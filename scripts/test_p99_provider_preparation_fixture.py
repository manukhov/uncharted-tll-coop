from contextlib import ExitStack
import dataclasses
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import p99_provider_preparation_fixture as exam
import p99_provider_preparation_profile as profile
import p99_wrist_import_profile as old_profile
from p99_wrist_abi_manifest import digest
from p99_wrist_mapped_windows import Identity
from test_p99_wrist_import_graph import fixture, SYMBOL, BASE, CONTRACT
from test_p99_wrist_import_profile import Kernel


def document():
    return {'schema': profile.SCHEMA, 'scope': profile.SCOPE,
        'providers': [{'name': 'first.dll', 'path': 'C:\\fixture\\first.dll', 'sha256': digest(fixture().raw)}],
        'api_routes': [{'importer': profile.IMPORTER, 'contract': CONTRACT, 'host': 'first.dll'}]}


class ProfileTests(unittest.TestCase):
    def test_new_scope_is_not_old_or_remote(self):
        doc = document()
        parsed = profile.read_profile(json.dumps(doc))
        self.assertEqual(parsed.api_routes[0].importer, profile.IMPORTER)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            parsed.providers[0].sha256 = '0'*64
        with self.assertRaisesRegex(ValueError, 'schema/scope'):
            old_profile.read_profile(json.dumps(doc))
        for key, value in (('schema', old_profile.SCHEMA), ('scope', 'owned-python-fixture-only'),
                           ('scope', 'remote'), ('unknown', 1)):
            changed = {**doc, key: value}
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                profile.read_profile(json.dumps(changed))

    def test_reserve_artifact_slot_and_enforce_count_before_rows(self):
        doc = document()
        doc['api_routes'] = []
        doc['providers'] = [{'name': f'p{i}.dll', 'path': f'C:\\f\\p{i}.dll', 'sha256': 'a'*64} for i in range(31)]
        self.assertEqual(len(profile.read_profile(json.dumps(doc)).providers), 31)
        doc['providers'].append({'name': 'p31.dll', 'path': 'C:\\f\\p31.dll', 'sha256': 'a'*64})
        with self.assertRaisesRegex(ValueError, 'profile rows'):
            profile.read_profile(json.dumps(doc))

    def test_old_importer_and_artifact_as_dependency_refused(self):
        doc = document()
        doc['api_routes'][0]['importer'] = old_profile.IMPORTER
        with self.assertRaisesRegex(ValueError, 'route profile'):
            profile.read_profile(json.dumps(doc))
        doc = document()
        doc['providers'][0].update(name=profile.IMPORTER, path='C:\\fixture\\'+profile.IMPORTER)
        with self.assertRaisesRegex(ValueError, 'provider profile name'):
            profile.read_profile(json.dumps(doc))

    def test_shared_strict_path_hash_duplicate_and_size_checks(self):
        for field, value in (('path', 'first.dll'), ('path', 'C:\\a\\..\\first.dll'),
            ('path', '\\\\server\\share\\first.dll'), ('path', 'C:\\f\\first.dll:ads'),
            ('sha256', 'a'*63), ('sha256', 'z'*64), ('name', 'api-ms-unknown.dll')):
            doc = document()
            doc['providers'][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                profile.read_profile(json.dumps(doc))
        for text in (' '*65537, '{"schema":1,"schema":2}'):
            with self.assertRaises(ValueError):
                profile.read_profile(text)
        for key in ('providers', 'api_routes'):
            doc = document()
            doc[key] *= 2
            with self.assertRaises(ValueError):
                profile.read_profile(json.dumps(doc))


class BindingTests(unittest.TestCase):
    def call(self, kernel, *, imports=None, doc=None):
        refs = []
        bindings, rows = exam.import_bindings(kernel, profile.read_profile(json.dumps(doc or document())),
            {'first.dll': bytes(kernel.file.raw)}, imports or [{'dll': CONTRACT, 'name': SYMBOL}], refs)
        return bindings, rows, refs

    def test_exact_new_importer_and_retained_duplicate_references(self):
        kernel = Kernel()
        bindings, rows, refs = self.call(kernel)
        self.assertEqual(bindings[0].address, BASE+fixture().exports[SYMBOL])
        self.assertEqual(refs, [BASE, BASE])
        self.assertEqual(rows[0]['steps'][0]['importer'], profile.IMPORTER)
        self.assertFalse(rows[0]['admitted_for_install'])

    def test_all_expected_resolutions_precede_any_export_oracle(self):
        kernel = Kernel()
        with self.assertRaises(ValueError):
            self.call(kernel, imports=[{'dll': 'first.dll', 'name': SYMBOL},
                                       {'dll': CONTRACT, 'name': 'MissingLateSymbol'}])
        self.assertEqual(kernel.proc_calls, [])

    def test_generator_inventory_is_owned_once_and_bounded(self):
        kernel = Kernel()
        bindings, _, _ = self.call(kernel, imports=iter([{'dll': CONTRACT, 'name': SYMBOL}]))
        self.assertEqual(len(bindings), 1)
        self.assertEqual(len(kernel.proc_calls), 1)
        for rows in (iter([]), ({'dll': CONTRACT, 'name': SYMBOL} for _ in range(4097))):
            kernel = Kernel()
            with self.assertRaisesRegex(ValueError, 'inventory count'):
                self.call(kernel, imports=rows)
            self.assertEqual(kernel.proc_calls, [])

    def test_missing_new_provider_or_route_cannot_use_loader_guess(self):
        for dll in ('bcrypt.dll', 'msvcp140.dll', 'api-ms-missing-route.dll'):
            kernel = Kernel()
            with self.subTest(dll=dll), self.assertRaises(ValueError):
                self.call(kernel, imports=[{'dll': dll, 'name': SYMBOL}])
            self.assertEqual(kernel.proc_calls, [])

    def test_wrong_loaded_path_source_or_export_refuses(self):
        for fault in ('path', 'source', 'export'):
            kernel = Kernel()
            if fault == 'path':
                kernel.full_path = 'C:\\different\\first.dll'
            elif fault == 'source':
                kernel.handles[CONTRACT] = BASE+0x10000
            else:
                kernel.proc += 1
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                self.call(kernel)
            if fault != 'export':
                self.assertEqual(kernel.proc_calls, [])


class CleanupKernel:
    def __init__(self, fault=None, throwing=False):
        self.events, self.fault, self.throwing = [], fault, throwing

    def operation(self, event, normal=1):
        self.events.append(event)
        if len(self.events)-1 == self.fault:
            if self.throwing:
                raise OSError('owned fault')
            return not normal
        return normal

    def FreeLibrary(self, module):
        return self.operation(('free', module))

    def GetModuleHandleW(self, path):
        return self.operation(('query', path), 0)

    def CloseHandle(self, handle):
        return self.operation(('close', handle))


class CleanupTests(unittest.TestCase):
    def test_each_acquisition_released_once_in_order(self):
        kernel, refs, handles = CleanupKernel(), [7, 7, 8], [41, 42]
        exam._release_owned(kernel, 123, refs, handles)
        self.assertEqual(kernel.events, [('free', 123), ('query', str(exam.ARTIFACT)),
            ('free', 8), ('free', 7), ('free', 7), ('close', 42), ('close', 41)])
        self.assertEqual(refs+handles, [])

    def test_each_false_or_exception_still_releases_all_remaining_tokens(self):
        for throwing in (False, True):
            for at in range(7):
                kernel, refs, handles = CleanupKernel(at, throwing), [7, 7, 8], [41, 42]
                with self.subTest(at=at, throwing=throwing), self.assertRaisesRegex(ValueError, 'cleanup failed'):
                    exam._release_owned(kernel, 123, refs, handles)
                self.assertEqual(len(kernel.events), 7)
                self.assertEqual(refs+handles, [])

    def test_no_phantom_artifact_release_after_preload_failure(self):
        kernel = CleanupKernel()
        exam._release_owned(kernel, None, [], [41])
        self.assertEqual(kernel.events, [('close', 41)])


class PageAPI:
    def __init__(self):
        self.changed, self.reads, self.metadata = False, [], {}
        self.ident = Identity(5, 9)
        self.data = bytes(4096)

    def identity(self, _):
        return self.ident

    def query(self, _, address):
        out = dict(BaseAddress=address, AllocationBase=BASE, RegionSize=4096,
                   State=0x1000, Type=0x1000000, Protect=4, AllocationProtect=8)
        out.update(self.metadata)
        if self.changed and self.reads:
            out['Protect'] = 8
        return SimpleNamespace(**out)

    def read(self, _, address, size):
        self.reads.append((address, size))
        return self.data


class PagesTests(unittest.TestCase):
    manifest = {'size_of_image': 0x10000, 'roles': {
        exam.disk.OFFER: {'rva': 0x3000}, exam.disk.PROVIDERS: {'rva': 0x4000}}}

    def call(self, api):
        exam._zero_pages(api, 1, Identity(5, 9), BASE, self.manifest)

    def test_both_full_pages_read_with_rw_or_wc(self):
        for protection in (4, 8):
            api = PageAPI()
            api.metadata['Protect'] = protection
            self.call(api)
            self.assertEqual(api.reads, [(BASE+0x3000, 4096), (BASE+0x4000, 4096)])

    def test_identity_metadata_and_exact_zero_content_refusal(self):
        for field, value in (('Protect', 2), ('Protect', 0x104), ('AllocationBase', BASE+65536),
                             ('RegionSize', 4095), ('Type', 0x20000), ('State', 0x2000), ('BaseAddress', None)):
            api = PageAPI()
            api.metadata[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.call(api)
            self.assertEqual(api.reads, [])
        for data in (bytes(4095), bytes(4095)+b'\x01', bytearray(4096)):
            api = PageAPI()
            api.data = data
            with self.assertRaises(ValueError):
                self.call(api)
        api = PageAPI()
        api.ident = Identity(5, 10)
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            self.call(api)

    def test_metadata_change_during_read_is_not_accepted_as_another_valid_state(self):
        api = PageAPI()
        api.changed = True
        with self.assertRaisesRegex(ValueError, 'metadata changed'):
            self.call(api)


class RunTests(unittest.TestCase):
    def test_invalid_scope_precedes_any_os_api(self):
        with patch.object(exam, 'WindowsAPI') as create:
            with self.assertRaises(ValueError):
                exam.run('a'*64, '{}')
            create.assert_not_called()

    def test_bad_artifact_or_provider_hash_never_loads_and_still_closes(self):
        for fault in ('artifact', 'provider', 'short'):
            kernel = SimpleNamespace(LoadLibraryExW=lambda *_: self.fail('must not load'))
            handles, closed = [], []
            def snapshot(_, path, owned):
                owned.append(len(owned)+1)
                handles.append(str(path))
                if len(handles) == 2 and fault == 'short':
                    raise ValueError('short read')
                return b'artifact' if len(handles) == 1 else b'bad provider'
            def release(_, base, refs, files):
                self.assertIsNone(base)
                self.assertEqual(refs, [])
                closed.extend(files)
            with self.subTest(fault=fault), patch.object(exam, 'WindowsAPI', return_value=SimpleNamespace(kernel=kernel)), \
                patch.object(exam, 'configure_loader'), patch.object(exam, 'locked_snapshot', side_effect=snapshot), \
                patch.object(exam.disk, 'manifest', side_effect=ValueError('artifact') if fault == 'artifact' else lambda *_: {}), \
                patch.object(exam, '_release_owned', side_effect=release):
                with self.assertRaises(ValueError):
                    exam.run('a'*64, json.dumps(document()))
            self.assertEqual(len(closed), 1 if fault == 'artifact' else 2)

    def flow(self, fault=None):
        events = []
        snapshots = {f'C:\\f\\p{i}.dll': bytes([i+1]) for i in range(7)}
        doc = document()
        doc['api_routes'] = []
        doc['providers'] = [{'name': f'p{i}.dll', 'path': path, 'sha256': digest(raw)}
                            for i, (path, raw) in enumerate(snapshots.items())]
        if fault == 'last_hash':
            doc['providers'][-1]['sha256'] = '0'*64

        class FlowKernel:
            loaded = fault == 'already_loaded'
            def LoadLibraryExW(inner, path, reserved, flags):
                events.append('load')
                self.assertEqual((path, reserved, flags), (str(exam.ARTIFACT), None, 0x900))
                self.assertEqual(sum(isinstance(e, tuple) and e[0] == 'snapshot' for e in events), 8)
                if fault == 'load':
                    return None
                inner.loaded = True
                return BASE
            def GetModuleHandleW(inner, path):
                self.assertEqual(path, str(exam.ARTIFACT))
                events.append('query')
                return BASE if inner.loaded else None
            def GetCurrentProcess(inner):
                return 1
            def FreeLibrary(inner, module):
                events.append(('free', module))
                if module == BASE:
                    inner.loaded = False
                return 1
            def CloseHandle(inner, handle):
                events.append(('close', handle))
                return 1

        kernel = FlowKernel()
        api = SimpleNamespace(kernel=kernel, identity=lambda _: Identity(5, 9))
        def snapshot(_, path, handles):
            events.append(('snapshot', str(path)))
            handles.append(100+len(handles))
            return b'artifact' if path == exam.ARTIFACT else snapshots[path]
        def bind(_, parsed, raw, imports, refs):
            events.append('bind')
            self.assertEqual(len(parsed.providers), 7)
            self.assertEqual(len(raw), 7)
            refs.extend([7, 7])
            if fault == 'bind':
                raise ValueError('bind')
            return [], []
        def plan(*_, **kwargs):
            events.append('plan')
            self.assertIs(kwargs['header_base'], exam.mapped.HeaderBase.LOADED)
            if fault == 'plan':
                raise ValueError('plan')
            return SimpleNamespace(size=0x10000, exclusions=[])
        def pages(*_):
            events.append('pages')
            if fault == 'pages_before' or fault == 'pages_after' and events.count('pages') == 2:
                raise ValueError('pages')
        def compare(*_):
            events.append('compare')
            if fault == 'compare':
                raise ValueError('compare')
            return {'comparison_passed': True, 'admitted_for_install': False}
        with ExitStack() as stack:
            for target, attr, kwargs in (
                (exam, 'WindowsAPI', {'return_value': api}), (exam, 'configure_loader', {}),
                (exam, 'locked_snapshot', {'side_effect': snapshot}),
                (exam.disk, 'manifest', {'return_value': {'imports': []}}),
                (exam, 'import_bindings', {'side_effect': bind}),
                (exam.preparation, 'build_plan', {'side_effect': plan}),
                (exam, '_zero_pages', {'side_effect': pages}),
                (exam.mapped, 'compare', {'side_effect': compare})):
                stack.enter_context(patch.object(target, attr, **kwargs))
            if fault:
                with self.assertRaises(ValueError):
                    exam.run('a'*64, json.dumps(doc))
            else:
                result = exam.run('a'*64, json.dumps(doc))
                self.assertFalse(result['artifact_exports_called'])
                self.assertFalse(result['preparation_entry_called'])
                self.assertTrue(result['artifact_unloaded_after_comparison'])
                self.assertTrue(result['owned_cleanup_succeeded'])
        return events

    def test_complete_order_keeps_files_and_each_ref_through_both_page_reads(self):
        events = self.flow()
        self.assertEqual([e for e in events if isinstance(e, str)],
            ['query', 'load', 'bind', 'plan', 'pages', 'compare', 'pages', 'query'])
        self.assertEqual(events[-12:], [('free', BASE), 'query', ('free', 7), ('free', 7),
            *[('close', i) for i in range(107, 99, -1)]])

    def test_each_pre_and_post_load_failure_has_only_owned_cleanup(self):
        for fault in ('last_hash', 'already_loaded', 'load', 'bind', 'plan', 'pages_before', 'compare', 'pages_after'):
            with self.subTest(fault=fault):
                events = self.flow(fault)
                self.assertEqual([e for e in events if isinstance(e, tuple) and e[0] == 'close'],
                                 [('close', i) for i in range(107, 99, -1)])
                loaded = fault not in ('last_hash', 'already_loaded', 'load')
                self.assertEqual(events.count(('free', BASE)), int(loaded))
                self.assertEqual(events.count(('free', 7)), 2 if loaded else 0)
                if fault in ('last_hash', 'already_loaded'):
                    self.assertNotIn('load', events)


if __name__ == '__main__':
    unittest.main()
