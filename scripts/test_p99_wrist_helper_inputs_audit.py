import struct
import unittest
from unittest.mock import patch

import generate_p99_chloe_source_rows as source
import p99_group_program_audit as program
import p99_wrist_helper_inputs_audit as audit


class SyntheticDependencyTests(unittest.TestCase):
    def test_scalar_vector_alignment(self):
        self.assertEqual(audit.cells(0x12DB,64)[0],(3,0x12D0))
        self.assertEqual(audit.cells(0x3E6,4),((2,0x3E4),))

    def test_scalar_coefficients_read_old_destination(self):
        data = bytearray(64)
        for lane in range(4):
            struct.pack_into("<HH",data,16+4*lane,0x103+4*lane,0x202+4*lane)
        op = program.Operator(3,0,47,(4,3,16,1))
        batches = audit.decode_batches(data,0,op)
        self.assertEqual(batches[0][0].inputs,((3,0x100),(2,0x200)))

    def test_simd_batch_reads_before_writes(self):
        graph = audit.Graph()
        a,b = (0,0),(0,4)
        graph.run([[audit.Edge(0,47,a,(b,)),audit.Edge(0,47,b,(a,))]])
        self.assertEqual(graph.trace((a,))[0],{b})
        self.assertEqual(graph.trace((b,))[0],{a})

    def test_sequential_overwrite_kills_old_dependency(self):
        graph = audit.Graph()
        target = (0,4)
        graph.run([[audit.Edge(0,47,target,((3,8),))],
                   [audit.Edge(1,52,target,())]])
        self.assertEqual(graph.trace((target,))[0],set())

    def test_opaque_operation_is_not_silently_readonly(self):
        graph = audit.Graph()
        graph.run([[audit.Edge(13,51,(0,16),(),True)]])
        self.assertEqual(graph.trace(((0,16),))[1],{(13,51,(0,16))})

    def test_op44_uses_own_lane_three_scalars(self):
        data = bytearray(96)
        for lane in range(4):
            struct.pack_into("<8H",data,16+16*lane,3,0,0,0,16*lane,
                             0x102+4*lane,0x202+4*lane,0x302+4*lane)
        op = program.Operator(0,0,44,(4,3,16))
        edges = audit.decode_batches(data,0,op)[0]
        self.assertEqual(edges[4].inputs,((2,0x104),(2,0x204),(2,0x304)))
        self.assertEqual(edges[4].destination,(0,16))


class PinnedHelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = audit.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        if not cls.path.exists():
            raise unittest.SkipTest('pinned Nadine skeleton asset not installed')
        cls.data = cls.path.read_bytes()
        cls.skeleton = source.load_skeleton(cls.data,source.NADINE_SKELETON)
        cls.report = audit.audit(cls.path)

    def test_complete_group6_input_sets(self):
        graph = audit.group6_graph(self.data,self.skeleton)
        for joint in audit.TARGETS:
            inputs,opaque,_ = graph.trace(audit.cells((joint-502)*64|1,64))
            self.assertEqual(inputs,audit.EXPECTED_INPUTS[joint])
            self.assertFalse(opaque)
            self.assertFalse(any(bank==0 or bank==2 for bank,_ in inputs))

    def test_no_group1_finger_export_dependency(self):
        self.assertTrue(all(not target['group1_shared_finger_export_reached']
                            for target in self.report['targets'].values()))
        self.assertEqual(self.report['intervening_group_write_counts'],
                         {'1':100,'2':500,'3':84,'4':248,'5':52})

    def test_names_and_auxiliary_origins_are_not_bind_parent_guesses(self):
        expected = {'535':{49,86},'536':{88},'537':{88},'554':{24,26}}
        for joint,wanted in expected.items():
            origins = self.report['targets'][joint]['g0_g1_copy_origins'].values()
            self.assertEqual({x['joint'] for x in origins if 'joint' in x},wanted)
        self.assertEqual(self.report['group0_auxiliary_scalar_producers']['AUX+400']['opcode'],129)
        self.assertEqual(self.report['group0_auxiliary_scalar_producers']['AUX+3E4']['optional_scalar'],'AUX+400')

    def test_auxiliary_source_corruption_rejected(self):
        data = bytearray(self.data)
        struct.pack_into('<I',data,0x5B0E0+36,0x4C0)
        with self.assertRaisesRegex(ValueError,'custom scalar operand'):
            audit.scalar_producers(data,self.skeleton)

    def test_intervening_group4_write_into_shared_rejected(self):
        data = bytearray(self.data)
        # Group4 op48 first destination changes from localJP to a required input.
        struct.pack_into('<H',data,0x60BC0+2,0x1927)
        with self.assertRaisesRegex(ValueError,'group4 overwrites'):
            audit.prove_shared_survival(data,self.skeleton,{(3,0x1924)})

    def test_group1_export_overlap_is_rejected(self):
        data = bytearray(self.data)
        struct.pack_into('<H',data,0x5CD70+0xC0,0x1927)
        with self.assertRaisesRegex(ValueError,'group1 overwrites'):
            audit.prove_shared_survival(data,self.skeleton,{(3,0x1924)})

    def test_group6_unreviewed_auxiliary_phase_rejected(self):
        data = bytearray(self.data)
        struct.pack_into('<I',data,0x60E50+0x640+48+8,4)
        with self.assertRaisesRegex(ValueError,'op49 auxiliary'):
            audit.group6_graph(data,self.skeleton)

    def test_no_native_execution(self):
        with patch('native_anim_pose_audit.MappedImage',side_effect=AssertionError('native execution')):
            report = audit.audit(self.path)
        self.assertFalse(report['numeric_helper_replay'])
        self.assertFalse(report['retail_or_game_execution'])


if __name__ == '__main__':
    unittest.main()
