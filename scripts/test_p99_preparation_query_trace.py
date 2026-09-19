"""Pure bounded trace/report tests; no DLL, retail execution or memory probing."""
import ctypes
from types import SimpleNamespace
import unittest

import p99_preparation_audit as audit
import p99_preparation_query_trace as trace


class QueryTraceTests(unittest.TestCase):
    def fixture(self):
        result = trace.new_trace()
        result.count = 2
        result.entries[0] = trace.QuerySample(0x1100, 0x1000, 0x1000, 3, 1, 7)
        result.entries[1] = trace.QuerySample(0x2200, 0x2000, 0x1000, 7, 3, 7)
        stats = SimpleNamespace(virtual_queries=2, virtual_query_ticks=10, total_ticks=20)
        ranges = [(0x1100, 0x100, "fixed_table", "table"), (0x2200, 0x100, "borrowed_owner", "player")]
        return result, stats, ranges

    def test_abi(self):
        self.assertEqual(ctypes.sizeof(trace.QuerySample), 40)
        self.assertEqual(ctypes.sizeof(trace.QueryTrace), 5144)
        self.assertEqual(trace.QueryTrace.entries.offset, 24)
        self.assertEqual(trace.QuerySample.last_stage.offset, 32)

    def test_complete_trace_reconciles(self):
        values, stats, ranges = self.fixture()
        entries = trace.validate_trace(values, stats, ranges)
        self.assertEqual([item["storage"] for item in entries], ["table", "player"])
        self.assertEqual(sum(item["ticks"] for item in entries), stats.virtual_query_ticks)
        result = trace.summarize([entries, entries], 1000, audit.distribution)
        self.assertEqual(result["categories"]["fixed_table"]["warm_total"]["median_ms"], 3)
        self.assertEqual(result["per_query"][1]["warm_time"]["max_ms"], 7)
        self.assertEqual(result["image_query_share_percent"], {"min": 30, "median": 30, "max": 30})

    def test_bad_headers_and_overflow_refused(self):
        for key, value in (("size", 0), ("version", 2), ("capacity", 0), ("capacity", 129),
                ("count", 1), ("count", 129), ("overflow", 1), ("reserved", 1)):
            values, stats, ranges = self.fixture()
            setattr(values, key, value)
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                trace.validate_trace(values, stats, ranges)

    def test_bad_entries_refused(self):
        for key, value in (("flags", 15), ("flags", 0), ("last_stage", 0), ("last_stage", 9),
                ("region_bytes", 0), ("region_bytes", 0xFFFFFFFFFFFFFFFF),
                ("region_base", 0x1101), ("address", 0x3000), ("ticks", 21)):
            values, stats, ranges = self.fixture()
            setattr(values.entries[0], key, value)
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                trace.validate_trace(values, stats, ranges)

    def test_no_silent_timing_loss(self):
        values, stats, ranges = self.fixture()
        values.entries[1].ticks = 6
        with self.assertRaises(ValueError):
            trace.validate_trace(values, stats, ranges)

    def test_tail_write_is_not_hidden_by_count(self):
        values, stats, ranges = self.fixture()
        values.entries[2].ticks = 1
        with self.assertRaises(ValueError):
            trace.validate_trace(values, stats, ranges)

    def test_classification_rejects_unknown_or_ambiguous(self):
        for ranges in ([], [(0x1000, 0x1000, "a", "a"), (0x1100, 2, "b", "b")]):
            with self.assertRaises(ValueError):
                trace.classify(0x1100, ranges)

    def test_classification_extent_is_half_open(self):
        ranges = [(0x1000, 16, "a", "a")]
        self.assertEqual(trace.classify(0x100F, ranges)["offset"], 15)
        with self.assertRaises(ValueError):
            trace.classify(0x1010, ranges)

    def test_topology_change_not_averaged(self):
        values, stats, ranges = self.fixture()
        before = trace.validate_trace(values, stats, ranges)
        values.entries[1].region_bytes = 0x2000
        after = trace.validate_trace(values, stats, ranges)
        with self.assertRaises(ValueError):
            trace.summarize([before, after], 1000, audit.distribution)

    def test_missing_warm_or_clock_refused(self):
        values, stats, ranges = self.fixture()
        entries = trace.validate_trace(values, stats, ranges)
        for samples, frequency in (([], 1000), ([entries], 1000), ([entries, entries], 0)):
            with self.assertRaises(ValueError):
                trace.summarize(samples, frequency, audit.distribution)


if __name__ == "__main__":
    unittest.main()
