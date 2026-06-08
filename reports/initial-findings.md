# Initial Findings

Build:

- `tll.exe` version `1.4.21058.0`
- SHA-256: `d6aeafb1ad7ec428cc0b4849fd45b0dfb1349e00142b7911d63b407aa4c1a73a`
- Preferred image base: `0x140000000`
- Runtime image base observed on 2026-06-06: `0x7FF7AB420000`

## Binary

- Normal PE64 image with relocations, unwind data, RTTI strings, and no obvious
  packer.
- Approximately 124,000 functions are represented in `.pdata`.
- No anti-cheat module was observed in the running process.
- `tll.pdb` is referenced by GUID
  `CA677CF5AE1A488CB08A65F171DE48891`, but is not available from Microsoft's
  public symbol server.

## Retained Network Runtime

The TLL executable retains substantial Uncharted 4 multiplayer/co-op code.
This is executable code, not only dead strings.

Known command registration functions and handlers:

| Command | Registration | Handler |
| --- | ---: | ---: |
| `net-spawn-coop-buddy` | `0x14007D040` | `0x140B56090` |
| `net-force-respawn-player` | `0x14007DE20` | `0x140B5AB40` |
| `net-switch-coop-mode-enabled` | `0x14008A2C0` | `0x140BFA6E0` |

Other useful functions:

- `spawn-player` command registration: `0x140048440`
- Likely full `spawn-player` script implementation: `0x140F73910`
- Remote-player health guard: `0x14031FED0`
- `CreateLevelDebugMenu` caller/function region: `0x140EE0950`

## Character Findings

- The `NadineBuddy` runtime class is registered at `0x14004D930`, with class
  implementation/constructor entry `0x1404A0F40`.
- The retail binary contains `chloe-base`, `nadine-base`, `nadine-buddy`, and
  `NadineBuddy` references in active code.
- Function `0x140B580C0` chooses a retained multiplayer player look from a
  network-manager mode field:
  - mode 0: `coop-drake`
  - mode 1: `coop-chloe`
  - mode 2: `coop-sullivan`
- The retained network player system therefore already supports Chloe, but
  not Nadine as a stock network-player look.
- Nadine remains represented by a real `NadineBuddy` class, so the two likely
  implementation paths are:
  1. make a network player use a Nadine-compatible look/controller; or
  2. route remote input into the existing `NadineBuddy` instance.

The first path may reuse more of the original multiplayer replication. The
second preserves campaign scripts more naturally. Both require a live-level
object inspection before choosing.

## Live Rooftop Scene

Runtime scan on process `17400`, module base `0x7FF6F1850000`:

- `Player`/Chloe object: `0x10E02FF000` at the baseline scan
- Active `NadineBuddy`: `0x10E05AC980`
- Secondary/scripted `NadineBuddy`: `0x10E08BFB80`
- `Player` vtable RVA: `0x2A4BFE8`
- `NadineBuddy` vtable RVA: `0x2949638`

Object addresses are allocations and may change after checkpoints or scripted
transitions. The scanner identifies them by vtable and inline process name.

Transform data appears at common offsets in both object layouts:

- `+0x240`
- `+0x250`
- `+0x280`

The vehicle-scene comparison confirmed the following:

- `+0x240` is a stable world-space transform/position block. Moving Chloe
  approximately 30 world units changed this block on `Player`, while the
  corresponding `NadineBuddy` block remained stationary in the parked car.
- `+0x250` matches `+0x240` for an unparented `Player`, but is local-space for
  a `NadineBuddy` parented to the vehicle.
- `+0x280` follows the world position with an approximately one-unit
  root/capsule offset.

The common vtable slot 87 (offset `0x2B8`) on both `Player` and
`NadineBuddy` points to `0x141BB8320`. That function reads and rewrites all
three blocks, including writes at `0x141BB8602`, `0x141BB860B`, and
`0x141BB8614`. Its calling convention and role must be established before
using it as a transform setter.

## Unparented Nadine Write Test

In an active combat scene, the sole live `NadineBuddy` had no parent handle
and its transform blocks at `+0x130`, `+0x240`, and `+0x250` agreed. Two
one-shot writes changed `+0x130.x` by `+1.0`, with object identity and parent
state validated immediately before each write.

