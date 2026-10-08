import unittest

import p99_wrist_abi_manifest as abi
import p99_provider_preparation_manifest as prep
from test_p99_wrist_abi_manifest import Fixture as OldFixture


class Fixture(OldFixture):
    """Synthetic ten-export DLL, never loaded. Import inventory is explicit."""
    def __init__(self):
        self.raw = bytearray(0x9400)
        self.raw[:2] = b'MZ'
        self.pack(0x3c, '<I', 0x80)
        self.raw[0x80:0x84] = b'PE\0\0'
        self.pack(0x84, '<HHIIIHH', 0x8664, 6, 0, 0, 0, 0xf0, 0x2022)
        self.pack(0x98, '<H', 0x20b)
        self.pack(0xa8, '<IIQII', 0x1000, 0x1000, 0x180000000, 4096, 512)
        self.pack(0xd0, '<II', 0xa000, 0x400)
        self.pack(0xdc, '<HH', 2, 0x140)
        self.pack(0x104, '<I', 16)
        sections = (('.text', 0x1000, 0x1000, 0x60000020),
            ('.rdata', 0x2000, 0x4000, 0x40000040),
            ('.pdata', 0x6000, 0x1000, 0x40000040),
            ('.p99prep', 0x7000, 0x1000, 0xc0000040),
            ('.p99deps', 0x8000, 0x1000, 0xc0000040),
            ('.reloc', 0x9000, 0x1000, 0x42000040))
        for i, (name, rva, size, flags) in enumerate(sections):
            self.pack(0x188+i*40, '<8sIIIIIIHHI', name.encode(), size, rva,
                      size, rva-0xc00, 0, 0, 0, 0, flags)
        self.directory(0, 0x2000, 0x600)
        names = sorted(prep.ROLES)
        self.exports = {n: 0x7000 if n == prep.OFFER else 0x8000 if n == prep.PROVIDERS
                        else 0x1000+i*0x80 for i, n in enumerate(names)}
        self.write(0x2080, b'fixture.dll\0')
        self.rpack(0x2000, '<IIHHIIIIIII', 0, 0, 0, 0, 0x2080, 1, 10, 10, 0x20a0, 0x2100, 0x2140)
        self.name_at, self.ordinal_at, self.eat_at, records = {}, {}, {}, []
        name_at = 0x2180
        for i, name in enumerate(names):
            self.rpack(0x20a0+i*4, '<I', self.exports[name])
            self.rpack(0x2100+i*4, '<I', name_at)
            self.rpack(0x2140+i*2, '<H', i)
            self.name_at[name], self.ordinal_at[name], self.eat_at[name] = name_at, 0x2140+i*2, 0x20a0+i*4
            self.write(name_at, name.encode()+b'\0')
            name_at += len(name)+1
            if name in prep.PAGES:
                continue
            rva, unwind = self.exports[name], 0x5700+i*16
            self.write(rva, b'\x90'*63+b'\xc3')
            self.write(unwind, b'\x01\0\0\0')
            records.append((rva, rva+32, unwind))
            if name == prep.ENTRY:
                self.write(0x5800, b'\x21\0\0\0')
                self.rpack(0x5804, '<III', rva, rva+32, unwind)
                records.append((rva+32, rva+64, 0x5800))
        self.records = sorted(records)
        self.directory(3, 0x6000, len(records)*12)
        for i, record in enumerate(self.records):
            self.rpack(0x6000+i*12, '<III', *record)
        self.directory(5, 0x9000, 12)
        self.rpack(0x9000, '<IIHH', 0x5000, 12, 0xa500, 0)
        self.rpack(0x5500, '<Q', 0x180001000)
        self.directory(1, 0x2800, (len(prep.IMPORTS)+1)*20)
        dll_at, ilt, iat, hint = 0x2900, 0x2b00, 0x2e00, 0x3100
        self.import_name_at, self.import_iat, self.dll_at = {}, {}, {}
        for i, (dll, symbols) in enumerate(sorted(prep.IMPORTS.items())):
            self.dll_at[dll] = dll_at
            self.write(dll_at, dll.encode()+b'\0')
            self.rpack(0x2800+i*20, '<IIIII', ilt, 0, 0, dll_at, iat)
            dll_at += len(dll)+1
            for symbol in sorted(symbols):
                self.import_name_at[(dll, symbol)] = hint+2
                self.import_iat[(dll, symbol)] = iat
                self.write(hint, b'\0\0'+symbol.encode()+b'\0')
                self.rpack(ilt, '<Q', hint)
                self.rpack(iat, '<Q', hint)
                hint = (hint+len(symbol)+3+1) & ~1
                ilt += 8
                iat += 8
            ilt += 8
            iat += 8
        self.directory(12, 0x2e00, iat-0x2e00)
        assert dll_at <= 0x2b00 and ilt <= 0x2e00 and iat <= 0x3100 and hint < 0x5500

    def manifest(self):
        return prep.manifest(self.raw, abi.digest(self.raw))


