# 2026-07-15 STATIC: native-context melee press relay (build cq)

## Exact hypothesis

`DispatchPlayerMeleeButtonHoldReleaseAndGrappleReactionCommands 0x141272A50`
already runs for the retained slot-1 Player from the stock controller tick. If
only its `melee-r1` `PlayerInputCommandState_IsCommandActive` call at
`0x141273AE2` returns true once for `Player+0x39A0`, the dispatcher will build
the complete stack payload and insert it through
`PushOrRotateMeleeCommandQueueEntry 0x141273FA0` in the correct engine context.
The normal melee controller can then query and consume that command without a
global input hook, manual dispatcher call, direct queue push, or direct animation
call.

Local PC melee is captured at the sibling `melee-lmouse-press` callsite
`0x141273D3B`; local gamepad melee is captured at the same `melee-r1` callsite.
A 150 ms protocol pulse survives several state packets. Its receiver-side rising
edge increments a saturated atomic counter, and exactly one slot-1 `melee-r1`
query consumes exactly one count.

## Competing hypotheses

1. Patch the global `PlayerInputCommandState_IsCommandActive 0x141E39D50` entry.
   Rejected by build cn/co: it broke GiveWeapon and weapon visibility.
2. Call `0x141272A50` or `0x141273FA0` manually from a mod input/update hook.
   Rejected by ck/cm: the synchronous dispatcher hung both copies and the direct
   queue push crashed the receiver because the native context/payload was absent.
3. Force `melee-lmouse-press` on the puppet. Rejected statically: that branch is
   gated by `Joypad_IsKeyboardMouseInputModeActive 0x14183E2D0`, which returns
   `Joypad+0xBE1`; the slot-1 Joypad does not reliably own PC input mode.
4. Call the animation/decision vtables directly. Rejected by ch/cj and unsafe:
   the guessed descriptor failed and the direct TryAnim/publish path was tied to
   boot crashes.

## Independent static evidence

1. **Call graph and owner context.**
   `PlayerMeleeController_DispatchQueuedButtonCommandsTick 0x141279DF0` reads its
   Player back-reference at `controller+0x108`, queue base at `+0x150`, advances
   once per engine-clock stamp, and calls `0x141272A50(queue, player)` itself.
2. **Exact dataflow and callsites.**
   `0x141272A50` derives `PlayerInputCommandState = player+0x39A0`. At
   `0x141273AE2`, the `melee-r1` hash is passed to `0x141E39D50`; a true result
   immediately builds the native payload and calls `0x141273FA0`. The analogous
   mouse call is exactly `0x141273D3B`.
3. **Vtable identity and downstream query.**
   The queue-owner vtable `0x142A6C2E8` maps `+0x1E8` to the natural dispatcher
   tick, `+0x1F8` to `0x141279E50`, and `+0x220` to `0x14127A250`.
   `0x14127A250` independently hashes literal `melee-r1` and invokes `+0x1F8`;
   `0x141279E50` forwards the queue at owner `+0x150`, the command descriptor,
   and Player back-reference to `0x1412712D0`.
4. **Input-mode discriminator.**
   `0x14183E2D0` returns zero when the global input manager blocks PC input,
   otherwise `Joypad+0xBE1`. The native poll `0x14183B410` writes that field from
   keyboard/mouse versus pad activity. This excludes the mouse branch for remote
   injection and selects the unconditional gamepad `melee-r1` branch.

The evidence quota is met by the natural call graph, exact instruction/dataflow,
and an independent vtable/downstream command path. The input-mode writer/readers
also falsify the mouse-injection alternative.

## Build cq implementation

- Patches only the two five-byte E8 callsites and validates both still target
  `0x141E39D50` before modifying code.
- Uses one nearby FF25 relay and calls the original function first.
- Captures only original-true local `lmouse-press` or `r1` results.
- Forces only `r1`, only for retained slot 1, only with fresh peer state, and
  consumes one pending press atomically.
- Keeps the rejected global PlayerInput detour and all manual queue/animation
  paths disabled.
- Adds no runtime logging, hash scans, VirtualQuery, allocation, or queue calls
  to the per-tick hook.

## Falsifiable runtime prediction

