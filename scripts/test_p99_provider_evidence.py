import dataclasses
from itertools import repeat
import struct
from types import SimpleNamespace
import unittest

import p99_provider_evidence as evidence
from p99_wrist_mapped_windows import Identity
from p99_provider_evidence_fixture import cleanup_parent, file_native_path, drop
from test_p99_wrist_import_graph import fixture, provider, SYMBOL, BASE
from test_p99_wrist_mapped_image import Memory, IDENTITY

NATIVE = r'\Device\HarddiskVolume3\fixture\first.dll'


class Remote(Memory):
    def __init__(self, f):
        super().__init__(f, base=BASE)
        self.path = NATIVE
        self.path_calls = 0
        self.on_path = None

    def mapped_path(self, handle, base):
        self.path_calls += 1
        if self.on_path:
            self.on_path(self)
        return self.path


def setup():
    f = fixture()
    return provider(f), Remote(f)


def compare(p, api, identity=IDENTITY, path=NATIVE, entries=None):
    return evidence.compare_provider(1, identity, p, path,
                                    [p.lookup(SYMBOL).rva] if entries is None else entries, api=api)


class ProviderEvidenceTests(unittest.TestCase):
    def test_exact_remote_ranges_and_explicit_limits(self):
        p, api = setup()
        r = compare(p, api)
        self.assertTrue(r['evidence_matched'])
        self.assertEqual((r['pid'], r['creation'], r['base']), (IDENTITY.pid, IDENTITY.creation, BASE))
        self.assertEqual(r['provider_file_sha256'], p.sha256)
        self.assertEqual(r['compared_bytes'], sum(x['size'] for x in r['ranges']))
        self.assertEqual(len(r['ranges']), 5)
        for key in ('header_image_base_compared', 'full_provider_bytes_verified',
                    'mapped_file_object_identity_verified', 'api_set_schema_verified',
                    'dependency_closure_verified', 'atomic_snapshot', 'module_lifetime_verified',
                    'admitted_for_install'):
            self.assertIs(r[key], False, key)

    def test_each_selected_range_corruption_refuses(self):
        for rva in (0x30, 0x88, 0xd0, 0x188, 0x2000, 0x2080, 0x20a0, 0x1080):
            p, api = setup()
            api.data[rva] ^= 1
            with self.subTest(rva=hex(rva)), self.assertRaisesRegex(ValueError, 'evidence mismatch'):
                compare(p, api)

    def test_image_base_exclusion_does_not_grant_header_certificate(self):
        p, api = setup()
        struct.pack_into('<Q', api.data, 0xb0, BASE+0x10000)
        self.assertFalse(compare(p, api)['header_image_base_compared'])

    def test_outside_prefix_is_explicitly_not_verified(self):
        p, api = setup()
        api.data[p.lookup(SYMBOL).rva+32] ^= 1
        self.assertFalse(compare(p, api)['full_provider_bytes_verified'])

    def test_pid_and_creation_must_match_borrowed_handle(self):
        for identity in (Identity(IDENTITY.pid+1, IDENTITY.creation), Identity(IDENTITY.pid, IDENTITY.creation+1)):
            p, api = setup()
            with self.subTest(identity=identity), self.assertRaisesRegex(ValueError, 'identity changed'):
                compare(p, api, identity)
            self.assertEqual(api.reads, 0)

    def test_process_change_at_last_boundary_refuses(self):
        p, api = setup()
        api.on_path = lambda a: setattr(a, 'identity_value', Identity(27, 999)) if a.path_calls == 2 else None
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            compare(p, api)

    def test_wrong_native_path_case_or_alias_refuses(self):
        for path in (NATIVE.replace('first.dll', 'second.dll'), NATIVE.replace('fixture', 'FIXTURE')):
            p, api = setup()
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, 'path differs'):
                compare(p, api, path=path)
            self.assertEqual(api.reads, 0)

    def test_path_change_at_last_boundary_refuses(self):
        p, api = setup()
        api.on_path = lambda a: setattr(a, 'path', NATIVE+'x') if a.path_calls == 2 else None
        with self.assertRaisesRegex(ValueError, 'path differs'):
            compare(p, api)

    def test_native_path_shape(self):
        for path in ('C:\\first.dll', r'\Device\Volume\..\x', r'\Device\\x', NATIVE+'\0',
                     NATIVE+'/', '\\Device\\', NATIVE+'a'*32768):
            with self.subTest(path=path[:60]), self.assertRaises(ValueError):
                evidence.native_path(path)

    def test_unloaded_private_guarded_or_other_allocation_refuses(self):
        for override in ({'State': 0x10000}, {'Type': 0x20000}, {'AllocationBase': BASE+0x10000},
                         {'Protect': 0x102}, {'Protect': 4}):
            p, api = setup()
            api.query_override = override
            with self.subTest(override=override), self.assertRaisesRegex(ValueError, 'image state'):
                compare(p, api)

    def test_short_copy_and_mapping_change_during_copy_refuse(self):
        p, api = setup()
        api.short_read = True
        with self.assertRaisesRegex(ValueError, 'short/nonbytes'):
            compare(p, api)
        p, api = setup()
        api.after_read = lambda a: a.query_override.update(AllocationProtect=4)
        with self.assertRaisesRegex(ValueError, 'metadata changed'):
            compare(p, api)

    def test_stale_snapshot_cannot_be_reused_after_unload(self):
        p, api = setup()
        first = compare(p, api)
        api.query_override['State'] = 0x10000
        with self.assertRaises(ValueError):
            compare(p, api)
        self.assertFalse(first['module_lifetime_verified'])

    def test_equal_boundaries_still_do_not_claim_no_aba(self):
        p, api = setup()
        # An unload/reload between observations cannot be excluded by matching
        # snapshots. No API in this reader obtains a loader lease.
        self.assertFalse(compare(p, api)['atomic_snapshot'])

    def test_forged_provider_and_bad_entry_sets_refuse(self):
        p, api = setup()
        with self.assertRaisesRegex(ValueError, 'record differs'):
            compare(dataclasses.replace(p, size=p.size+4096), api)
        for entries in ([], [0x1000, 0x1000], [True], [0x1001], [0x4000], repeat(0x1000)):
            with self.subTest(kind=type(entries).__name__), self.assertRaises(ValueError):
                compare(p, api, entries=entries)

    def test_prefix_is_exactly_the_documented_32_bytes(self):
        p, api = setup()
        self.assertEqual(compare(p, api)['ranges'][-1]['size'], 32)


