# Scripts

Use IDA MCP for normal read-only navigation: function lookup, callers/callees,
xrefs, disassembly, pseudocode, strings, globals, stack variables and structures.
The former one-shot `ida_dump_*`, name lookup and xref scripts were removed
because MCP provides the same result without reopening the 764 MB database.

The remaining scripts have one of four roles:

- `ida_mcp_server.ps1` / `ida_mcp_server.py` start the read-only server on the
  single `ida/tll.i64`. Stop it before any headless write.
- `ida_apply_labels.py` is the only normal IDB writer. `ida_audit_*`,
  `ida_find_*`, `ida_scan_*`, `ida_dump_data_words.py`,
  `ida_dump_instruction_range.py`, `ida_dump_vtable.py` and
  `ida_resolve_vtable_slot.py` are reproducible batch operations not exposed as
  one MCP call.
- `pe_*` tools read the retail executable directly. They are independent of IDA
  and are retained for raw PE, `.pdata`, RIP-reference and RTTI verification.
- `launch_coop.ps1` launches one or two game instances and injects the current
  DLL. `launch_coop.cmd` is its double-click/Command Prompt wrapper.

Build the scanner and DLLs with `scanner/build-release.cmd`.
