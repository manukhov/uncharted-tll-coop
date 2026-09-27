"""Packed skin regression tests; no native/game execution or installation writes."""
from pathlib import Path
import hashlib
import struct
import unittest

import pak_skin_influence_audit as skin
from nadine_other_weapon_clearance_audit import positive_joints


ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "reports/actor_packages/nadine-rural-body.pak"
P99 = ROOT / "reports/actor_packages/p99-silencer-dlc.pak"


class TinyPak:
    """Minimal owned arrays; no synthetic metadata scanner assumptions."""
    def __init__(self, words, *, count=None, offset=0, palette=580):
        self.data = bytearray(0x200)
        self.pages = [skin.Page(0, len(self.data))]
        self.pointers = {0x70: 0x80, 0x90: 0xA0, 0x98: 0xC0}
        struct.pack_into("<I", self.data, 0x68, palette)
        struct.pack_into("<I", self.data, 0x84, len(words))
        struct.pack_into("<II", self.data, 0xA0, len(words) if count is None else count, offset)
        struct.pack_into(f"<{len(words)}I", self.data, 0xC0, *words)

    def resolve_absolute(self, address):
        return self.pointers[address]

    def absolute_to_page(self, address):
        for index, page in enumerate(self.pages):
            if page.file_offset <= address < page.file_offset + page.size:
                return index, address - page.file_offset
        raise ValueError("outside every page")


MESH = skin.MeshHeader(0, 1, 3, 1, 1)


class PackedWeightTests(unittest.TestCase):
    def test_literal_asymmetric_and_full_weight_words(self):
        for word, joint, weight in ((0x0480E154, 18, 57684),
                                    (0x1A4D0000, 105, 0xD0000),
                                    (0x003FFFFF, 0, 4194303),
                                    (0xFFFFFFFF, 1023, 4194303),
                                    (0x6CA00000, 434, 2097152),
                                    (0x6CE00000, 435, 2097152)):
            with self.subTest(word=hex(word)):
                record = skin.decode_packed_influence(word)
                self.assertEqual((record.joint, record.weight_quantized), (joint, weight))
        self.assertEqual(skin.decode_packed_influence(0x003FFFFF).weight, 1.0)

    def test_upper_six_weight_bits_are_not_lost(self):
        for high_bits in range(1, 64):
            word = (105 << 22) | (high_bits << 16)
            self.assertEqual(word & 0xFFFF, 0)
            row = skin.decode_packed_influence(word)
            self.assertEqual(row.joint, 105)
            self.assertGreater(row.weight, 0)

    def test_shader_scale_bits_not_reciprocal_power_of_two(self):
        self.assertEqual(struct.unpack("<I", struct.pack("<f", skin.PACKED_WEIGHT_SCALE))[0], 0x34800002)
        half = skin.decode_packed_influence(0x00200000).weight
        self.assertEqual(half, 0.5000001192092896)
        self.assertGreater(half + half, 1.0)  # shader does NOT renormalize the sum
        self.assertEqual(skin.decode_packed_influence(0).weight, 0.0)

    def test_invalid_packed_words(self):
        for value in (-1, 1 << 32, 1.5):
            with self.subTest(value=value), self.assertRaises(ValueError):
                skin.decode_packed_influence(value)

    def test_record_references_preserved_zero_weight_not_positive(self):
        pak = TinyPak([1 << 22, (2 << 22) | 0x10000])
        uses = skin.parse_sparse_influences(pak, MESH, 580)
        self.assertEqual(set(uses), {1, 2})
        self.assertEqual(uses[1].records, 1)
        self.assertEqual(uses[1].total_weight, 0)
        self.assertEqual(uses[2].total_weight, 65536)
        self.assertEqual(list(positive_joints(pak, MESH)), [{2}])

    def test_root_only_validation_uses_all_22_bits(self):
        self.assertEqual(list(positive_joints(TinyPak([0x3FFFFF]), MESH)), [{0}])
        with self.assertRaisesRegex(ValueError, "not fully weighted"):
            list(positive_joints(TinyPak([0xFFFF]), MESH))

    def test_unpacked_descriptor_rejected(self):
        pak = TinyPak([0x3FFFFF])
        struct.pack_into("<I", pak.data, 0x88, 1)
        with self.assertRaisesRegex(ValueError, "unpacked"):
            list(skin.iter_vertex_influences(pak, MESH, 580))

    def test_invalid_vertex_ranges_and_palette(self):
        for count, offset in ((17, 0), (1, 2), (2, 0), (1, 4)):
            with self.subTest(count=count, offset=offset), self.assertRaises(ValueError):
                list(skin.iter_vertex_influences(TinyPak([0x3FFFFF], count=count, offset=offset), MESH, 580))
        with self.assertRaisesRegex(ValueError, "out-of-range"):
            list(skin.iter_vertex_influences(TinyPak([580 << 22]), MESH, 580))

    def test_cross_page_array_rejected_even_if_last_byte_valid(self):
        pak = TinyPak([0x3FFFFF])
        pak.pages = [skin.Page(0, 0xC2), skin.Page(0xC2, 0x13E)]
        with self.assertRaisesRegex(ValueError, "crosses"):
            list(skin.iter_vertex_influences(pak, MESH, 580))


@unittest.skipUnless(BODY.exists() and P99.exists(), "pinned local assets unavailable")
class PinnedAssetWeightsTests(unittest.TestCase):
    def test_body_positive_support_and_quantization_extent(self):
        pak = skin.PagedPak(BODY)
        self.assertEqual(hashlib.sha256(pak.data).hexdigest(),
                         "bc75642917f19fcc9f42b869131df1687bd1cc179635578352a83d87d611423f")
        meshes = skin.find_mesh_headers(pak, 580)
        self.assertEqual(len(meshes), 37)
        lost = 0
        sums = []
        for mesh in meshes:
            for rows in skin.iter_vertex_influences(pak, mesh, 580):
                lost += sum(row.weight_quantized > 0 and row.weight_quantized & 0xFFFF == 0 for row in rows)
                sums.append(sum(row.weight_quantized for row in rows))
        self.assertEqual(lost, 23)
        self.assertEqual((min(sums), max(sums)), (4194291, 4194313))
        mesh = next(mesh for mesh in meshes if mesh.absolute == 0x181AE8)
        rows = list(skin.iter_vertex_influences(pak, mesh, 580))
        selected = {28, 30, *range(105, 142, 2)}
        self.assertEqual(sum(bool(r) and {x.joint for x in r if x.weight_quantized} <= selected for r in rows), 3040)
        self.assertIn(skin.PackedInfluence(105, 0xD0000), rows[1941])

    def test_p99_all_partitions_have_exact_single_full_weights(self):
        pak = skin.PagedPak(P99)
        self.assertEqual(hashlib.sha256(pak.data).hexdigest(),
                         "af2982391ffa7212fdbae08da13a56084736b6c53f817d3c1a5c0ed698c65866")
        meshes = skin.find_mesh_headers(pak, 23)
        self.assertEqual(len(meshes), 9)
        self.assertEqual(sum(m.vertex_count for m in meshes), 10055)
        joints = set()
        for mesh in meshes:
            for rows in skin.iter_vertex_influences(pak, mesh, 23):
                self.assertEqual(len(rows), 1)
                self.assertEqual(rows[0].weight_quantized, 4194303)
                self.assertEqual(rows[0].weight, 1.0)
                joints.add(rows[0].joint)
        self.assertEqual(joints, {0, 1, 2, 8})


if __name__ == "__main__":
    unittest.main()
