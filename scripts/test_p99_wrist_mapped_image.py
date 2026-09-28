import dataclasses
import ctypes as ct
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import p99_wrist_abi_manifest as disk
import p99_wrist_mapped_image as mapped
from p99_wrist_mapped_windows import Identity, Reader, WindowsAPI, MBI
from test_p99_wrist_abi_manifest import Fixture

BASE, TARGET = 0x200000000, 0x700001000
IDENTITY = Identity(27, 123456)


class Memory:
    """Owned fake mapping built independently from Plan, with OS fault seams."""
    def __init__(self, fixture, base=BASE, target=TARGET):
        self.image = disk.Image(fixture.raw, disk.digest(fixture.raw))
        self.base, self.identity_value = base, IDENTITY
        self.data = bytearray(self.image.pe.OPTIONAL_HEADER.SizeOfImage)
        self.data[:0x400] = fixture.raw[:0x400]
        for s in self.image.sections:
            count = min(s['virtual_size'], s['raw_size'])
            self.data[s['rva']:s['rva']+count] = fixture.raw[s['raw_offset']:s['raw_offset']+count]
        # These exact fixture locations are independent of the plan generator.
        struct.pack_into('<Q', self.data, 0x2600, base+0x1000)
        struct.pack_into('<Q', self.data, 0x2590, target)
        self.after_read = None
        self.reads, self.queries, self.boundaries = 0, 0, 0
        self.query_override = {}
        self.short_read = False
        self.read_addresses = []

    def identity(self, handle):
        self.boundaries += 1
        return self.identity_value

    def query(self, handle, address):
        self.queries += 1
        offset = address-self.base
        s = next((s for s in self.image.sections if s['rva'] <= offset < s['rva']+0x1000), None)
        protection = 0x20 if s and s['characteristics'] & disk.EXECUTE else 0x02
        region = SimpleNamespace(BaseAddress=address & ~0xfff, AllocationBase=self.base,
                                 RegionSize=0x1000, State=0x1000, Type=0x1000000,
                                 Protect=protection, AllocationProtect=0x80)
        for name, value in self.query_override.items():
            setattr(region, name, value)
        return region

    def read(self, handle, address, size):
        self.reads += 1
        self.read_addresses.append(address-self.base)
        result = bytes(self.data[address-self.base:address-self.base+size])
        if self.after_read:
            self.after_read(self)
        return result[:-1] if self.short_read else result


def setup(base=BASE, change=None):
    fixture = Fixture()
    if change:
        change(fixture)
    bindings = [mapped.Binding('KERNEL32.dll', 'GetLastError', TARGET)]
    plan = mapped.build_plan(fixture.raw, disk.digest(fixture.raw), base, bindings)
    memory = Memory(fixture, base)
    reader = Reader(1, IDENTITY, base, plan.size, api=memory)
    return fixture, plan, memory, reader