class PathApiTests(unittest.TestCase):
    def test_full_path_result(self):
        api = evidence.ModuleAPI.__new__(evidence.ModuleAPI)
        def path(handle, base, buffer, count):
            buffer.value = NATIVE
            return len(NATIVE)
        api.kernel = SimpleNamespace(K32GetMappedFileNameW=path)
        self.assertEqual(api.mapped_path(1, BASE), NATIVE)

    def test_failed_truncated_wrong_count_or_non_native_result(self):
        for result, text in ((0, ''), (32768, NATIVE), (len(NATIVE)+1, NATIVE), (4, 'C:\\x')):
            api = evidence.ModuleAPI.__new__(evidence.ModuleAPI)
            def path(handle, base, buffer, count):
                buffer.value = text
                return result
            api.kernel = SimpleNamespace(K32GetMappedFileNameW=path)
            with self.subTest(result=result, text=text), self.assertRaises(ValueError):
                api.mapped_path(1, BASE)


class FixtureCleanupTests(unittest.TestCase):
    def test_all_parent_cleanup_attempted_despite_each_failure(self):
        for fault in ('none', 'poll', 'kill', 'wait', 'stdin', 'stdout', 'stderr', 'file-2', 'file-1'):
            calls = []

            def op(name, result=None):
                def invoke(*args, **kwargs):
                    calls.append(name)
                    if name == fault:
                        raise OSError(name)
                    return result
                return invoke

            child = SimpleNamespace(poll=op('poll'), kill=op('kill'), wait=op('wait', 0),
                stdin=SimpleNamespace(close=op('stdin')), stdout=SimpleNamespace(close=op('stdout')),
                stderr=SimpleNamespace(close=op('stderr')))
            kernel = SimpleNamespace(CloseHandle=lambda handle: op('file-'+str(handle), 1)())
            with self.subTest(fault=fault):
                if fault == 'none':
                    cleanup_parent(child, kernel, [1, 2])
                else:
                    with self.assertRaises(ValueError):
                        cleanup_parent(child, kernel, [1, 2])
                self.assertEqual(calls[-2:], ['file-2', 'file-1'])
                for operation in ('wait', 'stdin', 'stdout', 'stderr'):
                    self.assertIn(operation, calls)

    def test_closehandle_false_does_not_skip_other_handles(self):
        calls = []
        def close(handle):
            calls.append(handle)
            return int(handle != 2)
        with self.assertRaisesRegex(ValueError, 'CloseHandle refused'):
            cleanup_parent(None, SimpleNamespace(CloseHandle=close), [1, 2])
        self.assertEqual(calls, [2, 1])

    def test_release_false_is_not_retried(self):
        calls = []
        def free(handle):
            calls.append(handle)
            return 0
        with self.assertRaisesRegex(ValueError, 'uncertain'):
            drop(SimpleNamespace(FreeLibrary=free), 123)
        self.assertEqual(calls, [123])

    def test_file_path_is_queried_from_owned_handle_in_native_namespace(self):
        def query(handle, buffer, count, flags):
            self.assertEqual((handle, flags), (123, 2))
            buffer.value = NATIVE
            return len(NATIVE)
        self.assertEqual(file_native_path(SimpleNamespace(GetFinalPathNameByHandleW=query), 123), NATIVE)

    def test_file_path_failed_or_truncated(self):
        for value in (0, 32768, len(NATIVE)+1):
            def query(handle, buffer, count, flags):
                buffer.value = NATIVE
                return value
            with self.subTest(value=value), self.assertRaises(ValueError):
                file_native_path(SimpleNamespace(GetFinalPathNameByHandleW=query), 123)


if __name__ == '__main__':
    unittest.main()
