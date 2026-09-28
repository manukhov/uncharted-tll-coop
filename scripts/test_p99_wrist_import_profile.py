import ctypes as ct
import dataclasses
import json
import unittest

from p99_wrist_abi_manifest import digest
from p99_wrist_import_profile import IMPORTER, SCHEMA, read_profile
from p99_wrist_mapped_fixture import import_bindings, locked_snapshot
from test_p99_wrist_import_graph import fixture, SYMBOL, BASE, CONTRACT


def document():
    return {'schema': SCHEMA, 'scope': 'owned-python-fixture-only',
            'providers': [{'name': 'first.dll', 'path': 'C:\\fixture\\first.dll', 'sha256': digest(fixture().raw)}],
            'api_routes': [{'importer': IMPORTER, 'contract': CONTRACT, 'host': 'first.dll'}]}


class ProfileTests(unittest.TestCase):
    def test_owned_normalized_frozen_profile(self):
        doc = document()
        doc['providers'][0]['name'] = 'FIRST'
        profile = read_profile(json.dumps(doc))
        self.assertEqual(profile.providers[0].name, 'first.dll')
        self.assertEqual(profile.api_routes[0].importer, IMPORTER)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            profile.providers[0].sha256 = '0'*64

    def test_scope_and_unknown_fields(self):
        for key, value in (('scope', 'remote'), ('schema', 'v2'), ('unknown', 1), ('providers', [])):
            doc = document()
            doc[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                read_profile(json.dumps(doc))

    def test_duplicate_json_keys_and_bound(self):
        for text in ('{"scope":1,"scope":2}', ' '*65537):
            with self.subTest(text=text[:40]), self.assertRaises(ValueError):
                read_profile(text)

    def test_bad_provider_paths(self):
        for path in ('first.dll', 'C:first.dll', 'C:\\a\\..\\first.dll', '\\\\server\\share\\first.dll',
                     'C:\\a\\first.dll:stream', 'C:\\a\\second.dll', 'C:\\a\\first.dll\0', '\\\\?\\C:\\first.dll'):
            doc = document()
            doc['providers'][0]['path'] = path
            with self.subTest(path=path), self.assertRaises(ValueError):
                read_profile(json.dumps(doc))

    def test_bad_hash_duplicate_provider_and_artifact(self):
        for change in ('hash', 'duplicate', 'artifact'):
            doc = document()
            if change == 'hash':
                doc['providers'][0]['sha256'] = '0'
            elif change == 'duplicate':
                doc['providers'].append(doc['providers'][0])
            else:
                doc['providers'][0]['name'] = IMPORTER
            with self.subTest(change=change), self.assertRaises(ValueError):
                read_profile(json.dumps(doc))

    def test_bad_and_duplicate_routes(self):
        for field, value in (('host', 'unknown.dll'), ('importer', '*'), ('contract', 'ordinary.dll')):
            doc = document()
            doc['api_routes'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                read_profile(json.dumps(doc))
        doc = document()
        doc['api_routes'] *= 2
        with self.assertRaises(ValueError):
            read_profile(json.dumps(doc))


class Kernel:
    """Own fake loader; permits observing, not supplying, expected export values."""
    def __init__(self):
        self.file = fixture()
        self.path = 'C:\\fixture\\first.dll'
        self.handles = {self.path.lower(): BASE, 'first.dll': BASE, CONTRACT: BASE}
        self.proc = BASE+self.file.exports[SYMBOL]
        self.proc_calls = []
        self.full_path = self.path
        self.filename_count = None
        self.file_handle, self.file_size, self.short_read = 123, len(self.file.raw), False

    def GetModuleHandleExW(self, flags, name, out):
        if name.lower() not in self.handles:
            return 0
        ct.cast(out, ct.POINTER(ct.c_void_p))[0] = self.handles[name.lower()]
        return 1

    def GetModuleFileNameW(self, base, buffer, size):
        buffer.value = self.full_path
        return len(buffer.value) if self.filename_count is None else self.filename_count

    def GetProcAddress(self, base, name):
        self.proc_calls.append((base, name))
        return self.proc

    def CreateFileW(self, path, access, share, security, disposition, attributes, template):
        assert access == 0x80000000 and share == 1 and disposition == 3
        return self.file_handle

    def GetFileSizeEx(self, handle, size):
        ct.cast(size, ct.POINTER(ct.c_int64))[0] = self.file_size
        return 1

    def ReadFile(self, handle, buffer, size, out, overlapped):
        ct.memmove(buffer, bytes(self.file.raw), size)
        ct.cast(out, ct.POINTER(ct.c_uint32))[0] = size-int(self.short_read)
        return 1


class OwnedBindingTests(unittest.TestCase):
    def call(self, kernel=None, dll='first.dll'):
        kernel = kernel or Kernel()
        refs = []
        bindings, results = import_bindings(kernel, read_profile(json.dumps(document())),
            {'first.dll': bytes(kernel.file.raw)}, [{'dll': dll, 'name': SYMBOL}], refs)
        return bindings, results, refs

    def test_expected_disk_graph_and_references(self):
        bindings, rows, refs = self.call()
        self.assertEqual(bindings[0].address, BASE+fixture().exports[SYMBOL])
        self.assertEqual(refs, [BASE, BASE])
        self.assertFalse(rows[0]['provider_mapped_bytes_verified'])
        self.assertFalse(rows[0]['remote_lifetime_verified'])

    def test_api_route_source_checked(self):
        bindings, rows, refs = self.call(dll=CONTRACT)
        self.assertEqual(bindings[0].dll, CONTRACT)
        self.assertEqual(rows[0]['steps'][0]['kind'], 'api-set-input')
        kernel = Kernel()
        kernel.handles[CONTRACT] = BASE+0x10000
        with self.assertRaisesRegex(ValueError, 'source differs'):
            self.call(kernel, CONTRACT)
        self.assertEqual(kernel.proc_calls, [])

    def test_observed_export_is_not_used_as_expectation(self):
        for proc in (None, BASE+0x333, BASE+0x10000):
            kernel = Kernel()
            kernel.proc = proc
            with self.subTest(proc=proc), self.assertRaisesRegex(ValueError, 'independent disk graph'):
                self.call(kernel)

    def test_missing_wrong_path_and_truncation_refused(self):
        for fault in ('missing', 'wrong', 'truncated', 'failed'):
            kernel = Kernel()
            if fault == 'missing':
                del kernel.handles[kernel.path.lower()]
            elif fault == 'wrong':
                kernel.full_path = 'C:\\wrong\\first.dll'
            else:
                kernel.filename_count = 32768 if fault == 'truncated' else 0
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                self.call(kernel)
            self.assertEqual(kernel.proc_calls, [])

    def test_snapshot_locks_then_reads_same_handle(self):
        kernel, handles = Kernel(), []
        self.assertEqual(locked_snapshot(kernel, kernel.path, handles), bytes(kernel.file.raw))
        self.assertEqual(handles, [123])

    def test_failed_open_never_retained(self):
        for value in (None, ct.c_void_p(-1).value):
            kernel, handles = Kernel(), []
            kernel.file_handle = value
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'file lock'):
                locked_snapshot(kernel, kernel.path, handles)
            self.assertEqual(handles, [])

    def test_failed_read_and_size_keep_handle_for_caller_cleanup(self):
        for fault in ('short', 'empty', 'huge'):
            kernel, handles = Kernel(), []
            if fault == 'short':
                kernel.short_read = True
            else:
                kernel.file_size = 0 if fault == 'empty' else 64*1024*1024+1
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                locked_snapshot(kernel, kernel.path, handles)
            self.assertEqual(handles, [123])


if __name__ == '__main__':
    unittest.main()