class MappedImageTests(unittest.TestCase):
    def test_no_overlapping_header_section_or_empty_region(self):
        for stages in ([('header', 0, bytes(0x1800), 2), ('text', 0x1000, bytes(0x100), 32)],
                       [('empty', 0, b'', 2)]):
            with self.assertRaisesRegex(ValueError, 'comparison regions'):
                mapped._disjoint_regions(stages)

    def test_unrecognized_guard_slot_override_is_not_masked(self):
        # Declared load-config metadata points at a compared RO qword. No
        # policy permits its arbitrary replacement by an observed loader value.
        def config(f):
            f.directory(10, 0x2a00, 0x140)
            f.rpack(0x2a00, '<I', 0x140)
            f.rpack(0x2a70, '<Q', 0x180002700)
        _, plan, memory, reader = setup(change=config)
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        struct.pack_into('<Q', memory.data, 0x2700, TARGET)
        with self.assertRaisesRegex(ValueError, 'mapped mismatch .rdata'):
            mapped.compare(plan, reader)

    def test_explicit_loaded_header_policy_not_observed_wildcard(self):
        f, original, memory, reader = setup()
        plan = mapped.build_plan(f.raw, original.sha256, BASE, original.imports,
                                 header_base=mapped.HeaderBase.LOADED)
        with self.assertRaisesRegex(ValueError, 'mapped mismatch headers'):
            mapped.compare(plan, reader)  # Still disk preferred value.
        struct.pack_into('<Q', memory.data, 0xb0, BASE)
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        struct.pack_into('<Q', memory.data, 0xb0, BASE+65536)
        with self.assertRaisesRegex(ValueError, 'mapped mismatch headers'):
            mapped.compare(plan, reader)
        with self.assertRaisesRegex(ValueError, 'header ImageBase policy'):
            mapped.build_plan(f.raw, original.sha256, BASE, original.imports, header_base='guess')

    def test_preferred_positive_negative_delta(self):
        for base in (0x180000000, 0x200000000, 0x100000000):
            with self.subTest(base=base):
                _, plan, memory, reader = setup(base)
                result = mapped.compare(plan, reader)
                self.assertTrue(result['comparison_passed'])
                self.assertEqual(result['dir64_applied'], 1)
                self.assertEqual(result['iat_bindings_compared'], 1)
                self.assertFalse(result['admitted_for_install'])
                self.assertFalse(result['atomic_snapshot'])
                self.assertFalse(result['dependency_integrity_verified'])
                self.assertFalse(result['module_lifetime_verified'])
                self.assertEqual(memory.boundaries, 2)
                self.assertTrue(all(rva < 0x4000 for rva in memory.read_addresses))

    def test_immutable_coverage_corruption(self):
        fixture = Fixture()
        addresses = [0, 0x80, 0x2000, 0x20a0, 0x3000, 0x2904, 0x2600, 0x2590, 0x2598,
                     0x2700, fixture.exports['TllP99WristCmd16V1'], fixture.exports['TllP99WristCmd16V1']+20]
        for address in addresses:
            with self.subTest(rva=hex(address)):
                _, plan, memory, reader = setup()
                memory.data[address] ^= 1
                with self.assertRaisesRegex(ValueError, 'mapped mismatch'):
                    mapped.compare(plan, reader)

    def test_binding_keys_are_exact_not_observed(self):
        f = Fixture()
        one = mapped.Binding('KERNEL32.dll', 'GetLastError', TARGET)
        bads = [[], [one, one], [dataclasses.replace(one, dll='ntdll.dll')],
                [dataclasses.replace(one, name='getlasterror')], [dataclasses.replace(one, address=0)],
                [dataclasses.replace(one, address=BASE+0x1000)],
                [one, dataclasses.replace(one, name='GetProcAddress')]]
        for bad in bads:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                mapped.build_plan(f.raw, disk.digest(f.raw), BASE, bad)
        good = mapped.build_plan(f.raw, disk.digest(f.raw), BASE, [dataclasses.replace(one, dll='kernel32.DLL')])
        self.assertEqual(len(good.imports), 1)

    def test_wrong_independent_target_does_not_learn_iat(self):
        f, _, memory, reader = setup()
        plan = mapped.build_plan(f.raw, disk.digest(f.raw), BASE,
                                 [mapped.Binding('KERNEL32.dll', 'GetLastError', TARGET+1)])
        with self.assertRaisesRegex(ValueError, 'mapped mismatch'):
            mapped.compare(plan, reader)

    def test_trust_hash_checked_and_input_owned(self):
        f, plan, memory, reader = setup()
        f.raw[0] ^= 1
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
            mapped.build_plan(f.raw, plan.sha256, BASE, plan.imports)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            plan.base = BASE+65536

    def test_base_overflow_alignment_and_null(self):
        f = Fixture()
        for base in (0, 0x10001, mapped.USER_END-4096, mapped.USER_END, True):
            with self.subTest(base=base), self.assertRaises(ValueError):
                mapped.build_plan(f.raw, disk.digest(f.raw), base,
                                  [mapped.Binding('KERNEL32.dll', 'GetLastError', TARGET)])
        self.assertFalse(mapped._address(mapped.USER_END-65536, 65537))

    def test_changed_relocation_table_not_masked(self):
        f, plan, _, _ = setup()
        f.rpack(0x5008, '<H', 0)
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
            mapped.build_plan(f.raw, plan.sha256, BASE, plan.imports)

    def test_iat_dir64_overlap_cannot_be_admitted(self):
        f = Fixture()
        f.rpack(0x5008, '<H', 0xa590)
        with self.assertRaises(ValueError):
            mapped.build_plan(f.raw, disk.digest(f.raw), BASE,
                              [mapped.Binding('KERNEL32.dll', 'GetLastError', TARGET)])
        f = Fixture()
        result = f.manifest()
        result['dir64_relocations'].append({'rva': 0x2590, 'preferred_value': 0x180001000})
        with patch.object(disk, 'manifest', return_value=result), self.assertRaisesRegex(ValueError, 'DIR64/IAT overlap'):
            mapped.build_plan(f.raw, disk.digest(f.raw), BASE,
                              [mapped.Binding('KERNEL32.dll', 'GetLastError', TARGET)])

    def test_partial_immutable_relocation_refused(self):
        with self.assertRaisesRegex(ValueError, 'partial immutable'):
            setup(change=lambda f: f.pack(0x188+40+8, '<I', 0x604))

    def test_entire_chain_tail_and_import_lookup_must_be_compared(self):
        with self.assertRaisesRegex(ValueError, 'immutable span|metadata'):
            setup(change=lambda f: f.pack(0x188+40+8, '<I', 0x904))
        def move_ilt_outside_virtual_size(f):
            f.pack(0x188+40+8, '<I', 0xa00)
            f.rpack(0x2500, '<I', 0x2f80)
            f.rpack(0x2f80, '<QQ', 0x25b0, 0)
        with self.assertRaisesRegex(ValueError, 'metadata'):
            setup(change=move_ilt_outside_virtual_size)

    def test_low_level_windows_read_boolean_and_exact_count(self):
        api = WindowsAPI.__new__(WindowsAPI)
        for success, count in ((False, 8), (True, 7), (False, 0)):
            def read(handle, address, buffer, size, out, success=success, count=count):
                ct.cast(out, ct.POINTER(ct.c_size_t))[0] = count
                return success
            api.kernel = SimpleNamespace(ReadProcessMemory=read)
            with self.subTest(success=success, count=count), self.assertRaisesRegex(ValueError, 'incomplete memory read'):
                api.read(1, BASE, 8)

    def test_low_level_query_return_size(self):
        api = WindowsAPI.__new__(WindowsAPI)
        for size in (0, ct.sizeof(MBI)-1):
            api.kernel = SimpleNamespace(VirtualQueryEx=lambda *_: size)
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, 'incomplete memory query'):
                api.query(1, BASE)

    def test_mutable_publication_and_discardable_bytes_explicitly_not_verified(self):
        _, plan, memory, reader = setup()
        memory.data[0x4000:0x6000] = bytes([0xab])*0x2000
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        self.assertTrue(any(e.name == '.p99prep' and 'mutable' in e.reason for e in plan.exclusions))
        self.assertTrue(any(e.name == '.reloc' and 'discardable' in e.reason for e in plan.exclusions))

    def test_raw_padding_is_not_virtual_data(self):
        def change(f):
            f.pack(0x188+8, '<I', 0x800)
            f.write(0x1f00, b'PADDINGXX')
        _, plan, memory, reader = setup(change=change)
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        self.assertTrue(any(e.rva == 0x1800 and e.reason == 'raw alignment padding' for e in plan.exclusions))

    def test_virtual_zerofill_is_compared(self):
        _, plan, memory, reader = setup(change=lambda f: f.pack(0x188+16, '<I', 0x800))
        self.assertTrue(mapped.compare(plan, reader)['comparison_passed'])
        memory.data[0x1800] = 1
        with self.assertRaisesRegex(ValueError, 'mapped mismatch'):
            mapped.compare(plan, reader)

    def test_wrong_plan_reader_binding(self):
        _, plan, _, reader = setup()
        with self.assertRaisesRegex(ValueError, 'reader/plan'):
            mapped.compare(dataclasses.replace(plan, base=BASE+65536), reader)

    def test_identity_before_and_after(self):
        for when in ('before', 'after'):
            with self.subTest(when=when):
                _, plan, memory, reader = setup()
                if when == 'before':
                    memory.identity_value = Identity(28, IDENTITY.creation)
                else:
                    memory.after_read = lambda m: setattr(m, 'identity_value', Identity(27, 999))
                with self.assertRaisesRegex(ValueError, 'process identity'):
                    mapped.compare(plan, reader)
                if when == 'before':
                    self.assertEqual(memory.reads, 0)

    def test_metadata_before_read_rejects_modifiers_and_cow(self):
        for field, value in [('Protect', 4), ('Protect', 8), ('Protect', 0x102), ('Protect', 0x20),
                             ('Protect', 0x202), ('Type', 0x20000), ('State', 0x2000),
                             ('AllocationBase', BASE+65536), ('RegionSize', 0), ('BaseAddress', BASE+1)]:
            with self.subTest(field=field, value=value):
                _, plan, memory, reader = setup()
                memory.query_override[field] = value
                with self.assertRaises(ValueError):
                    mapped.compare(plan, reader)
                self.assertEqual(memory.reads, 0)

    def test_observable_metadata_change_after_read(self):
        for field, value in [('Protect', 4), ('AllocationBase', BASE+65536), ('AllocationProtect', 4),
                             ('RegionSize', 0x2000)]:
            with self.subTest(field=field):
                _, plan, memory, reader = setup()
                memory.after_read = lambda m: m.query_override.update({field: value})
                with self.assertRaises(ValueError):
                    mapped.compare(plan, reader)

    def test_short_read_and_read_failure(self):
        _, plan, memory, reader = setup()
        memory.short_read = True
        with self.assertRaisesRegex(ValueError, 'short'):
            mapped.compare(plan, reader)
        memory.short_read = False
        memory.read = lambda *_: (_ for _ in ()).throw(OSError('read failed'))
        with self.assertRaises(OSError):
            mapped.compare(plan, reader)

    def test_reader_range_and_protection_bounds(self):
        _, plan, memory, reader = setup()
        for address, size, protect in ((BASE-1, 1, 2), (BASE+plan.size, 1, 2),
                                      (BASE, 65537, 2), (BASE, 0, 2), (BASE, 1, 4)):
            with self.subTest(address=address, size=size), self.assertRaises(ValueError):
                reader.read(address, size, protect)
        self.assertEqual(memory.reads, 0)


if __name__ == '__main__':
    unittest.main()
