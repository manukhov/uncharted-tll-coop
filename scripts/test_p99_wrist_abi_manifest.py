import struct
import unittest
from unittest.mock import patch

import p99_wrist_abi_manifest as abi


class Fixture:
    """Owned synthetic PE, no game/build artifact dependency and no execution."""
    def __init__(self):
        self.raw = bytearray(0x5400)
        self.raw[:2] = b'MZ'
        self.pack(0x3c, '<I', 0x80)
        self.raw[0x80:0x84] = b'PE\0\0'
        self.pack(0x84, '<HHIIIHH', 0x8664, 5, 0, 0, 0, 0xf0, 0x2022)
        op = 0x98
        self.pack(op, '<H', 0x20b)
        self.pack(op+16, '<IIQII', 0x1000, 0x1000, 0x180000000, 4096, 512)
        self.pack(op+56, '<II', 0x6000, 0x400)
        self.pack(op+68, '<HH', 2, 0x140)
        self.pack(op+108, '<I', 16)
        for i, (name, flags) in enumerate((('.text', 0x60000020), ('.rdata', 0x40000040),
                ('.pdata', 0x40000040), ('.p99prep', 0xc0000040), ('.reloc', 0x42000040))):
            self.pack(0x188+i*40, '<8sIIIIIIHHI', name.encode(), 0x1000,
                      (i+1)*0x1000, 0x1000, 0x400+i*0x1000, 0, 0, 0, 0, flags)
        self.directory(0, 0x2000, 0x500)
        names = sorted(abi.ROLES)
        self.exports = {n: (0x4000 if n == 'TllP99WristPreparedV1' else 0x1000+i*0x80)
                        for i, n in enumerate(names)}
        self.write(0x2080, b'fixture.dll\0')
        self.rpack(0x2000, '<IIHHIIIIIII', 0, 0, 0, 0, 0x2080, 1, 8, 8, 0x20a0, 0x20c0, 0x20e0)
        name_at, records = 0x2100, []
        self.name_at, self.ordinal_at, self.eat_at = {}, {}, {}
        for i, name in enumerate(names):
            self.rpack(0x20a0+i*4, '<I', self.exports[name])
            self.rpack(0x20c0+i*4, '<I', name_at)
            self.rpack(0x20e0+i*2, '<H', i)
            self.name_at[name], self.ordinal_at[name], self.eat_at[name] = name_at, 0x20e0+i*2, 0x20a0+i*4
            self.write(name_at, name.encode()+b'\0')
            name_at += len(name)+1
            if name == 'TllP99WristPreparedV1':
                continue
            rva, unwind = self.exports[name], 0x2800+i*16
            self.write(rva, bytes([0x90])*31+b'\xc3')
            self.write(unwind, b'\x01\0\0\0')
            records.append((rva, rva+16, unwind))
            if name == 'TllP99WristCmd16V1':
                self.cmd_unwind = unwind
                self.write(0x2900, b'\x21\0\0\0')
                self.rpack(0x2904, '<III', rva, rva+16, unwind)
                records.append((rva+16, rva+32, 0x2900))
        self.records = sorted(records)
        self.directory(3, 0x3000, len(records)*12)
        for i, record in enumerate(self.records):
            self.rpack(0x3000+i*12, '<III', *record)
        # One image-relative data pointer requiring DIR64 relocation.
        self.directory(5, 0x5000, 12)
        self.rpack(0x5000, '<IIHH', 0x2000, 12, 0xa600, 0)
        self.rpack(0x2600, '<Q', 0x180001000)
        # One named import and a terminated descriptor/thunk pair.
        self.directory(1, 0x2500, 40)
        self.directory(12, 0x2590, 16)
        self.rpack(0x2500, '<IIIII', 0x2580, 0, 0, 0x2540, 0x2590)
        self.write(0x2540, b'KERNEL32.dll\0')
        self.rpack(0x2580, '<QQ', 0x25b0, 0)
        self.rpack(0x2590, '<QQ', 0x25b0, 0)
        self.write(0x25b0, b'\0\0GetLastError\0')

    def pack(self, at, fmt, *values):
        struct.pack_into(fmt, self.raw, at, *values)

    def write(self, rva, data):
        self.raw[rva-0xc00:rva-0xc00+len(data)] = data

    def rpack(self, rva, fmt, *values):
        self.write(rva, struct.pack(fmt, *values))

    def directory(self, index, rva, size):
        self.pack(0x98+112+index*8, '<II', rva, size)

    def manifest(self):
        return abi.manifest(self.raw, abi.digest(self.raw))


