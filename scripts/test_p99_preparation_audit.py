"""Pure checks of full-preparation report admission, never mapped code."""
import ctypes
from contextlib import contextmanager
from types import SimpleNamespace
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
        self.assertEqual(ctypes.sizeof(audit.GuardMutation), 24)

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

    def test_illegal_table_counts_mutate_only_count_and_refuse_before_find(self):
        class Fixture:
            fields = {"table_count": (123, 4)}
            job = 456
            count = 175
            verified = 0

            @contextmanager
            def patch(self, address, payload):
                self.asserted = (address, len(payload))
                self.count = int.from_bytes(payload, "little")
                try:
                    yield
                finally:
                    self.count = 175

            def verify(self):
                if self.count != 175:
                    raise AssertionError("count not restored")
                self.verified += 1

        fixture = Fixture()
        seen = []

        def refuse(_image, _job, _iterations, _output, _size, stats):
            seen.append(fixture.count)
            stats._obj.failed_stage = 3
            return -5

        output = SimpleNamespace(address=789, size=1008,
                                 read=lambda: bytes(1008), verify=lambda: None)
        bridge = SimpleNamespace(P99PrepareAudit=refuse)
        result = audit.check_invalid_table_counts(bridge, SimpleNamespace(address=1), fixture, output)
        self.assertEqual(seen, [257, 4096, 0xFFFFFFFF])
        self.assertEqual([item["value"] for item in result], seen)
        self.assertEqual(fixture.asserted, (123, 4))
        self.assertEqual(fixture.verified, 3)

        def forbidden_find(*args):
            args[-1]._obj.find_calls = 1
            return -5

        bridge.P99PrepareAudit = forbidden_find
        with self.assertRaisesRegex(ValueError, "native Find"):
            audit.check_invalid_table_counts(bridge, SimpleNamespace(address=1), fixture, output)
        self.assertEqual(fixture.count, 175)


if __name__ == "__main__":
    unittest.main()
