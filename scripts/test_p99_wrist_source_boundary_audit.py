import unittest
from unittest.mock import patch

import p99_wrist_source_boundary_audit as audit


class BoundaryInstructionTests(unittest.TestCase):
    def reader(self, updates=None):
        instructions = {**audit.INSTRUCTIONS, **(updates or {})}
        return lambda address, size: instructions.get(address, b'')[:size]

    def test_call_and_table_address_are_distinct(self):
        result = audit.verify_boundary_instructions(self.reader())
        self.assertEqual(result['post_cmd16_boundary'], '141ABA978')
        self.assertFalse(result['custom_table_address_is_cmd16_call'])

    def test_lea_cannot_be_admitted_as_a_call(self):
        with self.assertRaisesRegex(ValueError, 'CALL'):
            audit.direct_call_target(self.reader(), audit.TABLE_LEA)

    def test_wrong_cmd16_callee_rejected(self):
        code = bytearray(audit.INSTRUCTIONS[audit.CMD16_CALL])
        code[1] ^= 1
        with self.assertRaisesRegex(ValueError, 'target'):
            audit.verify_boundary_instructions(self.reader({audit.CMD16_CALL: code}))

    def test_truncated_call_rejected(self):
        with self.assertRaisesRegex(ValueError, 'complete direct CALL'):
            audit.verify_boundary_instructions(self.reader({audit.CMD16_CALL: b'\xe8'}))

    def test_noncall_instruction_rejected(self):
        with self.assertRaisesRegex(ValueError, 'CALL'):
            audit.verify_boundary_instructions(self.reader({audit.CMD16_CALL: b'\x90'*5}))

    def test_wrong_table_lea_rejected(self):
        code = bytearray(audit.INSTRUCTIONS[audit.TABLE_LEA])
        code[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'LEA target'):
            audit.verify_boundary_instructions(self.reader({audit.TABLE_LEA: code}))

    def test_changed_segment_argument_rejected(self):
        with self.assertRaisesRegex(ValueError, 'boundary bytes'):
            audit.verify_boundary_instructions(self.reader({0x141ABA96B: b'\x48\x8d\x55\x88'}))

    def test_changed_return_boundary_rejected(self):
        with self.assertRaisesRegex(ValueError, 'boundary bytes'):
            audit.verify_boundary_instructions(self.reader({audit.CMD16_RETURN: b'\x90'*5}))

    def test_wrong_async_caller_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Async Process call'):
            audit.verify_boundary_instructions(self.reader({0x141A92E78: b'\xe8\x00\x00\x00\x00'}))

    def test_native_two_argument_boundary_pinned(self):
        result = audit.verify_boundary_instructions(self.reader())
        self.assertEqual(result['native_input_arguments'], ['RCX=status', 'RDX=original SegmentContext'])
        self.assertTrue(result['incoming_R8_R9_are_not_extra_arguments'])
        for address in (0x14217B64F,0x14217B66D,0x14217B7A5):
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, 'boundary bytes'):
                audit.verify_boundary_instructions(self.reader({address: b'\x90'*7}))

    def test_process_native_frame_geometry_requires_exact_full_prologue(self):
        frame = audit.verify_boundary_instructions(self.reader())['process_entry_return_slot']
        self.assertEqual(frame['rbp_below_entry_return_slot'], 0x38 + 0x1570)
        self.assertEqual(frame['segment_below_entry_return_slot'], 0x15A8 + 0x80)
        self.assertEqual(frame['status_below_entry_return_slot'], 0x15A8 - 0x58)
        self.assertTrue(frame['slot_must_be_published_independently_before_native_call'])
        self.assertFalse(frame['Segment_plus_8_is_parent_pointer'])
        self.assertFalse(frame['invocation_identity_is_owner_or_lifetime_admission'])
        original = audit.INSTRUCTIONS[audit.PROCESS]
        self.assertEqual(len(original), 33)  # 32 would split the final SUB.
        for index in range(len(original)):
            changed = bytearray(original)
            changed[index] ^= 1
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, 'boundary bytes'):
                audit.verify_boundary_instructions(self.reader({audit.PROCESS: changed}))


class SourceOperandBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = audit.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        if not path.exists():
            raise unittest.SkipTest('pinned Nadine asset unavailable')
        cls.data = path.read_bytes()

    def test_same_execution_window_is_after_not_before_cmd16(self):
        result = audit.prove_post_cmd16_inputs(self.data)
        self.assertEqual(result['unchanged_local_JP_rows'], [24, 26, 49, 51])
        self.assertEqual(result['shoulder22_only_writer'], {'ordinal': 36, 'opcode': 40})
        self.assertTrue(result['five_inputs_coexist_after_same_g0_execution'])
        self.assertFalse(result['pre_cmd16_workspace22_admitted'])

    def test_minimum_ranges_do_not_contain_provenance(self):
        result = audit.prove_post_cmd16_inputs(self.data)
        self.assertEqual(result['raw_operand_bytes'], 256)
        self.assertEqual([(r['segment_pointer_offset'], r['buffer_offset'], r['size'])
                         for r in result['raw_operand_ranges']],
                        [(0x50,0x480,48),(0x50,0x4E0,48),(0x50,0x930,48),
                         (0x50,0x990,48),(0x58,0x5C0,64)])
        self.assertFalse(result['raw_bytes_include_owner_validity_or_generation'])
        self.assertFalse(result['raw_workspace_bytes_are_all_semantic_components'])

    def test_shoulder_uses_imported_prefix_not_expanded_tail(self):
        result = audit.prove_post_cmd16_inputs(self.data)
        self.assertEqual(result['shoulder22_source_local_rows'], [0,14,15,16,17,18,20,22])
        self.assertEqual(result['normal_cmd14_import_rows'], 66)
        self.assertEqual(result['normal_cmd14_import_bytes'], 3168)
        self.assertFalse(result['shoulder22_depends_on_expanded_tail_or_selected_four_JP'])

    def test_changed_shoulder_ancestry_rejected(self):
        original = audit.dependency.Graph.trace
        def altered(graph, outputs):
            leaves, opaque, ancestors = original(graph, outputs)
            leaves.add((0,48*67))
            return leaves, opaque, ancestors
        with patch.object(audit.dependency.Graph, 'trace', new=altered):
            with self.assertRaisesRegex(ValueError, 'shoulder source ancestry'):
                audit.prove_post_cmd16_inputs(self.data)

    def test_any_local_partial_or_vector_overlap_write_is_rejected(self):
        original = audit.dependency.program.writes
        for joint in audit.LOCAL_JOINTS:
            def extra(data, base, op):
                writes = original(data,base,op)
                # Starts 16 bytes before the row: the second half overlaps it.
                return writes+([(48*joint-16,32)] if op.ordinal == 0 else [])
            with self.subTest(joint=joint), patch.object(audit.dependency.program,'writes',side_effect=extra):
                with self.assertRaisesRegex(ValueError, 'source boundary writer'):
                    audit.prove_post_cmd16_inputs(self.data)

    def test_later_workspace_writer_rejected(self):
        original = audit.dependency.program.writes
        def extra(data, base, op):
            writes = original(data,base,op)
            return writes+([(0x5C1,16)] if op.ordinal == 50 else [])
        with patch.object(audit.dependency.program,'writes',side_effect=extra):
            with self.assertRaisesRegex(ValueError, 'source boundary writer'):
                audit.prove_post_cmd16_inputs(self.data)

    def test_missing_shoulder_producer_rejected(self):
        original = audit.dependency.program.writes
        def remove(data, base, op):
            writes = original(data,base,op)
            return [] if op.ordinal == 36 else writes
        with patch.object(audit.dependency.program,'writes',side_effect=remove):
            with self.assertRaisesRegex(ValueError, 'source boundary writer'):
                audit.prove_post_cmd16_inputs(self.data)


if __name__ == '__main__':
    unittest.main()
