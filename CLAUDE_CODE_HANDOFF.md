# Claude Code Handoff

## User-facing workflow

- Write in Russian.
- Be concise.
- Report only new findings.
- Do not repeat already established conclusions unless a correction is needed.
- Do not reread historical sections unless necessary.
- As soon as the next step is clear, continue automatically instead of stopping for status updates.
- Stop only for:
  - a real blocker,
  - a risky choice,
  - required user input,
  - or a large finished result worth showing.

## Canon / source of truth

Primary working files:

- `reports/initial-findings.md`
- `ROADMAP.md`
- `TASK_POOL.md`
- `scripts/ida_apply_labels.py`

When you get a meaningful result, update those files immediately.

## IDA / local environment notes

- Current workspace root:
  - `/mnt/c/users/vladimir/desktop/TLLCoopResearch`
- Retail binary:
  - `/mnt/c/Program Files (x86)/Steam/steamapps/common/Uncharted Legacy of Thieves Collection/tll.exe`
- IDA:
  - `/mnt/c/Program Files/IDA Professional 9.3/idat.exe`

Current IDA state:

- working packed DB:
  - `ida/tll.i64`
- unpacked side-files also exist locally:
  - `ida/tll.id0`
  - `ida/tll.id1`
  - `ida/tll.id2`
  - `ida/tll.nam`
  - `ida/tll.til`

Important behavior:

- `tll.i64` is still corruption-prone.
- Headless label apply usually returns exit code `1`.
- Even with exit code `1`, the unpacked side-files often update and contain the latest label state.
- Treat `initial-findings.md` + `ida_apply_labels.py` as the real canon, not the packed DB timestamp.

Useful verification command:

```bash
python3 -m py_compile scripts/ida_apply_labels.py
```

Headless label apply:

```powershell
& "C:\Program Files\IDA Professional 9.3\idat.exe" -A "-SC:\users\vladimir\desktop\TLLCoopResearch\scripts\ida_apply_labels.py" "C:\users\vladimir\desktop\TLLCoopResearch\ida\tll.i64"; exit $LASTEXITCODE
```

## What was cleaned up

Removed:

- old probe DLL build wrappers
- old `slot1_probe*.cpp` iterations
- temporary `_tmp_*` reports
- runtime/probe logs
- generated `.bin` dumps
- old tracked report logs
- stale rebuild side-files under `ida/`

Kept:

- durable scripts
- durable research reports / xref outputs / disasm extracts
- `scanner/src/runtime_code_dump.cpp`
- `scanner/src/remote_input_client.cpp`
- `scanner/src/coop_protocol.h`
- `scanner/src/teleport_hook.cpp`

`scanner/CMakeLists.txt` was pruned to remove the deleted probe targets.

## Latest technical state

### 1. Main lifecycle correction

The important current focus is no longer blind `slot1` runtime probing.

The strongest current blocker is the **ctxB owner rebind lifecycle**.

Confirmed:

- startup bind exists
- shutdown exists
- restart path exists
- but restart delivery is deferred and not yet proven to run in campaign at the right time

### 2. Startup side

Outer callback:

- `0x1409F9B20`

Important inner bringup block:

- `0x1409FA94E`

That inner block:

- installs mode-dependent global selection service at `0x143880318`
- runs `0x1409EB500`
- then binds three shared contexts through `service->virtual +0x20`:
  - `0x143E08C80`
  - `0x143E0AC40`
  - `0x143E0EF00` (`ctxB`)

So the old idea "startup skipped ctxB" is wrong.

### 3. Shutdown side

Broad shutdown family:

- `0x1409FD360`

This is not a small local ctxB helper.
It is a broad front-end / net shutdown path that:

- resets shared-context entry state
- clears `0x143E0EF08`
- clears `0x143E08C88`
- tears down `0x143880318` through `virtual +0x18` then `virtual +0x0`

Important upper callers already pinned:

- `0x1409EE7D0` -> stage tokens like `added-to-stage` / `popped`
- `0x1409EE9B0` -> explicit multiplayer/front-end shutdown material
- `0x1409F78B0` -> `update`
- `0x1409F7C60` -> `update` / `popped`
- `0x1409FF21E`
- `0x141CE32E9` -> gated high-level state path

### 4. Restart side

Restart worker:

- `0x1409FBA60`

Critical correction:

- it does **not** jump directly into `0x1409FA94E`
- it packages `0x1409F9B20` as a callback record

Deferred delivery chain now pinned:

- `0x1415B43B0` -> alloc batch
- `0x1415B4870` -> copy `0x80`-byte work/callback records
- `0x1415B47C0` -> finalize batch
- `0x1415B4560` -> enqueue into per-lane scheduler queue rooted at `0x1437E9700`

Useful details:

- queue is `0x2200` stride per lane
- visible depth cap is `0x400`

This means:

- the current blocker is **not** “there is no path back to startup”
- the blocker is:
  - which consumer drains this deferred lane queue,
  - and under what runtime conditions the queued `0x1409F9B20` callback actually runs

## Exact next step

Do **not** go back to blind gameplay probes first.

Next step is static:

1. Trace the consumer / drain path for the deferred queue under `0x1437E9700`.
2. Find where queued records submitted by `0x1415B4560` are executed.
3. Prove how and when that execution reaches callback `0x1409F9B20`.
4. From there, explain why live campaign can still end up with `ctxB+0x8 == 0`:
   - either callback never runs,
   - runs on a lane not active in campaign,
   - gets gated/dropped,
   - or runs but later teardown wins again.

## Files most worth reading first

Read these first, and prefer the newest tail sections rather than re-reading everything:

- `reports/initial-findings.md`
- `ROADMAP.md`
- `TASK_POOL.md`
- `scripts/ida_apply_labels.py`

Then, if needed:

- `scanner/src/runtime_code_dump.cpp`
- `scanner/src/teleport_hook.cpp`
- `scanner/src/remote_input_client.cpp`
- `scanner/src/coop_protocol.h`

## What not to waste time on first

- Do not resume the old `slot1_probe*.dll` iteration loop. Those were removed on purpose.
- Do not re-open the old hypothesis that `BuildRespawnTransformBlock` itself was the blocker.
- Do not treat `0x1409FA94E` as a standalone public seam; it is an inner block inside `0x1409F9B20`.
- Do not trust packed `ida/tll.i64` timestamp over the markdown/script canon.

## Practical repo habit

After each meaningful finding:

1. update `reports/initial-findings.md`
2. update `ROADMAP.md`
3. update `TASK_POOL.md`
4. update `scripts/ida_apply_labels.py`
5. run `python3 -m py_compile scripts/ida_apply_labels.py`
6. optionally run headless IDA apply, knowing it may return `1`
