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