class AbiManifestTests(unittest.TestCase):
    def reject(self, change, message):
        fixture = Fixture()
        change(fixture)
        with self.assertRaisesRegex(ValueError, message):
            fixture.manifest()

    def test_full_manifest_is_inert_and_covers_chained_body(self):
        f = Fixture()
        before = bytes(f.raw)
        m = f.manifest()
        self.assertEqual(f.raw, before)
        self.assertFalse(m['admitted_for_install'])
        self.assertFalse(m['loaded_image_verified'])
        self.assertEqual(m['patch_rvas'][0], m['patch_rvas'][1])
        self.assertNotEqual(m['patch_rvas'][2], m['patch_rvas'][3])
        chunks = m['roles']['TllP99WristCmd16V1']['fragments']
        self.assertEqual([c['size'] for c in chunks], [16, 16])
        self.assertEqual(len(m['sections']), 5)
        self.assertEqual(len(m['dir64_relocations']), 1)

    def test_hash_is_required_and_checked_before_parse(self):
        f = Fixture()
        for bad in ('', None, 'a'*63, 'g'*64, '0'*64):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, 'SHA256'):
                abi.manifest(f.raw, bad)
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            abi.manifest(bytes(len(f.raw)), abi.digest(f.raw))

    def test_missing_or_unexpected_role(self):
        self.reject(lambda f: f.write(f.name_at['TllP99WristCmd16V1'], b'TllP99WristCmd16V2\0'), 'export')

    def test_ordinal_export_and_count_refused(self):
        self.reject(lambda f: f.rpack(0x2018, '<I', 7), 'export count')

    def test_export_directory_not_truncated_or_writable(self):
        self.reject(lambda f: f.directory(0, 0x2000, 1), 'export directory extent')
        self.reject(lambda f: f.directory(0, 0x2000, 40), 'outside directory')
        self.reject(lambda f: f.pack(0x188+40+36, '<I', 0xc0000040), 'readonly')

    def test_duplicate_ordinal(self):
        self.reject(lambda f: f.rpack(0x20e2, '<H', 0), 'ordinal')

    def test_forwarder_refused(self):
        self.reject(lambda f: f.rpack(f.eat_at['TllP99WristAsyncV1'], '<I', 0x2300), 'forwarded')

    def test_alias_between_distinct_roles_refused(self):
        self.reject(lambda f: f.rpack(f.eat_at['TllP99WristProcess1V1'], '<I',
                                     f.exports['TllP99WristProcess0V1']), 'aliased')

    def test_export_interior_not_function_entry(self):
        self.reject(lambda f: f.rpack(f.eat_at['TllP99WristAsyncV1'], '<I',
                                     f.exports['TllP99WristAsyncV1']+1), 'exact runtime')

    def test_export_at_chained_secondary_refused(self):
        self.reject(lambda f: f.rpack(f.eat_at['TllP99WristCmd16V1'], '<I',
                                     f.exports['TllP99WristCmd16V1']+16), 'primary unwind')

    def test_unmapped_unwind_refused(self):
        self.reject(lambda f: f.rpack(0x3008, '<I', 0x9000), 'unmapped')

    def test_chain_cycle_refused(self):
        self.reject(lambda f: f.rpack(0x2904, '<III', f.exports['TllP99WristCmd16V1']+16,
                                     f.exports['TllP99WristCmd16V1']+32, 0x2900), 'cyclic')

    def test_missing_chain_parent_refused(self):
        self.reject(lambda f: f.rpack(0x2904, '<I', 0x1001), 'missing')

    def test_chain_work_is_bounded_and_frame_descriptor_matches(self):
        with patch.object(abi, 'MAX_CHAIN_DEPTH', 0):
            self.reject(lambda f: None, 'depth bound')
        self.reject(lambda f: f.write(0x2903, b'\x15'), 'frame descriptor')

    def test_bad_unwind_flags_and_version(self):
        for first in (2, 0xe1, 0x29):
            with self.subTest(first=first):
                self.reject(lambda f: f.write(0x2800, bytes([first])), 'unwind')

    def test_runtime_overlapping_ranges(self):
        self.reject(lambda f: f.rpack(0x300c, '<I', 0x1001), 'ordering')

    def test_rw_code_refused(self):
        self.reject(lambda f: f.pack(0x188+36, '<I', 0xe0000020), 'permission')

    def test_code_in_nonexec_section_refused(self):
        self.reject(lambda f: f.pack(0x188+36, '<I', 0x40000020), 'RX')

    def test_virtual_and_raw_overlap(self):
        self.reject(lambda f: f.pack(0x188+40+12, '<I', 0x1000), 'virtual overlap')
        self.reject(lambda f: f.pack(0x188+40+20, '<I', 0x400), 'raw overlap')

    def test_truncation_and_overlay_refused(self):
        self.reject(lambda f: f.raw.pop(), 'raw extent')
        self.reject(lambda f: f.raw.append(0), 'overlay')

    def test_prepared_page_nonzero_or_not_rw_refused(self):
        self.reject(lambda f: f.write(0x4000, b'\x01'), 'zero bytes')
        self.reject(lambda f: f.pack(0x188+3*40+36, '<I', 0x40000040), 'RW prepared')

    def test_bad_relocation_type_or_block(self):
        self.reject(lambda f: f.rpack(0x5008, '<H', 0x3600), 'DIR64')
        self.reject(lambda f: f.rpack(0x5004, '<I', 10), 'block extent')
        self.reject(lambda f: f.rpack(0x2600, '<Q', 1), 'outside image')

    def test_directory_and_tls_refused(self):
        self.reject(lambda f: f.directory(9, 0x2700, 40), 'unsupported directory')
        self.reject(lambda f: f.directory(5, 0, 12), 'partial directory')

    def test_installer_import_refused(self):
        self.reject(lambda f: f.write(0x25b2, b'CreateThread\0'), 'installer')

    def test_no_partial_import_manifest(self):
        self.reject(lambda f: f.directory(1, 0x2500, 20), 'terminator')
        self.reject(lambda f: f.rpack(0x2500, '<I', 0), 'partial import')
        self.reject(lambda f: f.rpack(0x2590, '<Q', 0), 'mismatched disk IAT')
        self.reject(lambda f: f.directory(12, 0x2590, 24), 'IAT coverage')
        self.reject(lambda f: f.rpack(0x2514, '<IIIII', 0x9000, 0, 0, 0x9000, 0x9000), 'unmapped')

    def test_import_ordinals_and_duplicate_descriptors_refused(self):
        def ordinal(f):
            f.rpack(0x2580, '<Q', 0x8000000000000001)
            f.rpack(0x2590, '<Q', 0x8000000000000001)
        self.reject(ordinal, 'ordinal')
        self.reject(lambda f: f.rpack(0x2514, '<IIIII', 0x2580, 0, 0, 0x2540, 0x2590), 'duplicate import DLL')

    def test_changed_body_requires_new_trust_and_manifest(self):
        f = Fixture()
        first = f.manifest()
        f.write(f.exports['TllP99WristCmd16V1']+20, b'\x41')
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            abi.manifest(f.raw, first['file_sha256'])
        second = f.manifest()
        self.assertNotEqual(first['roles']['TllP99WristCmd16V1']['fragments'],
                            second['roles']['TllP99WristCmd16V1']['fragments'])


if __name__ == '__main__':
    unittest.main()
