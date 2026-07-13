# Player aim eligibility callsite relay (2026-07-13)

Status: **RUNTIME reached downstream; visual blend remains blocked**.

## Runtime discriminator from the downstream test

Two-copy test, PIDs 16212/1692, commit `a2d8cf4`:

- sender logged `STRICT-AIM-SEND ON`;
- receiver repeatedly logged `AIM-POSE-OVERRIDE site=0x10e934b src=peer`;
- receiver logged no `AIM-WORLD-BASIS relay` calls.

The peer ADS and pose seed therefore arrived, but the ground GunMoveShield path
did not reach PlayerAimController slot `+0x58`. This confirms the previously listed
eligibility-blocker hypothesis; it does not reject the downstream target relay.

## Hypothesis

The slot-1 puppet fails `Player_CheckAimActionEligibility 0x1410E3670` at the
ground GunMoveShield call `0x140E8573C`. Calling the original eligibility function
and conditionally changing only its false result to true for fresh peer ADS will
let the stock branch continue into PlayerAimController with its exact target,
body-forward, weapon limits, blend, and follow-up updates.

## Competing hypotheses

1. Conditional eligibility admission reaches slot `+0x58` and produces the
   directional aim pose.
2. Another branch between `0x140E857F1` and the slot calls still rejects the
   puppet.
3. Slot `+0x58` runs after admission, but downstream animation suppresses its
   weights.
4. Manually calling slot `+0x58` could bypass the gate, but would require
   reconstructing weapon-specific limits and blend state; it is kept as a later
   fallback, not the preferred candidate.

## Independent evidence

1. Raw disassembly shows `call Player_CheckAimActionEligibility` at
   `0x140E8573C`, `test al,al`, and the false branch at `0x140E85749`. The true
   branch at `0x140E857F1` builds body-forward and reaches slot `+0x58` at
   `0x140E858CE` or `0x140E85925`.
2. The downstream runtime test delivered strict ADS and the exact pose seed but
   observed zero slot `+0x58` calls, isolating the break above that slot.
3. GunMoveShield itself is confirmed running for both screen 0 and screen 1, so
   this is a state result, not a missing puppet update.
4. The original eligibility function checks multiple Player-owned action,
   weapon, camera, look-suppression, and command-state subsystems. Reproducing or
   mutating all of those fields would have a larger and less bounded effect than
   overriding this single result at the known aim callsite.

## Implementation

The DLL verifies the instruction at `module+0xE8573C` is an `E8` call whose
resolved target is exactly `module+0x10E3670`. It allocates a rel32-near absolute
jump relay and patches only that call destination.

The wrapper always calls the original first and preserves its true result. It
forces false to true only when:

- `player == retained_player_object(1)`;
- `Player+0x4010 == 1`;
- peer state is at most 250 ms old;
- strict peer ADS and a valid peer pose seed are present.

Slot 0 and every non-ADS frame are byte-for-byte original behavior.

## Runtime prediction

- `AIM-ELIGIBILITY relay original=0 forced=1` appears during peer ADS;
- it is followed by `AIM-WORLD-BASIS relay`, whose forward vector changes with
  peer camera yaw and pitch;
- the remote upper body follows the aim while local movement/HUD remain intact.
- Eligibility lines without world-basis lines select competing hypothesis 2.
- Both lines without visual motion select competing hypothesis 3.

## Runtime result

PIDs 14320/14884 reached `AIM-WORLD-BASIS relay` repeatedly with changing peer
camera vectors, but emitted no `AIM-ELIGIBILITY ... forced=1` lines during the
observed interval. The original eligibility result was therefore already true in
that test. The user still saw no aim animation, selecting competing hypothesis 3:
the remaining blocker is downstream of slot `+0x58`.
