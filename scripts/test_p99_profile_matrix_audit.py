"""Pure profile matrix slice tests; no mapped image or native code."""
import math
from pathlib import Path
import struct
import unittest

import p99_profile_matrix_audit as a


def row(scale=(1., 1., 1.), q=(0., 0., 0., 1.), t=(0., 0., 0.)):
    return (*scale, 0., *q, *t, 0.)


class MatrixMathTests(unittest.TestCase):
    def test_identity_and_output_layout(self):
        work = a.propagate(a.IDENTITY, row((2, 3, 4), t=(5, 6, 7)), True)
        self.assertEqual(a.export_affine(work), (2, 0, 0, 5, 0, 3, 0, 6, 0, 0, 4, 7))
        self.assertEqual(a.export_transform(work),
                         (2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1))

    def test_flag8_suppresses_basis_scale_not_translation(self):
        parent = a.propagate(a.IDENTITY, row((2, 3, 4), t=(10, 20, 30)), True)
        compensated = a.propagate(parent, row(t=(1, 2, 3)), True)
        inherited = a.propagate(parent, row(t=(1, 2, 3)), False)
        self.assertEqual(compensated[1], (12, 26, 42))
        self.assertEqual(inherited[1], compensated[1])
        self.assertEqual(compensated[0], a.IDENTITY[0])
        self.assertEqual(a.export_affine(inherited), (2, 0, 0, 12, 0, 3, 0, 26, 0, 0, 4, 42))

    def test_quaternion_direction_and_parent_rotation(self):
        q = (0., 0., math.sqrt(.5), math.sqrt(.5))
        parent = a.propagate(a.IDENTITY, row(q=q), True)
        child = a.propagate(parent, row(t=(1, 0, 0)), True)
        for got, expected in zip(child[1], (0, 1, 0)):
            self.assertAlmostEqual(got, expected)

    def test_local_scale_is_applied_only_by_export(self):
        work = a.propagate(a.IDENTITY, row((2, 3, 4)), True)
        self.assertEqual(work[0], a.IDENTITY[0])
        self.assertEqual(work[2], (2, 3, 4))
        self.assertEqual(a.export_affine(work)[0], 2)

    def test_no_implicit_quaternion_normalization(self):
        self.assertNotEqual(a.rotation((0, 0, 1, 1)), a.rotation((0, 0, 2, 2)))

    def test_invalid_meaningful_lanes(self):
        values = list(row())
        for index in (*range(3), *range(4, 11)):
            bad = values.copy(); bad[index] = math.nan
            with self.assertRaises(ValueError):
                a.propagate(a.IDENTITY, bad, True)
        values[3] = values[11] = math.nan
        self.assertEqual(a.propagate(a.IDENTITY, values, True), a.IDENTITY)

    def test_parent_encodings(self):
        self.assertEqual(a.parent_encoding(0, 0, -1), 9)
        self.assertEqual(a.parent_encoding(0, 2, 0), 0x41)
        self.assertEqual(a.parent_encoding(0, 28, 26), 0x6C9)
        self.assertEqual(a.parent_encoding(1, 105, 28), 0xFDB)
        self.assertEqual(a.parent_encoding(1, 104, 27), 0xF9B)
        with self.assertRaises(ValueError):
            a.parent_encoding(1, 104, 26)


ASSET = Path(__file__).resolve().parents[1] / "reports/actor_packages/nadine-dlc-base.pak"


