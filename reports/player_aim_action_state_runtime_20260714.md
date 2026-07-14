# Player aim action-state runtime (2026-07-14)

Status: **RUNTIME — shield-wrapper hypothesis REJECTED; GunMove path confirmed**.

PIDs: receiver **21228**, sender **13148**. Log: `reports/locomotion_hook.log`.

## Result vs prediction

| Marker | Predicted (shield-gate theory) | Observed |
|--------|-------------------------------|----------|
| `AIM-WRAPPER` | `shieldDrawn=0 dispatch=0` on pistol | **Absent** (0 lines) |
| `AIM-ACTION-STATE` | `GunMoveShield` or wrong hash | **`GunMove` `0x12D842C0`** (stable ADS) |
| `AIM-DISPATCH-ENTRY` | absent on pistol | **Present** every ADS sample |
| `AIM-ELIGIBILITY` | absent or forced | **`original=1 forced=0`** |
| `AIM-WORLD-BASIS` | absent | **Present** with sane yaw/pitch |
| `AIM-WEIGHTS-POST` | all zero | **Non-zero** after blend ramp |

Brief transition hash `0xE1939F43` appears on ADS entry (1–2 samples) before
settling to `GunMove`. Name unresolved from known gun-move strings.

## Representative sequence (seq=325, pistol ADS)

```
AIM-ACTION-STATE ok=1 hash=0xe1939f43 name=unknown ...
AIM-DISPATCH-ENTRY actionOk=1 hash=0x12d842c0 name=GunMove
AIM-ELIGIBILITY reached original=1 forced=0
AIM-WORLD-BASIS relay ... yawPre=-12.3 pitchPre=-12.7 ...
AIM-ADDITIVE-PERMISSION original=0 forced=1
AIM-WEIGHTS-POST L=0 R=0.796 D=0 U=0.557 blend=0
AIM-BLEND-TICK request=1->1 blend=0.26->0.26 ... blend=1->1
AIM-WEIGHTS-POST ... blend=1  (subsequent frames)
```

## Conclusions

1. **REJECTED:** pistol puppet is blocked by `Player::GunMoveShield` wrapper
   `0x140E86930` / `ProcessShield` gate. Live path is **`Player::GunMove`**
   (`0x12D842C0`), which reaches common dispatch without that wrapper.
2. **REJECTED:** missing world-basis in this run. Eligibility passes natively;
   `AIM-WORLD-BASIS` and directional weights publish once blend reaches 1.
3. **CONFIRMED:** additive permission still needs relay (`original=0 forced=1`).
4. **CONFIRMED:** blend request/target forcing is active; first ADS frame can
   show `blend=0` weights before `AIM-BLEND-TICK` ramps to 1.

## Contrast with PIDs 200/13948 (prior palette run)

That run had `GunMove` blend/IK active but **zero weights and no world-basis**.
This run differs by installed permission/blend relays and stable `GunMove` state.
The remaining visual gap (if any) is downstream of published weights, not action
dispatch selection.

## Next static/runtime step

Trace why `0xE1939F43` transition appears at ADS entry and whether visual aim
still fails on receiver despite non-zero `AIM-WEIGHTS-POST`. If visual fails,
resume palette/render consumption trace from `AnimManager_PerformAnimBlend`
with weights confirmed non-zero.