class PreparationManifestTests(unittest.TestCase):
    def reject(self, change, message):
        f = Fixture()
        change(f)
        with self.assertRaisesRegex(ValueError, message):
            f.manifest()

    def test_new_contract_covers_both_pages_and_all_entry_fragments(self):
        f = Fixture()
        original = bytes(f.raw)
        result = f.manifest()
        self.assertEqual(bytes(f.raw), original)
        self.assertEqual(result['schema'], prep.SCHEMA)
        self.assertEqual(len(result['roles']), 10)
        self.assertEqual(len(result['imports']), 59)
        self.assertEqual([r['size'] for r in result['roles'][prep.ENTRY]['fragments']], [32, 32])
        self.assertEqual(result['preparation_entry']['request_bytes'], 133952)
        self.assertEqual(len(bytes.fromhex(result['preparation_entry']['prefix_hex'])), 32)
        for key in ('admitted_for_install', 'loaded_image_verified', 'dependency_closure_verified', 'preparation_executed'):
            self.assertIs(result[key], False)

    def test_contracts_cannot_accept_each_other_and_old_policy_is_unchanged(self):
        old, new = OldFixture(), Fixture()
        for parser, f in ((abi.manifest, new), (prep.manifest, old)):
            with self.assertRaisesRegex(ValueError, 'export count'):
                parser(f.raw, abi.digest(f.raw))
        old.write(0x25b2, b'VirtualProtect\0')
        with self.assertRaisesRegex(ValueError, 'installer'):
            old.manifest()
        self.assertEqual(len(abi.ROLES), 8)

    def test_missing_hash_or_changed_bytes_refused(self):
        f = Fixture()
        for sha in (None, '', '0'*64, 'x'*64):
            with self.subTest(sha=sha), self.assertRaisesRegex(ValueError, 'SHA256'):
                prep.manifest(f.raw, sha)
        sha = abi.digest(f.raw)
        f.write(f.exports[prep.ENTRY]+40, b'\xcc')
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
            prep.manifest(f.raw, sha)

    def test_unexpected_or_missing_exports_refused(self):
        self.reject(lambda f: f.write(f.name_at[prep.ENTRY], b'TllP99PrepareProvidersV2\0'), 'export')
        self.reject(lambda f: f.rpack(0x2018, '<I', 9), 'export count')
        self.reject(lambda f: f.rpack(f.ordinal_at[prep.PROVIDERS], '<H', 0), 'ordinal')
        self.reject(lambda f: f.rpack(f.eat_at[prep.ENTRY], '<I', 0x2400), 'forwarded')

    def test_pages_cannot_alias_swap_or_be_interior(self):
        self.reject(lambda f: f.rpack(f.eat_at[prep.PROVIDERS], '<I', 0x7000), 'aliased')
        def swap(f):
            f.rpack(f.eat_at[prep.PROVIDERS], '<I', 0x7000)
            f.rpack(f.eat_at[prep.OFFER], '<I', 0x8000)
        self.reject(swap, 'named RW')
        self.reject(lambda f: f.rpack(f.eat_at[prep.PROVIDERS], '<I', 0x8008), 'unmapped|named RW')

    def test_pages_zero_and_exact_rw_sections(self):
        for i, rva in ((3, 0x7000), (4, 0x8000)):
            with self.subTest(page=hex(rva)):
                self.reject(lambda f: f.write(rva+4095, b'\x01'), 'zero bytes')
                self.reject(lambda f: f.pack(0x188+i*40+36, '<I', 0x40000040), 'named RW')
                self.reject(lambda f: f.pack(0x188+i*40+36, '<I', 0xc2000040), 'named RW')
                self.reject(lambda f: f.pack(0x188+i*40+36, '<I', 0xe0000040), 'permission')
                self.reject(lambda f: f.pack(0x188+i*40+8, '<I', 4095), 'named RW')

    def test_preparation_requires_primary_exact_unwind_boundary(self):
        self.reject(lambda f: f.rpack(f.eat_at[prep.ENTRY], '<I', f.exports[prep.ENTRY]+1), 'exact runtime')
        self.reject(lambda f: f.rpack(f.eat_at[prep.ENTRY], '<I', f.exports[prep.ENTRY]+32), 'primary unwind')
        self.reject(lambda f: f.rpack(0x5804, '<I', 0x1010), 'missing')

    def test_entry_prefix_must_not_be_relocated(self):
        def relocate(f):
            at = f.exports[prep.ENTRY]+8
            f.rpack(0x9000, '<IIHH', at & ~4095, 12, 0xa000 | (at & 4095), 0)
            f.rpack(at, '<Q', 0x180001000)
        self.reject(relocate, 'prefix contains DIR64')

    def test_prefix_cannot_cross_primary_range(self):
        def shorten(f):
            f.rpack(0x6004, '<I', 0x1010)
            f.rpack(0x5808, '<I', 0x1010)
        self.reject(shorten, 'prefix exceeds')

    def test_import_allowlist_is_exact_by_dll_and_name(self):
        changes = [('kernel32.dll', 'VirtualProtect', b'CreateThread\0'),
                   ('kernel32.dll', 'FlsAlloc', b'FlSAlloc\0'),
                   ('bcrypt.dll', 'BCryptHash', b'OpenFile\0')]
        for dll, name, replacement in changes:
            with self.subTest(name=name):
                self.reject(lambda f: f.write(f.import_name_at[(dll, name)], replacement), 'import inventory')
        self.reject(lambda f: f.write(f.dll_at['bcrypt.dll'], b'otherx.dll\0'), 'import inventory')

    def test_imports_are_complete_not_truncated_or_prebound(self):
        self.reject(lambda f: f.directory(1, 0x2800, 20), 'terminator')
        self.reject(lambda f: f.rpack(0x2e00, '<Q', 0), 'mismatched disk IAT')
        self.reject(lambda f: f.rpack(0x2804, '<I', 1), 'bound/partial')

    def test_tail_body_and_unwind_hashes_change_even_beyond_prefix(self):
        f = Fixture()
        first = f.manifest()
        f.write(f.exports[prep.ENTRY]+40, b'\x41')
        second = f.manifest()
        self.assertEqual(first['preparation_entry']['prefix_hex'], second['preparation_entry']['prefix_hex'])
        self.assertNotEqual(first['roles'][prep.ENTRY]['fragments'][1], second['roles'][prep.ENTRY]['fragments'][1])
        f.write(0x5900, b'\x42')
        self.assertNotEqual(second['sections'][1]['raw_sha256'], f.manifest()['sections'][1]['raw_sha256'])

    def test_generic_pe_rejections_still_apply(self):
        self.reject(lambda f: f.directory(9, 0x5900, 40), 'unsupported directory')
        self.reject(lambda f: f.raw.append(0), 'overlay')
        self.reject(lambda f: f.pack(0x188+36, '<I', 0xe0000020), 'permission')


if __name__ == '__main__':
    unittest.main()
