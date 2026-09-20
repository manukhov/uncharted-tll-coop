"""Lexical guardrails for init-only appearance diagnostics; no game execution.

These source-shape checks are not a C++ parser or a proof of runtime semantics,
hook timing, or rendered identity. The separate C++ helper tests exercise the
real atomic budget, including concurrency; the user still verifies appearance.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)


def block_after(text: str, marker: str) -> str:
    """Select one balanced braced block, ignoring comments/string literals."""
    masked = TOKENS.sub(lambda match: " " * len(match.group()), text)
    start = masked.index("{", text.index(marker) + len(marker))
    depth = 0
    for index in range(start, len(masked)):
        if masked[index] == "{":
            depth += 1
        elif masked[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise ValueError(f"Unclosed block after {marker!r}")


def code_only(text: str) -> str:
    return TOKENS.sub(lambda match: "" if match.group().startswith(("//", "/*"))
                      else match.group(), text)


def compact(text: str) -> str:
    return re.sub(r"\s+", "", code_only(text))


class LocalAppearanceWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = (ROOT / "scanner/src/coop_runtime.cpp").read_text(encoding="utf-8")
        cls.hook = block_after(cls.runtime, "std::uint32_t* __fastcall hooked_appearance(")
        cls.logger = block_after(cls.runtime, "void report_local_appearance_init(")
        cls.install = block_after(cls.runtime, "DWORD WINAPI install_hook(")
        cls.role = block_after(cls.runtime, "bool start_state_net()")
        cls.with_output = block_after(cls.hook, "if (a2 != nullptr && from_lookbuilder)")
        cls.no_output = block_after(cls.hook, "else if (from_lookbuilder)")

    def test_reports_only_from_exact_init_branch(self):
        runtime = code_only(self.runtime)
        hook = code_only(self.hook)
        self.assertIn("constexpr std::uintptr_t kLookBuilderAppearCallRva = 0x1bb4ab9;", runtime)
        self.assertIn("boolfrom_lookbuilder=(rva==kLookBuilderAppearCallRva);", compact(hook))
        self.assertEqual(runtime.count("report_local_appearance_init("), 4)  # Definition + three sites.
        self.assertEqual(hook.count("report_local_appearance_init("), 3)
        outside = self.hook.replace(self.with_output, "").replace(self.no_output, "")
        self.assertNotIn("report_local_appearance_init(", code_only(outside))
        self.assertIn("InitOutcome::NoOutput", self.no_output)
        self.assertNotIn("retained_player_object(", self.no_output)

    def test_reuses_single_conditionally_evaluated_retained_lookup(self):
        hook = compact(self.hook)
        self.assertEqual(hook.count("retained_player_object(0)"), 1)
        self.assertIn("constboolrequested=g_local_make_nadine||g_force_local_nadine;", hook)
        self.assertIn("void*constlocal=requested?retained_player_object(0):nullptr;", hook)
        self.assertIn("if(requested&&a1==local)", compact(self.with_output))
        self.assertIn("requested?coop::appearance::InitOutcome::LocalMismatch:"
                      "coop::appearance::InitOutcome::RoleDisabled", compact(self.with_output))

    def test_hash_selection_is_unchanged_and_report_follows_write(self):
        selected = compact(block_after(self.with_output, "if (requested && a1 == local)"))
        self.assertEqual(selected.count("g_nadine_swap_hash"), 1)
        self.assertIn("conststd::uint32_tselected_hash=g_nadine_swap_hash;", selected)
        self.assertLess(selected.index("*a2=selected_hash;"), selected.index("report_local_appearance_init("))
        self.assertLess(selected.index("report_local_appearance_init("), selected.index("returna2;"))
        self.assertIn("InitOutcome::HashSelected,a1,local,requested,selected_hash)", selected)
        self.assertIn("volatile std::uint32_t g_nadine_swap_hash = 0xd27c074fu;", self.runtime)

    def test_buddy_legacy_and_original_fallback_order_is_preserved(self):
        hook = compact(self.hook)
        buddy = block_after(self.hook, "if (a2 != nullptr && g_avatar_show_chloe && g_net_role_b &&")
        self.assertIn("g_last_nadine_object!=nullptr&&a1==g_last_nadine_object)", hook)
        self.assertIn("*a2=0x3f334972u;returna2;", compact(buddy))
        legacy = block_after(self.with_output, "if (g_appearance_swap_active && g_nadine_appearance_hash != 0)")
        self.assertIn("*a2=0xd27c074fu;returna2;", compact(legacy))
        self.assertEqual(hook.count("g_original_appearance(a1,a2)"), 1)
        self.assertIn("if(g_original_appearance!=nullptr){returng_original_appearance(a1,a2);}", hook)
        self.assertLess(hook.index("g_avatar_show_chloe"), hook.index("constboolrequested="))
        self.assertLess(hook.index("constboolrequested="), hook.index("g_appearance_swap_active"))
        self.assertLess(hook.index("g_appearance_swap_active"), hook.index("g_original_appearance(a1,a2)"))

    def test_diagnostic_has_no_second_game_lookup_and_claims_before_logging(self):
        logger = code_only(self.logger)
        self.assertIn("if (!g_local_appearance_init_log_budget.claim(outcome)) return;", logger)
        self.assertLess(logger.index(".claim(outcome)"), logger.index("std::snprintf("))
        self.assertEqual(logger.count("boot_log_line(line);"), 1)
        self.assertIn("visual=pending", logger)
        for forbidden in ("retained_player_object", "g_original_appearance", "VirtualQuery",
                          "readable_bytes", "safe_get_look_id", "GetAsyncKeyState", "__try",
                          "reinterpret_cast", "*player", "*retained"):
            self.assertNotIn(forbidden, logger)
        self.assertIn("volatile bool g_log_appearance_callers = false;", self.runtime)

    def test_role_banner_describes_request_not_rendered_identity(self):
        role = code_only(self.role)
        self.assertIn("g_local_make_nadine = g_net_role_b;", role)
        self.assertIn("; requested-local-look=", role)
        self.assertIn("; actual appearance unverified", role)
        self.assertNotIn("B (player 2, local Nadine)", role)
        self.assertNotIn("A (player 1, local Chloe)", role)

    def test_installer_reports_independent_failure_and_final_readiness(self):
        install = code_only(self.install)
        self.assertIn("bool local_appearance_hook_ready = false;", install)
        for status in ("unexpected-target", "protect-failed", "readback-failed", "restore-failed", "installed"):
            self.assertIn(f'"{status}"', install)
        self.assertIn("if (before == g_module + 0x0A1DCC0)", install)
        self.assertIn("local_appearance_hook_ready = readback && appearance_protection_restored;", install)
        self.assertIn("LOCAL-LOOK INSTALL status=%s", install)
        self.assertIn("boot_log_line(appearance_line);", install)
        self.assertIn("LOCAL-LOOK READY hook=%u state-net=%u requested=%s hash=%08x", install)
        self.assertIn("local_appearance_hook_ready ? 1u : 0u, state_net_started ? 1u : 0u", install)
        self.assertIn("visual=pending; load-scene only after this marker; no live rebuild", install)
        self.assertLess(install.index("LOCAL-LOOK INSTALL"), install.index("start_state_net()"))
        self.assertLess(install.index("start_state_net()"), install.index("LOCAL-LOOK READY"))

    def test_p99_observer_and_both_pose_writers_remain_disabled(self):
        for name in ("kEnableP99SourceObservation", "kEnableP99GroupShadow",
                     "kEnableLegacyNadineP99ChloeGripTransfer"):
            self.assertIn(f"constexpr bool {name} = false;", self.runtime)
        self.assertIn("static_assert(!kEnableP99GroupShadow,", self.runtime)


if __name__ == "__main__":
    unittest.main()
