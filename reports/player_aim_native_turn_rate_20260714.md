# Player aim native turn-rate relay (2026-07-14)

Status: **STATIC, runtime pending**.

## Exact hypothesis

During strict ADS, relay the sender's actual resolved block-0 right-stick axes
`+0x08/+0x0C` into puppet destination 3 only for the duration of
`Input_ScaleCopyVirtual 0x14183B410`. These transient native rates drive the
receiver's camera/turn-in-place path and become zero when mouse motion stops.
Keeping the aim additive within the stock 60-degree arc and allowing native
`controller+0x8EC` blending prevents the broken entry pose while the root turns.

## Competing hypotheses

1. Mouse camera motion does not appear in resolved block-0 right-stick axes.
2. The axes reach `Joypad3+0x50/+0x54`, but screen-1 CameraControlStick does not
   consume them for the retained Player.
3. Native camera turns, but the post-update network body-quaternion blend cancels
   turn-in-place.
4. The pose remains wrong because reconstructed camera `-Z`, not root ownership,
   is the remaining direction error.

## Independent evidence

1. `FrameState_PerSlotInputApply 0x140289150` and branch `0x1402893D0` map
   right-stick fields into resolved block `+0x08/+0x0C` before calling
   `Input_ScaleCopyVirtual 0x14183B410`.
2. `Input_ScaleCopyVirtual` copies source `+0x08/+0x0C` at
   `0x14183B559..0x14183B568`, then publishes processed right stick at
   `Joypad+0x50/+0x54` at `0x14183C19E..0x14183C1AD`. The existing focus and
   keyboard-mode bypass is already runtime-confirmed for destination 3.
3. `CameraControlStick::UpdateLocation 0x1407541E0` owns camera turn-rate state;
   settings loaded by `0x1408B3740` include `disable-camera-turn`, threshold,
   maximum speed, and speed scale. GunMoveShield consumes resulting camera/Player
   state later rather than reading right-stick axes directly.
4. Runtime previously proved a sustained non-zero right stick spins both a normal
   Player and the puppet, while zero stops the turn. The rejected implementation
   sent `(cameraYaw-bodyYaw)*0.55`, a persistent position error, and combined it
   with a camera-root writer. The new source is the native transient rate and the
   automatic camera-root writer is absent.
5. `PlayerAimController_UpdateBlendState 0x140CAFA20` owns gradual `+0x8EC`
   convergence. The mod instead wrote `1.0` directly before/after the tick; current
   logs show `blend=0->1` on entry, matching the user's broken transition.

## Narrow change

- Sender samples local screen selector block 0 `+0x08/+0x0C`; it no longer derives
  an absolute camera/body error.
- Receiver enables the existing destination-3 save/apply/restore sandwich by
  default and logs source versus `Joypad3+0x50/+0x54`.
- The mod keeps only request byte `controller+0x8A0`; native blend tick owns
  `+0x8EC`.
- Dead screen-1 horizontal limits fall back to the stock ordinary-ADS 60 degrees.
- The unjustified full 32-byte pose-seed override at `0x140CB0300` is removed.

## Falsifiable prediction

While the sender holds ADS and moves the mouse, `AIM-TURN-SEND` is non-zero and
the receiver emits matching `AIM-TURN-RELAY` processed axes. Once the mouse stops,
both disappear instead of leaving a sustained rate. The puppet turns with the
camera, `AIM-WORLD-BASIS yawPre` trends inside the 60-degree additive arc, and the
entry blend rises over multiple ticks rather than `0->1`. If sender axes remain
zero, candidate 1 wins and camera-quaternion delta is the next source. If processed
axes are non-zero but yaw/root does not move, candidates 2 or 3 win. If root moves
but aim direction remains inverted, candidate 4 wins.
