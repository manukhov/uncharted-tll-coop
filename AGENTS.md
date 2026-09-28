# AGENTS.md — TLL Co-op Research

This workspace is for **reverse engineering** `tll.exe` (1.4.21058.0). The project
goal is described in `COOP_STATUS.md`, `ROADMAP.md`, and `TASK_POOL.md`. Confirmed
findings and rejected hypotheses are recorded in `COOP_STATUS.md`;
`FINDINGS_ARCHIVE.md` preserves the detailed historical registry through 2026-07-16,
while `reports/` contains only local temporary dumps and diagnostic artifacts.
Before proposing anything, read the relevant sections of these files — do not
reopen investigations that have already been recorded.

The only IDA database is `ida/tll.i64` (IDA Professional 9.3, headless
`idat.exe`). Analysis scripts live in `scripts/`; runtime tools live in `scanner/`.
At the end of every turn, leave the repository clean, committed, and labeled
(see “Handoff”).

---

## Primary directive: reverse engineer thoroughly first, then run

You may launch the game and inject on your own — no permission request is needed.
However, launching the game is **an exam that validates a hypothesis you have
already established on paper.** It is NOT an exploration method and NOT a way to
test a guess.

The workflow this file is intended to eliminate: pick the first plausible
function, call it a hypothesis, and immediately launch/inject “to see whether it
works.” That cycle spends an entire game launch on a guess and almost always
produces a false theory. **Do not do this.** Earn the right to launch through
static analysis; do not use a launch as a substitute for it.

For every task, assume by default that the answer can be found statically in
`tll.i64` and that you have not yet investigated deeply enough.

### Only the user controls game input

The agent may launch/restart game instances and inject once a runtime test has
been justified by static analysis or the user has explicitly requested a launch.
As soon as such a test is fully prepared and a fresh DLL has built successfully,
the agent must launch the required number of instances and inject immediately,
without asking for separate launch confirmation. While working on **appearance 1**,
only one `tll.exe` instance is allowed; do not launch a second instance.
After launch, the agent **must not control the game**: do not send keyboard,
mouse, RawInput/XInput, or virtual gamepad input; switch game windows to control
the character; operate menus; select a save/weapon; or perform the test scenario
on the user's behalf. The user performs all game actions. The agent only provides
a short, precise scenario beforehand, reads logs/process state, and analyzes the
visual result reported by the user.

---

## Hypothesis lifecycle (mandatory)

Every claim about how the game works must pass through these stages in order.

1. **State the hypothesis precisely** — function/address/offset, what it does,
   and the observable behavior it should produce. “Looks like a firing handler”
   is not a hypothesis. “`0x1409FBA60` writes a firing event into the per-slot
   input block at `+0xNNN` and is reached from the campaign aiming path” is one.

2. **List competing hypotheses.** There is almost always more than one candidate
   — write them down. A single candidate means you stopped searching too early.

3. **Collect static evidence until the quota below is met.** Use it to
   **eliminate alternatives**, not merely to support your favorite. Rejecting
   other candidates on the most specific available detail is much stronger than
   accumulating support for the preferred answer.

4. **Write a falsifiable prediction.** Before launch: “If this is correct, doing
   X at runtime will produce Y, and specifically will NOT produce Z (which the
   rejected candidate would produce).” A runtime test without a written
   prediction that distinguishes the candidates is pointless — do not launch it.

5. **Launch the required instances yourself**, but let the user perform the game
   scenario; compare the logs and the user's visual result with the prediction.

6. **Record the result** in the findings registry, regardless of the outcome.

---

## Evidence quota before any runtime test

A hypothesis is **not** ready for the game until it has at least **three
independent** supporting pieces of evidence — of different *kinds*, not three
pieces of the same dump. Acceptable kinds include:

- cross-references (callers/callees) confirming that the call graph converges;
- disassembly/pseudocode showing that the function reads/writes the claimed
  fields at the claimed offsets;