With two fresh copies on build cq, a local melee press should produce one visible
native melee attempt on the peer puppet, while local weapon pickup/give/switch
continues to work and neither process hangs or crashes. Holding the button must
not enqueue a punch every frame. CPU/frame pacing should remain at the cleaned cp
baseline because production runtime diagnostics stay disabled.

If the puppet still does not animate but remains stable, this falsifies the claim
that a correctly queued `melee-r1` command is sufficient for the retained Player;
the next static target is the downstream action eligibility/target decision after
`0x1412712D0`, not another input or queue bypass. If weapon give regresses, the
two-callsite isolation assumption is falsified.

## 2026-07-15 regression discriminator (build cs)

After build cq was introduced, the user reported that visually the two copies no
longer connected. Build cr proved that both processes exchange and accept their
first 456-byte state packet, but both processes then disappeared shortly after
the local Players became valid. The cq relay is the only newly installed code
patch after cp: it redirects two calls in the naturally scheduled melee
dispatcher through an indirect FF25 relay.

Competing hypotheses:

1. The cq callsite relay destabilizes the natural Player melee tick after Player
   creation, preventing the downstream puppet path or terminating the process.
2. The earlier cp diagnostics cleanup removed a functional side effect despite
   its source paths being diagnostic.
3. Both processes were manually closed and the visual failure is independently
   in puppet spawn/apply timing.

Evidence for isolating candidate 1 first: the vtable graph proves the dispatcher
is naturally scheduled; disassembly proves both patched E8 sites are reached
inside that dispatcher; and runtime proved transport succeeds immediately before
the processes disappear. This does not yet confirm causality.

Falsifiable prediction: build cs, with only
`g_enable_puppet_melee_callsite_relay=false`, must omit the relay install line,
exchange state packets, keep both processes alive, and restore the visible peer.
If packet exchange remains but the peer is still absent, cq is rejected as the
connection-regression cause and cp becomes the next rollback boundary. Melee is
explicitly parked during this discriminator.

## 2026-07-15 build cu: isolated melee runtime exam

The ct user test confirmed the missing-peer regression came from cp disabling the
required spawn descriptor capture, not from UDP. Production capture is now
mandatory and its diagnostic scan remains off. Build cu changes only the cq
melee callsite relay from parked to enabled on top of that corrected baseline.

Prediction: both visible peers must spawn and remain stable; one local melee tap
must produce at most one native peer melee attempt, and holding melee must not
enqueue a command every frame. A visible animation confirms the native R1 queue
path is sufficient. Stable peers with no animation reject sufficiency and move
the next static target downstream of `0x1412712D0`. Any process exit or weapon
regression rejects the FF25 callsite relay itself and it must be parked again.

## 2026-07-15 build cu result and square-path correction

**REJECTED (user):** peers remained visible and connected, but the remote Player
did not visibly fight. This rejects the claim that translating every local melee
press to `melee-r1` is sufficient. It does not reject the isolated local press
capture or transport.

Competing downstream explanations were:

1. the forced R1 command was never consumed;
2. the retained Player did not schedule the normal action dispatcher;
3. R1 was consumed but rejected by Player state/cooldown gates;
4. `PlayerMeleeController::TryAttack` rejected it because no local target rated.

Static evidence separates the command families. The dispatcher at `0x141272A50`
queries `melee-square` through `Joypad_IsCommandActive` at `0x141272B99` and
immediately inserts that exact command through `0x141273FA0`. Independently,
PlayerMeleeController vtable `+0x210 = 0x141279F10` resolves literal
`melee-square`; `PlayerMoveStick_MainUpdate` consumes it and calls the free melee
animation helper `0x1410FC9F0`. By contrast, vtable `+0x220 = 0x14127A250`
resolves `melee-r1`; `Player_TryConsumeQueuedMeleeActions 0x1410F5740` routes it
to literal `player-r1-attacks` and `PlayerMeleeController::TryAttack
0x141275910`, which succeeds only when `MeleeAttackRater_RateTargets` selects a
local target. The exact call graph, independent string/dataflow identities, and
vtable slot split satisfy the static evidence quota.

Build cv keeps the two exact PlayerInput callsite patches only for local
R1/lmouse capture. It no longer forces remote R1. The already-established
Joypad-3 command hook consumes one fresh pending pulse only when the stock
dispatcher asks for `melee-square`; no manual queue or animation calls are made.