No visible teleport occurred. Nadine continued walking under AI control.
Therefore `+0x130` is transform state/cache rather than an authoritative
movement input; the locomotion update overwrites it before presentation. Do
not use repeated direct writes to this field. The next target is either the
Nadine movement-controller input path or an engine set-transform/teleport
function that also updates physics and animation state.

## Native Teleport Test

`NadineBuddy` vtable slot `0xC48` points to `0x140416B20`. Its effective
signature is:

`Teleport(NadineBuddy *this, Locator *destination, uint32_t mode,
bool updatePhysics)`.

A one-shot game-thread vtable hook successfully called this method. Nadine
visibly despawned and reappeared near Chloe, confirming that the native
teleport path works and updates the character beyond the cached transform
fields. The first test used the wrong locator source, however: the virtual
locator getter's returned pointer was treated as the output buffer. The next
build uses the output buffer explicitly and replaces only its position with
the confirmed `NadineBuddy+0x240` world position plus a small offset.

The corrected native teleport moved Nadine away successfully. A few seconds
later the game teleported her back to Chloe through the independent
`Buddy::UpdateTeleportToPlayer` path. That path checks the global byte at
RVA `0x3D441AD`; value `1` exits before buddy catch-up logic. The retained
debug option `Disable Buddy Teleport` and script command
`disable-buddy-teleport` use the same flag. A verified runtime toggle now
sets it to `1`, and the next diagnostic DLL sets it before testing teleport.

Further testing showed that the apparent return to Chloe happened during the
native teleport itself even with the catch-up flag disabled. The destination
layout requires both position at `+0x30` and a valid orientation quaternion at
`+0x40`. The test buffer supplied position but left orientation zero, causing
the engine to reject it and use a fallback near the player. The current DLL
copies Nadine's quaternion from `object+0x140` into `destination+0x40`.

The corrected test succeeded visibly: Nadine moved approximately three metres
from her own position without falling back to Chloe. This confirms the
following native control primitives:

- locate the active `NadineBuddy` instance;
- read a stable world position and orientation;
- disable automatic buddy catch-up teleport;
- invoke the native NPC teleport path while preserving physics and animation
  state.

Native teleport is suitable for initial spawn, checkpoint recovery, and large
network corrections. It should not be used as the normal per-frame movement
mechanism; smooth remote control requires feeding the locomotion controller or
an equivalent NPC move command.

## Native Nadine Locomotion

The native NPC move command at `0x1403F6890` and navigation-goal builder at
`0x141DF2D60` were recovered and tested on build `1.4.21058.0`.

```cpp
void BuildNavigationFrame(
    void* output,
    const float* worldPosition,
    uint32_t projectionMode,
    float navigationRadius
);

void NpcMoveTo(
    void* npc,
    const void* navigationFrame,
    const void* facing,
    const void* moveParameters,
    uint32_t movementMode,
    bool allowRun,
    bool unknownFlag,
    void* optionalContext,
    int32_t optionalId,
    bool updatePhysics
);
```

The navigation radius is read from
`*(float*)(*(uintptr_t*)(NadineBuddy + 0x15B8) + 0x1C8)`. The test preserved
Nadine's current facing and movement parameters, projected her world position
plus `3.0` on the X axis onto navigation, and issued `NpcMoveTo` from her
game-thread update callback.

The result was confirmed visually: Nadine ran smoothly toward the requested
point using normal navigation and animation. This provides a native movement
path for remote control without transform writes or repeated teleports.

## Debug Command Caveat

The documentation and registration records for `process-list` and
`manual-cam` remain, but their registered callbacks are release stubs:

- `process-list`: `0x141F68DB0` returns zero
- `manual-cam`: `0x141F68E10` returns zero

The network handlers listed above are large, non-trivial implementations and
are therefore higher-value targets.

## Next Dynamic Check

Attach x64dbg while a gameplay checkpoint with Chloe and Nadine is loaded.
Set one-shot breakpoints on the three network handlers above, then determine
whether their command registrations are reachable from the retail script
dispatcher. No handler should be called manually until its argument schema and
required network-manager state are understood.
