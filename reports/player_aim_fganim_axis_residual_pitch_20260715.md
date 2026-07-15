# ADS FgAnim axis and residual pitch basis (RUNTIME + STATIC, 2026-07-15)

## Observed result of build da

The corrected translation/quaternion layout made the late phase-`0x40` relay
visible and stable: during ADS the remote model now turns diagonally as the peer
camera moves. The user observed three precise remaining failures:

1. horizontal facing is exactly 180 degrees opposite the reticle;
2. vertical camera motion produces no visible pitch;
3. entering ADS can expose a folded/broken transition pose.

This runtime result confirms `0x141A8FC00 -> 0x1418C55D0` as the visible body-yaw
boundary and rejects stale render publication as the remaining yaw cause.

## Selected hypothesis

The symptoms have two independent owners. The render-world quaternion uses the
opposite horizontal axis at this FgAnim boundary. Meanwhile
`PlayerAimController_ComputeDirectionalAimWeights 0x140CB32B0` still compares the
camera target against GunMoveShield's stale body basis, so horizontal aim saturates
while the late world publication turns the visible body. The controller should
receive the normalized horizontal projection of the relayed camera `+Z`; the full
target retains `Y`, leaving pitch as the only additive residual.

The occasional folded entry has a separate conflicting writer: the mod writes
peer pose output to `Player+0x1720/+0x1730` both before and after native update,
although `Player_PostAnimUpdate 0x1410E9210` recomputes those fields every frame.
Those manual output writes must remain parked.

## Competing hypotheses

1. **Selected:** invert only the late FgAnim horizontal quaternion, align the aim
   controller's logical body basis with camera-horizontal `+Z`, and park direct
   pose-output writes.
2. Preserve the native body basis and change only the render sign. This fixes the
   180-degree body error but leaves the known 60-degree horizontal saturation, so
   it predicts no reliable vertical additive.
3. Put camera pitch into the FgAnim world quaternion. Rejected because it tilts the
   complete body and attachments rather than driving authored upper-body aim.
4. Continue writing `Player+0x1720/+0x1730`. Rejected by prior runtime as inert and
   by the exact native writer; these are output snapshots, not controller inputs.
5. Remove the FgAnim aim-add mirror. Not selected yet: prior runtime showed the
   controller/publish state changing while render-bound FgAnim fields stayed stale.

## Independent evidence

1. **Runtime boundary and axis discriminator.** Build da modifies only the remote
   FgAnim world quaternion during fresh ADS. The visible model now turns, proving
   the selected publication reaches rendering. Its exact 180-degree error selects
   an axis/sign correction, not stale packets or a missing render consumer.
2. **Directional-weight data flow.** Slot `+0x58` receives target and body-forward;
   `0x140CB32B0` normalizes `target-origin`, computes signed angles against that
   body-forward, and publishes left/right/up/down through `0x140CB41F0`. Previous
   runtime recorded right saturation at one when relative yaw exceeded 60 degrees.
3. **Native target convention.** `PlayerAimController_GetTargetPointFromReticle
   0x140CB1720` expands route quaternion local `+Z`. The relay's full target uses
   the same convention, so its normalized XZ projection is the matching logical
   body basis while its Y component remains the pitch residual.
4. **Independent output writer.** `Player_PostAnimUpdate 0x1410E9210` resolves the
   pose at return `0x1410E934B` and stores it to `Player+0x1720/+0x1730` every
   frame. Prior direct-write runtime was inert. Pre/post external writes can only
   race evaluation and are not a valid vertical control surface.

## Build db prediction

Build `2026-07-15db` negates only the XZ direction passed to the FgAnim yaw helper,
passes normalized camera-horizontal `+Z` as slot `+0x58` body-forward, retains the
full camera target and native vertical limits, and removes both calls that write
`Player+0x1720/+0x1730`. It emits one bounded marker after the first sample with
`abs(cameraY) >= 0.10`.

Predicted visible result: the remote faces the reticle rather than away from it;
left/right camera motion turns the whole body; up/down motion changes upper-body
aim; ADS entry no longer folds. If the marker reports non-zero up/down weights but
vertical pose remains absent, the blocker is below the controller in FgAnim
parameter consumption. If entry still folds after the output writes are parked,
the remaining owner is ADS/action-state forcing rather than pose-seed output.