Falsifiable prediction: a local air melee tap produces one visible free melee
animation on the peer puppet, while holding does not repeat every frame and
weapon pickup/switch remains healthy. If there is still no animation, the R1
target-rater alternative is excluded; the next discriminator is whether
`MeleeCommandQueue_QueryMeleeSquare` returns true for slot 1 versus
`0x1410FC9F0` rejecting the naturally consumed command. A crash or lost peer
rejects the square injection and requires immediate rollback to cu.

## 2026-07-15 build cv result and cw stage probe

**REJECTED (user):** cv also kept both peers stable but showed no remote hit
animation. Therefore selecting the correct square command family is necessary
but not sufficient.

The remaining hypotheses are now mutually distinguishable:

1. the remote Joypad square query is never reached, so the pending pulse remains;
2. the dispatcher accepts square, but controller `+0x210` cannot retrieve it;
3. `+0x210` retrieves square, but the intermediate MoveStick gates skip TryAnim;
4. TryAnim is called and returns zero because one of its explicit gates rejects
   the puppet;
5. TryAnim returns one, but the resulting animation state is not published to the
   visible puppet pose.

The static quota is met for a stage-only runtime discriminator. The exact
dispatcher dataflow forces square at `0x141272B99` and inserts it through
`0x141273FA0`; independent vtable identity maps `+0x210` to
`MeleeCommandQueue_QueryMeleeSquare 0x141279F10`; and the sole code xref to
`Player_TryStartFreeMeleeAnimation 0x1410FC9F0` is direct E8 callsite
`0x140F07A0A`, immediately after the `+0x210` result test at `0x140F079E5`.
The TryAnim body independently shows explicit global, network-mode, action-state,
and controller virtual gates before returning one.

Build cw adds no periodic telemetry. It emits at most one line for each stage:
`square-query-forced`, `queue-square=1` (or one delayed zero), and
`try-anim=<0|1> enabled=<byte> netHi=<byte>`. The queue observation swaps only
the known controller vtable `+0x210`; TryAnim observation patches only the direct
E8 callsite and calls the original entry. The rejected RIP-relative prologue
trampoline remains uninstalled.

Falsifiable prediction: one remote melee tap must produce
`square-query-forced`. If no later queue line appears, slot-1 MoveStick is not
querying the controller. A delayed zero identifies queue insertion/resolution;
queue one without TryAnim identifies the intervening movement gates; TryAnim zero
identifies the helper gates; TryAnim one with no visible animation moves the bug
strictly downstream into animation publication/evaluation.

## 2026-07-15 build cw result and cx local-square capture

**REJECTED at the sender boundary:** the fresh cw run connected both peers and
materialized both remote Players, but one user melee tap produced none of the
stage lines. In particular, the receiver never logged `square-query-forced`.
This falsifies the assumption that the two local PlayerInput callsites cover a
free PC melee tap; the queue and TryAnim hypotheses were not exercised.

The corrected sender hypothesis is that a free melee tap is observed at the
dispatcher-owned Joypad query itself. Three independent observations select it:

1. dispatcher disassembly passes literal `melee-square` to
   `Joypad_IsCommandActive` at `0x141272B99` and immediately builds the native
   queue payload on success;
2. `PlayerInputCommandState_GetJoypadIndex 0x1402B1890` and the confirmed
   `Player+0x39B8` binding select the actual local Joypad (runtime slot 0 = 2),
   distinct from the PlayerInput lmouse/R1 query surface;
3. the earlier cg runtime probe observed a real local active hash
   `0xd338e42f`, exactly the hash of `melee-square`, while cw proved that the
   lmouse/R1-only sender pulse remained absent for the same visible action.

Build cx therefore captures only an original-true `melee-square` result on the
Joypad selected by retained slot 0's live `Player+0x39B8`. It keeps the existing
R1/lmouse callsites as context-specific fallbacks, sets the same bounded 150 ms
wire pulse, and adds only one sender marker, `local-square-captured`.

Falsifiable prediction: one free melee tap logs `local-square-captured` on the
sender and `square-query-forced` on the peer. Absence of the sender line rejects
the Joypad ownership/query hypothesis. Sender presence without receiver force
selects packet edge transport. Once receiver force appears, the existing queue
and TryAnim stage outcomes retain their prior meanings.
