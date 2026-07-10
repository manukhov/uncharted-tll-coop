# PlayerLookAtIkController — static field map (2026-07-07)

## Mount point

| Player offset | Object | Ctor / vtable | Size |
|---------------|--------|---------------|------|
| **`+0x1E38`** | `PlayerLookAtIkController` | ctor `0x140ED94B0`, vtable `0x142A2D550` | `0x460` |
| `+0x1DD0` | `CharacterWallHandIK` (**not look-at**) | ctor `0x14024A090` | `0x540` |
| `+0x1E48` | perch (`player-perch.cpp`) | vtable `0x142A32C08` | `0x2B0` |

Mounted from `AssembleLargePlayerOwnerSubsystemGraph` @ `0x1410CA720` (xref `0x1410CBACA`).

RTTI in vtable `0x142A2D550`: `IPlayerLookAtIkController`, `IPlayerLookAtController`, `PlayerLookAtController`.

## Vtable `0x142A2D550` (confirmed methods)

| Slot | RVA | Symbol | Role |
|------|-----|--------|------|
| 0 | `0x140EDB580` | dtor | |
| 1 | `0x140ED95F0` | setup | Registers `*player-ik-look-point*`, bone chain (`headb`, `spinea`, …) |
| 2 | `0x140EDA500` | relocate | Pointer fixup on heap moves |
| 5 | `0x140EDA5F0` | **SetLookPoint** | mode **1**: world point |
| 6 | `0x140EDA630` | **SetLookLine** | mode **2**: point + direction (32 B) |
| 7 | `0x140EDA680` | **UpdateIk** | Per-frame IK solve (`player-look-at-ik.cpp:203`) |

## `PlayerLookAtIkController` fields (offsets from `this`)

| Offset | Hex | Evidence | Role |
|--------|-----|----------|------|
| +288 | `0x120` | ctor | `JointTree` embedded |
| +304 | `0x130` | ctor `= 1` | |
| +312..328 | | ctor | owner / ref ptrs |
| +306 | `0x132` | UpdateIk | `m_numJoints` (int16) |
| +328 | `0x148` | UpdateIk | external owner ptr |
| +368 | `0x170` | ctor | joint chain array (88 B slots) |
| +632 | `0x278` | ctor | secondary chain |
| +808 | `0x328` | setup | heap buffer: `numJoints * 32` bytes |
| +816 | `0x330` | SetLook / UpdateIk | **per-slot float weight** (`slot * 4`) |
| +824 | `0x338` | SetLook | per-slot int param |
| +848 | `0x350` | SetLook mode 1 | slot data: `+ (slot << 6)` + 16 B point |
| +864 | `0x360` | SetLook mode 2 | slot data part A |
| +880 | `0x370` | SetLook mode 2 | slot data part B (+16 B) |
| +912 | `0x390` | UpdateIk | **slot table** (stride 88 B / 22 DWORDs) |
| +972 | `0x3CC` | setup | hash / id for `headb` joint |
| +976 | `0x3D0` | UpdateIk | joint index A |
| +980 | `0x3D4` | UpdateIk | joint index B |
| +992 | `0x3E0` | UpdateIk | **active mode** (1 = point @ +0x3F0, 2 = line @ +0x400/+0x410) |
| +1008 | `0x3F0` | UpdateIk | **world look point** (mode 1, float4) |
| +1024 | `0x400` | UpdateIk | mode 2 start (float4) |
| +1040 | `0x410` | UpdateIk | mode 2 end (float4) |
| +1056 | `0x420` | UpdateIk | **byte: IK enabled** |
| +1072 | `0x430` | UpdateIk | smooth accumulator |
| +1088 | `0x440` | UpdateIk | smooth accumulator |

## SetLook API (virtual)

**Mode 1 — `0x140EDA5F0` (vtable +0x28):**
```cpp
void SetLookPoint(this, unsigned slot, int param, const float pos[4], float weight);
// writes: weight -> +0x330+slot*4
//          mode flag 1 -> *(this + ((slot+13)<<6))
//          pos -> +0x350 + (slot<<6)
//          param -> +0x338+slot*4
```

**Mode 2 — `0x140EDA630` (vtable +0x30):**
```cpp
void SetLookLine(this, unsigned slot, int param, const float data[8], float weight);
// mode flag 2; data[0..3] -> +0x360+(slot<<6); data[4..7] -> +0x370+(slot<<6)
```

**UpdateIk — `0x140EDA680` (vtable +0x38):** reads active slot from table `+0x390`, copies into `+0x3F0..+0x410`, solves head chain.

## Co-op head-look plan (build-ready hypothesis)

**Sender:** when `|yaw(cam) - yaw(body)| > 0.15` rad and not ADS → `flags |= 0x0400`, send `camera_quat` (reuse existing field).

**Receiver:** on puppet (`Player+0x1E38`):
1. `world_target = peer.pos + horizontal_forward(peer.camera_quat) * 8m` (+ optional pitch)
2. Call **SetLookPoint** (vtable+0x28) on slot used by `*player-ik-look-point*` (discover slot index from setup path, or slot 0 if sole entry)
3. Ensure `+0x420 = 1`; call **UpdateIk** (vtable+0x38) if not invoked by native tick

**Do NOT:** screen-1 pose seed, full `Player+0x140` blend while moving, `look_stick` → right stick.

## Decompile artifacts

- `reports/lookat_vtable_2d550.txt`
- `reports/lookat_decompile_methods.txt`
- `reports/lookat_string_xrefs.json`
- `reports/lookat_xrefs_vtable.txt`
