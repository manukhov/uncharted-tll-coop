import dataclasses
import struct
import unittest

import p99_provider_preparation_manifest as disk
import p99_provider_preparation_mapped as preparation
import p99_wrist_abi_manifest as old_disk
import p99_wrist_mapped_image as mapped
from p99_wrist_mapped_windows import Reader
from test_p99_provider_preparation_manifest import Fixture
from test_p99_wrist_abi_manifest import Fixture as OldFixture
from test_p99_wrist_mapped_image import Memory as OldMemory, BASE, TARGET, IDENTITY


class Memory(OldMemory):
    """Synthetic mapping, built from fixture layout, NOT the comparison Plan."""
    def __init__(self, fixture, base=BASE):
        self.image = disk.Image(fixture.raw, old_disk.digest(fixture.raw))
        self.base, self.identity_value = base, IDENTITY
        self.data = bytearray(0xa000)
        self.data[:0x400] = fixture.raw[:0x400]
        for s in self.image.sections:
            count = min(s['virtual_size'], s['raw_size'])
            self.data[s['rva']:s['rva']+count] = fixture.raw[s['raw_offset']:s['raw_offset']+count]
        struct.pack_into('<Q', self.data, 0x5500, base+0x1000)
        self.bindings = []
        for i, ((dll, name), at) in enumerate(fixture.import_iat.items()):
            address = TARGET+i*0x100
            struct.pack_into('<Q', self.data, at, address)
            self.bindings.append(mapped.Binding(dll, name, address))
        self.after_read = None
        self.reads = self.queries = self.boundaries = 0
        self.query_override = {}
        self.short_read = False
        self.read_addresses = []


def setup(*, base=BASE, change=None, header_base=mapped.HeaderBase.PREFERRED):
    fixture = Fixture()
    if change:
        change(fixture)
    memory = Memory(fixture, base)
    if header_base is mapped.HeaderBase.LOADED:
        struct.pack_into('<Q', memory.data, 0xb0, base)
    plan = preparation.build_plan(fixture.raw, old_disk.digest(fixture.raw), base,
                                  memory.bindings, header_base=header_base)
    return fixture, plan, memory, Reader(1, IDENTITY, base, plan.size, api=memory)