- vtable slot identity (RTTI name + slot index) for virtual calls;
- string/data xrefs tying the function to a known subsystem;
- structural consistency with a confirmed finding in `FINDINGS_ARCHIVE.md` or
  `COOP_STATUS.md`;
- a second independent path leading to the same conclusion.

Cannot reach three? **Say so and keep investigating statically.** Do not promote
a weak static argument into a game launch “to find out faster.” That is exactly
the anti-pattern we are eliminating.

---

## Reverse engineer deeply — expected depth

This is difficult, multi-step work; reaching a conclusion quickly is not the goal.
The only goal is to reach a conclusion that survives a runtime test.

- When distinguishing candidates, explicitly reason through the alternatives
  (use extended reasoning) before declaring a winner.
- Follow the call graph in **both** directions — callers and callees — before
  concluding what a function does. A function rarely makes sense in isolation.
- Resolve virtual calls to specific vtable slots and RTTI names; do not base a
  hypothesis on an unresolved indirect call.
- Trace data, not just code: where a structure field is written, where it is
  read, who owns it, and how long it lives.
- One confirmed function is worth ten “possibly” functions. Depth matters more
  than breadth.

---

## The IDA database is shared state — keep it current

`ida/tll.i64` is in .gitignore (too large) and is the **only IDB** in the workspace:
do not create persistent copies such as `tll_mcp.i64`, `tll_*_temp.i64`, or other
parallel databases. `tll.i64` is the source of truth for names, comments, and
structures. A database full of `sub_140XXXXXX` wastes time on the next turn.

- When a function, global, or structure field is **CONFIRMED** (or a strong
  `STATIC` case), **name it** and **apply the label to the IDB.** Use the established
  pipeline: add to / regenerate `scripts/ida_apply_labels.py`, run it headlessly
  via `idat.exe -A -S...`, and verify `save_database returned True` (keep the
  expected output locally in `.codex/` or `reports/`).
- Use consistent subsystem-prefixed names aligned with existing names (for
  example, `InputManager_NormalizeStick`, `InputManager_PollPads_SingleActivePad`).
- Before a headless write, stop MCP with
  `scripts/ida_mcp_server.ps1 -Action Stop`. After applying labels, check that the
  target resolves to its new name and its xrefs look correct, then restart MCP.
- Use IDA MCP to read functions, xrefs, pseudocode, strings, globals, and
  structures. A new headless script is justified only for a batch check that MCP
  does not support; do not add one-off dumpers for already supported queries.

---

## Git — commit your findings

Commit after each meaningful unit of work — a confirmed finding, a rejected
hypothesis, a working mod change, or a new analysis script. Do not end a turn with
uncommitted work.

- **Use imperative, specific messages stating the finding and addresses**, in
  the style of the existing log, for example:
  - `Locate enemy HP: HealthSystem at enemy+0x4a8, current HP int@+0x3c`
  - `Disprove Player+0x1A8 bone path: it's a 32-byte effect table, not transforms`
- Commit `scripts/`, code, and durable findings/status documentation. The
  `reports/` directory is local and entirely ignored: move information needed
  for continuation into `COOP_STATUS.md` or `TASK_POOL.md`. **Never** commit
  ignored artifacts (`ida/*.i64`, `scanner/build/`, `*.bin`, `*.dll`, `*.exe`,
  `*.log`).
- **Immediately run `git push` for the current branch after every successful
  commit.** Do not accumulate unpublished local commits or end the turn until
  the remote repository has confirmed the push.
- A failed runtime test also deserves a commit: record it as a REJECTED finding
  so it will not be investigated again.

---

## Findings registry

- `COOP_STATUS.md` is the durable registry of **confirmed** facts and rejected
  hypotheses, with addresses; `reports/` stores only local raw dumps.
- `FINDINGS_ARCHIVE.md` preserves the detailed registry through 2026-07-16. Do not
  add new results there: continue the chronology in `COOP_STATUS.md`.
