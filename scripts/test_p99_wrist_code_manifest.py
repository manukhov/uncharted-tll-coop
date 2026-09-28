import hashlib
from pathlib import Path
import struct
from types import SimpleNamespace
import unittest

import p99_wrist_code_manifest as audit


class CodeNormalizationTests(unittest.TestCase):
    def region(self, rva=0x1A92A00):
        return next(r for r in audit.REGIONS if r.rva == rva)

    def fixture(self, region, installed=False):
        # Deliberately unrelated nonuniform owned bytes, not native instructions.
        raw = bytearray((i*37+11)&255 for i in range(region.size))
        plans = tuple(b'\xe8'+struct.pack('<i',0x112233+i*0x100)
                      for i in range(len(audit.CALLS)))
        for index,(rva,stock,_) in enumerate(audit.CALLS):
            if region.rva <= rva < region.rva+region.size:
                offset = rva-region.rva
                raw[offset:offset+5] = plans[index] if installed else stock
        return raw,plans

    def test_all_fifteen_wrappers_and_required_workers(self):
        audit.validate_layout()
        self.assertEqual(len(audit.REGIONS),63)
        self.assertEqual(sum(r.role == 'opcode' for r in audit.REGIONS),15)
        regions = {r.rva:r for r in audit.REGIONS}
        self.assertEqual(regions[0x2192FD0].size,0x314)
        self.assertEqual(regions[0x218E120].size,0x754)
        for rva in (0x2190910,0x2181430,0x217FE80,0x2182A20,0x2182E50):
            self.assertIn(rva,regions)
        self.assertNotIn(0x21932F0,regions)  # Authored g0 has no arity4.
        self.assertNotIn(0x218CAC0,regions)  # Nor the generic arity branch.

    def test_frame_envelopes_include_complete_prefix_and_leas(self):
        regions = {r.rva:r for r in audit.REGIONS}
        self.assertEqual(regions[0x1A92A00].size,0xC3D)
        self.assertEqual(regions[0x1AB97C0].size,0x1A4C)
        self.assertEqual(regions[0x217B630].size,0x197)
        for address,expected in audit.boundary.INSTRUCTIONS.items():
            rva = address-audit.IMAGE_BASE
            self.assertTrue(any(r.rva <= rva and rva+len(expected) <= r.rva+r.size
                                for r in audit.REGIONS))

    def test_stock_is_not_normalized_blindly(self):
        region = self.region()
        raw,_ = self.fixture(region)
        self.assertEqual(audit.canonicalize(region,raw),bytes(raw))
        raw[audit.CALLS[0][0]-region.rva+1] ^= 1
        with self.assertRaisesRegex(ValueError,'self-call bytes'):
            audit.canonicalize(region,raw)

    def test_three_own_installed_calls_restore_only_displacement_bytes(self):
        for rva in (0x1A92A00,0x1AB97C0):
            region = self.region(rva)
            stock,_ = self.fixture(region)
            installed,plans = self.fixture(region,True)
            before = bytes(installed)
            result = audit.canonicalize(region,installed,'installed',plans)
            self.assertEqual(result,bytes(stock))
            self.assertEqual(bytes(installed),before)  # Pure Python API owns a copy.
            permitted = {site-region.rva+i for site,_,_ in audit.CALLS
                         if region.rva <= site < region.rva+region.size for i in range(1,5)}
            self.assertTrue(all(a == b or i in permitted for i,(a,b) in enumerate(zip(result,before))))

    def test_changed_e8_opcode_rejected_even_with_installed_displacement(self):
        region = self.region()
        raw,plans = self.fixture(region,True)
        raw[audit.CALLS[0][0]-region.rva] = 0xE9
        with self.assertRaisesRegex(ValueError,'self-call bytes'):
            audit.canonicalize(region,raw,'installed',plans)

    def test_unplanned_third_party_call_not_masked(self):
        region = self.region()
        raw,plans = self.fixture(region,True)
        raw[audit.CALLS[1][0]-region.rva+3] ^= 1
        with self.assertRaisesRegex(ValueError,'self-call bytes'):
            audit.canonicalize(region,raw,'installed',plans)

    def test_no_partial_plan_or_unknown_stage(self):
        region = self.region()
        raw,plans = self.fixture(region,True)
        for bad in ((),plans[:2],plans+(plans[0],),tuple(b'\xe8' for _ in plans),
                    tuple(b'\xe9'+plan[1:] for plan in plans)):
            with self.subTest(bad=bad),self.assertRaises(ValueError):
                audit.canonicalize(region,raw,'installed',bad)
        with self.assertRaisesRegex(ValueError,'code stage'):
            audit.canonicalize(region,raw,'mixed',plans)
        with self.assertRaisesRegex(ValueError,'stock stage'):
            audit.canonicalize(region,raw,'stock',plans)

    def test_other_changed_code_survives_normalization_for_hash_rejection(self):
        region = self.region()
        raw,plans = self.fixture(region,True)
        baseline = audit.canonicalize(region,raw,'installed',plans)
        raw[0] ^= 1
        changed = audit.canonicalize(region,raw,'installed',plans)
        self.assertNotEqual(hashlib.sha256(baseline).digest(),hashlib.sha256(changed).digest())
        self.assertEqual(changed[0],raw[0])

    def test_nonpatch_region_keeps_every_byte(self):
        region = self.region(0x2192FD0)
        raw,plans = self.fixture(region)
        self.assertEqual(audit.canonicalize(region,raw),bytes(raw))
        self.assertEqual(audit.canonicalize(region,raw,'installed',plans),bytes(raw))

    def test_extent_and_identity_refuse(self):
        region = self.region()
        raw,_ = self.fixture(region)
        for bad in (raw[:-1],raw+b'\x00'):
            with self.assertRaisesRegex(ValueError,'region identity/extent'):
                audit.canonicalize(region,bad)
        unknown = audit.Region(region.rva,region.size,'unreviewed',region.role)
        with self.assertRaisesRegex(ValueError,'region identity/extent'):
            audit.canonicalize(unknown,raw)

    def test_relocation_overlap_is_fail_closed_including_partial_words(self):
        region = audit.Region(0x100,16,'owned','test')
        for rva in (0xF9,0xFF,0x100,0x108,0x10F):
            with self.subTest(rva=rva),self.assertRaisesRegex(ValueError,'relocation overlaps'):
                audit.reject_relocation_overlaps((region,),((rva,8),))
        audit.reject_relocation_overlaps((region,),((0xF8,8),(0x110,8)))
        with self.assertRaisesRegex(ValueError,'relocation extent'):
            audit.reject_relocation_overlaps((region,),((0x80,0),))

    def test_wrong_disk_binary_refuses_before_parsing_or_reading_code(self):
        with self.assertRaisesRegex(ValueError,'retail executable SHA-256'):
            audit.read_image_regions(SimpleNamespace(data=b'not the pinned executable'))


class PinnedCodeManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not Path(audit.boundary.DEFAULT_EXE).exists():
            raise unittest.SkipTest('pinned retail PE unavailable')
        if not (audit.ROOT/'reports/actor_packages/nadine-dlc-base.pak').exists():
            raise unittest.SkipTest('pinned authored asset unavailable')
        cls.report,cls.header = audit.audit()

    def test_generated_header_exact(self):
        self.assertEqual(audit.HEADER.read_text(encoding='utf-8'),self.header)

    def test_no_relocations_or_admission_are_silently_normalized(self):
        self.assertEqual(self.report['covered_base_relocations'],0)
        self.assertGreater(self.report['image_dir64_relocations_checked'],0)
        self.assertFalse(self.report['transitive_closure_complete'])
        self.assertFalse(self.report['source_or_runtime_admission'])
        self.assertIn('kTransitiveClosureComplete = false',self.header)
        self.assertIn('kSourceAdmission = false',self.header)

    def test_all_region_digests_have_exact_sizes(self):
        self.assertEqual(self.report['covered_bytes'],46690)
        self.assertTrue(all(len(bytes.fromhex(r['sha256'])) == 32 for r in self.report['regions']))
        rotation = next(r for r in self.report['regions'] if r['rva'] == 0x218F660)
        # Independent older rotation audit pinned the complete same native body.
        self.assertEqual(rotation['sha256'],'9c921e24738d1dab26b6665fa8acc74e00f88e1a6470941d226afd4173a85665')

    def test_authored_auxiliary_branches_are_not_narrow_wrist_subset(self):
        branches = self.report['branches']
        self.assertIn((12,3),branches['op47_ordinal_arity'])
        self.assertIn((38,0,16,8),branches['op49_ordinal_norm_offset_vectors'])
        self.assertEqual(branches['op50_ordinal_aux_count'],[(24,1),(29,1),(37,1)])
        self.assertEqual(branches['op51_ordinal_aux_count'],[(35,1),(45,1),(47,1)])

    def test_tailcalls_are_in_inventory_not_lost_by_get_callees(self):
        edges = self.report['control_edges']['direct_and_tail_edges']
        for target in (0x2189FB0,0x218BC40,0x218C530,0x2192FD0,0x2190910,0x2189370):
            self.assertTrue(any(e['target_rva']==target and e['mnemonic']=='jmp' and e['covered']
                                for e in edges),hex(target))
        for target in (0x218CAC0,0x21932F0):
            self.assertTrue(any(e['target_rva']==target and e['authored_branch_excluded'] for e in edges))

    def test_external_and_indirect_gaps_remain_visible(self):
        graph = self.report['control_edges']
        self.assertFalse(graph['linear_decode_is_reachability_proof'])
        self.assertTrue(any(not e['covered'] and not e['authored_branch_excluded']
                            for e in graph['direct_and_tail_edges']))
        for site in (0x217B742,0x1A92B4D,0x1A934D5,0x24ED25C):
            self.assertTrue(any(e['site_rva']==site for e in graph['indirect_edges']),hex(site))
        # Selected non-frame math/dispatch envelopes have no unknown immediate
        # transfer beyond the explicit arity exclusions and cookie-failure tail.
        for edge in graph['direct_and_tail_edges']:
            if edge['covered'] or edge['authored_branch_excluded']:
                continue
            owner = next(r for r in audit.REGIONS if r.rva <= edge['site_rva'] < r.rva+r.size)
            self.assertTrue(owner.role=='frame' or edge['site_rva']==0x24EAB4C,edge)


if __name__ == '__main__':
    unittest.main()