class PreparationMappedTests(unittest.TestCase):
    def test_all_bindings_and_immutable_regions_compared_without_admission(self):
        for base in (BASE, 0x180000000, 0x100000000):
            with self.subTest(base=hex(base)):
                _, plan, memory, reader = setup(base=base)
                result = mapped.compare(plan, reader)
                self.assertEqual(result['iat_bindings_compared'], 59)
                self.assertEqual(result['dir64_applied'], 1)
                self.assertEqual(result['compared_bytes'], 0x6400)
                self.assertFalse(result['admitted_for_install'])
                self.assertFalse(result['module_lifetime_verified'])
                self.assertFalse(result['dependency_integrity_verified'])
                self.assertFalse(result['atomic_snapshot'])
                self.assertEqual(memory.boundaries, 2)
                self.assertEqual({r.name for r in plan.regions}, {'headers', '.text', '.rdata', '.pdata'})

    def test_old_and_new_public_entrypoints_are_not_interchangeable(self):
        f, _, memory, _ = setup()
        with self.assertRaisesRegex(ValueError, 'export count'):
            mapped.build_plan(f.raw, old_disk.digest(f.raw), BASE, memory.bindings)
        old = OldFixture()
        with self.assertRaisesRegex(ValueError, 'export count'):
            preparation.build_plan(old.raw, old_disk.digest(old.raw), BASE, [])

    def test_both_publication_pages_must_not_be_discardable(self):
        for i in (3, 4):
            with self.subTest(section=i), self.assertRaisesRegex(ValueError, 'named RW'):
                setup(change=lambda f: f.pack(0x188+i*40+36, '<I', 0xc2000040))

    def test_both_publication_pages_remain_explicitly_unverified(self):
        _, plan, memory, reader = setup()
        memory.data[0x7000:0xa000] = b'\xab'*0x3000
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        for name in ('.p99deps', '.p99prep'):
            self.assertTrue(any(e.name == name and e.size == 4096 and e.reason.startswith('mutable') for e in plan.exclusions))
        self.assertTrue(all(rva < 0x7000 for rva in memory.read_addresses))

    def test_new_entry_chains_and_each_root_iat_detect_corruption(self):
        f = Fixture()
        offsets = [0, 0x2000, 0x6000, 0x5804, 0x5500,
                   f.exports[disk.ENTRY], f.exports[disk.ENTRY]+40, *f.import_iat.values()]
        for rva in offsets:
            with self.subTest(rva=hex(rva)):
                _, plan, memory, reader = setup()
                memory.data[rva] ^= 1
                with self.assertRaisesRegex(ValueError, 'mapped mismatch'):
                    mapped.compare(plan, reader)

    def test_required_new_bindings_cannot_be_dropped_or_invented(self):
        f, _, memory, _ = setup()
        for key in (('bcrypt.dll', 'BCryptHash'), ('msvcp140.dll', '?_Xout_of_range@std@@YAXPEBD@Z'),
                    ('kernel32.dll', 'FlsAlloc'), ('kernel32.dll', 'VirtualProtect')):
            missing = [b for b in memory.bindings if (b.dll, b.name) != key]
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'incomplete/unexpected'):
                preparation.build_plan(f.raw, old_disk.digest(f.raw), BASE, missing)
        for extra in (memory.bindings[0], mapped.Binding('kernel32.dll', 'GetProcAddress', TARGET)):
            with self.assertRaises(ValueError):
                preparation.build_plan(f.raw, old_disk.digest(f.raw), BASE, memory.bindings+[extra])

    def test_wrong_expected_addresses_do_not_learn_the_observed_iat(self):
        f, _, memory, reader = setup()
        changed = list(memory.bindings)
        changed[-1] = dataclasses.replace(changed[-1], address=TARGET+0x100000)
        plan = preparation.build_plan(f.raw, old_disk.digest(f.raw), BASE, changed)
        with self.assertRaisesRegex(ValueError, 'mapped mismatch .rdata'):
            mapped.compare(plan, reader)

    def test_new_metadata_reader_checks_complete_chain_tail(self):
        # The parser can read raw padding; the mapped plan must refuse metadata
        # after VirtualSize. Cutting through the new entry's CHAININFO payload
        # catches pairing a new manifest with missing/wrong metadata tracking.
        with self.assertRaisesRegex(ValueError, 'metadata read outside|partial immutable'):
            setup(change=lambda f: f.pack(0x188+40+8, '<I', 0x3804))

    def test_new_metadata_reader_tracks_import_lookup_beyond_virtual_size(self):
        def move(f):
            f.pack(0x188+40+8, '<I', 0x3a00)
            f.rpack(0x2800, '<I', 0x5c00)
            f.write(0x5c00, bytes(f.raw[0x2b00-0xc00:0x2b00-0xc00+32]))
        with self.assertRaisesRegex(ValueError, 'metadata read outside'):
            setup(change=move)

    def test_header_base_policy_is_explicit(self):
        _, plan, memory, reader = setup(header_base=mapped.HeaderBase.LOADED)
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        struct.pack_into('<Q', memory.data, 0xb0, BASE+65536)
        with self.assertRaisesRegex(ValueError, 'mapped mismatch headers'):
            mapped.compare(plan, reader)

    def test_input_is_owned_and_hash_cannot_be_refreshed_from_observed_bytes(self):
        f, plan, memory, reader = setup()
        f.raw[0] ^= 1
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
            preparation.build_plan(f.raw, plan.sha256, BASE, memory.bindings)


if __name__ == '__main__':
    unittest.main()
