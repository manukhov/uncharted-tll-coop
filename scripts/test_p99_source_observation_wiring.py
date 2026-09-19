"""Structural guardrails for the source-only runtime exam; no game or native calls."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ObservationWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = (ROOT / "scanner/src/coop_runtime.cpp").read_text(encoding="utf-8")
        cls.group = (ROOT / "scanner/src/nadine_p99_group_runtime.inl").read_text(encoding="utf-8")
        cls.observer = (ROOT / "scanner/src/nadine_p99_source_observation_runtime.inl").read_text(encoding="utf-8")

    def test_both_pose_writers_remain_disabled(self):
        for name in ("kEnableP99GroupShadow", "kEnableLegacyNadineP99ChloeGripTransfer"):
            self.assertIn(f"constexpr bool {name} = false;", self.runtime)
        self.assertIn("static_assert(!kEnableP99GroupShadow,", self.runtime)
        self.assertIn("state_net_started && install_p99_group_package(true)", self.runtime)

    def test_installs_only_two_producers_and_verifies_pose_sites(self):
        self.assertIn("sites(storage.data(), observe_only ? 2 : 5)", self.group)
        self.assertIn("!observe_only && i<3", self.group)
        self.assertIn("if (observe_only) {\n            constexpr std::array<std::uintptr_t,3> untouched", self.group)
        self.assertIn("hooked_p99_source_observation_async", self.group)

    def test_no_pose_pointer_publication_or_stock_retry(self):
        # Ignore comments: the observer must contain no live pose handoff.
        code = "\n".join(line.split("//")[0] for line in self.observer.splitlines())
        for forbidden in ("frame.job =", "frame.process =", "copied.data()",
                          "hooked_p99_group_cmd16", "p99_group_candidate_async", "__except"):
            self.assertNotIn(forbidden, code)
        self.assertEqual(code.count("prepare_p99_group_values(job, &owned, &observation)"), 1)
        wrapper = code.split("void __fastcall hooked_p99_source_observation_async", 1)[1]
        self.assertEqual(wrapper.count("p99_group_invoke_native("), 1)
        self.assertEqual(wrapper.count("g_p99_group_async(job);\n        return;"), 2)
        self.assertIn("__finally", wrapper)
        self.assertIn("frame.native_return_slot = 0;", wrapper)
        self.assertIn("g_p99_group_fiber.leave(frame, previous)", wrapper)

    def test_source_lease_ends_before_stock(self):
        candidate = self.observer.split("__declspec(noinline) void p99_source_observation_candidate", 1)[1].split(
            "void __fastcall hooked_p99_source_observation_async", 1)[0]
        self.assertNotIn("p99_group_invoke_native", candidate)
        self.assertIn("if (leased)", candidate)
        self.assertIn("g_p99_source_observation.abandon();", candidate)
        self.assertLess(candidate.index("finish_probe("), candidate.index("prepare_p99_group_values("))
        self.assertLess(candidate.index("prepare_p99_group_values("), candidate.index("finish_preparation("))

    def test_expiry_precedes_foreign_job_gate_and_fls_errors_report(self):
        wrapper = self.observer.split("void __fastcall hooked_p99_source_observation_async", 1)[1]
        self.assertLess(wrapper.index("p99_observe::kLifetimeMs"), wrapper.index("g_p99_group_fiber.enter"))
        self.assertIn('p99_source_observation_stop_report("fls-enter")', wrapper)
        self.assertIn('p99_source_observation_stop_report("fls-leave")', wrapper)


if __name__ == "__main__":
    unittest.main()
