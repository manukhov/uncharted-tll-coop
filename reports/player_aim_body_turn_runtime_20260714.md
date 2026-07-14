# Player aim body-turn runtime (2026-07-14)

## PIDs 21060/10864 — pos-Z build REJECTED

User: puppet still fixed in ADS; **worse** — aims nowhere near initial reticle.

### Observed

- `AIM-TURN-DIR` active: `peerPosZ` overridden into `GunMoveController+0x150`.
- `AIM-ROOT-YAW-SYNC` fought a recurring `before≈47°` root heading with `alpha=0.92`.
- `AIM-RENDER-DISC` yawDelta shrank to ~3–9° in places, but visual direction wrong.

### Conclusion

**REJECTED:** body turn via pose **positive-Z** on `+0x150` and post root sync.
`+0x150` is built from screen camera **look (-Z)** in native `0x140E8F990`; pos-Z
is the aim-target expansion, not body-turn heading. User regression matches ~180°
class axis error.

### v2 build (bn)

- `+0x150` relay uses `camera_forward_horizontal` (-Z) on peer pose_seed.
- Post `sync_puppet_root_yaw_from_peer_camera` **parked** (`g_enable_gun_move_body_turn_post_sync=false`).
- `AIM-TURN-DIR` logs `peerNegZ` and `peerPosZ` together.

### Prediction

Puppet ADS horizontal aim aligns with peer reticle; no "random wrong compass"
regression. If still fixed, competitor is downstream additive/render, not axis.

## PIDs 13400/21384 — neg-Z relay REJECTED

User: same as before — fixed wrong aim, no tracking.

- `AIM-TURN-DIR` peerNegZ changes every sweep; stock native `+0x150` stays
  `~(-0.306,0.952)` (receiver-local).
- `Player+0x140` rootYaw **stuck ~29.8deg** across peerYaw -151..+83 in
  `AIM-RENDER-DISC` while `+0x150` is overridden post-native.
- Conclusion: patching `+0x150` after `0x140E8F990` does not rotate the puppet
  root; `0x140E8FD00` path inactive or overridden for slot-1 puppet.

### v3 build (bo) — REJECTED

PIDs 2084/7800. User: still one fixed direction.

- `AIM-ROOT-YAW-SNAP` fires every ADS frame (`dot=0` full memcpy).
- `AIM-RENDER-DISC` seq=1051: `peerYaw=37.3` `rootYaw=37.8` `yawDelta=-0.5°` with
  live `pubAdd` — **root aligned, visual still fixed**.
- Many samples show `yawDelta=±180°` because RENDER-DISC measures `peerFwd` with
  pose +Z while snap uses camera -Z; metric artifact only.
- `AIM-WORLD-BASIS` body gains Y tilt after snap (`body.y != 0`).

**CONFIRMED:** visible ADS aim is **not** owned by `Player+0x140` root yaw. Branch
closed for all root writers (GunMove `+0x150`, post snap). Continue at
post-anim/render skeleton consumption (`0x141AB97C0` eval → DrawControl).