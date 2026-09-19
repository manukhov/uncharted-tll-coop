"""Pure checks of full-preparation report admission, never mapped code."""
import ctypes
import unittest

import p99_preparation_audit as audit


class PreparationReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = (audit.ROOT / "reports/p99-full-chain-agz-final.full-output.bin").read_bytes()[:1008]

    def valid_stats(self):
        return audit.Stats(size=136, version=1, completed_iterations=1,
            qpc_frequency=10_000_000, total_ticks=5000, virtual_queries=100,
            resolve_calls=12, find_calls=12, retarget_calls=42, blend_calls=21,
            virtual_query_ticks=4000)

    def test_exact_abi(self):
        self.assertEqual(ctypes.sizeof(audit.Stats), 136)
        self.assertEqual(audit.Stats.qpc_frequency.offset, 16)
        self.assertEqual(audit.Stats.virtual_queries.offset, 80)
        self.assertEqual(audit.Stats.virtual_query_ticks.offset, 128)

    def test_success_is_full_path(self):
        result = audit.validate_stats(self.valid_stats(), self.rows)
        self.assertEqual(result["total_ms"], .5)
        self.assertEqual(result["counters"]["retarget_calls"], 42)

    def test_partial_or_missing_phase_counts_fail(self):
        cases = {"size": 0, "version": 2, "completed_iterations": 0,
                 "failed_stage": 1, "qpc_frequency": 0, "total_ticks": 0,
                 "virtual_queries": 0, "resolve_calls": 11, "find_calls": 11,
                 "virtual_query_ticks": 0, "math_ticks": 5001,
                 "retarget_calls": 41, "blend_calls": 20, "rejected_calls": 1}
        for field, value in cases.items():
            with self.subTest(field=field), self.assertRaises(ValueError):
                stats = self.valid_stats()
                setattr(stats, field, value)
                audit.validate_stats(stats, self.rows)

    def test_extra_iterations_not_silently_averaged(self):
        stats = self.valid_stats()
        stats.completed_iterations = 2
        with self.assertRaises(ValueError):
            audit.validate_stats(stats, self.rows)

    def test_query_time_must_be_inside_total(self):
        stats = self.valid_stats()
        stats.virtual_query_ticks = 5001
        with self.assertRaises(ValueError):
            audit.validate_stats(stats, self.rows)

    def test_every_output_byte_is_pinned(self):
        for index in range(1008):
            changed = bytearray(self.rows)
            changed[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                audit.validate_stats(self.valid_stats(), changed)
        for changed in (b"", self.rows[:-1], self.rows + b"x"):
            with self.assertRaises(ValueError):
                audit.validate_stats(self.valid_stats(), changed)

    def test_distribution_keeps_outlier_and_budget_misses(self):
        result = audit.distribution([.25] * 19 + [4.0])
        self.assertEqual(result, {"count": 20, "min_ms": .25, "median_ms": .25,
                                 "p95_ms": .25, "max_ms": 4.0, "over_1ms": 1})

    def test_invalid_distribution(self):
        for values in ([], [-1], [float("nan")], [float("inf")]):
            with self.assertRaises(ValueError):
                audit.distribution(values)


if __name__ == "__main__":
    unittest.main()