- Label every finding: `CONFIRMED` (passed its runtime prediction), `STATIC`
  (strong static evidence, not yet tested at runtime), `HYPOTHESIS` (incomplete),
  or `REJECTED`.
- Record rejections with an address and reason. The project already does this
  well (for example, the rejected `Player+0x1A8` effect-table path) — maintain
  that standard.

---

## Context and tool hygiene

- Reports here are large (some exceed 300 KB). **Never `cat` a large report into
  context** — use `grep`, `sed -n 'A,Bp'`, or targeted reads. Loading a whole
  600 KB xref file impairs reasoning and encourages grabbing the first match
  and moving on.
- For a deep pass through many functions, send a **sub-agent** (Task tool) to
  return a concise summary with addresses instead of flooding the main thread.
- Use MCP for routine, targeted IDA queries. Reserve headless scripts for
  reproducible batch audits or writing labels to the only IDB.

---

## Handoff

Before stopping, leave the repository clean for the next agent:

1. **Findings registry updated** — new CONFIRMED/STATIC/REJECTED entries recorded.
2. **IDB labeled and saved** — names for every newly confirmed item applied to
   `ida/tll.i64`, with `save_database returned True`.
3. **Git clean** — all tracked work committed with descriptive messages, every
   commit successfully sent via `git push`; `git status` shows only ignored
   artifacts.
4. **State note** — if an investigation is in progress, record the current
   hypothesis, collected evidence, and next specific step in `COOP_STATUS.md` or
   `TASK_POOL.md` so the next turn can continue without guessing.

---

## Anti-patterns (do not do these)

- “This is probably it; let's launch and see.” → Meet the evidence quota and
  write the prediction first.
- Name a single candidate without listing what else it could be.
- Treat one supporting dump as confirmation.
- Read an entire large report into context instead of using grep.
- Confirm a function but leave it as `sub_XXXX` in the IDB.
- End a turn with uncommitted work or an unsaved database.
- Reinvestigate something already marked REJECTED.

---

## MCP “ida-tll”: fast access to IDA

The **`ida-tll`** MCP server runs IDA Pro through headless **idalib**; the only
database, `ida/tll.i64`, is already loaded into memory (faster than loading 764 MB
with an `idat.exe` script each time). Use MCP for targeted graph queries, not a
one-off headless `idat` run.

**Working (READ):** `get_function_by_address` / `get_function_by_name`,
`get_xrefs_to`, `get_xrefs_to_field`, `get_callers`, `get_callees`,
`disassemble_function` (argument: `start_address`), `list_functions`,
`list_strings` / `list_strings_filter`, `list_globals`, `get_entry_points`,
`decompile_function`, `get_stack_frame_variables`, and structures
(`get_defined_structures`, `analyze_struct_detailed`, `search_structures`, …).

**Decompilation:** `decompile_function` WORKS (wrapper fix: is_window_active
bypasses IDA's PyQt5 stub, which raised NotImplementedError in headless mode).

**⚠ CRITICAL — MCP opens the only `ida/tll.i64`:**

- MCP is for **READING** only; do not use MCP `rename_function` / `set_comment`.
- To apply labels, stop MCP, run `scripts/ida_apply_labels.py` headlessly on
  `ida/tll.i64`, verify `save_database returned True`, then restart MCP. Opening
  the database in the server and `idat.exe` simultaneously is forbidden.

**Server:** endpoint `http://172.21.144.1:8745/mcp` (streamable-http, WSL adapter
only). If it does not respond, use `scripts/ida_mcp_server.ps1` (idempotent startup;
database loading takes ~1–2 minutes). `-Action Stop` releases the IDB for writing,
`-Action Restart` restarts the server after labels have been saved, and
`-Action Status` reports its state. `scripts/ida_mcp_server.py` is the Windows
Python 3.12 + idalib wrapper.
