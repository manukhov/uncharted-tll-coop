# Bounded external crash capture (AGV)

The P99 package is still disabled. This tool does not authorize a game exam.
Use only after the static admission and written discriminator are ready, with
one already-started/post-loader `tll.exe`. The user performs all game input.

Build `tll_coop_runtime` and `tll_crash_watch` in Release. The DLL now keeps
optimized `/Zi /DEBUG:FULL /OPT:REF /OPT:ICF` symbols; preserve the resulting DLL
**and PDB from the same build** before rebuilding. Freeze these, the executable
hash, git revision, PID and creation time in a new local `reports/` session.
Never replace an old session's binaries/dump. Dump module records and the PDB
signature must be checked when attributing addresses; a matching filename is
not sufficient.

The watcher accepts only an explicit PID plus absolute expected image path.
It refuses existing output directories and does not launch or inject the game:

```powershell
$captureArgs = @('--pid', $targetProcessId, '--expected-image', ('"' + $gamePath + '"'),
    '--out', ('"' + $newSessionSubdirectory + '"'), '--seconds', 180)
$watcher = Start-Process -FilePath $absoluteWatcherPath -ArgumentList $captureArgs -WindowStyle Hidden -PassThru
```

The parent of the output directory must already exist. Wait for `ready.txt`,
verify its PID/creation timestamp and that the watcher is alive **before**
injecting or starting the user scenario. `ready.txt` remains historical after
detachment; it is not a liveness signal. At timeout the watcher detaches without
terminating the game. If the log reports `SAFETY_HOLD`, do not kill the watcher:
setting no-kill-on-exit and detaching both failed; it keeps retrying instead
of exiting under Windows' default debugger-kills-target policy.

Every ordinary first-chance exception is passed to the game's own handlers.
Only the initial debugger-attach breakpoint is consumed. No per-exception
stack formatting or file writes occur for handled exceptions; a 32-entry
code/PC/thread ring is emitted at the end. The first **second-chance** exception
gets `second-chance.dmp` before continuation/termination. The dump contains the
actual event thread's registers, exception code/address, modules and stacks.
The external debugger serializes DbgHelp use and uses local exception pointers.
No game memory patch, input, global WER registry setting or in-game VEH is added.

Limits: attachment itself pauses threads and debugger events alter timing;
do not use this run to claim production performance. Game-handled errors that
call `ExitProcess`, external termination, machine failure or watcher/dump I/O
failure may produce no exception dump. `NO_SECOND_CHANCE_CONTEXT` explicitly
means the recorded exit/last handled event cannot identify a crash instruction.
Do not call this universal crash coverage or the cause of the previous AGR crash.
Do not upload dumps: they contain process memory and stay in ignored local paths.

`ctest -R crash_watch --output-on-failure` creates only owned test children:
handled exception, handled error-exit77, unhandled exception, program breakpoint,
fail-fast, and timeout. It validates dump exception/register/module/thread/stack
streams, preserved handlers/exit codes, wrong-image/output-directory refusal,
and live-child survival after timeout. Fixture startup is gated before debugger
attachment; no `tll.exe` is involved. API policy failure/detach double failure
is code-reviewed but not injected into a live test.

References: [DebugActiveProcess](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocess),
[ContinueDebugEvent](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-continuedebugevent),
[DebugSetProcessKillOnExit](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-debugsetprocesskillonexit),
[MiniDumpWriteDump](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump).
