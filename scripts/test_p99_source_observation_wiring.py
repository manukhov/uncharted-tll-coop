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
        cls.cmake = (ROOT / "scanner/CMakeLists.txt").read_text(encoding="utf-8")

    def assert_in_order(self, text, *needles):
        cursor = 0
        for needle in needles:
            position = text.find(needle, cursor)
            self.assertGreaterEqual(position, cursor, f"missing/out-of-order: {needle}")
            cursor = position + len(needle)

    def test_both_pose_writers_remain_disabled(self):
        for name in ("kEnableP99GroupShadow", "kEnableLegacyNadineP99ChloeGripTransfer"):
            self.assertIn(f"constexpr bool {name} = false;", self.runtime)
        self.assertIn("static_assert(!kEnableP99GroupShadow,", self.runtime)
        self.assertIn("state_net_started && install_p99_group_package(true)", self.runtime)
        self.assertIn("constexpr bool kEnableP99SourceObservation = false;", self.runtime)

    def test_exam_is_explicit_runtime_only_opt_in(self):
        self.assertIn(
            '#if defined(TLL_P99_SOURCE_OBSERVATION_EXAM) && TLL_P99_SOURCE_OBSERVATION_EXAM\n'
            'constexpr bool kEnableP99SourceObservation = true;\n#else\n'
            'constexpr bool kEnableP99SourceObservation = false;\n#endif', self.runtime)
        self.assertRegex(self.cmake, r'option\(TLL_P99_SOURCE_OBSERVATION_EXAM "[^"\n]+" OFF\)')
        self.assertIn(
            'if(TLL_P99_SOURCE_OBSERVATION_EXAM)\n'
            '    target_compile_definitions(tll_coop_runtime PRIVATE TLL_P99_SOURCE_OBSERVATION_EXAM=1)\n'
            'endif()', self.cmake)
        # No global definition or propagation into the full-TU offline helper.
        self.assertEqual(self.cmake.count("TLL_P99_SOURCE_OBSERVATION_EXAM"), 3)

    def test_trace_preserves_normal_job_short_circuit_reads(self):
        normal = self.group.split("bool seh_p99_group_normal_job(", 1)[1].split(
            "struct P99PreparationObservation", 1)[0]
        self.assertIn("p99_preselect::Trace* trace = nullptr", normal)
        self.assert_in_order(normal,
            "pages.range(reinterpret_cast<std::uintptr_t>(job), 0x38)",
            "p99_group_load<std::uintptr_t>(job) == 0",
            "p99_group_load<std::uintptr_t>(job, 8)", "pages.range(fg, 0x2A8)",
            "p99_group_load<std::uintptr_t>(f, 0x18)", "pages.range(art, 0x48)",
            "if (!pages.range(raw, 0x40)) return false;",
            "const auto skeleton_id =", "if (skeleton_id != p99_order::kNadineSkeletonId) return false;",
            "const auto parameter_count =", "if (parameter_count != 6480) return false;",
            "output->fg = fg;", "trace->normal_passed = true;")
        self.assertEqual(normal.count("const auto skeleton_id ="), 1)
        self.assertEqual(normal.count("const auto parameter_count ="), 1)
        self.assertNotIn("boot_log_line", normal)

    def test_trace_preserves_selector_owner_order_and_optional_clock_calls(self):
        selector = self.runtime.split("bool seh_select_p99_chloe_transfer_epoch(", 1)[1].split(
            "bool seh_p99_chloe_joint_cache_plan_fits", 1)[0]
        self.assertIn("p99_preselect::Trace* trace = nullptr", selector)
        self.assert_in_order(selector,
            "p99_source_range(purpose, job, 0x10, phase)",
            "Stage::Retained", "p99_source_retained_player(purpose, phase)",
            "Stage::FgKind", "p99_source_range(purpose, fg,",
            "Stage::Owner", "seh_resolve_p99_source_owner_scope(player, &owner, purpose, &captured_owner, phase)",
            "!owner.valid || owner.player != player", "Stage::Target",
            "Stage::Registry", 'trace->detail = "runtime-ids"')
        self.assertNotIn("boot_log_line", selector)
        self.assertIn("if (trace) trace->enter(stage, p99_preselection_stamp(), detail);", self.runtime)

    def test_preselect_trace_keeps_reader_local_and_exports_no_borrowed_pointers(self):
        selected = self.observer.split("bool p99_source_observation_selected(", 1)[1].split(
            "__declspec(noinline) void p99_source_observation_candidate", 1)[0]
        self.assertIn("P99SourceReader pages(&P99_SOURCE_QUERY_FUNCTION, &p99_source_copy);", selected)
        self.assert_in_order(selected,
            "if (seh_p99_group_normal_job(job, &normal, pages, &trace))",
            "Stage::SelectGate", "if (seh_select_p99_chloe_transfer_epoch(",
            "P99SourcePurpose::kOwnedGroupValues, &pages, &trace)", "Stage::Identity",
            "selected.fg_anim_data", "selected.target_skeleton", "trace.queries = pages.query_count();",
            "trace.selected = admitted;", "return admitted;")
        for forbidden in ("boot_log_line", "stop_report(", "readable_bytes(",
                          "VirtualQuery(", "p99_group_invoke_native(", "static P99SourceReader"):
            self.assertNotIn(forbidden, selected)

    def test_trace_stays_inside_original_clock_envelope_without_budget_reset(self):
        candidate = self.observer.split("__declspec(noinline) void p99_source_observation_candidate", 1)[1].split(
            "void __fastcall hooked_p99_source_observation_async", 1)[0]
        self.assert_in_order(candidate,
            "const auto begin = p99_group_cost_start();", "p99_preselect::Trace trace{};",
            "trace.start(", "p99_source_observation_selected(job, trace)",
            "const auto probe_end = p99_group_cost_start();", "trace.finish(",
            "coop::p99_group_budget::Budget budget{g_p99_group_cost_limit};",
            "const bool clocks = trace.clock_valid && begin.valid && probe_end.valid",
            "!budget.charge(static_cast<std::uint64_t>(begin.value.QuadPart)",
            "finish_probe(", "prepare_p99_group_values(job, &owned, &observation)",
            "const auto end = p99_group_cost_start();", "finish_preparation(\n"
            "            static_cast<std::uint64_t>(begin.value.QuadPart)")
        self.assertEqual(candidate.count("Budget budget"), 1)
        self.assertNotIn("budget.spent =", candidate)

    def test_trace_stop_is_single_terminal_scalar_report(self):
        report = self.observer.split("void p99_source_observation_stop_report(", 1)[1].split(
            "bool p99_source_observation_selected(", 1)[0]
        self.assert_in_order(report,
            "if (reason == p99_observe::Reason::kNone && external_reason == nullptr) return;",
            "g_p99_source_observation_ready.store(false", "g_p99_source_observation_reported.exchange(true)",
            "std::snprintf(", "boot_log_line(line);")
        self.assertIn("trace=unavailable", report)
        for field in ("trace->selected", "trace->normal_passed", "trace->entered_mask", "trace->clock_valid",
                      "trace->queries", "trace->skeleton_id", "trace->parameter_count"):
            self.assertIn(field, report)
        for forbidden in ("P99SourceReader", "reinterpret_cast", "readable_bytes(", "VirtualQuery("):
            self.assertNotIn(forbidden, report)

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
