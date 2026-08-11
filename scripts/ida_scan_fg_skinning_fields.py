"""Batch-audit candidate FgAnimData/FgInstance skinning-pointer field accesses.

Run only against the canonical ida/tll.i64 after stopping the MCP server.  The
report is intentionally written under reports/ (gitignored); this script is a
reproducible static-analysis helper, not a database mutator.
"""

from pathlib import Path

import ida_auto
import ida_kernwin
import idautils
import idc


TOKENS = (
    "+78h]",
    "+80h]",
    "+88h]",
    "+90h]",
    "+98h]",
    "+230h]",
    "+240h]",
    "+250h]",
    "+258h]",
    "+260h]",
    "+268h]",
)


def main() -> None:
    ida_auto.auto_wait()
    rows: list[str] = []
    for function_ea in idautils.Functions():
        function_rows: list[str] = []
        for ea in idautils.FuncItems(function_ea):
            disasm = idc.generate_disasm_line(ea, 0) or ""
            lowered = disasm.lower()
            if not any(token in lowered for token in TOKENS):
                continue
            if "[rsp+" in lowered or "[rbp+" in lowered:
                continue
            function_rows.append(f"  0x{ea:X}: {disasm}")
        if function_rows:
            rows.append(
                f"0x{function_ea:X} {idc.get_func_name(function_ea)}\n"
                + "\n".join(function_rows)
            )

    repo_root = Path(__file__).resolve().parents[1]
    report_path = repo_root / "reports" / "ida_fg_skinning_field_accesses.txt"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n\n".join(rows) + "\n", encoding="utf-8")
    print(f"wrote {report_path} ({len(rows)} functions)")
    ida_kernwin.qexit(0)


if __name__ == "__main__":
    main()
