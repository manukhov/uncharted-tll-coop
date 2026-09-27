import math
import struct
import unittest
from unittest import mock

import p99_wrist_source_admission_audit as a


class FinalizeValidityTests(unittest.TestCase):
    def test_missing_row_uses_reference_and_valid_row_is_not_overwritten(self):
        ref = a.direct.grip.row()
        current = a.direct.grip.row(rotation=(.6, 0., 0., .8))
        defaults = {26: ref, 49: ref}
        result = a.finalize_missing_rows({26: current, 49: 'stale'}, defaults,
                                          valid_before_finalize={26})
        self.assertIs(result[26], current)
        self.assertIs(result[49], ref)

    def test_validity_is_required_not_inferred_from_readable_reference(self):
        defaults = {26: a.direct.grip.row()}
        with self.assertRaises(TypeError):
            a.finalize_missing_rows({}, defaults)
        with self.assertRaises(ValueError):
            a.finalize_missing_rows({}, defaults, valid_before_finalize={26})
        with self.assertRaises(ValueError):
            a.finalize_missing_rows({27: defaults[26]}, defaults, valid_before_finalize={27})

    def test_two_upstream_inputs_survive_identical_distal_local_grip(self):
        h = math.sqrt(.5)
        defaults = {26: a.direct.grip.row(), 28: a.direct.grip.row(translation=(0.,0.,-.05))}
        left = {26:a.direct.grip.row(), 28:defaults[28]}
        right = {26:a.direct.grip.row(rotation=(h,0.,0.,h)), 28:defaults[28]}
        x = a.finalize_missing_rows(left, defaults, valid_before_finalize={26,28})
        y = a.finalize_missing_rows(right, defaults, valid_before_finalize={26,28})
        self.assertEqual(x[28], y[28])
        self.assertNotEqual(x[26], y[26])
        # This is a counterexample to unique upstream ownership, not a chosen pose.

    def test_nonfinite_valid_row_refused_not_replaced_by_default(self):
        bad = a.direct.grip.row(translation=(float('nan'),0.,0.))
        with self.assertRaises(ValueError):
            a.finalize_missing_rows({26:bad}, {26:a.direct.grip.row()}, valid_before_finalize={26})


class PinnedSourceAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.nadine = a.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        archive = a.weapon.DEFAULT_ARCHIVE
        if not all(p.is_file() for p in (cls.nadine,archive,archive.with_name('dc1.psarc'))):
            raise unittest.SkipTest('pinned installed assets unavailable')
        parser = a.source.native.load_audit_module()
        actor = parser.Psarc(archive,None)
        dc = parser.Psarc(archive.with_name('dc1.psarc'),None)
        s = a.source
        cls.inputs = [actor.extract(s.ANIMATION_ENTRY_NAME),actor.extract(s.PLAYER_SKELETON.entry_name),
                      actor.extract(s.CHLOE_SKELETON.entry_name),cls.nadine.read_bytes(),
                      dc.extract(s.RETARGET_ENTRY_NAME)]
        with mock.patch('native_anim_pose_audit.MappedImage',side_effect=AssertionError('retail forbidden')):
            cls.result = a.inspect_assets(*cls.inputs)

    def test_all_declared_groups_including_empty_programs_checked(self):
        player,chloe = self.result['clips']
        self.assertEqual(player['active_groups'],[0,7])
        self.assertEqual(player['terminator_only_groups'],[1,2,3,4,5,6])
        self.assertEqual(chloe['active_groups'],[0,2])
        self.assertEqual(chloe['terminator_only_groups'],[1,3,4,5,6])
        self.assertEqual(player['source_globals'],[30,32,*range(640,677,2)])
        self.assertEqual(chloe['source_globals'],[39,41,*range(268,305,2)])

    def test_arm_retarget_records_exist_but_grip_valid_bits_do_not(self):
        rows = {r['joint']:r for r in self.result['upstream']}
        for joint,source,mode in ((22,24,2),(24,26,1),(26,28,1)):
            record = rows[joint]['retarget_maps'][0]
            self.assertTrue(record['record_present'])
            self.assertEqual((record['source_global'],record['mode']),(source,mode))
            self.assertFalse(record['source_authored_by_grip_clip'])
        self.assertEqual(self.result['target_grip_valid_globals'],[28,30,*range(105,142,2)])

    def test_offsets49_and51_have_no_map_or_procedural_parameter_writer(self):
        rows = {r['joint']:r for r in self.result['upstream']}
        for joint in (49,51):
            row = rows[joint]
            self.assertTrue(all(not m['record_present'] for m in row['retarget_maps']))
            self.assertEqual(row['group0_JP_writers'],[])
            self.assertEqual(row['reference_admission'],'ONLY_IF_FINAL_TARGET_VALID_BIT_CLEAR')
            self.assertFalse(row['actual_pose_value_recovered'])
        self.assertEqual(rows[49]['name'],'r_elbow_twist_b_offset')

    def test_reported_fallback_rows_are_exact_raw_defaults_not_profile_samples(self):
        data = self.inputs[3]
        for row in self.result['upstream']:
            offset = int(row['reference_file_offset'],16)
            raw = data[offset:offset+48]
            self.assertEqual(struct.pack('<12f',*row['raw_reference_row']),raw)
            self.assertEqual(a.hashlib.sha256(raw).hexdigest(),row['raw_reference_sha256'])

    def test_new_write_in_old_empty_clip_group_refused(self):
        s = a.source
        data = bytearray(self.inputs[0])
        animation = s.resolve_animation(s.PagedPak(bytes(data)),s.PLAYER_ANIMATION)
        table = animation.raw_offset+s.native.u32(data,animation.raw_offset+0x14)
        program = animation.raw_offset+s.native.u32(data,table+16)
        struct.pack_into('<H',data,program,0x316)
        skeleton = s.load_skeleton(self.inputs[1],s.PLAYER_SKELETON)
        with self.assertRaisesRegex(ValueError,'previously empty'):
            a.complete_sparse_sources(bytes(data),animation,skeleton)

    def test_changed_source_asset_refused(self):
        inputs = list(self.inputs)
        inputs[0] = inputs[0][:-1]+bytes([inputs[0][-1]^1])
        with self.assertRaises(ValueError):
            a.inspect_assets(*inputs)

    def test_no_helper_or_runtime_admission(self):
        self.assertFalse(self.result['full_helper_input_admitted'])
        self.assertFalse(self.result['retail_or_game_execution'])
        self.assertFalse(self.result['runtime_writes'])
        self.assertTrue(self.result['same_grip_does_not_determine_upstream_pose'])

    def test_same_grip_has_distinct_possible_upstream_helper_weights(self):
        control = self.result['upstream_nonuniqueness_control']
        self.assertFalse(control['grip_clip_source_bytes_changed'])
        self.assertFalse(control['real_pose_selected'])
        self.assertFalse(control['native_numeric_equivalence_proven'])
        self.assertEqual(control['cases'][0]['weights']['AUX+3EC'], 0.)
        self.assertAlmostEqual(control['AUX3EC_absolute_difference'],.5184412095075523,places=12)
        self.assertIn('NOT_LIVE',control['classification'])


if __name__ == '__main__':
    unittest.main()
