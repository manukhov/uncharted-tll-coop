"""Batch-audit candidate FgAnimData/FgInstance skinning-pointer field accesses.

Run only against the canonical ida/tll.i64 after stopping the MCP server.  The
report is intentionally written under reports/ (gitignored); this script is a
reproducible static-analysis helper, not a database mutator.
"""

from pathlib import Path

import ida_auto
import ida_pro
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


# Small instruction-level spine for the normal FgInstance publication path.
# The broad field scan above is useful for excluding competing pointers; these
# exact instructions make the surviving same-frame record/latch/descriptor
# chain reproducible without relying on a decompiler rendering.
EXACT_CHAIN_EAS = (
    (0x141930BC0, "frame record stores its frame number"),
    (0x141930E20, "producer reads the current render frame"),
    (0x141930E3A, "producer publishes record at instance[frame % 3]"),
    (0x14193186C, "consumer selects instance[requested frame % 3]"),
    (0x14193192D, "record current pointer -> jointData+0x18"),
    (0x14193194E, "record companion pointer -> jointData+0x28"),
    (0x141931964, "record CPU/state pointer -> jointData+0x90"),
    (0x141967ABA, "deformer builder publishes the requested frame first"),
    (0x141967BA3, "builder reloads that same requested frame"),
    (0x141967BC0, "builder selects instance[frame % 3] again"),
    (0x141967BC4, "builder verifies record frame identity"),
    (0x141967C38, "jointData+0x18 load for final current latch"),
    (0x141967C3C, "latch current pointer to jointData+0x00"),
    (0x141967C3F, "jointData+0x28 load for companion latch"),
    (0x141967C43, "latch companion pointer to jointData+0x08"),
    (0x141877B2F, "SubMeshSkinning encodes current/companion pointers"),
    (0x14174417A, "descriptor preserves current buffer address bits"),
    (0x141744183, "descriptor publishes encoded current buffer"),
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
    exact_rows = ["EXACT NORMAL SKINNING FRAME CHAIN"]
    for ea, meaning in EXACT_CHAIN_EAS:
        disasm = idc.generate_disasm_line(ea, 0) or "<no instruction>"
        exact_rows.append(f"  0x{ea:X}: {disasm}  ; {meaning}")

    report_path.write_text(
        "\n".join(exact_rows) + "\n\n" + "\n\n".join(rows) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {report_path} ({len(rows)} functions)")
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
