"""Pinned asset/negative topology checks; no retail function or live process."""
import hashlib
import math
import mmap
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import p99_weapon_mesh_audit as a


class ReadOnlyArchiveTests(unittest.TestCase):
    def test_archive_mapping_is_read_only_and_closed_after_owned_extraction(self):
        mappings = []

        class FakePsarc:
            def __init__(self, path, oodle, *, data):
                mappings.append(data)
                self.data = data
                with self_outer.assertRaises(TypeError):
                    data[0] = 0

            def extract(self, name):
                self_outer.assertFalse(self.data.closed)
                return bytes(self.data[:]) + name.encode()

        self_outer = self
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.psarc"
            path.write_bytes(b"read-only-fixture")
            with mock.patch.object(a, "Psarc", FakePsarc):
                values = a.load_assets(path)
            self.assertEqual(path.read_bytes(), b"read-only-fixture")
        self.assertTrue(mappings[0].closed)
        self.assertEqual(values, (b"read-only-fixture" + a.P99_ENTRY.encode(),
                                  b"read-only-fixture" + a.PISTOL_ENTRY.encode()))


class PresentationEquationTests(unittest.TestCase):
    def setUp(self):
        h = math.sqrt(.5)
        self.attachment = a.pose.row(rotation=(0, 0, h, h), translation=(5, 7, 11))
        self.primary = a.pose.row(rotation=(h, 0, 0, h), translation=(2, 3, 4))
        self.root = a.pose.row(rotation=(0, h, 0, h), translation=(-13, 17, 19))
        self.cache = a.pose.row(rotation=(.5, -.5, .5, .5), translation=(101, 103, 107))

    def test_nonidentity_root_cancels_when_current_and_render_roots_agree(self):
        result = a.corrected_main_frame(self.attachment, self.primary, self.root,
                                       self.cache, self.cache, self.root)
        expected = a.pose.blend(self.attachment, self.primary)
        self.assertLess(a.pose.row_error(result["main_frame"], expected), 1e-12)
        # Known noncommuting Z90 * X90 and translated primary.
        for value, wanted in zip(result["main_frame"][4:11], (.5, .5, .5, .5, 2, 9, 15)):
            self.assertAlmostEqual(value, wanted)
        self.assertGreater(a.pose.row_error(result["world"], expected), 1.)
        self.assertFalse(result["live_frame_proven"])

    def test_identity_primary_anchors_main_to_attachment_not_inverse_root(self):
        result = a.corrected_main_frame(self.attachment, a.pose.row(), self.root,
                                       self.cache, self.cache, self.root)
        self.assertLess(a.pose.row_error(result["main_frame"], self.attachment), 1e-12)

    def test_stale_render_root_does_not_cancel(self):
        result = a.corrected_main_frame(self.attachment, a.pose.row(), self.root,
                                       self.cache, self.cache, a.pose.row())
        self.assertGreater(a.pose.row_error(result["main_frame"], self.attachment), 1.)

    def test_nonroot_selector_is_not_silently_treated_as_root(self):
        child = a.pose.blend(self.cache, a.pose.row(translation=(1, 0, 0)))
        result = a.corrected_main_frame(self.attachment, a.pose.row(), self.root,
                                       self.cache, child, self.root)
        self.assertGreater(a.pose.row_error(result["main_frame"], self.attachment), .5)

    def test_nonunit_scale_and_nonfinite_frames_refused(self):
        for bad in (a.pose.row(scale=(2, 1, 1)), a.pose.row(translation=(math.nan, 0, 0)),
                    a.pose.row(rotation=(0, 0, 0, 2))):
            with self.assertRaises(ValueError):
                a.corrected_main_frame(bad, a.pose.row(), self.root, self.cache, self.cache, self.root)

    def test_class_mapping_matches_both_native_branches(self):
        for raw, offline in ((-1, -1), (-5, -1), (0, 0), (1, 1), (16, 0), (17, 1), (22, 1), (23, 23)):
            self.assertEqual(a.campaign_weapon_class(raw), offline)
            self.assertEqual(a.campaign_weapon_class(raw, online=True), raw if raw >= 0 else -1)
        for bad in (True, 1., 1 << 32):
            with self.assertRaises(ValueError):
                a.campaign_weapon_class(bad)

    @unittest.skipUnless(a.DEFAULT_ARCHIVE.with_name("dc1.psarc").exists(), "local dc1 unavailable")
    def test_pinned_p99_class_excludes_long_gun_constructor(self):
        path = a.DEFAULT_ARCHIVE.with_name("dc1.psarc")
        with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapping:
            data = a.Psarc(path, None, data=mapping).extract("weapon-art.bin")
        result = a.audit_weapon_art(data)
        self.assertEqual([r["campaign_class"] for r in result["records"]], [0, 1])
        self.assertEqual([r["passes_long_gun_init_class_gate"] for r in result["records"]], [False, True])
        self.assertFalse(result["p99_long_gun_jiggle_constructed_by_normal_init"])
        with self.assertRaisesRegex(ValueError, "SHA256"):
            a.audit_weapon_art(data[:-1])