@unittest.skipUnless(ASSET.exists(), "pinned local artifact unavailable")
class PinnedSliceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = ASSET.read_bytes()
        cls.skeleton = a.source.load_skeleton(cls.data, a.source.NADINE_SKELETON)

    def test_complete_required_slice(self):
        result = a.audit(self.data)
        self.assertEqual(result["proof"]["operator_count"], 53)
        self.assertEqual(result["proof"]["required_matrix_count"], 71)
        self.assertEqual(set(result["forward_affine_rows"]), set(a.REQUIRED_GLOBALS))
        self.assertFalse(result["game_or_native_execution"])
        self.assertFalse(result["runtime_builder_operands_ready"])

    def test_authored_bind_inverse_is_independent_geometry_oracle(self):
        inputs = a.authored.skeleton_inputs(self.data, self.skeleton)
        proof = a.prove_slice(self.data, self.skeleton)
        workspace = {-1: a.IDENTITY}
        maximum = 0.
        for event in proof["events"]:
            joint = event["global"]
            group, local = self.skeleton.location(joint)
            processing = self.skeleton.processing[self.skeleton.groups[group].processing_index]
            values = struct.unpack_from("<12f", inputs["references"],
                                        48*(processing.first_parameter + local))
            workspace[joint] = a.propagate(workspace[event["parent"]], values,
                                           event["disable_inherit"])
            forward = a.export_affine(workspace[joint])
            inverse = struct.unpack_from("<12f", inputs["matrices"], 48*joint)
            for i in range(3):
                for column in range(4):
                    value = sum(inverse[4*i+k]*forward[4*k+column] for k in range(3))
                    value += inverse[4*i+3] if column == 3 else 0
                    maximum = max(maximum, abs(value - float(i == column)))
        self.assertLess(maximum, 1e-6)

    def test_analytic_half_subnormal_zeroing_changes_only_translation(self):
        # Analytic input override, not an instruction-exact native/DAZ model.
        # This asserts only the required matrix slice, not mode2 behavior.
        profile = a.profile_decoder.audit(self.data)
        original = a.audit(self.data, profile)
        packed = bytearray.fromhex(profile["scalar_model_rows_f32_hex"])
        changed = []
        for parameter in range(426):
            for lane in (0, 1, 2, 8, 9, 10):
                offset = 48*parameter + 4*lane
                value = struct.unpack_from("<f", packed, offset)[0]
                if 0 < abs(value) < 2**-14:
                    changed.append((parameter, lane))
                    struct.pack_into("<f", packed, offset, math.copysign(0., value))
        self.assertEqual(len(changed), 26)
        self.assertEqual({lane for _, lane in changed}, {8, 9, 10})
        profile["scalar_model_rows_f32_hex"] = packed.hex()
        profile["scalar_model_rows_sha256"] = a.source.sha256(packed)
        zeroed = a.audit(self.data, profile)
        maximum_translation_delta = 0.
        for joint in a.REQUIRED_GLOBALS:
            before = original["forward_affine_rows"][joint]
            after = zeroed["forward_affine_rows"][joint]
            self.assertEqual(tuple(before[i] for i in (0, 1, 2, 4, 5, 6, 8, 9, 10)),
                             tuple(after[i] for i in (0, 1, 2, 4, 5, 6, 8, 9, 10)))
            maximum_translation_delta = max(maximum_translation_delta,
                                            *(abs(before[i] - after[i]) for i in (3, 7, 11)))
        self.assertGreater(maximum_translation_delta, 0.)
        self.assertAlmostEqual(maximum_translation_delta, 7.560920467747367e-5, delta=1e-12)

    def test_parent_mutation_rejected(self):
        data = bytearray(self.data)
        op = a.program.operators(data, self.skeleton.spec.raw_offset, a.PROGRAMS[1])[0]
        at = self.skeleton.spec.raw_offset + a.PROGRAMS[1] + op.payload[2] + 2
        struct.pack_into("<H", data, at, 0xFDB)  # left root must use left palm
        with self.assertRaisesRegex(ValueError, "parent/scale flag"):
            a.prove_slice(data, self.skeleton)

    def test_export_mutation_rejected(self):
        data = bytearray(self.data)
        at = self.skeleton.spec.raw_offset + a.PROGRAMS[0] + 0x35A0 + 121*4 + 2
        struct.pack_into("<H", data, at, 0x741)
        with self.assertRaisesRegex(ValueError, "complete parent matrices"):
            a.prove_slice(data, self.skeleton)

    def test_prefix_write_mutation_rejected(self):
        data = bytearray(self.data)
        # Op52's first authored float4 destination originally auxiliary+0x70.
        at = self.skeleton.spec.raw_offset + a.PROGRAMS[0] + 432
        struct.pack_into("<H", data, at, 0)
        with self.assertRaisesRegex(ValueError, "parameter prefix"):
            a.prove_slice(data, self.skeleton)

    def test_non_propagation_required_matrix_write_rejected(self):
        data = bytearray(self.data)
        at = self.skeleton.spec.raw_offset + a.PROGRAMS[0] + 432
        struct.pack_into("<H", data, at, 0x41)
        with self.assertRaisesRegex(ValueError, "required matrix"):
            a.prove_slice(data, self.skeleton)

    def test_late_scratch_overwrite_rejected(self):
        data = bytearray(self.data)
        op = a.program.operators(data, self.skeleton.spec.raw_offset, a.PROGRAMS[0])[50]
        at = self.skeleton.spec.raw_offset + a.PROGRAMS[0] + op.payload[2]
        struct.pack_into("<H", data, at, 0xF93)
        with self.assertRaisesRegex(ValueError, "scratch overwritten"):
            a.prove_slice(data, self.skeleton)


if __name__ == "__main__":
    unittest.main()
