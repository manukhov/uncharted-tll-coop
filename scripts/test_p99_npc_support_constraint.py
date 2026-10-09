import hashlib
import math
from pathlib import Path
import re
import struct
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import p99_grip_composition_audit as grip
import p99_npc_support_constraint as support


def axis_angle(axis, degrees):
    length = math.sqrt(sum(value * value for value in axis))
    half = math.radians(degrees) / 2.0
    sine = math.sin(half) / length
    return tuple(value * sine for value in axis) + (math.cos(half),)


class NpcSupportConstraintTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = support.audit()

    def test_disk_only_schema_and_no_runtime_admission(self):
        report = self.report
        self.assertEqual(report["schema"], "tll-p99-npc-support-constraint-v1")
        self.assertEqual(
            report["classification"],
            "STATIC_DISK_ONLY_NADINE_SUPPORT_CONSTRAINT_CANDIDATE",
        )
        self.assertEqual(
            report["candidate_fix_status"],
            "DOWN_ENDPOINT_RUNTIME_REJECTED_AS_VISIBLE_FIX")
        for key in (
                "retail_executable_mapped", "native_routine_called",
                "game_launched_or_attached", "runtime_writer_or_admission_proven",
                "live_clip_selection_proven", "visual_fix_proven"):
            self.assertFalse(report[key])

    def test_compiled_down_endpoint_matches_fresh_asset_constraint(self):
        header = support.ROOT / 'scanner/src/nadine_p99_support_constraint.h'
        text = header.read_text(encoding='utf-8')
        block = text.split('inline constexpr Locator kNativeDownSupportConstraint{', 1)[1].split('};', 1)[0]
        bits = [int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]{8})u', block)]
        self.assertEqual(len(bits), 7)  # P3 then Q4; native padding is separately zero.
        compiled = struct.pack('<7I', *bits)
        row = self.report['clips']['weapon_down']['constraint']['target_constraint']
        self.assertEqual(compiled, struct.pack('<7f', *row[8:11], *row[4:8]))
        self.assertEqual(hashlib.sha256(compiled).hexdigest(),
                         support.TARGET_CONSTRAINT_SHA256['weapon_down'])

    def test_compiled_roll_normal_matches_fresh_authored_arm(self):
        header = support.ROOT / 'scanner/src/nadine_p99_arm_roll.h'
        text = header.read_text(encoding='utf-8')
        block = text.split('inline constexpr Vector kAuthoredDownNormal{', 1)[1].split('};', 1)[0]
        bits = [int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]{8})u', block)]
        self.assertEqual(len(bits), 3)
        expected = self.report['clips']['weapon_down']['authored_proximal_geometry']['incoming_bend_normal']
        self.assertEqual(struct.pack('<3I', *bits), struct.pack('<3f', *expected))

    def test_mode2_absolute_uses_reference_scale_and_half_translation(self):
        a = SimpleNamespace(normalized=axis_angle((1, 2, 3), 28))
        e = SimpleNamespace(normalized=axis_angle((3, -1, 2), -37))
        source_row = grip.row((7, 8, 9), axis_angle((2, 3, -1), 51), (1, 2, 3))
        source_ref = grip.row(translation=(.10003, -.20007, .3333))
        reference = grip.row((2, 3, 4), axis_angle((1, 0, 0), 100), (4, 5, 6))
        result = support.retarget_mode2_absolute(source_row, reference, source_ref, a, e)
        self.assertEqual(result[:3], (2, 3, 4))
        self.assertEqual(result[4:8], grip.qmul(
            grip.qmul(grip.qconj(e.normalized), source_row[4:8]), a.normalized))
        for axis in range(3):
            half = struct.unpack('<e', struct.pack('<e', source_ref[8 + axis]))[0]
            delta = struct.unpack('<f', struct.pack('<f', source_row[8 + axis] - half))[0]
            expected = struct.unpack('<f', struct.pack('<f', delta + reference[8 + axis]))[0]
            self.assertEqual(result[8 + axis], expected)
        self.assertNotEqual(result[8:11], reference[8:11])

    def test_mode2_rounds_each_translation_operation(self):
        identity = SimpleNamespace(normalized=(0., 0., 0., 1.))
        result = support.retarget_mode2_absolute(
            grip.row(translation=(16777216., 0., 0.)),
            grip.row(translation=(-16777216., 0., 0.)),
            grip.row(translation=(-1., 0., 0.)), identity, identity)
        self.assertEqual(result[8], 0.)  # unrounded double algebra would yield 1
        with self.assertRaisesRegex(ValueError, 'invalid row'):
            support.retarget_mode2_absolute(
                grip.row(translation=(math.nan, 0., 0.)), grip.row(),
                grip.row(), identity, identity)

    def test_authored_proximal_plane_is_ancestor_invariant_not_live_admission(self):
        parents = {19: 18, 20: 18, 21: 19, 22: 20, 23: 21, 24: 22,
                   25: 23, 26: 24, 27: 25, 28: 26, 29: 27, 30: 28}
        ancestor = grip.row(rotation=axis_angle((2, -1, 4), 71), translation=(3, -5, 7))
        for label in ('weapon_up', 'weapon_down'):
            clip = self.report['clips'][label]
            rows = {row['target_global']: tuple(row['target_local_row'])
                    for row in clip['authored_rows_19_30']}
            self.assertEqual(set(rows), set(range(19, 31)))
            self.assertEqual(clip['retargeted_rows_19_30_f32_sha256'],
                             support.TARGET_ARM_ROWS_SHA256[label])
            geometry = clip['authored_proximal_geometry']
            transformed = support.authored_arm_geometry(rows, parents, ancestor)
            for axis in range(3):
                self.assertAlmostEqual(geometry['incoming_bend_normal'][axis],
                                       transformed['incoming_bend_normal'][axis], places=11)
            for name in ('shoulder', 'elbow', 'wrist'):
                self.assertLess(support.transform_error(
                    geometry['left_shoulder_elbow_wrist'][name],
                    transformed['left_shoulder_elbow_wrist'][name]), 1e-11)
            self.assertFalse(geometry['live_anchor_or_pole_proven'])
            self.assertFalse(geometry['post_solve_or_skin_clearance_proven'])
            self.assertGreater(geometry['upper_length_asset_units'], 0)
            self.assertGreater(geometry['lower_length_asset_units'], 0)
            bad_parents = dict(parents)
            bad_parents[25] = 21
            with self.assertRaisesRegex(ValueError, 'hierarchy changed'):
                support.authored_arm_geometry(rows, bad_parents)

    def test_collinear_authored_arm_does_not_invent_a_pole(self):
        parents = {19: 18, 20: 18, 21: 19, 22: 20, 23: 21, 24: 22,
                   25: 23, 26: 24, 27: 25, 28: 26, 29: 27, 30: 28}
        rows = {joint: grip.row(translation=(1, 0, 0)) for joint in parents}
        with self.assertRaisesRegex(ValueError, 'degenerate'):
            support.authored_arm_geometry(rows, parents)

    def test_both_raw_clips_descriptors_programs_and_row149_are_pinned(self):
        report = self.report
        self.assertEqual(report["inputs"]["animation_sha256"],
                         support.ANIMATION_SHA256)
        for label in ("weapon_up", "weapon_down"):
            clip = report["clips"][label]
            self.assertEqual(clip["raw_sha256"], support.RAW_SHA256[label])
            self.assertEqual(clip["group_count"], 7)
            self.assertEqual(
                clip["group_descriptors"],
                [[f"{value:08X}" for value in descriptor]
                 for descriptor in support.GROUP_DESCRIPTORS],
            )
            self.assertEqual(clip["decoded_group0_globals"], list(range(19, 31)))
            self.assertEqual(clip["decoded_group1_globals"], list(range(67, 105)))
            self.assertEqual(
                clip["source_rows_19_30_f32_sha256"],
                support.SOURCE_ROWS_SHA256[label],
            )
            self.assertEqual(len(clip["programs"]), 2)
            for group, program in enumerate(clip["programs"]):
                self.assertEqual(
                    program["program_sha256"],
                    support.PROGRAM_PINS[label][group][2],
                )
        retarget = report["retarget"]
        self.assertEqual(retarget["row_index"], 149)
        self.assertEqual(retarget["row_offset"], "1C40")
        self.assertEqual(retarget["map_offset"], "2AA0")
        self.assertEqual(retarget["target_profile"], "00000000")
        self.assertEqual(retarget["arm_records"]["source_globals"], list(range(19, 31)))
        self.assertEqual(retarget["arm_records"]["target_globals"], list(range(19, 31)))
        self.assertEqual(
            retarget["arm_records"]["modes"],
            [support.EXPECTED_MODES[joint] for joint in range(19, 31)],
        )

    def test_semantic_palm_prop_constants_and_exact_differences(self):
        report = self.report
        self.assertEqual(report["terminal_semantics"], {
            "left_wrist": {"global": 25, "string_id": "57F325C7", "name": "l_wrist"},
            "left_palm": {"global": 27, "string_id": "81DC7B7E", "name": "l_palm"},
            "left_prop": {
                "global": 29, "string_id": "A7479EC2",
                "name": "l_hand_prop_attachment",
            },
        })
        for label in ("weapon_up", "weapon_down"):
            clip = report["clips"][label]
            self.assertEqual(
                clip["retargeted_terminal_rows"],
                {key: list(row) for key, row in support.EXPECTED_TARGET_ROWS[label].items()},
            )
            self.assertEqual(
                clip["retargeted_terminal_pair_f32_sha256"],
                support.TARGET_PAIR_SHA256[label],
            )
            constraint = clip["constraint"]
            self.assertEqual(
                constraint["source_constraint_p_q_f32_sha256"],
                support.SOURCE_CONSTRAINT_SHA256[label],
            )
            self.assertEqual(
                constraint["target_constraint_p_q_f32_sha256"],
                support.TARGET_CONSTRAINT_SHA256[label],
            )
            self.assertAlmostEqual(
                constraint["source_to_target_rotation_delta_degrees"], 0.0,
                places=10,
            )
            self.assertAlmostEqual(
                constraint["source_to_target_origin_delta_asset_units"],
                0.0010358877480030032, places=12,
            )
        difference = report["up_down_difference"]
        self.assertEqual(difference["target_palm_transform_error"], 0.0)
        self.assertAlmostEqual(
            difference["target_constraint_rotation_delta_degrees"],
            34.17653337909534, places=10,
        )
        self.assertAlmostEqual(
            difference["target_constraint_origin_delta_asset_units"],
            0.049663136811397654, places=12,
        )

    def test_world_wrist_cancels_for_noncommuting_ancestors(self):
        raw_palm = support.EXPECTED_TARGET_ROWS["weapon_up"]["left_palm"]
        raw_prop = support.EXPECTED_TARGET_ROWS["weapon_up"]["left_prop"]
        # The identity is a rigid-transform theorem. Normalize the shipped f32
        # quaternions for this synthetic double-precision regression; the audit
        # itself separately pins the exact unmodified authored constants.
        palm = grip.row(
            rotation=grip.qnorm(raw_palm[4:8]), translation=raw_palm[8:11])
        prop = grip.row(
            rotation=grip.qnorm(raw_prop[4:8]), translation=raw_prop[8:11])
        qx = axis_angle((1.0, 0.0, 0.0), 37.0)
        qy = axis_angle((0.0, 1.0, 0.0), -53.0)
        self.assertGreater(grip.qerror(grip.qmul(qx, qy), grip.qmul(qy, qx)), 0.1)
        ancestor_a = grip.row(
            rotation=grip.qmul(qx, qy), translation=(4.0, -3.0, 2.0))
        ancestor_b = grip.row(
            rotation=grip.qmul(qy, qx), translation=(-7.0, 5.0, 11.0))
        reduced = support.support_constraint(palm, prop)
        world_a = support.world_support_constraint(ancestor_a, palm, prop)
        world_b = support.world_support_constraint(ancestor_b, palm, prop)
        self.assertLess(support.transform_error(reduced, world_a), 1e-12)
        self.assertLess(support.transform_error(reduced, world_b), 1e-12)
        self.assertLess(support.transform_error(world_a, world_b), 1e-12)
        self.assertGreater(
            support.transform_error(
                support.compose(ancestor_a, ancestor_b),
                support.compose(ancestor_b, ancestor_a),
            ),
            0.1,
        )

    def test_type5_primary_callback_does_not_prove_weight_one(self):
        callback = self.report["type5_weight_callback_14860"]
        self.assertEqual(callback["descriptor_offset"], "14860")
        self.assertEqual(callback["code_offset"], "14E50")
        self.assertEqual(callback["pool_offset"], "14E90")
        self.assertEqual(callback["descriptor_sha256"],
                         support.TYPE5_DESCRIPTOR_SHA256)
        self.assertEqual(callback["code_sha256"], support.TYPE5_CODE_SHA256)
        self.assertEqual(callback["bounded_pool_sha256"], support.TYPE5_POOL_SHA256)
        self.assertEqual(callback["bounded_pool_hex"], "f9a28d4d00000000")
        self.assertEqual(callback["unsupported_static_opcodes"], ["15", "1C"])
        self.assertEqual(callback["linear_decoder_result"], "r6")
        self.assertFalse(callback["pool_contains_f32_one_literal"])
        self.assertFalse(callback["endpoint_weight_one_proven"])


if __name__ == "__main__":
    unittest.main()