class PinnedP99MeshTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not a.DEFAULT_ARCHIVE.is_file():
            raise unittest.SkipTest("pinned actor77.psarc is not installed")
        cls.p99, cls.pistol = a.load_assets()
        cls.report = a.audit_assets(cls.p99, cls.pistol, include_geometry=True)

    def weapon(self, offset=None, payload=b""):
        raw = bytearray(self.p99)
        if offset is not None:
            raw[offset:offset+len(payload)] = payload
        return a.PagedPak(a.BytesSource(bytes(raw)))

    def skeleton(self, offset, payload):
        raw = bytearray(self.pistol)
        raw[offset:offset+len(payload)] = payload
        return a.PagedPak(a.BytesSource(bytes(raw)))

    def test_exact_pins_and_owned_geometry_dimensions(self):
        self.assertEqual(hashlib.sha256(self.p99).hexdigest(), a.P99_SHA256)
        self.assertEqual(hashlib.sha256(self.pistol).hexdigest(), a.PISTOL_SHA256)
        self.assertEqual(len(self.report["vertices_asset_units"]), 1567)
        self.assertEqual(len(self.report["triangles"]), 2290)
        indices = [i for triangle in self.report["triangles"] for i in triangle]
        self.assertEqual((len(indices), min(indices), max(indices)), (6870, 0, 1566))

    def test_static_frame_does_not_claim_contact_or_live_root(self):
        self.assertEqual(self.report["frame_model"], "conditional-p99-weapon-bind-root-v1")
        self.assertFalse(self.report["live_frame_proven"])
        self.assertFalse(self.report["additional_authored_mesh_transform_proven_absent"])
        self.assertFalse(self.report["main_mesh"]["skin_weight_semantics_validated"])
        self.assertIn("not an isolated contact ROI", self.report["geometry_scope"])
        self.assertIn("same-Nadine", self.report["right_prop_frame_condition"])

    def test_summary_omits_large_geometry_and_calls_no_mapped_retail_helper(self):
        with mock.patch("native_anim_pose_audit.MappedImage",
                        side_effect=AssertionError("retail mapping forbidden")):
            summary = a.audit_assets(self.p99, self.pistol)
        self.assertNotIn("vertices_asset_units", summary)
        self.assertNotIn("triangles", summary)

    def test_aabb_is_raw_half_vertex_extent_not_a_grip_contact_score(self):
        self.assertEqual(self.report["main_mesh"]["aabb_asset_units"], [
            [-0.01473236083984375, 0.016448974609375],
            [-0.0640869140625, 0.06048583984375],
            [-0.044891357421875, 0.1646728515625]])

    def test_three_distinct_lods_and_exact_material(self):
        lods = self.report["ownership"]["lods"]
        self.assertEqual([x["headers"] for x in lods], [
            ["AC518", "AC5C8", "AC678"], ["AC728", "AC7D8", "AC888"],
            ["AC938", "AC9E8", "ACA98"]])
        self.assertEqual(self.report["main_mesh"]["material_name"], a.GRIP_MATERIAL)
        self.assertEqual(self.report["main_mesh"]["raw_singleton_influence"], "003FFFFF")

    def test_unknown_asset_bytes_refused_before_parsing(self):
        damaged = bytearray(self.p99)
        damaged[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "P99 asset size/SHA256"):
            a.audit_assets(bytes(damaged), self.pistol)
        with self.assertRaisesRegex(ValueError, "pistol-base asset size/SHA256"):
            a.audit_assets(self.p99, self.pistol[:-1])

    # Private post-pin checks are exercised explicitly: production never skips
    # the complete SHA gates, but topology regressions must not pass merely
    # because every negative test stopped at the first digest comparison.
    def test_wrong_foreground_skeleton_refused(self):
        with self.assertRaisesRegex(ValueError, "foreground skeleton/LOD"):
            a._validate_ownership(self.weapon(0xB0D2C, struct.pack("<I", 0xC39CA709)))

    def test_wrong_model_id_refused(self):
        with self.assertRaisesRegex(ValueError, "model lookup"):
            a._validate_ownership(self.weapon(0xB0CD8, struct.pack("<I", 0x8075397D)))

    def test_valid_but_wrong_mesh_pointer_refused(self):
        # Redirect main to the actual slide descriptor, not an invalid pointer.
        pointer = self.p99[0xAF9E0:0xAF9E8]
        with self.assertRaisesRegex(ValueError, "submesh ownership"):
            a._validate_ownership(self.weapon(0xAF9F0, pointer))

    def test_valid_but_wrong_material_pointer_refused(self):
        pointer = self.p99[0xAC518+0x58:0xAC518+0x60]
        with self.assertRaisesRegex(ValueError, "grip material ownership"):
            a._read_main_mesh(self.weapon(0xAC5C8+0x58, pointer))

    def test_changed_grip_material_name_refused(self):
        with self.assertRaisesRegex(ValueError, "grip material ownership"):
            a._read_main_mesh(self.weapon(0x95C18, b"X"))

    def test_root_semantic_and_nonidentity_bind_refused(self):
        for offset, payload, message in (
                (0x1998, struct.pack("<I", 0x05FC3888), "root semantic"),
                (0x8A0, struct.pack("<f", 0.125), "root local is not identity"),
                (0x1330, struct.pack("<f", 2.0), "root inverse bind is not identity")):
            with self.subTest(offset=hex(offset)):
                with self.assertRaisesRegex(ValueError, message):
                    a._validate_skeleton(self.skeleton(offset, payload))

    def test_position_nonfinite_and_nonzero_w_refused(self):
        for offset, payload in ((0xE745C, struct.pack("<H", 0x7C00)),
                                (0xE745C+6, struct.pack("<e", 1.0))):
            with self.subTest(offset=hex(offset)):
                with self.assertRaises(ValueError):
                    a._read_main_mesh(self.weapon(offset, payload))

    def test_out_of_range_index_refused(self):
        with self.assertRaisesRegex(ValueError, "index outside vertex"):
            a._read_main_mesh(self.weapon(0x118C18, struct.pack("<H", 1567)))

    def test_non_singleton_and_unaligned_influence_refused(self):
        for offset, value in ((0x11F3A0, 2), (0x11F3A4, 1)):
            with self.subTest(offset=hex(offset)):
                with self.assertRaisesRegex(ValueError, "singleton influence layout"):
                    a._read_main_mesh(self.weapon(offset, struct.pack("<I", value)))

    def test_raw_influence_golden_is_not_reinterpreted_or_normalized(self):
        offset = struct.unpack_from("<I", self.p99, 0x11F3A4)[0]
        with self.assertRaisesRegex(ValueError, "raw singleton influence golden"):
            a._read_main_mesh(self.weapon(0x126118+offset, struct.pack("<I", 0x0000FFFF)))

    def test_influence_span_outside_page_refused(self):
        with self.assertRaises(ValueError):
            a._read_main_mesh(self.weapon(0x11F3A4, struct.pack("<I", 0xFFFFFFFC)))


if __name__ == "__main__":
    unittest.main()
