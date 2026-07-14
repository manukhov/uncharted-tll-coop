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

### v3 build (bo)

- GunMoveController relay **parked**.
- Post-update **full root yaw snap** from peer camera -Z on `Player+0x140` during
  ADS (`AIM-ROOT-YAW-SNAP`). Prediction: `rootYaw` tracks `peerYaw` in
  `AIM-RENDER-DISC`; visible aim follows or selects downstream skeleton owner.