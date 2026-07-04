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

Current project target:

- not "remote-drive Nadine as a better NPC"
- but "make Nadine a full second-player gameplay participant"
- meaning:
  - independent control ownership
  - independent camera ownership
  - traversal / interact / combat participation
  - world and script systems reacting to Nadine the same way they react to
    Chloe-class actors

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

### Retained Player-Spawn Stack

The retained `spawn-player` and co-op buddy paths converge into a real
player/network-player stack rather than the campaign buddy locomotion path.

Current working map:

- `0x140048440`: registers the `spawn-player` script command entry.
- `0x140F73910`: large retained `spawn-player` implementation.
  - If a retained spawn descriptor is present, it copies that block through
    `0x140F73C90` and then calls `0x1410F1910`.
  - Otherwise it falls back to `0x1410F3350`.
- `0x1410F1910`: retained spawn path with an explicit descriptor block.
- `0x1410F3350`: fallback spawn path without an explicit descriptor block.
- `0x1410F14C0`: central spawn-commit path shared by both branches.
  - It accepts resolved look/transform/context data and writes the resulting
    live player handle into the global retained slot table at `0x143EEBE80`.
- `0x1410F1840`: resolves a retained-player mode id from a `(handle,id)` pair.
  - Seeds the output from global default `0x143EEBE7C`.
  - Can override that mode from the retained pair's runtime object at `+0x48`
    through shared callback `0x143682D30`.
- `0x1410F18A0`: resets retained-player runtime globals.
  - Clears the global block rooted at `0x143EEC160` and
    `0x143EEC220..0x143EEC262`.
  - Seeds the default scalar at `0x143EEC240`.
- `0x14007D040`: registers `net-spawn-coop-buddy`.
- `0x140B56090`: large retained co-op buddy spawn handler.
  - It resolves context, copies roster/state payload, computes a transform,
    and then calls `0x141D22420`.
- `0x141D22420`: resolves a live entity from a retained roster/slot entry.
  - The function walks nested lists, tests per-entry flags, and can recurse
    until it finds a valid active entity handle.

This is the first concrete evidence that TLL still ships a real retained
player/network-player instantiation layer which is upstream of locomotion.
That is a better long-term target for "full second player" work than further
NPC `MoveTo` tuning.

The explicit retained-spawn branch is now clearer too:

- `0x1410F1910`
  - resolves mode and runtime-global state through:
    - `0x141B39750`
    - `0x1410F1840`
    - `0x141C077B0`
  - then commits through `CommitResolvedPlayerSpawn`
  - after commit, if the live retained entity resolves, it enters a heavier
    post-spawn runtime path that includes:
    - `0x141D21C80`
    - `0x141D21DE0`
    - `0x141C88050`
    - and later gating around live-entity byte `+0x4268`
  - it also reuses the same `"Player"`-labelled descriptor material from
    `0x1428D8960`

- `0x1410F3350`
  - fallback spawn path follows the same broad pattern:
    - builds defaults through `0x141C077B0`
    - initializes shared player-labelled registries through `0x141D18CF0` and
      `0x1417651D0`
    - commits through `CommitResolvedPlayerSpawn`
    - resolves the live entity and, on the active path, sets
      `live_entity + 0x4268 = 1`
  - implication: explicit retained spawn and fallback retained spawn converge
    not only at `CommitResolvedPlayerSpawn`, but also in a common post-spawn
    player-labelled runtime path

This is the strongest current bridge from retained spawn into probable
player/action ownership. The next best reverse-engineering targets are no
longer camera-only helpers, but the post-spawn consumers:

- `0x141C88050`
- `0x141D21C80`
- `0x141D21DE0`

That post-spawn cluster is now partially mapped:

- `0x141D21DE0`
  - resolves a live entity from the retained/runtime pair stored at
    `object + 0x60`
  - validates `pair + 0x30/+0x38` against `entity + 0x8 -> +0xBC`
  - marks the resolved entity active through byte `+0x72`
  - returns the live entity pointer on success

- `0x141D21E20`
  - same retained-pair resolution as above
  - then gates the resolved entity through a second runtime registry bitset
    using global index `0x146920B54`
  - returns null if the retained pair resolves but the entity is not enabled in
    that registry mask

- `0x141D21C80`
  - validates the retained pair at `object + 0x60`
  - resolves the live entity and marks it active
  - then, depending on the `edx` mode flag, prepares a temporary descriptor
    through either `0x1417C32A0` or `0x1417C35A0`
  - if a live entity is still present, calls entity virtual slot `+0x538` with
    that descriptor
  - returns a boolean-like success result
  - implication: this is not just "get the entity"; it is a retained-pair to
    entity-runtime dispatch bridge

- `0x141C88050`
  - widely reused outside retained spawn
  - builds a temporary `0x200`-byte runtime record, then dispatches it through
    a global mode-selected table based on state under `0x143FE1088`
  - explicit retained spawn feeds it the same player-labelled string/material
  used elsewhere in the runtime
  - implication: this looks like a shared player/runtime record publisher, not
    a spawn-local helper

- `0x141D21F90`
  - very widely used across the binary, far outside retained spawn
  - resolves the gated live entity through `0x141D21E20`
  - hashes a fixed runtime selector block, looks up selector metadata through
    `0x140159C00`, and uses that selector to call entity virtual slot `+0x530`
  - writes a compact `0x30`-byte output record with default scalar/vector
    fields and an optional returned id pair from the queried entity-side object
  - implication: this is likely a shared runtime/action query builder on top of
    the retained/live entity bridge, and is a better next target than more
    camera-only plumbing

- selector strings observed on this query/attachment layer
  - `ParentToJoint`
  - `spawn-rope`
  - `interactable-def`
  - `rope-holster-offset`
  - `rope-bot`
  - `interact-hook`
  - `winch-hook`
  - implication: this is not a generic math helper layer. It is a selector-
    driven runtime attachment/action query API over live entities.

- `0x141D231E0`
  - resolves the retained/runtime pair at `object + 0x60`
  - queries the attachment-capable runtime object using the selector
    `ParentToJoint`
  - falls back to a cached/base transform path when the specialized runtime
    object is absent
  - composes the resulting transform with the owner's local transform
  - implication: this is a retained-entity attachment transform builder, not
    just another spawn-local helper

- `0x141D21C80`
  - now best understood as entity-side runtime command dispatch
  - after retained-pair validation it builds a temporary descriptor and calls
    entity virtual slot `+0x538`
  - implication: the `+0x530 / +0x538` pair looks like a query/command
    interface on top of the live entity, which is structurally much closer to
    action ownership than anything in the earlier NPC locomotion path

- `0x1417C4800`
  - helper immediately under several selector-driven consumers
  - checks whether a hashed selector id is enabled in a target bitset registry
  - uses a global hash-bucket table rooted at `0x14383F198`
  - implication: selector-driven runtime queries are not free-form string
    dispatch; they are gated by an explicit capability/availability registry

Two more usage observations:

- `0x1413FEC60` and `0x1413DADA0`
  - are higher-level consumers of the same retained-entity query layer
  - they sit above selector hashing, retained-pair resolution, and
    `CheckSelectorHashEnabledInBitsetRegistry`
  - they mix selector-driven queries with broader gameplay-type strings such as
    `DriveableVehicle` and `DriveableBoat`
  - implication: this entity runtime interface is shared across traversal,
    vehicles, hooks/rope, and interactables rather than being a narrow rope-only subsystem

- not every xref to a selector string is a gameplay consumer
  - several small callsites around:
    - `0x1400A6DE0`
    - `0x14010C900`
    - `0x141358FE2`
  - first resolve the string through callback `0x143682D30`, then publish the
    resulting hash/metadata into global tables
  - selectors observed on that registration-style path include:
    - `player-set-rope-magnet-stick`
    - `interactable-set-interaction-regions`
    - `interact-avail`
    - `interact-use-adv`
  - implication: the binary has both:
    1. startup/registration paths for selector hashes
    2. real runtime consumers that later use those hashed selectors through
       `+0x530 / +0x538`

- a more gameplay-facing confirmed consumer exists around `0x14055D9D7`
  - it drives the string `npc-holster-weapon`
  - combines selector-driven state with NPC/weapon state and then branches into
    further runtime behavior via `0x141DE55E0` or entity virtual `+0xB8`
  - implication: the selector system already reaches weapon/holster behavior,
    not only rope/interactable transforms

- `0x141DE55E0`
  - is not just a scalar getter
  - after a guard through object virtual `+0x5E8`, it resolves the nested
    object behind `owner + 0x15C8 -> virtual +0x58` and then dispatches
    action state through that nested object's virtual `+0xA0`
  - implication: the holster path is already landing in a deeper action-facing
    object stack rather than ending at selector logging

- `0x141DE5630`
  - resolves the nested control object behind `owner + 0x15C8 -> virtual +0x50`
  - then tail-calls nested virtual `+0xA8`

- `0x141DE5660`
  - same nested control-object resolution through `virtual +0x50`
  - then tail-calls nested virtual `+0xB0` when the nested object exists

- `0x141DE5690`
  - hashes the selector string `injured-layer`
  - scans the companion-owned selector table behind `owner + 0x1A0`
    - entries at `+0x298`
    - count at `+0x2A4`
    - stride `0x18`
  - and writes a `0/1` flag byte into the matching entry
  - implication: this layer is not only generic action dispatch; it also owns
    named gameplay state flags for the nested companion/control stack

- `0x141DE57F0`
  - hashes the same `injured-layer` selector and reads back the matching flag
    byte from the same table
  - implication: `0x141DE5690/0x141DE57F0` form a real set/get pair for a
    named behavior or damage-layer gate

- `0x141DF0000`
  - reads the type/control object behind `owner + 0x15C0`
  - compares it against the singleton at `0x143738130`
  - if needed, gates through owner virtual `+0xC20`
  - then returns either:
    - an indexed scalar from `owner + 0x1A38` using index `owner + 0x19E0`, or
    - the fallback scalar at `owner + 0x1760`
  - implication: the outer owner already tracks per-action or per-state scalar
    data adjacent to the nested control stack

- `0x141DF00A0`
  - resolves `owner + 0x15C8 -> virtual +0x78`
  - then tail-calls the returned nested object's virtual `+0xA8`
  - falls back to the default float at `0x142BE8FC4` when no nested object is
    present
  - implication: the same nested control family already exposes float-valued
    query paths, not only bool/state dispatch

- `0x141DE5980`
  - resolves `owner + 0x15C8 -> virtual +0x20`
  - then tail-calls the returned nested object's virtual `+0x178`
  - implication: the compact nested control interface is wider than
    `+0xA0/+0xA8/+0xB0` and likely includes higher-level action/query hooks

- the immediately adjacent cluster around `0x141DE59B0..0x141DE5AA0` expands
  that same nested-control family further:
  - `0x141DE59B0`
    - resolves `owner + 0x15C8 -> virtual +0x20`
    - forwards caller blob pointer `rdx` into nested virtual `+0x188`
    - returns the original blob pointer
  - `0x141DE59E0`
    - reads `owner + 0x15C0 -> +0x14C` into caller output
  - `0x141DE5A00`
    - returns cached state id `owner + 0x1748`
  - `0x141DE5A10`
    - writes `edx` into `owner + 0x1748`
    - then resolves `owner + 0x15C8 -> virtual +0x28`
    - and forwards the same id into nested virtual `+0x168`
  - `0x141DE5A40`
    - resolves `owner + 0x15C8 -> virtual +0x20`
    - forwards `edx` into nested virtual `+0x158`
  - `0x141DE5A70`
    - same shape, but nested virtual `+0x160`
  - `0x141DE5AA0`
    - resolves `owner + 0x15C8 -> virtual +0x20`
    - then tail-calls nested virtual `+0xE0`

Implication:
- the object behind `+0x15C8` is not a tiny three-method hook.
- It already exposes a fairly broad action-state interface with:
  - state-id forwarding (`+0x158/+0x160/+0x168`)
  - query/action dispatch (`+0xA0/+0xA8/+0xB0/+0xE0`)
  - higher-level hooks (`+0x178/+0x188`)
- and the outer owner keeps explicit local state for it at `+0x1748`, plus a
  parallel type/control object at `+0x15C0`.

Current best interpretation:
- the object behind `owner + 0x15C8` exposes a compact action/control interface
  through:
  - `+0xA0`
  - `+0xA8`
  - `+0xB0`
  - `+0xE0`
  - `+0x158`
  - `+0x160`
  - `+0x168`
  - `+0x178`
  - `+0x188`
- and the outer owner uses `+0x50/+0x58` to reach the right nested runtime
  object before dispatch, plus `+0x20/+0x28/+0x78` for sibling query/state paths
- implication: this is materially closer to a reusable gameplay control layer
  than the earlier retained-camera or NPC locomotion paths

- `0x14030CD60`
  - despite sitting in the `npc-holster-weapon` path, it is only a tiny
    staging stub that copies `r8/r9` to shadow stack slots and returns
  - implication: it is not the real behavior sink and can be ignored for
    controller/action ownership work

Direct caller recovery through `objdump` is still more reliable than IDA xrefs
on this database. For the current nested-control layer that yielded:

- `0x141DE55E0`
  - callers at `0x140484D1A`, `0x14055D9F5`, `0x14055DC44`, `0x1406C11C3`,
    `0x14124E82C`, `0x141285C72`, `0x141DE7762`, `0x141DE7FDF`
- `0x141DE5660`
  - caller at `0x1404BCE0A`
  - plus sibling tail-jumps to `0x141DE55E0` at `0x1404BCE74` and `0x1404BCE86`
- `0x141DE5690`
  - callers at `0x141DE738F` and `0x141DE8DA5`
- `0x141DF0000`
  - broad caller set including `0x1403D3EF9`, `0x1403DE3C6`, `0x1404027A4`,
    `0x14041ED6B`, `0x14049E354`, `0x1404A01B7`, `0x1404A5BE0`, `0x1404A97AB`,
    `0x1405078BF`, `0x140507CE2`, `0x140507CF0`, `0x140660B03`, `0x14069C25D`,
    `0x14122AB11`, `0x14122AB2D`, `0x14124ED63`, `0x14124F7DE`, `0x141DEC8A6`,
    `0x141DECE75`, `0x141DF0480`, `0x141E2AB4D`, `0x141FD7625`, `0x14213D3F0`

Two local consumers of `SetNestedInjuredLayerFlagByCompanionTable` matter:

- around `0x141DE738F`
  - a runtime threshold compare sets `edx = 0/1`
  - then `injured-layer` is updated through `0x141DE5690`
  - immediately after, fields at offsets `+0x84/+0x88` inside
    `owner + 0x1A0 -> +0x58 -> *` are forced to `-1.0f`
- around `0x141DE8DA5`
  - the same threshold/update pattern appears again, but one branch reuses the
    incoming state in `r14d`

Implication:
- the `injured-layer` table is tied to live gameplay state transitions, not a
  passive metadata registry
- and the nested `+0x15C8` control family still sits immediately next to those
  transitions, which keeps it a strong candidate for the real action-ownership
  bridge we need

One more caller pattern is already useful:

- around `0x1404BCDE0`
  - one branch resolves `owner + 0x15C8 -> virtual +0x58` to a nested runtime
    object, then calls `ForwardToNestedControlObject_B0`
  - if the returned value is:
    - `0`: the code falls through to entity virtual `+0xB8` with `edx = 1`
    - `1`: the code calls entity virtual `+0xB8` with `edx = 0`
    - anything else: it exits without the entity-side fallback
  - the sibling branch goes through `owner + 0x15C8 -> virtual +0x50 -> +0xB0`
    and then tail-jumps into `DispatchNestedActionStateViaControlObject_A0`
    with `edx = 0/1`

Implication:
- nested virtual `+0xB0` already behaves like a small decision/ownership gate,
  not a passive getter
- and the outer code is explicitly arbitrating between nested-control handling
  and direct entity action dispatch through virtual `+0xB8`

Two direct callers of `SetControlActionStateIdAndForward_168` already narrow
its role:

- around `0x14058BA0C`
  - the write to `owner + 0x1748` happens only when bit `owner + 0x27D0 bit0`
    is set and the source state at `owner + 0x24C8` is `< 3`
  - after that the caller immediately continues into a larger behavior/action
    setup path through `owner + 0x15B8`

- around `0x1406B412E`
  - the update is gated by `owner + 0x15C0` fields `+0x80` and `+0x120`
  - it is skipped when the cached state `owner + 0x1748` already matches `esi`
  - it only writes/forwards when the requested state is `< 3`
  - otherwise the caller falls back to sibling handlers at `0x1406B3640` or
    `0x1406B3940`

Implication:
- `owner + 0x1748` is a real low-cardinality control/action mode, not a loose
  gameplay counter
- and the nested object's `virtual +0x168` is part of an explicit state
  synchronization path between the outer owner and the deeper action/control
  stack

That state path now sits inside a clearer router:

- `0x1406B3D70`
  - seeds a probe flag byte and scalar through `0x1406B2390`
  - if the probe flag comes back set, it calls `0x1406B3640`
  - otherwise it passes the returned state id in `eax/edx` into `0x1406B3940`
  - afterward it clears bit `0x80` in `owner + 0x1D57`
  - and pushes the same scalar into `owner + 0x1A0 -> +0x58 -> * + 0xD0`

- `0x1406B3640`
  - is the heavier sibling path
  - builds a large temporary descriptor stack
  - allocates/attaches intermediate objects
  - updates owner subgraphs around `+0x2830` and `+0x2010`
  - and clears bit `0x8` in `owner + 0x15AA` on the successful branch

- `0x1406B3940`
  - is the more compact sibling path
  - takes the low-cardinality state id in `edx`
  - rebuilds local transforms and runtime state from the owner
  - consumes current locomotion payload around `+0x24B0..+0x24C8`
  - and then rejoins the same broader routing/update flow

The two direct caller sites on the compact/state-id path still matter:
- `0x1406B3DBD`
- `0x1406B418E`
- plus the follow-up case at `0x1406B41E6` when a side object under
  `owner + 0x1F80 -> +0x30` reports mode bits resolving to `2`

Implication:
- the current `0/1/2`-style action/control mode is not cosmetic
- it already decides which of two substantial control-routing backends will
  rebuild the owner's runtime state
- and the compact branch explicitly bridges from the cached mode id into the
  current locomotion payload, which keeps this path highly relevant for
  replacing the laggy NPC-control backend with something closer to player
  ownership

Upstream of that router, `0x1406B2390` is now constrained enough to name by
behavior:

- inputs
  - owner object in `rcx`
  - active runtime/locomotion owner in `rdx`
  - output byte pointer in `r8`
  - output scalar pointer in `r9`

- what it reads
  - owner transform at `owner + 0x100`
  - pending locomotion command block at `runtime + 0x2470`
  - command type / related fields from that block
  - current control object at `owner + 0x15C0`
  - cached mode at `owner + 0x1748`

- what it does
  - computes a world-space delta and derived scalar from owner transform versus
    the pending locomotion command data
  - resolves a threshold/config record through `0x141765580`
  - compares the derived scalar against multiple thresholds in that record
  - writes the probe flag byte through `r8` when certain thresholds or control
    object gates are hit
  - optionally writes a float through `r9`
  - returns a low-cardinality state id in `eax`

- observed return behavior
  - returns `0` immediately on one threshold-exceeded branch while also forcing
    the probe flag byte to `1`
  - otherwise maps parts of the threshold record through small states:
    `0`, `1`, `2`
  - in fallback branches it can reuse the cached state from `owner + 0x1748`
  - for one command-type-`3` path it may also return the cached/derived values
    from the pending command block itself

Implication:
- `0x1406B2390` is the real bridge between locomotion geometry and the control
  routing stack
- the laggy NPC locomotion block at `+0x2470` is already being interpreted here
  into small control modes and threshold gates
- so if we want to replace the current buddy/NPC feel with player-like control,
  this function is one of the highest-value places to keep mapping next

`0x141765580` also clarified:

- it is not the threshold math itself
- it is a broader registry/config resolver that hashes and resolves records
  through the strings:
  - `global`
  - `*start-task-node*`
- on hit it returns a direct table/record pointer
- otherwise it falls back through global registries and task-node structures

Practical implication:
- `ProbeControlRoutingThresholdsAndSelectStateId` does the geometric/control
  decision-making itself
- but the thresholds/state record it consumes come from a reusable global
  task-node/config layer, not from hardcoded locomotion constants
- that means the current control-mode routing is already tied into a higher
  authored runtime system, not only raw NPC steering

Additional consumers of `ResolveGlobalTaskNodeThresholdRecord` sharpen the
record shape:

- `0x1403E03C1`
  - computes a CRC/hash over runtime bytes, calls `0x141765580`, stores the
    returned pointer into `object + 0x8`, and treats the result as valid only
    when both `record != null` and `object + 0x10 != null`

- `0x1406AD545`
  - same CRC/hash -> resolver pattern
  - stores the returned pointer into `object + 0x8`
  - returns success immediately when the pointer is non-null

- `0x140462134`
  - resolves the record, then branches on `*(u32*)record`
  - observed first-level values already behave like a small mode enum:
    - `0`
    - `1`
    - `2`
    - `3`
  - this is the strongest evidence so far that the record starts with a mode id
    or mode kind discriminator at offset `+0x0`

Current best constraints for the record consumed through `r10` in
`ProbeControlRoutingThresholdsAndSelectStateId`:
- `+0x0`   : small mode/discriminator value
- `+0x4`   : threshold float
- `+0x8`   : threshold float
- `+0xC`   : threshold float used in the early “return 0 and force probe flag”
             branch
- `+0x10`  : small state/mode selector used to derive `0/1/2`
- `+0x14`  : optional scalar-output gate/count
- `+0x98`  : enable/allow byte used in one early branch
- `+0x99`  : secondary gate byte
- `+0x9B`  : additional gate byte used with one threshold path

This is still conservative, but it is no longer just “some blob of thresholds”.
It already behaves like:
1. mode discriminator
2. multiple threshold floats
3. a small control-mode selector
4. a few byte gates

The large consumer around `0x140462134` pushes this further:

- after resolving the record, it branches directly on `*(u32*)record`
- the `mode 0/1/2/3` branches are not cosmetic; they drive materially different
  selection/update paths
- multiple branches:
  - rebuild per-entry hash buffers
  - query runtime ids through `0x140159AA0` / `0x1401599F0`
  - set or clear bitmaps in `0x143D44880`
  - and populate per-owner state blocks around `+0x500/+0x510`

This means the record is already controlling authored runtime selection logic,
not just one threshold compare for movement.

Additional practical constraints:
- `mode 1` and `mode 2` branches both perform repeated
  `hash -> record -> runtime id -> bitset update` loops
- one branch ORs `0x80` into entries of `0x143D44880`
- another branch ANDs entries with `0x7F`
- later branches build transform/state output blocks and flag bytes near
  `+0x500/+0x510`

Mode-specific behavior is now clearer from the same large consumer:

- `mode 0`
  - immediately falls into a large branch that builds a nontrivial state block
    and later populates transform/output data near `+0x500/+0x510`
  - it does not do the same candidate-mask walk as modes `1/2`
  - instead it enters the more direct authored-build path that later feeds the
    shared post-processing path through `+0x500/+0x510`

- `mode 1`
  - runs a repeated selection loop over candidate ids
  - resolves hashed/runtime objects
  - sets bits in owner-local selection masks
  - then reduces that mask set toward a chosen candidate
  - if a candidate is finally chosen, its resolved runtime byte in
    `0x143D44880` gets the high bit ORed with `0x80`
  - subsequent passes can also clear that bit with `&= 0x7F`, so mode `1`
    behaves like a choose/promote path rather than a pure filter

- `mode 2`
  - also runs repeated selection loops
  - but is more aggressive about setting/clearing the high bit in the global
    byte table at `0x143D44880`
  - and then folds the result back into owner-local masks
  - the first pass fills owner-local bitsets only when the resolved runtime
    byte is non-negative
  - the follow-up pass can drop back into the same global-byte mutation path,
    but only after popcount/reduction over the provisional owner-local mask set

- `mode 3`
  - enters yet another large path with per-entry resolution and follow-up state
    building rather than the simpler mode-1/2 loops
  - it is still an authored-build path, but it is not the same straight-line
    output builder as mode `0`

The branch mapping is now firm:

- `*(u32 *)record == 0` -> `mode 0`
- `*(u32 *)record == 1` -> `mode 1`
- `*(u32 *)record == 2` -> `mode 2`
- `*(u32 *)record == 3` -> `mode 3`

The large consumer at `0x140462020` is therefore best treated as:

- `ExecuteAuthoredControlSelectionDescriptorModes`

and not as a random helper around `ResolveGlobalTaskNodeThresholdRecord`.

Direct callsite search with `objdump` sharpens this further:

- `0x140463AAA -> 0x140462020`
- `0x140463C15 -> 0x140462020`

Those two wrappers are useful because they both latch the result bit back into
`owner + 0x30 bit 0`, rather than treating the descriptor consumer as a pure
query helper.

- the smaller wrapper at `0x140463A70`
  - `RefreshOwnerDecisionBitFromAuthoredDescriptor`
  - refreshes owner/runtime pointers from `owner + 0x8 -> +0xA0`
  - seeds `owner + 0x20/+0x28`
  - calls `ExecuteAuthoredControlSelectionDescriptorModes`
  - then folds `al` into `owner + 0x30 bit 0`

- the larger wrapper at `0x140463AC0`
  - `UpdateOwnerStateThroughAuthoredDescriptorAndRuntimeGates`
  - builds a wider context around `runtime + 0xA0`
  - consults selector capability gating through `CheckSelectorHashEnabledInBitsetRegistry`
  - conditionally resolves retained slot `0` through `0x1410F0D10`
  - compares/transfers transform state
  - and only then calls `ExecuteAuthoredControlSelectionDescriptorModes`
    when `owner + 0x30 bit 0` is not already set
  - after that it keeps running through additional runtime gates driven by:
    - `owner + 0x30 bit 0x4`
    - runtime flags at `runtime + 0x238` / `runtime + 0x57C`
    - a mode-like field at `rbx + 0x8`
    - cached candidate/state fields around `rbx + 0x530`, `+0xC8`, `+0x561`

That makes the descriptor layer look more like a cached owner-state decision
path than a one-off movement preset lookup.

The two wrapper functions are also not isolated in code. A direct binary search
for their little-endian pointers places them in a dense `.rdata` function table
at:

- `0x14293E240`

Relevant slice of that table:

- `0x14293E240 -> 0x140463A70`
- `0x14293E248 -> 0x140463FC0`
- `0x14293E250 -> 0x140459CB0`
- `0x14293E258 -> 0x140463AC0`
- `0x14293E260 -> 0x14045EB50`
- `0x14293E268 -> 0x142C828A8`
- `0x14293E270 -> 0x1404648D0`
- `0x14293E278 -> 0x140186A50`
- `0x14293E280 -> 0x1404641A0`
- `0x14293E288 -> 0x140464710`
- `0x14293E290 -> 0x140459CB0`
- `0x14293E298 -> 0x1404643C0`
- `0x14293E2A0 -> 0x14045EB50`
- `0x14293E2A8 -> 0x142C828F8`

Implication:

- `RefreshOwnerDecisionBitFromAuthoredDescriptor` and
  `UpdateOwnerStateThroughAuthoredDescriptorAndRuntimeGates` belong to a wider
  owner-state dispatch family, not to a one-off helper path.
- the nearby siblings at `0x1404641A0`, `0x1404643C0`, `0x140464710`,
  `0x1404648D0` should be treated as the next likely owner-family methods,
  especially because they live in the same pointer table and share the same
  surrounding constants/constructor region near `0x14293E230`.

That follow-up mapping is now partly in place:

- `0x1404641A0`
  - resolves two selector ids through `0x140159C00`
  - both use the same `runtime + 0xD4` registry object and authored hash blocks
  - then pushes the pair into shared owner/runtime state through `0x140461500`
  - this looks like a compact selector-state publish step, not locomotion

- `0x1404643C0`
  - samples runtime timing/state from `runtime + 0xA0`
  - derives a scalar through `0x140159B40`
  - runs the same selector-id resolution machinery
  - then either pushes state through `0x140461500` or falls back to
    `owner + 0x40` state-blob notifications
  - this looks like a threshold-gated update sibling of the same owner family

- `0x140464710`
  - clears/reset-flushes two already familiar runtime blocks:
    - `runtime + 0x2830`
    - `runtime + 0x15E0`
  - emits a small debug/state record through `runtime + 0x4038` when present
  - and flushes the pending node at `owner+8 -> 0x30` back into
    `runtime + 0x1F78`
  - this is clearly a reset/teardown member of the same owner family

This strengthens the class-family interpretation:

- `0x140463A70` / `0x140463AC0` are the decision/update members
- `0x1404641A0` / `0x1404643C0` are selector-state publish/update members
- `0x140464710` is the reset/teardown member

So the table at `0x14293E240` is not an arbitrary callback list; it is behaving
like a coherent owner-state dispatch family.

The next large sibling at `0x140464930` confirms that this family also owns a
materialize/build path:

- `0x140464930`
  - queries an actor-like runtime object through `runtime + 0xA0 -> virtual +0xA10`
  - rejects the path unless that object advertises the familiar type tag
    `0xAC710202`
  - builds a large structured state block on the stack around `rbp + 0x380`
  - seeds that block from:
    - actor state at `object + 0x128`
    - runtime state at `runtime + 0xD8`
    - global/runtime pair data from `0x146467208`
  - pushes that descriptor through `0x141C9ED50`
  - then builds an intermediate object via:
    - `0x14048AE80`
    - `0x14048F180`
    - allocation + ctor through `0x14048B270`
  - and finally attaches/materializes it back through `0x14048F260`

This is the strongest indicator so far that the owner-family is not just
post-update logic. It has a full construction/materialization branch around a
live actor-like runtime object.

The supporting bridge below it is now clearer:

- `0x141C9ED50`
  - takes a structured descriptor plus a target runtime-slot object
  - if the descriptor type matches a small built-in table, it routes directly
    through `0x1417F8910`
  - otherwise it scans a live registry/roster, activates matching entities via
    `entity virtual +0xE0`, and then routes them through the same
    `0x1417F8910` path
  - so this is not a pure serializer; it is a descriptor-to-live-runtime
    dispatch bridge

- `0x14048B270`
  - constructs a `0x4D0` object
  - sets vtable `0x142941FB0`
  - initializes an embedded subobject at `+0xB0` through `0x14048AE80`
  - optionally copies a compact `0x20`-byte state header plus virtual payload
    from the source descriptor
  - this looks like the materialized owner-state object builder, not a generic
    container

- `0x14048B200`
  - is a thin allocation wrapper for the same `0x4D0` variant
  - if given an existing descriptor pointer it just forwards into
    `0x14048B270`
  - otherwise it allocates `0x4D0` bytes through the shared allocator and then
    calls `0x14048B270`
  - so `0x14048B270` is the real builder and `0x14048B200` is just the
    convenience allocation front-end

- `0x14048F260`
  - attaches that materialized object back to `owner + 0x30`
  - syncs it against `runtime + 0x1F78`
  - stores the owner pointer at `object + 0x28`
  - stores runtime timing at `object + 0x38`
  - toggles flag bits in `object + 0x8` and updates its local state word
  - then calls the object virtual `+0x60` activation hook

Cross-checking callers matters here:

- `0x14048F260` is used extremely widely across the executable, not just from
  the `0x14293E240` owner-family table.
- `0x14048B270` is much narrower and shows up as the constructor-side pair for
  a subset of those attach paths.

Practical implication:

- `0x14048F260` should be treated as a shared attach primitive for materialized
  owner/runtime state objects.
- `0x14048B270` is one concrete builder for a `0x4D0` variant of that shared
  state object family.

The neighboring lifecycle helpers make that shared-family interpretation much
stronger:

- `0x14048F180`
  - is the matching detach primitive for `owner + 0x30`
  - if the old object is marked active it calls its virtual `+0x68`
  - writes the low `0x0FFFFFFF` bits of `owner+0x10` back into
    `runtime + 0x1F78 + 0xC`
  - clears `owner + 0x30`
  - then opens a shared update window through globals around:
    - `0x143048470`
    - `0x1430484D0`
    - `0x1430484E8`
    - `0x1430484F0`
    - `0x143048504`
  - so this is not local cleanup; it is a globalized detach/swap phase

- `0x14048F320`
  - is a lighter-weight local swap helper on the same `owner + 0x30` slot
  - clears the old object's owner pointer at `+0x28`
  - clears its low active bits in `object + 0x8`
  - then binds and activates the replacement object with the same
    `object + 0x28`, `object + 0x38`, and virtual `+0x60` pattern as
    `0x14048F260`
  - unlike `0x14048F180`, it does not open the global update window

- `0x14048F410`
  - pushes a tracked live-entity pair into the shared materialized-state global
    at `0x143D45E60`
  - then conditionally notifies the current `owner + 0x30` object through
    virtual `+0x50` or `+0x48` depending on mode/flag bits
  - this makes the family look actor-tracking aware, not just container-like

This sharpens the model:

`0x14048F180 / 0x14048F260 / 0x14048F320 / 0x14048F410`

is a shared lifecycle cluster for the materialized object currently attached at
`owner + 0x30`, while:

`0x14048B200 / 0x14048B270`

describe one specific `0x4D0` builder variant that plugs into that shared
family.

The narrow callers that allocate this `0x4D0` variant are now more useful than
the attach helper itself:

- `0x14045B1C0..0x14045B4C4`
  - resolves a live entity through `0x1417C3790`
  - mirrors its `(handle,id)` into `owner + 0x470`
  - builds a transient descriptor around `rsp + 0x1E0`
  - detaches the old `owner + 0x30` object through `0x14048F180`
  - allocates the `0x4D0` variant through `0x14159F730 -> 0x14048B270`
  - reattaches through `0x14048F260`
  - and finally commits a derived state id back into the owner through
    `0x14048EF90`

- `0x140464930`
  - does the same pattern but from a heavier actor-runtime path:
    `runtime + 0xA0 -> virtual +0xA10 -> tag 0xAC710202 ->
    stack descriptor -> 0x141C9ED50 -> 0x14048B270 -> 0x14048F260`

- `0x14047A780..0x14047A841`
  - follows the same detach/build/attach sequence after resolving an entity
    through `0x141E05680` and validating it through `0x1403D67C0`
  - then immediately notifies `owner + 0x40` through `0x14189AF80`

- `0x140687920..0x140687A24`
  - reads a runtime/pose-like blob through `0x141DF35F0`
  - packages transform-ish data from `blob + 0xF0/+0x100/+0x110`
  - emits it through `0x141D6F7B0`
  - then uses the same `0x14048AE80 -> 0x14048F180 -> 0x14048B270 -> 0x14048F260`
    sequence on `object + 0x1F80`
  - this shows the `0x4D0` materialized-state variant is not isolated to the
    original owner-family region around `0x140464930`

- `0x14069BD02..0x14069BD4D`
  - sits under heavier selector/threshold gating
  - resolves a live object from `r14 + 0x24` through `0x141DF35F0`
  - turns it into a transient descriptor through `0x14048AE80`
  - detaches the current materialized object from `rsi + 0x1F80`
  - allocates and rebuilds the `0x4D0` variant
  - so the same family is reused in a selector-gated gameplay path, not only
    in the actor-runtime builder

- `0x1406B6B01..0x1406B6C2D`
  - is even more important for the main project
  - it sits directly in the owner that also carries pending locomotion payload
    at `+0x24C8/+0x24CC` and the well-known control/runtime blocks
  - copies those pending locomotion fields into the transient descriptor built
    by `0x14048AE80`
  - detaches/allocates/rebuilds/reattaches the same `0x4D0` variant through
    `rdi + 0x1F80`
  - then feeds the result back into owner-local state at `+0x2830`,
    `+0x4038`, and `+0x1FA8`

That last path matters a lot:

- it proves the shared materialized-state family is already wired directly into
  the same runtime object that owns the locomotion command block
- so the bridge is no longer only:
  `actor runtime -> materialized object`
- it is also:
  `pending locomotion/control state -> materialized object`

This significantly reduces the gap between the old movement research and the
new player-like ownership research. They are now meeting in the same
materialized-state lifecycle around `+0x1F80 -> +0x30`.

The class-local side of the `0x4D0` variant is now a little clearer too:

- vtable `0x142941FB0`
  - begins with a freeing dtor at `0x14048B3E0`
  - an in-place teardown variant at `0x14048B450`
  - and a compact header/payload clone helper at `0x14048B4A0`

- `0x1404586B0`
  - initializes the object-local materialized-state host
  - seeds an embedded sub-vtable at `object + 0x40`
  - clears owned runtime/object pointers up through `+0xA0`

- `0x1404588A0`
  - tears that host back down
  - releases owned runtime/object pointers at `+0x50/+0x90`
  - and restores the host vtable state

This means the `0x4D0` object is not just a flat copied blob. It owns:

- a compact copied header/payload region rooted at `+0xB0`
- an embedded materialized-state host at `+0x40`
- and its own teardown/copy lifecycle

The owning slot around `+0x1F80` is also much cleaner now:

- `0x1403DD180`
  - tiny getter for `owner + 0x1F80`

- `0x140409160`
  - returns the currently attached materialized object from
    `owner + 0x1F80 -> +0x30`

- `0x140409170`
  - thin owner-side wrapper over `DetachMaterializedStateObjectFromOwnerRuntime`

- `0x140409180`
  - thin owner-side wrapper over `AttachMaterializedStateObjectToOwnerRuntime`

- `0x1404091A0`
  - local clear path for `owner + 0x1F80 -> +0x30`
  - replays the object virtual `+0x68` when active
  - writes low state bits back into `owner + 0x1F78`
  - clears `slot + 0x30`
  - but does not open the global detach/update window the way `0x14048F180` does

The slot owner itself is part of a larger relocatable cluster:

- `0x1404280D0`
  - relocates `+0x1F78`, `+0x1F80`, `+0x1F88`, `+0x1FB0`, `+0x1FF8`
  - calls per-object relocate virtuals on those entries first
  - then rebases in-range pointers by the supplied delta

This is another strong sign that `+0x1F80` is a formal managed gameplay-state
slot, not an ad hoc scratch pointer.

One more bridge matters:

- `0x1403E0400`
  - asks the attached materialized object at `owner + 0x1F80 -> +0x30`
    for a small record through virtual `+0x30`
  - falls back to `owner + 0x1FF0` virtual `+0x68`
  - then falls back again through `0x1403E02C0`

So the materialized object is not write-only. Higher gameplay/control code is
already querying it for compact selection/state records.

So the decisive structural result is no longer just "there exists a builder."
It is this:

- there is a shared materialized-state lifecycle centered on `owner + 0x30`
- the `0x4D0` variant is only one plugin to that lifecycle
- and at least one of its narrow builders (`0x140464930`) is already anchored
  in a live actor-runtime path rather than in companion-only locomotion

This gives a much more complete picture of the bridge we were missing:

`actor-like runtime object -> structured descriptor -> live runtime dispatch ->
materialized 0x4D0 owner-state object -> attachment at owner+0x30`

That is already close to a real ownership/materialization path, not to buddy
AI navigation.

This is still conservative, but it is enough to justify a working enum shape:

- `0` = state-output / authored build path
- `1` = candidate-selection / mask-population path
- `2` = candidate-selection with stronger global bitset gating
- `3` = alternate authored build path

That enum naming is still provisional and should stay provisional until more
callers are mapped, but it is already better than treating the record mode as
an opaque integer.

Implication:
- the record returned by `ResolveGlobalTaskNodeThresholdRecord` is best treated
  as an authored control-selection/task-node descriptor, not merely a threshold
  preset
- and `ProbeControlRoutingThresholdsAndSelectStateId` only uses an early subset
  of that descriptor

Two more usage constraints matter:

- `0x141D21C80` and `0x141D21F90` are not spawn-exclusive.
  - `0x1413BF0E0` and several `0x140B31xxx` sites use the same retained-pair
    validation and dispatch path in broader retained runtime logic.
- `0x141C88050` is also called from the script spawn layer itself at
  `0x140F73C1C`, before either `SpawnPlayerFromRetainedDescriptor` or
  `SpawnPlayerFallback`.
  - implication: player-labelled runtime record publication is not a
    post-spawn afterthought; it is already part of the retained spawn setup

Cross-checking with `objdump` adds two more useful constraints:

- `0x1410F14C0` has a small direct caller set centered on the retained
  spawn-player region:
  - `0x1410F1A2B`
  - `0x1410F2123`
  - `0x1410F2DE1`
  - `0x1410F3541`
- `0x141D22420` is used widely across the executable and also recurses into
  itself. This is consistent with a shared roster/entity resolver rather than a
  one-off spawn helper.
- The retained player-slot table at `0x143EEBE80` is referenced broadly across
  the binary, so it behaves like a central global registry for retained/local/
  remote player handles rather than a narrow spawn scratch buffer.

IDA currently saves names/comments correctly, but the database is not yielding
useful xrefs for these addresses yet. Until that is repaired, direct `objdump`
searches remain the more reliable way to enumerate callers for this retained
stack.

### Materialized-State Readiness Gates

The next useful convergence point is no longer the builder alone but the
readiness gating that decides whether a materialized owner-state object is
allowed to refresh.

- `0x140423330`
  - owner-side minimal readiness gate
  - requires `owner + 0x15C8`
  - checks nested control readiness through:
    - `(*(owner+0x15C8)+8)->0x28 -> virtual +0x60`
    - `0x1404C1060(owner+0x15C8)`
  - if those conditions do not short-circuit the failure path, it also blocks
    on owner virtual gates `+0x4C0`, `+0x7D0`, and `+0xA28`
  - final decision goes through `owner + 0x1808 -> virtual +0x0`

- `0x140423440`
  - stricter sibling readiness gate
  - requires owner flag `owner + 0x57C bit 0x2`
  - requires `owner + 0x15C8`
  - checks:
    - `(*(owner+0x15C8)+8)->0x28 -> virtual +0x60`
    - `(*(owner+0x15C8)+8)->0xC8 -> virtual +0xE0`
    - `0x1404C1060(owner+0x15C8)`
  - then enforces a runtime cooldown:
    - calls owner virtual `+0xA10`
    - compares `owner + 0x2308` against `owner->e0->0x10`
    - uses a float-derived threshold from `0x142BE9EB0`
  - final decision again goes through `owner + 0x1808 -> virtual +0x0`

This is already enough to say that `owner + 0x1808` is not incidental state.
It is a late decision object sitting behind both readiness gates.

The small helper cluster behind `owner + 0x15C8` is also much cleaner now:

- `0x1404C1060`
  - iterates a collection with entries at `+0x8` and count at `+0x10`
  - ORs all non-null entry virtual `+0x68` results
- `0x1404C10C0`
  - same layout, but ORs virtual `+0x70`
- `0x1404C1120`
  - same layout, but builds a two-qword bitset of entries whose virtual `+0x70`
    returns true
- `0x1404C11B0`
  - same layout, but broadcasts virtual `+0x50` to each entry

So `owner + 0x15C8` is not only a compact nested control interface. It also
owns an array-like sub-action or sub-node collection whose availability flags
already participate in higher-level owner-state gating.

Two direct callsites make that bridge concrete:

- `0x1403F935F`
  - old control-routing region
  - gathers nested control flags from `owner + 0x15C8 -> (+8) -> +0x20`
    virtual `+0xD8/+0xE0`
  - then calls `0x140423440`
  - stores the result into compact owner-local status bytes near `+0xD51/+0xD52`

- `0x14048BF60`
  - `0x4D0` materialized-state refresh/finalize path
  - resolves a descriptor from `object + 0xB4`
  - calls `0x140423440(owner)`
  - if the gate fails, sets `owner + 0x15AA bit 0`
  - then goes through `owner + 0x15C8 -> virtual +0x88`
  - conditionally marks `object + 0x4C8`
  - runs `0x14048EEF0`
  - tail-calls `0x140458630`

This is the strongest new bridge in the current stack:

`nested control family at +0x15C8 -> owner readiness gates -> late decision object at +0x1808 -> materialized 0x4D0 refresh path`

That is already well above buddy locomotion. It sits on the path that decides
whether owner-state refresh is allowed to participate at all.

There is also a direct proxy cluster for the `owner + 0x1808` object:

- `0x141DEF690`
  - thin wrapper that resolves `owner + 0x1808`
  - jumps directly into virtual slot `+0x0` with `(decision_object, owner)`
- `0x141DEF5D0`
  - calls slot `+0x0`
  - if that returns false, tail-calls slot `+0x8`
- `0x141DEF650`
  - calls slot `+0x0`
  - if that returns true, tail-calls slot `+0x18`
- `0x141DEF610`
  - calls slot `+0x0`
  - if that returns true, tail-calls slot `+0x20` with a fixed float constant
    from `0x142BEA0A8`

So the object at `+0x1808` is not just a one-bit predicate. In the relevant
owner family it already behaves like a small late-decision interface:

- primary predicate at slot `+0x0`
- alternate/follow-up actions at slots `+0x8`, `+0x18`, `+0x20`

That makes it a better candidate for world/action participation gating than the
older locomotion-only paths.

There is also a useful lifecycle clue on the same owner family:

- initialization path around `0x141DE1814`
  - clears `owner + 0x1800`
  - clears `owner + 0x1808`
  - seeds adjacent state blocks at `+0x1840`
- later update path around `0x141DE25F8..0x141DE2604`
  - runs owner virtual `+0xDE8`
  - stores the returned pointer into `owner + 0x1808`

So in this family `+0x1808` behaves like a refreshed strategy/decision object
chosen from the owner runtime, not like a fixed embedded subfield.

The refresh side is now clearer too.

In the large owner update path around `0x141DE2436..0x141DE2716`:

- the code publishes several player-/runtime-labelled records through repeated
  `0x1403CF8B0` calls
- resolves additional owner/runtime state through:
  - owner virtual `+0x28`
  - owner virtual `+0x168`
  - owner virtual `+0x2E0`
  - global/runtime helpers like `0x1417CD150`, `0x141E7D510`, `0x1418B7E50`
- refreshes the two 0x90-byte blocks starting at `owner + 0x15E0`
- then calls owner virtual `+0xDE8`
- and stores the returned pointer into `owner + 0x1808`

After that, the same path immediately derives replicated transform/state blocks
from `owner + 0x130/+0x140` and writes them into:

- `+0x18E0`
- `+0x18F0..+0x1940`
- `+0x1990...`
- and resets related cached vectors at `+0x17C0/+0x17C8`

So `owner virtual +0xDE8` is not an isolated getter. In this owner family it
is the late stage of a broader state-selection/refresh pipeline that ends by
materializing the decision object stored at `owner + 0x1808`.

Cross-checking raw callsites also shows virtual `+0xDE8` is used very widely
across the binary, including:

- `0x140F03C53`
- `0x140F04FBC`
- `0x140F60F9D`
- `0x140FD0A27`
- `0x1410BA6C3`
- `0x1410F6948`
- `0x1410FC970`
- `0x14125CC97`
- `0x141260B89`
- `0x14126BDA5`

That is consistent with a general actor/owner strategy-object refresh hook,
not with a one-off companion-only helper.

One more structural clue now looks solid:

- several `virtual +0xDE8` callsites do **not** use the return value directly
  as a gameplay result
- instead they either:
  - store it into `owner + 0x1808`
  - or immediately walk through `returned_object + 0xC0`
  - or feed the returned object into `0x140CA17E0`

`0x140CA17E0` in turn expects an object with:

- a parent/runtime pointer at `object + 0x8`
- and then reads broad owner/runtime state from that parent, including offsets
  like:
  - `parent + 0x1E58`
  - `parent + 0x1D60`
  - `parent + 0x1DA0`
  - `parent + 0x4010`
  - `parent + 0x47C8`
  - `parent + 0x4858/+0x4870/+0x4878`

So the strongest current interpretation is:

- `virtual +0xDE8` refreshes or resolves a current strategy/decision node
- that node is not a leaf boolean helper
- it is a node-like runtime object anchored back into the owning actor/runtime
  through `node + 0x8`

This matches the earlier late-decision interface result around `owner + 0x1808`
and narrows the next step:

- identify the node family behind the `+0xDE8` result
- then recover the meaning of `node + 0xC0` and its relationship to
  `0x140CA17E0`

That owner family is now partially pinned down too.

The constructor-like path at `0x141DE1600`:

- calls a base initializer `0x141E98C70`
- installs vtable `0x142B82F00`
- clears and seeds a dense state cluster from `+0x15A0` upward
- explicitly initializes:
  - `+0x15A0`
  - `+0x15C8`
  - `+0x15E0`
  - `+0x1738`
  - `+0x1740`
  - `+0x1748`
  - `+0x17C0/+0x17C8`
  - `+0x1800`
  - `+0x1808`
  - `+0x1810`
  - `+0x18C0`
  - `+0x18E0`
  - `+0x19A8`
  - `+0x19D8`

So `+15C8`, `+1748`, and `+1808` are not accidental neighbors from separate
systems. They are formal fields of the same owner-class family.

The relocate/update path at `0x141DE3C80..0x141DE3D70` confirms this:

- rebases `+15A0`
- rebases `+1738`
- rebases `+1740`
- rebases `+1808`
- rebases `+19A8`
- and also relocates the object behind `+19D8` via its virtual `+0x48`

That gives a stronger class-level picture:

- nested control family at `+15C8`
- low-cardinality control mode at `+1748`
- late-decision strategy/node pointer at `+1808`

all belong to one coherent owner/runtime class, not to three unrelated helper
subsystems.

The same class family now has a partially recovered vtable at `0x142B82F00`.

Confirmed early entries:

- slot `0x00` -> `0x141DE1CA0`
  - freeing destructor wrapper
- slot `0x08` -> `0x141DE3AC0`
  - relocate/update method
- slot `0x58` -> `0x141DE2180`
- slot `0x98` -> `0x141DE99D0`

The in-place teardown path at `0x141DE1CE0` is especially useful:

- reinstalls vtable `0x142B82F00`
- walks and releases the nested control collection at `+0x15C8`
- releases the owned cluster at `+0x15D0`
- clears or reinitializes additional owned subobjects around:
  - `+0x1708`
  - `+0x1710`
  - and the wider `+0x1700..` region

This matters because it confirms the class is not a loose bag of fields. It
owns and tears down the nested control family formally, which strengthens the
interpretation that `+15C8`, `+1748`, and `+1808` are all parts of one actor
ownership/control layer.

Two more class-local vtable methods are now partially understood:

- `0x141DE2180` (slot `+0x58`)
  - large setup/activation path
  - sets `owner + 0x174C = 0`
  - sets bit `0x1` in `owner + 0x15A8`
  - clears `owner + 0x1728`
  - then uses a global callback pointer at `0x143898EF8` to build/store an
    owned object back into `owner + 0x1728`
  - so `+0x1728` belongs to the same owner lifecycle, not to an unrelated side
    system

- `0x141DE99D0` (slot `+0x98`)
  - clears `owner + 0x1500/+0x1508`
  - resolves a retained/live object through the global retained table at
    `0x14386FD00` using `owner + 0x108/+0x10C`
  - if resolution succeeds, copies its first `0x20` bytes into `owner + 0x110`
  - then combines that with owner transforms at `+0x120/+0x130`
  - writes a derived transform-like result back into later owner-local state

This is another good sign for the main goal:

- the same owner class that contains nested control and the late-decision node
  also has a direct retained/live refresh method.

So retained-player participation, control routing, and late decision state are
continuing to collapse into one class family rather than three separate tracks.

### IDB Migration Status

The repository now has a single working IDB again:

- `ida/tll.i64`

The temporary migration artifacts are gone:

- removed old working `ida/tll.i64`
- removed `ida/tll_fresh_packed.i64`
- removed `ida/tll_fresh.id0/.id1/.id2/.nam/.til`

What actually happened:

- the earlier `tll_fresh_packed.i64` was only opening cleanly while IDA still
  had access to its unpacked side files
- after cutover to a single packed file and clean reopen, the same corruption
  warning reproduces on the remaining `ida/tll.i64`
- so the migration to "one base" is complete, but the migration to a
  demonstrably clean packed IDB is not

Current practical rule:

- keep treating `reports/initial-findings.md` and `scripts/ida_apply_labels.py`
  as the reproducible source of truth
- treat `ida/tll.i64` as the single active working database
- do not assume that "single base" means "clean base"; a future true rebuild
  from retail `tll.exe` is still needed if we want a packed IDB without
  corruption warnings

That is already enough to make recovery safer, even before the fresh candidate
is fully validated and adopted as the main working IDB.

### Retained Slot Helpers

The retained player-slot table at `0x143EEBE80` is not just passive storage.
There is a small helper cluster around it:

- `0x1410F0D10`
  - reads one 16-byte slot entry from `g_RetainedPlayerSlotTable[slot]`
  - validates the embedded runtime id against `entity->processLike + 0xBC`
  - returns the live entity pointer when still valid
- `0x1410F0D60`
  - raw copy helper for a 16-byte slot entry
- `0x1410F0D80`
  - resolves a retained player entity by slot and then jumps into
    `0x14029ACB0`
- `0x1410F0DD0`
  - searches the six slot entries for a matching `(handle, id)` pair and
    returns the slot index
- `0x1410F0E00`
  - copies slot 0 into caller storage

This confirms the slot table stores fixed-size `(handle, runtime-id)` records,
with six entries in the current build.

Two more writers matter for slot ownership/promotion:

- `0x1410F3A80`
  - writes a resolved `(handle,id)` pair directly into
    `g_RetainedPlayerSlotTable[0]`
  - mirrors the same pair into caller-provided output storage
- `0x1410F3B20`
  - generalized retained-slot promotion path
  - resolves/builds a retained player record for the requested slot index
  - writes the resulting `(handle,id)` into `g_RetainedPlayerSlotTable[slot]`
  - marks the resolved entity with byte `+0x368C = 1`

This is the clearest current evidence that slot `0` is not hardwired by the
table format itself. The codebase already contains a generic "promote this slot"
path and a separate specialized "write primary slot 0" helper.

### Camera Link

There is now a concrete retained-player-to-camera bridge:

- `0x1410F3DD0`
  - validates slot 0 from `g_RetainedPlayerSlotTable`
  - marks the resolved entity active via the usual `+0x72` byte
  - calls `0x1416A5C20`
  - then fetches `GetCameraManager(0)` via `0x141C06420`
  - and updates camera state through `0x141C078D0`

This is the first direct evidence that the retained player slot system is tied
to gameplay camera ownership/update, not only to spawn bookkeeping.

Related active-state helper:

- `0x1410F3E40`
  - validates a retained slot entry
  - resolves the live entity
  - calls virtual slot `+0x860`
  - returns a boolean-like success result

The surrounding retained-slot utility cluster is also now clearer:

- `0x1410F3F50`
  - primary-slot availability check for slot `0`
  - gated by global runtime/debug flags
- `0x1410F3F80`
  - checks whether slot `0` already resolves to a live entity
  - otherwise falls back to `CheckRetainedPlayerSlotActive(0)`
- `0x1410F3FC0`
  - same idea as above, but for an arbitrary slot index
- `0x1410F8E90`
  - iterates all six retained slots
  - resolves each live entity
  - calls `0x1410F8D80` for each one

Another useful lookup helper:

- `0x1410F0F60`
  - resolves a retained slot into a live player handle/object
  - in some runtime/debug configurations it can consult an alternate retained
    lookup path before falling back to `g_RetainedPlayerSlotTable`

This means the retained stack is not hardcoded only around one active player.
The code already knows how to:

- store up to six `(handle,id)` player entries,
- test arbitrary slot liveness,
- refresh all retained player entities in a loop,
- and separately refresh camera state from primary slot `0`.

That split is important: it suggests "entity lifecycle for many slots" and
"camera ownership for the primary slot" are already separated concerns.

### Camera Attach / Detach

The camera side is now clearer too.

- `0x141C074E0`
  - enables a CameraManager attachment mode by setting `CameraManager+0x128 = 1`
  - toggles two camera-slot records in a larger global camera array
  - resolves a retained player through `0x141C0DFD0`
  - caches that retained player handle/id at `CameraManager + 0x2A8/+0x2B0`
  - marks a per-slot global byte at `0x14383DE48 + CameraManager->0x124`
- `0x141C07640`
  - performs the inverse transition with `CameraManager+0x128 = 0`
  - clears or resets the same camera-slot state

- `0x140745CD0`
  - explicit retained-binding sync gate for `CameraManager(0)`
  - flow:
    1. `GetCameraManager(0)`
    2. if mode/state is `4`, validate the cached retained handle/id at
       `CameraManager + 0x2A8/+0x2B0` against the current live entity
    3. if that cached binding is still valid, call
       `DetachCameraManagerFromRetainedPlayer`
    4. otherwise call `AttachCameraManagerToRetainedPlayer`
  - implication: retained camera binding on the primary camera manager is
    actively flipped at runtime, not only during setup

- `0x140746150`
  - parallel retained-binding sync gate for `CameraManager(1)`
  - repeats the same cached retained handle/id validation and
    attach/detach decision pattern, but on camera/viewport index `1`
  - implication: the binary already contains retained-player binding logic for
    a second camera manager, not only for `CameraManager(0)`

- around `0x14074D338`
  - the code registers `0x140746150` as a callback/handler through
    `0x141638C10`
  - a sibling registration immediately after it wires another handler
    (`0x140746340`) into the same cluster
  - implication: the `CameraManager(1)` retained-binding path is not a stray
    helper; it is part of an actual registered runtime handler set

- `0x14074D200 .. 0x14074D47B`
  - this whole region now looks like a builder/registrar for a retained-camera
    handler set
  - confirmed registered callbacks in that cluster include:
    - `0x140745CD0` for `CameraManager(0)` retained binding
    - `0x140746150` for `CameraManager(1)` retained binding
    - `0x140746260`
    - `0x140745D70`
    - `0x140746340`
  - implication: the retained-camera logic is packaged as a coordinated
    multi-handler runtime cluster, not a single isolated function

- `0x140746260`
  - companion handler that works through `CameraManager(0)`
  - reuses the same retained-entity validation (`+0x2A8/+0x2B0`, entity
    `+0xBC`, mark live entity `+0x72`)
  - then either:
    - calls `0x141C07420` when a live retained entity exists, or
    - toggles `CameraManager + 0x128` and updates two global camera-state
      blocks through `0x141837530` when no retained entity is present

- `0x140746340`
  - sibling handler that first mirrors/toggles a mode bit in byte `+0x6C`
    between `this` and `this + 0x58`
  - then runs the same retained validation and `0x141C07420` / global-state
    update path, but through `CameraManager(1)`
  - implication: this is no longer just "camera 1 attach/detach exists"; there
    is a visibly symmetric two-camera retained lifecycle package in the game

- `0x140746460` and the immediately following sibling handler
  - these sit one layer above the raw retained-binding toggles
  - they use a shared global `(handle,id)` cache at `0x143D76C10/0x143D76C18`
  - when a live retained entity exists, they:
    - mark it active via `+0x72`
    - call a virtual function at slot `+0x208`
    - write a flag-like byte at `CameraManager + 0x153`
  - when a live retained entity does not exist, they build a fallback
    descriptor and pass it through `0x141C0DFD0`
  - implication: this looks like a higher retained-camera owner/focus-target
    layer above plain attach/detach, and is currently the most promising next
    step toward understanding second-camera behavior

- `0x141C04CE0`
  - now looks like the concrete `CameraTargetState` apply/finalize path
  - behavior seen directly in the function body:
    - clears/initializes the destination state object
    - stores a target descriptor type/id pair
    - copies transform/scalar payload from the incoming descriptor
    - populates an internal sub-block around `+0x80`
    - refreshes or back-fills a retained `(handle,id)` pair around `+0xD0/+0xD8`
    - resolves a further runtime object through a virtual call and stores it at
      `+0x70`
  - implication: this is no longer a vague helper; it is a concrete camera
    target-state application function
  - destination is the `CameraManager + 0x2F0` subobject, not an abstract temp
    record

- `0x141C0DFD0`
  - wrapper/builder that prepares a descriptor payload and submits it into the
    camera object
  - uses a lock at `camera + 0x2C0`
  - reads the camera slot/index at `CameraManager + 0x124`
  - writes the built state into `camera + 0x2F0` via `0x141C04CE0`
  - then notifies an external observer via a virtual call on a global object
  - implication: this is the camera-target commit path above the raw retained
    entity selection layer

- `0x14015BB60`
  - bulk-copies a large camera-state struct spanning at least `0x3F8` bytes
  - this copy range includes:
    - the byte window around `+0x150..+0x156`
    - the committed target-state region around `+0x2F0`
  - implication: `+0x153` and `+0x2F0` belong to the same propagated
    camera-state object, not to unrelated side buffers

- `0x140882F00`
  - tiny accessor returning camera-state byte `+0x153`
  - implication: `+0x153` is treated as a standalone queried state flag, not
    only as an implementation detail of one big blob copy

- `0x141C07FC0`
  - tiny helper that seeds camera-state byte `+0x153` from the shared global
    toggle at `0x143682FA1`
  - sibling helpers immediately after it do the same for `+0x152` and `+0x151`
  - implication: bytes `+0x151..+0x153` form a small ordered camera-state flag
    window driven by the same global mode/toggle source

- cluster around `0x141C0CDBB .. 0x141C0CEF6`
  - local camera-state logic reads bytes `+0x153` and `+0x154`
  - if conditions fail, it shifts the byte window:
    - `+0x154 <- +0x153`
    - `+0x153 <- +0x152`
    - `+0x152 <- +0x151`
    - `+0x151 <- +0x150`
    - `+0x150 <- 0`
  - if a local vector/threshold test passes, it sets `+0x153 = 1`
  - implication: this is not a single boolean latch; it behaves more like a
    short rolling activity/history window inside the camera-state object

- `0x140882F10`
  - constructs/updates a runtime camera-state object, not a retained-only shim
  - object layout directly includes:
    - spatial state around `+0x20/+0x30/+0x40/+0x50`
    - retained/live target pair around `+0x70`
    - auxiliary retained pair around `+0x80/+0x88`
    - flag byte `+0x153`
    - a variable-sized sub-array at `+0x60/+0x68`
  - implication: retained-camera data is already merged into a general-purpose
    camera runtime object

- `0x140883530`
  - validates the auxiliary retained pair at `+0x80/+0x88`
  - if the live entity resolves, marks it active via byte `+0x72`
  - then falls through into `0x140883570`
  - implication: this is a local bridge from embedded retained target info into
    the broader camera-state update path

- `0x140883570`
  - main update path over the same runtime camera-state object
  - immediately queries `GetCameraManager(0)` and checks `CameraManager(0)+0x153`
  - if that flag is non-zero, it does not enter a retained sub-handler; it
    exits early through the common function epilogue at `0x140884266`
  - otherwise it continues with the normal vector/inertia/update math over the
    object state at `+0x20/+0x30/+0x40/+0x50`
  - implication: primary retained-camera state already gates the ordinary
    camera simulation path by suppressing it, not by routing to a detached side
    channel

- caller around `0x14088A760`
  - builds the runtime camera-state object at `owner + 0x4E0` via
    `BuildRuntimeCameraStateWithRetainedTarget`
  - then fills additional fields on the surrounding owner object:
    - writes a checksum/result flag into `owner + 0x54C`
    - writes a retained/live pair into `owner + 0x5F0`
  - then calls `UpdateRuntimeCameraState_GatedByPrimaryCameraFlag153` on the
    same `owner + 0x4E0` object
  - implication: the camera-state object sits inside a larger owner/controller
    object, not as a standalone heap record
  - `0x14088AC70` then:
    - validates another retained pair at `owner + 0x5F0/+0x5F8`
    - compares/propagates through companion objects at `+0x198`
    - calls `0x141754C10` when the companion flags require active retained
      camera-slot handling
  - implication: this owner is not just a camera math container; it also
    bridges retained-camera slot activity into another object graph

- `0x14088AC70`
  - takes the retained/live pair cached at `owner + 0x5F0/+0x5F8`
  - resolves the matching live entity and marks it active via byte `+0x72`
  - compares the owner's companion object at `owner + 0x198` against the live
    entity's companion object at `entity + 0x198`
  - if the live companion exposes bit `0x2` at `+0x18`, propagates retained
    camera-slot state through `0x141754C10`
  - otherwise clears the same bit on the owner-side companion through
    `0x141753DB0`
  - then either routes into `0x141754340` when live companion bit `0x1` is
    present or falls back to `0x141754820`
  - implication: this is a concrete bridge from retained player/entity state
    into a companion/camera-rig object graph, not a generic validation helper

- `0x14088AD40`
  - resolves a tracked entity from `owner + 0x1A8/+0x1B0` and validates it
    against the runtime registry at `0x14386FD00`
  - derives transforms and orientation data from that entity into the owner's
    local state around `+0x108/+0x110/+0x120/+0x130/+0x140`
  - queries four child nodes through virtual `+0x38` calls on the embedded
    runtime camera-state object at `owner + 0x4E0`
  - writes the resulting per-child transforms into the owner's bitset/object
    arrays around `+0x70` and through the pointer at `owner + 0x108`
  - finalizes through `0x141BCA6B0`, writes the world-space pair into
    `owner + 0x420/+0x430`, and marks the owner active via byte `+0x440`
  - implication: the object embedding the camera-state block at `+0x4E0` is a
    higher-level companion/camera-rig owner with its own child-node graph, not
    just a passive state carrier

- `0x141BB8C30`
  - large relocation helper for the same higher-level owner type
  - rebases plain tracked pointers at `+0x188`, `+0x198`, `+0x1A0`
  - walks a hashed/list-like node table rooted at `owner + 0x1A8` and rebases
    matching `entry + 0x8` pointers after CRC/hash checks
  - rebases pointer clusters at `+0x3F8/+0x400`, `+0x1E8`, `+0x340`, and
    object graph entries under `+0x218`
  - calls nested relocation helpers on `owner + 0x1A0`, `owner + 0x1A8`,
    `owner + 0x340`, and child objects under `+0x218`
  - implication: this is a full managed rig/controller object with several
    subordinate graphs, not a leaf state struct wrapped around camera math

- `0x141BBDD30`
  - takes cached owner pose/state at `+0x100/+0x120/+0x140`
  - resolves an external object through `owner + 0x1A0`
  - iterates a child-node pointer table at `[resolved + 0x4F8]` with count
    `[resolved + 0x500]`
  - for each eligible child node, copies the pose block into `node + 0x10`,
    `+0x30`, `+0x50` and sets bit `0x1` at `node + 0x191`
  - implication: the owner actively pushes a tracked pose into a runtime node
    set; this is much closer to a rig/controller update than to a passive
    camera descriptor

- `0x141BB95C0`
  - destructor path for the same owner type
  - compares guard/version state at `+0x2D8` against a global token
  - tears down nested objects at `+0x218` and `+0x238`
  - when byte `+0x16D` is set, packages the cached retained pair from
    `+0xA8/+0xBC` and notifies another system through `0x141B664C0`
  - implication: the cached retained entity pair is part of the owner's formal
    lifecycle, not just temporary runtime data

- `0x141EB7CE0` / `0x141EB7E70`
  - allocating and in-place constructors for the same owner type
  - both call the shared base constructor at `0x141BB3330`
  - both install the vtable at `0x142B99A98`
  - both zero the managed child-node cluster:
    - `+0x4A0` pointer array
    - `+0x4A8` logical count
    - `+0x4B0` mode/id field
    - `+0x4B8` managed graph pointer
    - `+0x4C0` guard byte
  - the allocating variant explicitly requests `0x4E0` bytes
  - implication: the `+0x4A0..+0x4C0` cluster is foundational to the class,
    not an optional extension bolted on by one callsite

- `0x141EB7EC0`
  - freeing destructor for the same `0x4E0` owner type
  - destroys the managed graph at `+0x4B8`, then tail-calls base teardown
  - frees the object when the incoming destructor flags set bit `0x1`
  - implication: `+0x4B8` is a first-class owned subobject in the class

- `0x141EB8890`
  - attach/init helper for the managed graph at `+0x4B8`
  - acquires a graph object through virtual slot `+0x790`
  - stores it into `owner + 0x4B8`
  - links it back to the owner through `0x141E60E80`
  - then continues common owner initialization via `0x141BBACC0`
  - implication: the owner owns a dedicated managed node graph object in
    addition to the plain pointer-array storage at `+0x4A0`
  - direct callsites include:
    - `0x14087D3A0`
    - `0x1402A2460`
    - `0x1409D2F39`
    - `0x140EA72A0`
    - `0x1414FBC30`
    - `0x14150A006`
    - `0x141F15576`
    - `0x141F44057`
  - each observed wrapper immediately sets a different bitfield on the
    companion object at `owner + 0x198` through `0x141754EF0`
  - implication: these are likely role/mode-specific rig initializers built on
    top of the same owner class, not unrelated ad hoc uses

- `vtable 0x142B99A98`
  - now confirmed as the vtable for this companion camera-rig owner family
  - includes the class-local destroy/relocate/refresh methods:
    - `0x141EB8950`
    - `0x141EB89E0`
    - `0x141EB8AE0`
    - and the shared owner-side methods `0x141BB95C0`, `0x141BB8C30`,
      `0x141BBDD30`
  - implication: this is a stable reusable runtime class family, not a single
    script object patched with unrelated helpers

- `0x14087D390`
  - thin wrapper over `AttachManagedNodeGraphToCompanionCameraRigOwner`
  - then ORs flag `0x10000000` into the companion object at `owner + 0x198`
    through `0x141754EF0`

- `0x1414FBC20`
  - same pattern, but writes mode flag `0x01000000`
  - this is especially relevant because the surrounding region is one of the
    previously observed reusers of the same runtime camera-state type

- `0x1402A2450`
  - same pattern again, but writes mode flag `0x00000800`
  - implication: the same owner/graph stack is reused under multiple
    semantically distinct modes

- companion object cluster around `0x141754460 .. 0x141754EF0`
  - object layout looks like a container with:
    - count at `+0x14`
    - entry-array base at `+0x8`
    - object flags at `+0x18`
    - mask bytes at `+0x1C/+0x1D`
  - each entry has size `0x250`
  - each entry stores:
    - mode/id at `+0x18`
    - child count at `+0x242`
    - two requested-mask bytes at `+0x243/+0x245`
    - two committed-mask bytes at `+0x244/+0x246`
    - extra state at `+0x248`
  - each child slot is `0x20` bytes and points to a linked runtime node whose
    own flags live at child `+0x18`, with byte `+0x1A0` used as another
    per-child activity/state bit

- `0x141754EF0`
  - ORs an arbitrary bitmask into child `+0x18` for every linked child node of
    every entry in the companion object
  - this is what the mode-specific rig wrappers use after graph attachment
  - implication: the wrapper flags are not camera-manager flags; they are
    propagating behavior bits into a child-node graph owned by the companion
    object

- `0x141754940`
  - narrower helper that finds the entry whose `entry + 0x18 == mode_id`
  - then ORs flag `0x80` into every linked child node's `+0x18`

- `0x141754460`
  - also keyed by `mode_id`
  - clears byte `+0x1A0` on every linked child node for the matching entry

- `0x141754820`
  - clears bit `0x1` on the companion object itself
  - clears child-node flag `0x40` for every linked child across all entries

- string evidence near `0x1428D8960`
  - literal `"Player"`
  - source path fragment:
    `shared/src/common/ai/simple-flocking.cpp`
  - implication: the companion object at `+0x198` is tied to an AI/simple-
    flocking style subsystem with player-labelled modes, not just generic
    camera bookkeeping

- `0x141BBACC0`
  - sits directly under `AttachManagedNodeGraphToCompanionCameraRigOwner`
  - initializes behavior bindings between the rig owner and companion object
    at `+0x198`
  - reads mode-dependent booleans from external source descriptors
  - then dispatches into the companion/flocking helpers:
    - `0x141754460`
    - `0x141754940`
  - implication: the rig-owner family is not just structurally adjacent to the
    flocking/player subsystem; it actively configures behavior modes there

- retained spawn bridge
  - `0x1410F14C0` (`CommitResolvedPlayerSpawn`) also copies the same
    `"Player"` string block from `0x1428D8960` during spawn commit
  - it stores the resolved retained pair back into `g_RetainedPlayerSlotTable`
    and packages additional state blocks around the same player-labelled data
  - implication: the retained spawn stack and the companion/flocking behavior
    stack are using the same semantic player-labelled descriptor material

- `0x140B56090`
  - `HandleNetSpawnCoopBuddy` is not just a roster helper
  - it resolves retained roster/live-entity state, builds a player-labelled
    descriptor path, and feeds that into the shared retained spawn machinery

- `0x140B580C0`
  - `SelectRetainedCoopPlayerLookHash` sits above the same player-labelled
    selection layer used by the retained coop buddy path

Current interpretation:
- retained player spawn
- coop buddy spawn
- companion camera-rig owner family
- companion/flocking player-labelled behavior stack

are no longer separate loose findings. They are touching the same semantic
“player/co-op buddy” descriptor layer.

That does not yet prove a true second local player controller exists, but it is
the strongest bridge so far between retained-coop plumbing and a higher-level
player-like behavior stack.

- `0x14088B250`
  - constructor for a derived class with size `0x500`
  - inherits the same managed cluster `+0x4A0..+0x4C0`
  - installs distinct vtable `0x1429B32F8`
  - adds derived fields at `+0x4E0/+0x4E8`
  - `0x1429B32F8` still points back to the same base family methods for
    destroy/relocate/managed-array handling, but overrides the slot occupied by
    `PushCompanionCameraRigPoseToChildNodes`
  - implication: this is a true class family with extensible rig/controller
    behavior, not a single concrete object

- caller around `0x1414FBA6E`
  - also constructs the same runtime camera-state object
  - immediately follows with `0x140883320`, which appends/updates one entry in
    the object's variable-sized `+0x60/+0x68` sub-array
  - then caches the camera-state pointer into `owner + 0x838`
  - implication: this same camera-state type is reused by at least one other
    higher-level owner/system, not only by the `0x14088A760` path

- `0x141EB89E0`
  - helper for a larger owner object with embedded pointer fields around
    `+0x4A0`, `+0x4A8`, `+0x4B8`
  - adjusts those pointers and the pointer array behind `+0x4A0` for a
    relocation/range update `(base,size)` style operation
  - is called from multiple camera-adjacent sites, including:
    - `0x14088A929`
    - `0x14025B5A8`
    - `0x140295350`
    - `0x1402BD2E0`
    - `0x1402C149B`
    - `0x1402DE354A`
  - implication: the owner wrapping `+0x4E0` is part of a broader relocatable
    runtime structure, likely a managed camera/controller node rather than a
    leaf gameplay helper
  - broader xref scan shows many additional callers spread across the binary
    (`0x14035CB18`, `0x1403A301A`, `0x14087D68C`, `0x1408957EF`, `0x1413DE64A`,
    `0x141534CB9`, `0x141F7085A`, `0x141FA7D88`, etc.)
  - implication: this relocation helper is generic for a whole family of
    managed runtime owners, so the `+0x4A0/+0x4E0/+0x5F0` structure is likely a
    reusable controller/rig type rather than a one-scene camera script object

- `0x141EB8950`
  - destruction-side sibling for the same managed owner
  - iterates the pointer array at `owner + 0x4A0` using logical length
    `(2 * owner[0x4A8] + 3)`
  - calls virtual slot `+0x28` on each non-null entry before tail-calling the
    parent teardown
  - implication: `owner + 0x4A0` is not scratch storage; it is a stable array
    of managed child nodes owned by the same higher-level runtime object

- `0x141EB8AE0`
  - refresh-side sibling for the same owner
  - requires `owner + 0x4A0`, `owner + 0x1A0`, and a clear guard byte at
    `owner + 0x4C0`
  - iterates the same `(2*N+3)` pointer array and refreshes each child through
    `0x141E4D760`
  - keeps the cached state vector at `+0x110` synchronized against the global
    registry at `0x14386FD00`
  - implication: the `(4A0,4A8,4B8,4C0,4E0,5F0)` cluster belongs to a managed
    owner/controller type with explicit destroy, relocate, and refresh phases

- `CameraManager + 0x153`
  - is written by the higher retained-camera owner/focus handlers
    (`0x140746460`, `0x1407465E0`)
  - is then read widely by downstream camera/system code as a byte flag
  - current best reading: a result/availability/active-state bit for the
    current retained-camera target path, not a random scratch byte

- `0x141C07420`
  - retained-camera mode-switch bridge
  - writes `CameraManager + 0x128 = mode`
  - uses `CameraManager + 0x124` as the base camera slot index
  - flips two global camera-state blocks in the shared array
    `global + 0xDB0`, stride `0xBF0`, using relative indices `+2` and `+4`
  - both writes go through `0x141837530`, which updates a large slot-local
    state blob at `slot_state + 0xBE0`
  - implication: this is a structured two-slot state transition layer, not a
    one-off flag flip

- `0x141C074E0` / `0x141C07640`
  - now confirmed as the heavy attach/detach paths over the same two-slot
    global camera-state array
  - `AttachCameraManagerToRetainedPlayer`:
    - enables retained mode at `+0x128 = 1`
    - updates the two global camera-state blocks
    - if needed, builds and commits a camera target descriptor via
      `0x141C0DFD0`
    - refreshes cached retained `(handle,id)` into `+0x2A8/+0x2B0`
    - sets a per-slot global byte in `0x14383DE48[CameraManager + 0x124] = 1`
  - `DetachCameraManagerFromRetainedPlayer`:
    - disables retained mode at `+0x128 = 0`
    - updates the same two global camera-state blocks in the opposite pattern
    - if a cached retained entity still resolves, invokes its virtual `+0x208`
      callback with mode `2`
    - clears the per-slot global byte in `0x14383DE48[...] = 0`
  - implication: retained-player camera ownership is expressed both through the
    per-camera object and through a shared global two-slot camera-state system

- `0x141837530`
  - shared slot-state blob writer used by:
    - `0x141C07420`
    - `AttachCameraManagerToRetainedPlayer`
    - `DetachCameraManagerFromRetainedPlayer`
    - both `HandleCameraManager0/1RetainedStateTransition` fallback branches
  - always receives a pointer into the global camera slot array
    `global + 0xDB0 + index * 0xBF0`
  - writes into a large local sub-block starting at `slot_state + 0xBE0`
  - observed call pattern uses:
    - `edx = 0 / 1`
    - `r8d = 0`
  - implication: retained-camera transitions are not just flipping flags; they
    re-materialize a per-slot camera-state blob in shared runtime storage

- `0x14383DE48`
  - now confirmed as a two-byte global retained-camera slot gate for camera
    slots `0` and `1`
  - it is written by attach/detach through `CameraManager + 0x124`
  - it is read by `0x141754C10`, which:
    - checks `0x14383DE48[0]` and `[1]`
    - if both are clear, ensures bit `0x2` is set on the target object
    - if either is set, clears that same bit through `0x141753DB0`
  - `0x141754C10` is not a one-off helper; it has many callsites spread across
    the binary, including `0x1401C909C`, `0x1402FEC13`, `0x14042FDF1`,
    `0x14088A300`, `0x140A21A94`, `0x14150D740`, `0x141ED640A`
  - its propagation helper `0x141753DB0` pushes the chosen mode into child
    entries through byte `+0x1A4` and byte `+0x1A0`
  - implication: other runtime systems already react to “any retained camera
    slot active”, not just to a specific primary camera manager

This makes `0x1410F3DD0` more concrete:

1. validate retained slot `0`
2. refresh/focus the entity
3. get `CameraManager(0)`
4. push retained-player-derived camera state through `0x141C078D0`
5. other callers can then attach or detach that CameraManager from the retained
   player through `0x141C074E0` / `0x141C07640`

One live caller chain is especially informative:

- around `0x140EDE5E0`
  - if no retained object is present, code falls back to `SpawnPlayerFallback`
    (`0x1410F3350`), then calls `DetachCameraManagerFromRetainedPlayer`
    (`0x141C07640`)
  - if retained state already exists, code calls
    `RefreshPrimaryRetainedPlayerCamera` (`0x1410F3DD0`), then
    `AttachCameraManagerToRetainedPlayer` (`0x141C074E0`)
  - both branches finish by resolving slot `0` through `0x1410F0F60`

That strongly suggests slot `0` is not just a convention in helper wrappers.
It is currently the promoted retained player that CameraManager screen `0`
binds to when this mode is active.

Current implication:

- camera work should now pivot around the retained slot helpers above;
- the next best targets are the writers for retained slot selection and the
  callers that decide when to run `AttachCameraManagerToRetainedPlayer` versus
  `DetachCameraManagerFromRetainedPlayer`;
- and specifically, the logic that treats `CameraManager(1)` as a retained-
  binding peer to `CameraManager(0)`, because that is the strongest current
  signal for a second in-engine camera path

### Retained Camera Message Layer

There is now a higher retained-camera message/transition layer above the raw
slot helpers:

- `0x1413BA140`
  - dispatches on three 32-bit opcode/hash values from the incoming message
  - one branch (`0xD8B09E0C`) clears local state and rebuilds camera-related
    transform data from the current live player/camera objects
  - one branch (`0xBA9A0259`) resolves a live entity/player record and caches
    its `(handle,id)` pair at `this + 0x658`
  - one branch (`0x40DDA52D`) re-reads that cached `(handle,id)` pair,
    validates the live entity, marks it active via byte `+0x72`, and triggers
    the usual activation helper at `0x1415BA3A0`

- `0x1413B9EA0`
  - separately re-activates the cached retained-player record stored at
    `this + 0x658`
  - then forwards the owning object into downstream retained/camera update
    logic

This is the first clear "message layer" above the retained slot/camera helpers.
It suggests camera ownership/focus transitions are not only driven by direct
slot-table writes, but also by explicit retained-player camera messages that
cache and later revive a chosen `(handle,id)` entity reference.

The producer side near this handler is also becoming clearer:

- `0x1414538E0`
  - builds a larger retained-camera state record
  - stores a `(handle,id)` pair at the start of the record
  - fills transform/camera payload and internal flags
- `0x1417BF750`
  - appends a compact 0x30-byte retained-camera record into a grow-only array
- `0x141634F20`
  - allocates and enqueues a retained-camera work item/object
  - links it into an owner-side queue and dispatches it through a virtual call

Current inference: retained-player camera transitions are handled through a
separate record/job pipeline, not only through direct `CameraManager` mutation.
That is useful because this higher layer appears to be handle-centric rather
than hardwired to `g_RetainedPlayerSlotTable[0]`.

That work-queue path is now clearly typed rather than monolithic:

- `0x141634F20`
  - queue helper for work-item type `0x1`
- `0x1416350A0`
  - larger payload-bearing queue helper for work-item type `0x2`
  - copies caller-supplied camera/state payload into the new item
- `0x141635210`
  - simple queue helper for work-item type `0x4`
  - stores a command id both in the queue entry and in the item at `+0x20`
- `0x1416354E0`
  - simple queue helper for work-item type `0x8`
- `0x1416355B0`
  - parameterized queue helper for work-item type `0x10`
  - stores two caller-provided integers into the new item

Shared structure observed across these helpers:

- owner object keeps a queue/count/bitmask cluster around `+0x28/+0x30/+0x48`
- linked/runtime context pointers are sourced from `owner + 0x58/+0x60/+0x78`
- queue entries are compact `0x20`-byte records with:
  - command id at `+0x0`
  - item pointer at `+0x8`
  - parameter/sentinel field at `+0x10`
  - work-item type at `+0x18`

There is also an explicit command-context initializer:

- `0x1416D1330`
  - initializes a retained-camera command context
  - resolves a runtime node by command id
  - caches the resolved node at `context + 0x50`
  - normalizes an aligned pointer/state value into `context + 0x40`

Current implication: this is no longer just a few camera helpers around
`CameraManager`. It is a real retained-camera command bus with typed items,
resolved runtime targets, and owner-side queue state. That is a much better
candidate for second-camera ownership than forcing everything through primary
slot refresh alone.

### Retained Camera Bus Owner

The retained-camera command bus now has a likely top-level owner/manager:

- `0x1417A4B00`
  - large constructor/initializer for the retained-camera bus
  - initializes multiple command-record blocks starting around `this + 0x60`
  - builds queue/descriptor storage through roughly `this + 0x4E0..0x518`
  - stores important external/context pointers at:
    - `this + 0x530`
    - `this + 0x538`
  - enables the subsystem with `this + 0x528 = 1`

Observed owner-side storage includes:

- `+0x4E0`
  - pointer to a `0x18`-stride descriptor array
- `+0x4F0`
  - descriptor count
- `+0x4F8`
  - pointer to an auxiliary pointer table
- `+0x508`
  - pointer to a larger `0x1C0`-stride object array
- `+0x518`
  - count for that larger object array

There is also a small wrapper already feeding this pipeline:

- `0x1409D6C90`
  - thin wrapper around `BuildRetainedCameraStateRecord`
  - stores the built record plus extra state in object fields around
    `+0x130..+0x170`

Current implication:

- the bus owner is no longer hypothetical; there is a concrete manager object
  with its own descriptor arrays and per-entry object storage
- this gives us a much cleaner next target than generic camera helpers:
  find who owns `ConstructRetainedCameraCommandBus`, who calls its per-entry
  build path, and where the queued command types are actually consumed

### Consumer / Query Side

The first consumer-side helpers around the retained-camera bus owner are now
clear enough to treat as a real API cluster:

- `0x1417A4AB0`
  - returns `owner + 0x58` when present
  - otherwise falls back to `owner + 0x50`
  - this is the current "primary source" selector used by later bus helpers

- `0x1417A5A90`
  - flushes/free-clears descriptor buffers referenced through `owner + 0x4E0`
  - resets owner state at least around:
    - `+0x4E8`
    - `+0x500`
    - `+0x510`
    - `+0x51C`
    - `+0x548..+0x588`
  - leaves subsystem enabled with `owner + 0x528 = 1`

- `0x1417A5C10`
  - checks whether the bus has active entries and whether the primary root
    reachable from `owner + 0x4F8` resolves to a non-null runtime node

- `0x1417A5C40`
  - resolves that active root and invokes its virtual slot `+0x10`

- `0x1417A5CA0`
  - checks a command route through the active root using `owner + 0x530`

- `0x1417A5D50`
  - resolves a command-specific result/value through the active root

- `0x1417A5DD0`
  - queries an integer-like value through the active root and writes `0` on
    failure

Current implication:

- there is a real owner-side retained-camera API for:
  - selecting the primary root,
  - flushing bus state,
  - testing/dispatching commands,
  - and querying active results
- the interesting bridge for second-camera work is now not only queue creation,
  but the root object reachable from `owner + 0x4F8` and the external context
  at `owner + 0x530`

### Root-Side Routing Layer

The next layer below the bus owner is now identifiable as a root-side routing
API:

- `0x1417AF120`
  - builds a compact snapshot structure from the root object
  - copies:
    - root timing/scalar state around `+0x168`
    - ids/counters around `+0x118/+0x120/+0x124/+0x128/+0x12C`
    - route/flag state derived from the attached bus at `root + 0x60`
  - also derives two boolean-like fields in the snapshot (`+0x2D/+0x2E`)

- `0x1417AEDE0`
  - uses `BuildRetainedCameraRootContextSnapshot`
  - then tests command handling through root branches at `root + 0x38` and
    fallback `root + 0x40`
  - returns a boolean-like result through downstream handlers

- `0x1417AEE60`
  - uses the same snapshot builder
  - then resolves a command-specific object/result through the same
    `root + 0x38 / +0x40` branch pair

- `0x141844170 .. 0x141845762`
  - this region now looks like a monolithic retained-camera route-graph
    constructor, not a clean runtime matcher API
  - it allocates many small typed nodes/records and links them into a graph or
    chain under the retained-camera root/bus
  - earlier addresses such as `0x141844DE0`, `0x141845590`, and `0x141845640`
    are better treated as internal construction blocks inside that body, not as
    standalone external functions

- `0x1418457A0`
  - standalone helper that builds a compact route-resolution record from a
    resolved route candidate
  - observed behavior:
    - initializes an output record with a boolean-like flag and default
      sentinel `-1`
    - reads candidate-linked pointers at `candidate + 0x30` and `+0x38`
    - scans a global runtime registry (`0x1466C3260` / count
      `0x1466C3458`)
    - on match, writes result indices/ranges into the output structure at
      `+0x10/+0x14/+0x18/+0x1C/+0x20`
  - this currently looks like the most concrete bridge from a resolved route
    candidate into runtime selection metadata

- `0x141845790`
  - tiny thunk/adapter into `0x1418457A0`
  - `0x141845CFB` stores this thunk into a stack-built descriptor/record,
    which strongly suggests `BuildRetainedCameraRouteResolutionRecord` is
    consumed as a callback or interface slot, not only via direct calls

- `0x141845980`
  - standalone retained-camera route-state helper, not a clean query-object
    matcher API
  - it is called from:
    - `0x141844140`, a small event/callback-style stub that reacts to event
      type `4` and passes `global + 0x1C0`
    - `0x141846D8C`, a conditional path gated by a bit in `global + 0xE80`
  - current best reading: this helper updates a small global retained-camera
    state block from the supplied record/context, likely for route-state sync,
    debug, or global bookkeeping rather than core matching

- `0x141844140`
  - small event/callback-style stub for retained-camera event type `4`
  - reads `global + 0x1C0`, copies it to a local stack slot, and forwards it
    into `0x141845980`

- `0x141BA3011`
  - one confirmed external callsite for `ConstructRetainedCameraRouteGraph`
  - meaning: the large route-graph constructor is part of a broader retained-
    camera setup/initialization path, not an isolated dead code island

Current implication:

- the retained-camera root is not just a passive node hanging off the bus
  owner; it has its own route tree under `+0x38/+0x40`
- command routing now appears to be:
  1. owner chooses active root
  2. root snapshot is built
  3. route tree matcher walks command branches
  4. result/object/bool is returned to the owner-side helper
- but the leaf matcher internals still need more care: a few addresses that
  initially looked like standalone matcher functions are actually internal
  blocks of the route-graph constructor, so the next reliable focus should be
  the standalone helper `0x1418457A0` and the true runtime callers higher up
  the stack

This is the closest path so far to actual camera-path selection logic.

### Discarded Route-Graph Branch

The later branch around `0x141844170/0x141845790/0x141845980` should now be
treated as a false lead for the co-op camera task:

- nearby strings resolve to render/debug surface-query plumbing, including
  `Impl::RequestSurfaceTypeData` and `Impl::QueueSurfaceQuery`
- `0x141845790` is used as a callback in that surface-query path
- `0x141845980` updates a small global state block in the same branch

So this branch should not currently be treated as the retained-player camera
ownership path. The stronger direction is the explicit retained-binding logic
around `AttachCameraManagerToRetainedPlayer`,
`DetachCameraManagerFromRetainedPlayer`, and the `CameraManager(0)/(1)` sync
gates above.

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
- `0x140B580C0` appears to compute a CRC/hash-like identifier for that look
  string, not merely return a direct string pointer.
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

## Continuous Manual Control

A toggleable local WASD controller was tested successfully. TLL uses X/Z as
its horizontal plane and Y as height. The working diagnostic maps keyboard
directions to X/Z, builds a goal 3 metres from Nadine, and refreshes it every
250 ms or immediately when the input direction changes.

The vtable update hook can observe several transient `NadineBuddy` instances,
so the controller locks one instance when `F8` enables manual mode. Only that
object receives movement commands.

Campaign AI initially competed with the manual goals. The native reversible
functions solve this:

- `0x1403F5D30`: `RequestDisableAi(npc, fromScript, outsideSkill)`
- `0x1403F5C80`: `RequestEnableAi(npc, fromScript)`

Manual mode calls `RequestDisableAi(npc, false, true)`. Disabling manual mode
calls `RequestEnableAi(npc, false)`. Testing confirmed four directions,
diagonals, rapid direction changes, stopping, reduced path-planning conflict,
and restoration of normal buddy AI.

## Camera-Relative Movement

The local controller was then upgraded from world-relative input to
camera-relative movement.

The active gameplay camera route is now best read as:

- `ResolveScreenRouteEntityByIndex` at `0x141C06420`
- `SelectPreferredCameraChildFromRouteEntity` at `0x141C0D4B0`

The current camera's own transform getter at virtual slot `+0x2F0`
(`0x1407723B0`) returns a stable camera-control transform, but it does not
track the final orbit/look rotation seen by the player. The final gameplay
camera orientation is instead reached through the resolved route entity and its
preferred camera child.

Confirmed camera data:

- `CameraManager + 0x1A0`: final camera position quaternion source block A
- `CameraManager + 0x1B0`: final camera orientation quaternion `x, y, z, w`
- `CameraManager + 0x1F0`: mirrored copy of `+0x1A0`
- `CameraManager + 0x200`: mirrored copy of `+0x1B0`

Two live snapshots taken before and after an approximately 90 degree yaw
rotation changed `CameraManager + 0x1B0` from:

- `0.111814, 0.274834, -0.0321977, 0.954425`

to:

- `0.0194229, 0.848863, -0.0335958, 0.527186`

while the camera-control transform returned by slot `+0x2F0` stayed unchanged.
This confirms that `CameraManager + 0x1B0` is the correct source for
camera-relative locomotion.

The host hook now converts that quaternion into flattened horizontal forward
and right vectors on the X/Z plane, normalizes them, and rotates `WASD` input
into world-space movement before issuing the native Nadine `MoveTo` command.

Validation on the live build confirmed:

- movement remains stable while rotating Chloe's camera through multiple
  headings;
- `W/S` follows camera forward/back;
- `A/D` follows camera left/right;

## Locomotion Command Block

The native `MoveTo` path is now understood as a producer for a larger internal
locomotion command block rather than a direct "move now" primitive.

Confirmed writer/consumer chain on build `1.4.21058.0`:

- `0x1403F5B60` resets and initializes a pending command block on the NPC.
  Important fields written there:
  - `npc + 0x24F4`: command type
  - `npc + 0x24F8`: start time / timestamp source
  - `npc + 0x2500`: duration / timeout-like value
  - `npc + 0x2560`: command flags byte
- `0x1403F5E20` through `0x1403F6DB0` are specialized command writers that fill
  the block at `npc + 0x2470`.
- `0x14041F2EA` passes `&npc[0x2470]` to `0x14041F700`, which copies the full
  command block into another internal structure at `npc + 0x2620`.
- `0x14041E0A8` and `0x14041E0BC` read `npc + 0x2500` and `npc + 0x24F8` as a
  time gate before that copy path runs.

Recovered block layout from the copy routine at `0x14041F700`:

- `+0x00 .. +0x4F`: navigation frame and movement payload
- `+0x50`: byte copied from `npc + 0x24C0`
- `+0x51`: byte copied from `npc + 0x24C1`
- `+0x54`: dword copied from `npc + 0x24C4`
- `+0x58`: dword copied from `npc + 0x24C8`
- `+0x5C`: dword copied from `npc + 0x24CC`
- `+0x60`: dword copied from `npc + 0x24D0`
- `+0x64`: dword copied from `npc + 0x24D4`
- `+0x68`: dword copied from `npc + 0x24D8`
- `+0x6C`: dword copied from `npc + 0x24DC`
- `+0x70 .. +0xB7`: additional copied movement state
- `+0xC0 .. +0xEC`: extended payload
- `+0xF0`: flag byte sourced from `npc + 0x2560`
- `+0xF1`: flag byte sourced from `npc + 0x2561`
- `+0x100 .. +0x180`: extra payload copied from the source block

This means the low-level writers near `0x1403F6C00` / `0x1403F6CB0` are not
self-sufficient direct movement calls. They only populate part of a larger
command object that is later validated, copied, and consumed elsewhere.

One live runtime test confirmed that directly calling `0x1403F6CB0` with a
guessed `(npc, direction, mode)` signature is unsafe: Nadine did not move and
the game crashed shortly after enabling that backend. That function must be
treated as unresolved until its required surrounding state is recovered.

Additional state findings:

- `0x140176990` is a getter for `npc + 0x1D57 bit 0x2`.
- `0x140177D4F` tests `npc + 0x1D57 bit 0x2` in a downstream state path, but
  this is not the primary locomotion dispatcher.
- `0x140420441` tests `npc + 0x1D57 bit 0x4`, suggesting at least one separate
  locomotion state branch keyed by that flag.

Current implication for the mod:

- `NpcMoveTo` remains the only confirmed stable runtime movement backend.
- Further work should target the consumer path around `0x14041F700`,
  `0x14041E0A8`, and the command-validation helpers near `0x1403E0400` /
  `0x1403DF370`, not blind calls into nearby writer functions.
- In parallel, the retained player-spawn stack above is now a higher-priority
  track for true second-player control, because it already owns player-like
  look/slot/entity resolution outside the campaign buddy AI path.

### Command-Type Dispatcher

The pending command type at `npc + 0x24F4` is actively interpreted later in the
runtime. Confirmed reads exist around `0x1406AD3BD`, `0x1406ADE67`,
`0x1406AE152`, `0x1406B2368`, `0x1406B2C97`, `0x1406B2F3C`, and
`0x1406B591C`.

Most useful recovered behavior so far:

- `0x1406B2360` is a small classifier on `npc + 0x24F4`.
  It returns true only for a subset of types covered by bitmask `0x914`
  over the range `0 .. 0x0B`.
- `0x1406B2C97` / `0x1406B2F3C` branch specially on command types
  `0x2`, `0x4`, `0x8`, and `0x0B`.
  - Types `0x2`, `0x8`, and `0x0B` forward the existing navigation frame from
    `npc + 0x2470`.
  - Type `0x4` rebuilds a navigation frame dynamically using
    `BuildNavigationFrame(0x141DF2D60)` and additional state such as
    `npc + 0x2508`, `npc + 0x120`, and the locomotion radius at
    `*(float*)(*(uintptr_t*)(npc + 0x15B8) + 0x1C8)`.
  - Other types fall through to a virtual call at `[npc_vtable + 0x1C8]`.
- `0x1406B591C` branches specially on types `0x9` and `0x11`:
  - type `0x9` calls `0x1406AD290`;
  - type `0x11` first walks through `npc + 0x15C8` and then calls
    `0x1404E9630`.
- `0x1406ADE67` and `0x1406AE152` also special-case type `0x12`.

This strongly suggests the lower-level writer family near `0x1403F5E20` /
`0x1403F6DB0` is building a typed command object, and the real path to
responsive control is to understand which existing type best matches
"continuous player steering" rather than inventing a raw direct call.

### Partial Writer -> Type Mapping

Recovered directly from immediate `mov edx, <type>` arguments passed into
`0x1403F5B60`:

- `0x1403F5E20` -> type `0x09`
  - writes nav frame to `+0x2470`
  - writes move payload to `+0x24B0`
  - writes mode/extra dword to `+0x2518`
  - writes flag bit into `+0x2561`
- `0x1403F5F90` -> type `0x0A`
  - similar to type `0x09`
  - writes `+0x251C`
  - writes `+0x2570`
  - more complex `+0x2561` flag packing
- `0x1403F60C0` -> type `0x0C`
  - nav-frame writer
  - writes `+0x251C`
- `0x1403F61C0` -> type `0x13`
  - writes `+0x2500`
  - toggles bit `0x2` in `+0x2560`
  - rebuilds navigation frame through `0x14014E240` and `BuildNavigationFrame`
- `0x1403F6280` -> type `0x14`
  - no nav-frame payload, mostly mutates `+0x2560`
- `0x1403F6460` -> type `0x08`
  - later patches `+0x2560` and `+0x2561`
- `0x1403F65F0` -> type `0x0B`
  - later patches `+0x2561`
- `0x1403F6840` -> type `0x03`
- `0x1403F68C0` -> type `0x02`
  - later writes `+0x2514`
  - mutates both `+0x2560` and `+0x2561`
- `0x1403F69D0` -> type `0x04`
  - writes `+0x2514`
  - mutates both `+0x2560` and `+0x2561`
- `0x1403F6AC0` -> type `0x15`
  - writes `+0x2550`
  - rebuilds nav frame using `npc + 0x15B8`
- `0x1403F6B70` -> type `0x0E`
  - writes `+0x2514`
  - mutates `+0x2561`
  - rebuilds nav frame using `npc + 0x15B8`
- `0x1403F6C20` -> type `0x07`
  - writes `+0x2558` and `+0x255C`
  - mutates `+0x2560`
  - clears `npc + 0x1D57 bit 0x2`
- `0x1403F6C90` -> type `0x05`
  - clears `npc + 0x1D57 bit 0x2`
- `0x1403F6CC0` -> type `0x06`
  - writes vector payload to `+0x24E0`
  - uses `npc + 0x15C8`
- `0x1403F6D70` -> type `0x0F`
- `0x1403F6D90` -> type `0x10`
- `0x1403F6DB0` -> type `0x11`
  - uses `npc + 0x15B8`
  - rebuilds nav frame from another object

Best current candidates for "continuous steering" are:

- type `0x07`: because it writes compact scalar parameters at `+0x2558` /
  `+0x255C` instead of only a target frame;
- type `0x02` / `0x04`: because the later interpreter has explicit dedicated
  branches for them;
- type `0x13`: because it combines a time field at `+0x2500`, a `+0x2560`
  flag change, and a navigation-frame rebuild in one path.

Type `0x06` remains interesting, but unresolved. The failed direct runtime call
hit the writer for type `0x06`, and the crash implies it depends on additional
state beyond the visible vector stored at `+0x24E0`.
- existing AI isolation, object locking, 3 m goal distance, and 250 ms refresh
  logic remain compatible.

The control hook was then refactored so movement logic no longer reads the
keyboard directly. The current DLL uses a small `InputCommand` structure
(`forward`, `right`, `input_mask`) and an `apply_control_command(...)` path.
The local keyboard now acts only as one producer for that command object.

This is the intended seam for the next phase: a remote client can publish the
same command shape without changing Nadine's host-side locomotion logic.

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

## Owner Runtime Bridge: Decision Node -> Nested Control -> Final Gameplay Hook

The owner/runtime class rooted at `0x142B82F00` has a large class-local update
method at `0x141DE8B03` that is currently the strongest bridge between the
late-decision object, nested control family, retained/live refresh, and the
final owner-side gameplay hook.

The method performs the following high-level sequence:

1. Calls owner virtual `+0x950`, then runs two class-local prep/update helpers
   at `0x141DEE740` and `0x141DEE960`.
2. Pulls an object through owner virtual `+0x438`, then calls that object's
   virtual `+0x18`.
3. Uses owner virtual `+0xD40`; when that succeeds, it queries the object from
   step 2 through virtual `+0x148` and writes a derived scalar into the owner
   block at `owner + 0x18C0`.
4. Resolves `owner + 0x1808` and calls the late-decision object's virtual
   slot `+0x10` with `(decision_object, owner)`.
5. Samples `owner + 0x4A8 -> virtual +0xF8`, clamps the result, and stores it
   into the object hanging off `owner + 0x1A0 -> +0x58 -> [0] + 0x80`.
6. Applies the familiar owner gates through virtual `+0xDD8`, `+0xC30`,
   owner byte `+0x15AB`, and virtual `+0x860`.
7. Reads nested control state through `owner + 0x15C0`:
   - when `owner + 0x15C0 -> +0x10 -> +0x10 == 0x143738088`, it calls
     `0x141E17D40`;
   - otherwise, after owner virtual `+0xDF8`, it calls `0x141E17B70` with
     `(owner + 0x15C0, 0x142B826E0, 0x950, 0x142B82290)`.
8. Replays the class-local retained/live refresh:
   - validates `owner + 0x108/+0x10C` against `g_RetainedPlayerSlotTable`
     (`0x14386FD00`);
   - copies the leading payload of the resolved object into `owner + 0x110`.
9. Gets an object from `owner + 0x15C8 -> virtual +0x28`, then branches on the
   nested-control selector tag at `owner + 0x15C0 -> +0x10 -> +0x10 -> +0x10`:
   - if tag `== 0x3353B841`, it calls `0x141DE73D0`, and for return values
     `< 3` dispatches nested-control virtual `+0x168` with that mode;
   - if tag `== 0x13086739`, it resolves `owner + 0x15C0 + 0x2F8` through
     `0x141DF35F0` and requires `*(u32*)(resolved + 0x60) == 0xAC710200`;
   - otherwise it falls through to owner virtual `+0xD30`.
10. Re-samples `owner + 0x4A8 -> virtual +0xF8`, publishes the result through
    `0x141DE5690`, then forces `-1.0f` into the two floats at
    `owner + 0x1A0 -> +0x58 -> [0] + 0x84/+0x88` when that object exists.
11. Iterates every entry in the nested control collection at `owner + 0x15C8`
    and calls each non-null entry's virtual `+0x38`.
12. Finishes with owner virtual `+0xE08`, passing the boolean accumulated in
    `bpl` from the earlier gate path.

This is a stronger result than the earlier isolated `+0x1808` proxies:

- the late-decision object is not just queried through the small
  `+0x0/+0x8/+0x18/+0x20` proxy family;
- a larger owner update path also consumes `owner + 0x1808` via virtual
  slot `+0x10`;
- that same path already touches retained-player state, nested control mode
  selection, nested control entry fanout, and the final owner virtual `+0xE08`.

So the current strongest interpretation is:

`decision node @ +0x1808 -> owner-class update 0x141DE8B03 -> nested control / retained-live refresh / per-entry fanout -> final owner gameplay hook +0xE08`

### Class-Local Control Mode Resolver

The helper at `0x141DE73D0` is now best treated as a class-local control mode
resolver for this same owner/runtime class:

- it starts from the low-cardinality mode field at `owner + 0x1748`;
- it requires the byte gate at `owner + 0x1894`;
- and it layers additional runtime/selection checks before returning a final
  mode used by nested-control virtual `+0x168`.

This matters because `+0x1748` is no longer just "a state id near the control
stack". In this owner family it is already feeding a higher-level runtime
decision path that sits above raw locomotion and directly before nested control
dispatch.

### Owner Virtual `+0xE08` and the `+0x1F78/+0x1F80` Cluster

Raw callsite review makes owner virtual `+0xE08` much less ambiguous than it
first looked. It is not a random catch-all virtual used everywhere; only a
small set of sites hit it, and most of them first gate through the managed
object at `owner + 0x1F78`.

Observed positive-gate patterns:

- `0x140DD630D`
  - calls `[owner + 0x1F78]->virtual +0x38`
  - if true, calls owner virtual `+0xE08`
- `0x140DD690C`
  - calls `[owner + 0x1F78]->virtual +0x30`
  - if true, calls owner virtual `+0xE08`
  - then immediately continues into `0x1410F7290` and follow-on entity/object
    work
- `0x140DE3F4D`
  - calls `[owner + 0x1F78]->virtual +0x48`
  - if true, calls owner virtual `+0xE08`
- `0x140F051F2`
  - calls `[owner + 0x1F78]->virtual +0x40`
  - if true, calls owner virtual `+0xE08`
- `0x140FC6A7D` / `0x140FC6AB1`
  - call `[owner + 0x1F78]->virtual +0x50`
  - if true, call owner virtual `+0xE08`
- `0x1410F2A2D`
  - calls `[owner + 0x1F78]->virtual +0x60` with explicit `(rdx, r8d)` args
  - if true, calls owner virtual `+0xE08`
- `0x1411B0259`
  - again calls `[owner + 0x1F78]->virtual +0x48`
  - if true, calls owner virtual `+0xE08`

The new class-local bridge at `0x141DE8B03` also ends in owner virtual
`+0xE08`, but there the boolean comes from the method's own gate path (`bpl`)
rather than directly from one `+0x1F78` predicate virtual.

This lines up cleanly with the earlier materialized-state work:

- `owner + 0x1F80` is the managed materialized-state slot;
- attached materialized objects live under `owner + 0x1F80 -> +0x30`;
- helper work around the same cluster also references `owner + 0x1F78`;
- now the same cluster repeatedly gates owner virtual `+0xE08`.

Current best interpretation:

- the `+0x1F78/+0x1F80` region is not just storage for a materialized object;
- it is a managed gameplay-state cluster whose predicate methods decide when
  the owner should run virtual `+0xE08`;
- owner virtual `+0xE08` therefore looks more like a generic
  state-transition/apply/finalize hook than a locomotion-only callback.

This is a useful structural result for the project goal, because it ties the
materialized-state branch back into the owner gameplay path instead of leaving
it as an isolated support subsystem.

### `owner + 0x1F78` Is a Separate Relocatable State Object

The adjacent helper paths now make the `+0x1F78/+0x1F80` split sharper.

From `0x1404091A0`:

- `owner + 0x1F80` is still the managed slot object;
- the currently attached materialized object is `slot->+0x30`;
- when that attached object is cleared, the helper writes
  `slot->state_word & 0x0FFFFFFF` into `(*(owner + 0x1F78)) + 0xC`;
- then it clears `slot->+0x30`.

So `owner + 0x1F78` is not the same object as `owner + 0x1F80`. It is an
adjacent state object that receives the low state bits emitted by the managed
slot.

`0x1404280D0` strengthens that interpretation:

- it relocates nested-control-adjacent objects first;
- then it handles `owner + 0x1F78` in two stages:
  1. if the pointee is in the rebased range, it rewrites `*(owner + 0x1F78)`
     itself;
  2. if the pointer value stored at `owner + 0x1F78` is in range, it rebases
     the field `owner + 0x1F78`;
- then it handles `owner + 0x1F80` separately by calling that object's
  virtual `+0x10`, followed by rebasing the field `owner + 0x1F80`.

This means:

- `owner + 0x1F78` points at a relocatable object/state cell whose own first
  qword is also a relocatable pointer;
- `owner + 0x1F80` is a neighboring managed object with a formal relocate
  virtual;
- the two are coupled, but they are not the same storage.

Combined with the earlier `+0xE08` callsite review, the current best picture is:

- `+0x1F80` manages the materialized gameplay-state object lifecycle;
- `+0x1F78` stores a closely related state object with:
  - virtual predicates at `+0x30/+0x38/+0x40/+0x48/+0x50/+0x60`
  - writable low state bits at offset `+0xC`;
- successful predicates on the `+0x1F78` object repeatedly trigger owner
  virtual `+0xE08`.

So the `+0x1F78/+0x1F80` cluster is now best treated as a two-object managed
gameplay-state cluster rather than one slot plus some incidental flags.

### Base Construction of the `+0x1F78/+0x1F80` Gameplay-State Cluster

The construction/setup side of this cluster is now partially visible.

There is a base zero-init region around `0x1403FDBE0` that clears the whole
tail cluster in one pass:

- `+0x1F78`
- `+0x1F80`
- `+0x1F88`
- `+0x1F90`
- `+0x1F98`
- `+0x1FA0`
- `+0x1FA8`
- `+0x1FB0`
- `+0x1FB8`
- `+0x1FC0`
- `+0x1FD0`
- `+0x1FD8`
- `+0x1FE0`
- `+0x1FE8`
- `+0x1FF0`
- `+0x1FF8`

So this is a formal base-owner cluster, not a few ad hoc fields added by the
derived owner/runtime class.

The follow-on setup block around `0x1403FFEE5` then populates the same region
with distinct objects:

- `owner + 0x1F78`
  - gets either null or the result of `0x140458210(..., 0x1000)`;
- `owner + 0x1F80`
  - gets either null or a fresh `0x40` allocation initialized by
    `0x140489ED0`;
- `owner + 0x1FB0`
  - gets a fresh `0x18` object whose fields are explicitly seeded:
    - `+0x8 = 0x16`
    - `+0x10 = 0`
    - vtable/object header = `0x142BD8760`
- `owner + 0x1FB8`
  - gets either null or a fresh `0xC0` object initialized by `0x1403BF880`.

This matters because it confirms the cluster is intentionally composite:

- `+0x1F78` is its own separately allocated state object;
- `+0x1F80` is its own separately allocated managed slot object;
- `+0x1FB0` and `+0x1FB8` are additional owned helper objects in the same
  gameplay-state family.

One more useful consumer falls out of this same layer:

- `0x1403F9570`
  - looks at `owner + 0x1F80 -> +0x30`
  - and returns the attached materialized object only if:
    - it exists, and
    - the selector/capability id at `attached + 0x20` passes
      `CheckSelectorHashEnabledInBitsetRegistry (0x1417C4800)`.

So the materialized object under `+0x1F80 -> +0x30` is already selector-gated,
not just passively attached.

Current best structural picture of the base cluster:

- `+0x1F78` = separate relocatable state/predicate object
- `+0x1F80` = separate managed slot object
- `+0x1F80 -> +0x30` = attached materialized gameplay-state object, exposed
  only when its selector capability is enabled
- `+0x1FB0/+0x1FB8` = additional owned helper/state objects in the same family

That picture now needs one correction: the owned family clearly extends beyond
`+0x1FB8`.

The same construction block that creates `+0x1FB0` and `+0x1FB8` also creates:

- `+0x1FC0` from a `0x370` allocation via `0x1406399E0`
- `+0x1FD0` from a `0xE30` allocation via `0x140401EA0`

and later runtime paths also consume:

- `+0x1FD8`
- `+0x1FE8`
- `+0x1FF0`

So the right interpretation is no longer "two helper objects next to the main
pair", but rather a broader owned gameplay-state family rooted in the
`+0x1F78 .. +0x1FF0` region.

### `+0x1F80` Has a Fallback Companion at `+0x1FF0`

Two independent runtime consumers show the same pattern:

- first ask `owner + 0x1F80` through its virtual `+0x30`;
- if that fails, ask `owner + 0x1FF0` through virtual `+0x68`;
- if that still fails, fall back into `0x1403E02C0`.

Concrete sites:

- `0x140414B72`
- `0x14041E640`

This is useful because it means `+0x1FF0` is not an unrelated late field.
It already acts as a sibling query/helper object for the managed slot at
`+0x1F80`.

### Additional Owned Objects Behind `virtual +0x5E8`

The path around `0x14041FA90` broadens the same family further.

After syncing `owner + 0x1F80` through `0x14048F410` and passing owner virtual
`+0x5E8`, it conditionally consumes:

- `owner + 0x1FC0` -> `0x14063AD40`
- `owner + 0x1FD8` -> `0x1403F9B30`
- `owner + 0x1FE8` as another owned runtime object

That makes the cluster look more and more like a bundle of coordinated
gameplay-state helpers rather than one dominant object plus a few incidental
allocations.

### Attach/Detach Semantics of `+0x1F80 -> +0x30`

The managed slot at `owner + 0x1F80` is now much clearer from the raw
attach/detach code around `0x14048F180`, `0x14048F260`, `0x14048F320`,
and `0x14048F3C0`.

Detach path (`0x14048F180` / `0x14048F3C0`):

- if an attached object exists at `slot->+0x30` and its flag word at `+0x8`
  has bits `0x1` or `0x2`, the object gets virtual `+0x68`;
- the low 28 bits of `slot->state_word` (`slot + 0x10`) are written back into
  `(*(owner + 0x1F78)) + 0xC`;
- `slot->+0x30` is then cleared.

Attach/swap path (`0x14048F260` / `0x14048F320`):

- the code first reopens the `owner + 0x1F78` state object:
  - clears bit `0x1` in `state_object + 0x14`;
  - advances `state_object + 0xC` by a delta derived from global timing values
    around `0x143048470/0x143048480`;
  - updates `state_object + 0x10` with a bounded/xored variant of that state;
- when a new attached object is present:
  - it xors `attached + 0x10` with the masked low 28 bits from
    `state_object + 0xC`;
  - clears bit `0x2` and sets bit `0x1` in `attached + 0x8`;
  - writes the slot owner pointer into `attached + 0x28`;
  - writes `owner->e0->0x10` into `attached + 0x38`;
  - calls attached virtual `+0x60`;
- finally the slot stores the new object in `slot->+0x30`.

So the attached materialized object is not passive data. It is activated
against the `+0x1F78` state object and explicitly bound back to the owner
runtime via:

- `attached + 0x28 = slot`
- `attached + 0x38 = owner->e0->0x10`

This is one of the strongest indications so far that the materialized object is
part of a live gameplay-state machine, not just a cached descriptor.

### First Concrete Virtual Interface on `+0x1F78`

Several non-owner callsites now show concrete virtual slots on the state object
at `owner + 0x1F78`:

- virtual `+0x88`
  - used as an availability/readiness gate before deeper work;
- virtual `+0x90`
  - returns an object that is then queried through virtual `+0x7A8`;
- virtual `+0xD0`
  - returns a bool-like result directly;
- virtual `+0x30`
  - acts as another fallback/action predicate.

Representative call patterns appear in the `0x1402B79xx .. 0x1402B7Dxx`
cluster:

- validate global/current live actor through `0x143EEBE80`;
- force `live_entity + 0x72 = 1`;
- resolve `live_entity + 0x1F78`;
- require `state_object->virtual +0x88 == true`;
- then either:
  - take `state_object->virtual +0x90` and require returned-object
    `virtual +0x7A8 == true`, or
  - query `state_object->virtual +0xD0`, or
  - fall back to `state_object->virtual +0x30`.

This cluster is important because it moves `+0x1F78` out of the realm of
"generic timing blob" and into a more actor-facing state/query interface.

### `0x1402B7950 .. 0x1402B7D80` Form a Current-Actor State-Query Family

The `0x1402B79xx .. 0x1402B7Dxx` region is now best treated as one coherent
family of helpers that query the state object at `current_live_actor + 0x1F78`.

Shared front half:

1. Start from the global current/live actor slot table at `0x143EEBE80`.
2. Validate the cached actor id against `live_actor + 0xBC`.
3. Force `live_actor + 0x72 = 1`.
4. Resolve `live_actor + 0x1F78`.
5. Require `state_object->virtual +0x88 == true`.

After that, the helpers diverge into different query styles:

- `0x1402B7950`
  - calls `state_object->virtual +0x90`
  - requires returned object `virtual +0x7A8 == true`
  - then uses the result as a handle/object to feed the later path
    (`0x1402C2440`) along with the current buffered element
- `0x1402B7B00`
  - same `+0x88` gate
  - same `+0x90 -> +0x7A8` path
  - but uses the final bool (`sil`) to write `returned_object + 0x565`
- `0x1402B7C90`
  - same `+0x88` gate
  - then queries `state_object->virtual +0xD0`
  - writes the bool-like answer directly to the output qword
- `0x1402B7D20`
  - if `+0x88` fails or `state_object` is null, it falls back into
    `state_object->virtual +0x30`

So we now have a stronger semantic picture of the `+0x1F78` object:

- virtual `+0x88` acts like a general "state/query path available" gate;
- virtual `+0x90` returns a deeper gameplay object that itself exposes
  virtual `+0x7A8`;
- virtual `+0xD0` looks like a direct boolean state query;
- virtual `+0x30` is a lower-level fallback predicate/action query.

This is useful because the cluster is already actor-facing:

- it starts from the current live actor;
- it marks that actor active;
- and then asks the `+0x1F78` object questions that look much closer to
  gameplay-state availability than to raw rendering or pure movement math.

### First Read on `+0x1FB0` and `+0x1FB8`

The adjacent helper objects created with the `+0x1F78/+0x1F80` cluster are now
partially characterized.

`owner + 0x1FB0`

- constructed from an `0x18` allocation;
- explicitly seeded with:
  - `vtable/object header = 0x142BD8760`
  - `+0x8 = 0x16`
  - `+0x10 = 0`

This is small enough that it is almost certainly a lightweight helper/state
record rather than a rich gameplay controller.

`owner + 0x1FB8`

- constructed from a `0xC0` allocation via `0x1403BF880`;
- `0x1403BF880` zeroes most of the object, sets:
  - `+0x0 = 0`
  - `+0x8 = 0`
  - `+0xC = 1`
  - `+0x2C = 1.0f`
  - and clears a larger pointer/flag region up through `+0x58`;
- then `0x1403BF8D0` performs a second-stage reset/cleanup pass on that same
  object.

So `+0x1FB8` also looks like a small local state/helper object, not the main
actor-state carrier. The real actor-facing query surface is still much more
likely to be `+0x1F78`, with `+0x1F80` managing the attached materialized
object.

### Returned Object from `+0x1F78 virtual +0x90`

The query family around `0x1402B79D8` and `0x1402B7C31` confirms one more thing
about the object returned by `state_object->virtual +0x90`:

- it is not used like the actor itself;
- it is immediately treated as a separate gameplay object with its own vtable;
- the first discriminating gate applied to it is `virtual +0x7A8`.

So the path now reads more precisely as:

`current live actor -> actor_state_object(+0x1F78) -> virtual +0x90 -> secondary gameplay object -> virtual +0x7A8`

That is a useful narrowing even though the semantic role of the returned object
is still unresolved.

### `+0x90` Returned Object Is a Current-Actor Runtime Participant

The raw `0x1402B7950 .. 0x1402B7C60` disassembly sharpens the semantics of the
object returned by `state_object->virtual +0x90`.

Shared current-actor path:

- start from `g_CurrentLiveActor` at `0x143EEBE80`;
- validate actor id against `live_actor + 0xBC`;
- force `live_actor + 0x72 = 1`;
- require `live_actor + 0x1F78`;
- require `state_object->virtual +0x88 == true`;
- then call `state_object->virtual +0x90`.

The returned object is then used in two concrete ways:

- `0x1402B7950`
  - requires `returned_object->virtual +0x7A8 == true`;
  - builds/advances a current buffered entry block around `[rsp+0x38]`;
  - then forwards the returned object to `0x1402C2440`.
- `0x1402B7B00`
  - follows the same `+0x88 -> +0x90 -> +0x7A8` gate;
  - then writes `sil` directly into `returned_object + 0x565`.

So this object is already more specific than a generic helper:

- it belongs to the current validated live actor path;
- it exposes a deep gameplay-facing virtual at `+0x7A8`;
- it carries mutable runtime state at least at byte `+0x565`;
- and it is valid enough to be handed into a downstream worker
  (`0x1402C2440`) after the `+0x7A8` gate passes.

That makes it much more plausible that `state_object->virtual +0x90` returns a
current actor-side gameplay/runtime participant rather than a plain data blob.

### `+0x1FF0` Is a Formal Sibling State-Query Provider

The sibling role of `owner + 0x1FF0` is now concrete across multiple live
paths:

- `0x14041E640`
- `0x1404925BB`
- `0x140410CAF`

Two different virtuals are now confirmed on the same object family:

- `virtual +0x68`
  - used as a fallback provider when
    `owner + 0x1F80 -> virtual +0x30` fails;
  - fills the same out-state record shape that the attached materialized object
    path fills.
- `virtual +0x48`
  - used in event/state shaping after the attached materialized object at
    `owner + 0x1F80 -> +0x30` receives `virtual +0x18`.

This means `+0x1FF0` is not just "late fallback storage". It is a formal
sibling provider in the same gameplay-state family as `+0x1F80`.

The repeated query pattern is now:

1. ask `owner + 0x1F80` through `virtual +0x30`;
2. if that fails, ask `owner + 0x1FF0` through `virtual +0x68`;
3. if that still fails, fall back to `0x1403E02C0(owner, out_state)`.

That pattern survives across different gameplay consumers, which strongly
suggests one shared "state query / state materialization" contract rather than
three unrelated helpers.

### `0x140410CAF` Confirms Cooperation Between Attached State and `+0x1FF0`

The event-facing path around `0x140410CAF` makes the split clearer:

- it resolves `owner + 0x1F80 -> +0x30`;
- if present, it calls attached-object `virtual +0x18(owner, event_ptr)`;
- then it resolves `owner + 0x1FF0`;
- if present, it calls sibling-object `virtual +0x48(owner, event_ptr)`.

This is the first direct evidence that the attached materialized object and the
`+0x1FF0` sibling are expected to cooperate on the same owner/event update
path. The cluster is therefore broader than:

- "slot object at `+0x1F80`"
- "mystery fallback at `+0x1FF0`"

and is better treated as a coordinated gameplay-state interface family.

### `+0x1FE8` Is a Stateful Actor-Bound Runtime Helper

The path around `0x14041FA90` already hinted that `owner + 0x1FE8` was active.
The raw branch now makes its role much stronger:

- when owner virtual `+0x5E8` passes, it takes `owner + 0x1FE8`;
- validates its embedded `(handle,id)` pair through the same
  `entity + 0xBC` rule used elsewhere;
- marks the resolved live actor active through `+0x72`;
- keeps a local state id at `object + 0x10`;
- calls `0x140643970(object, &out)` to compute a fresh result;
- if the result id changed:
  - calls live actor virtual `+0xF68`;
  - refreshes `object + 0x28` from `live_actor->e0->0x10`.

So `+0x1FE8` is not passive storage. It is a stateful helper object that:

- binds to a live actor by retained-like `(handle,id)`;
- recomputes some actor-relative runtime state;
- notifies the live actor when that state changes;
- and caches an owner/actor-linked pointer at `+0x28`.

That makes `+0x1FE8` another active member of the same gameplay-state
subsystem, not a dead tail field.

### Materialized-State Variant Builders at `+0x1F80`

The managed slot `owner + 0x1F80` is now confirmed to host multiple installed
object variants, not only the already-known `0x4D0` family.

Concrete replace-and-attach paths:

- `0x1404B7D50`
  - detaches the current object from `owner + 0x1F80`;
  - allocates `0x540`;
  - constructs it through `0x14045C0D0`;
  - reattaches through `0x14048F260`.
- `0x1404B7E30`
  - same detach/reattach pattern;
  - allocates `0x48`;
  - constructs it through `0x14045B8D0`.
- `0x140665923`
  - same detach/reattach pattern;
  - allocates `0x9B0`;
  - constructs it through `0x140480230`.

This strengthens the interpretation of `+0x1F80`:

- it is not hard-wired to one state object class;
- it is a managed installation point for multiple gameplay-state object
  variants;
- attach/detach via `0x14048F180/0x14048F260` is part of a broader variant
  lifecycle.

### `+0x1FB8` Still Looks Auxiliary, But Now as a Resettable Runtime Record

The `0x1403BF880/0x1403BF8D0` constructor pair for `owner + 0x1FB8` is now a
bit sharper.

It initializes:

- `+0x0 = 0`
- `+0x8 = 0`
- `+0xC = 1`
- `+0x20..+0x2F` from a fixed constant block
- `+0x2C = 1.0f`
- `+0x38/+0x40/+0x48/+0x50/+0x58 = 0x143077538`
- `+0x60/+0x68 = 0`

Then `0x1403BF8D0` performs an explicit reset path if the leading
handle/count-like pair is already populated.

This still does not look like the main actor-facing state object. It looks more
like a resettable runtime record/helper with a fixed default backend table.

### Construction and Public Accessors for `+0x1FD8/+0x1FE0/+0x1FE8/+0x1FF8`

The setup block around `0x1403FFE00 .. 0x140400120` is now concrete enough to
stop treating these as anonymous tail fields.

Confirmed construction:

- `owner + 0x1FD8`
  - allocated as `0x20`;
  - initialized through `0x14027D9D0`;
  - then forced to:
    - `+0x10 = -1`
    - `+0x14 = 0`
- `owner + 0x1FE0`
  - allocated as `0x10`;
  - initialized through `0x14043DAF0`.
- `owner + 0x1FE8`
  - allocated as `0x48`;
  - zero-initialized through `0x1404024E0`;
  - then configured through `0x140643570(object, local_state, owner)`.
- `owner + 0x1FF8`
  - later created as either:
    - `0x130` through `0x1403BE170`, or
    - `0xE0` through `0x1403BE200`,
  - depending on the earlier runtime path.

There are also duplicated owner getter clusters:

- `0x1403DB300/0x1403FC050` -> `owner + 0x1FD8`
- `0x1403DB310/0x1403FC060` -> `owner + 0x1FE0`
- `0x1403DB320/0x1403FC070` -> `owner + 0x1FE8`
- `0x1403FC080/0x1403FC090` -> `owner + 0x1FF8`

So these are not incidental offsets. They are a formal sub-family of owned
runtime helpers with dedicated public accessors.

### `+0x1FE8` Has an Explicit Actor-Bound Constructor Path

The `0x140400994 .. 0x1404009CB` setup path sharpens the role of
`owner + 0x1FE8`.

Construction sequence:

- allocate `0x48`;
- zero/init with `0x1404024E0`:
  - `+0x0 = 0`
  - `+0x8 = 0`
  - `+0x28 = 0`
  - `+0x30 = 0`
- store it at `owner + 0x1FE8`;
- if non-null, call `0x140643570(object, local_state_blob, owner)`.

The constructor `0x140643570` immediately:

- seeds the object from the owner's `(handle,id)` pair at `owner + 0xA8/+0xBC`;
- seeds `object + 0x10` from the runtime-global counter `0x143D4B0E8`;
- clears `object + 0x28` and `object + 0x38`;
- reads `owner + 0x1F70 -> +0x1C` and feeds that into a deeper setup path.

This confirms the earlier behavioral reading:

- `+0x1FE8` is a small actor-bound runtime helper object;
- it is intentionally anchored to the owner/live-actor identity at
  construction time;
- and it is not just a lazily filled scratch buffer.

### `+0x1FF0` Looks Like a Current-State Selection Object

The branch around `0x14041F1F8 .. 0x14041F55F` makes `owner + 0x1FF0`
substantially clearer.

Observed layout/behavior:

- `object + 0x70`
  - treated like a label/string or descriptor pointer for UI/debug/reporting
    calls through `0x1403CF8B0`;
- `object + 0x78`
  - compared against a freshly computed hash/selector-like value;
- `object + 0x7C`
  - read as a signed 16-bit id and copied into `owner + 0x236C/+0x2370`;
- `object + 0x80`
  - flag byte; bits `0x3` and `0x8` participate in candidate filtering and
    activation logic.

Observed virtuals:

- `virtual +0x20(owner, out)`
  - tested on candidate objects while scanning an owner-managed list;
- `virtual +0x30(owner, selected_state)`
  - called after a new current object is installed;
- `virtual +0x40(owner)`
  - called when replacing an already-installed current object;
- `virtual +0x48(owner, event_ptr)`
  - used in the event-shaping path around `0x140410CAF`;
- `virtual +0x68(owner, out_state)`
  - used as the fallback state-query provider.

Behaviorally, this is no longer just a fallback blob:

- it can be scanned as a candidate current object;
- it can become the installed current object at `owner + 0x1FF0`;
- it publishes descriptor/id data into owner state;
- and it participates in both query and event/update paths.

Best current reading:
`+0x1FF0` is a current-state selection/runtime object inside the broader
gameplay-state subsystem.

### Additional `+0x7A8` Consumers Strengthen the Capability-Gate Reading

The object returned by `state_object->virtual +0x90` is no longer the only
place where `virtual +0x7A8` matters.

Additional confirmed consumers:

- `0x1401A2D7A`
  - first resolves a runtime object through `0x141CE9C00`;
  - checks a bitset gate in `[object+0x30]+...`;
  - then calls `object->virtual +0x7A8`;
  - writes success as a bool-like output.
- `0x1402BD403`
  - calls `object->virtual +0x7A8`;
  - only if that passes does it continue with an additional object/flag test at
    `candidate + 0x108`.
- `0x14041458A`
  - under a specific runtime state (`[r12+0x128] == 1`), resolves a deeper
    object through owner virtual `+0xF48`;
  - then calls that object's `virtual +0x7A8` with an id from `r12 + 0x28`.

So `+0x7A8` is not a one-off quirk of the `+0x90`-returned object path. It
already behaves like a deeper gameplay capability / availability hook that
multiple higher-level systems rely on.

### `+0x1FF0` Has Owner-Side Accessors and Reset/Install Lifecycle

The helper cluster around `0x140416AB0 .. 0x14041C8D9` confirms that
`owner + 0x1FF0` is a first-class owner-managed object, not just an offset that
happens to be dereferenced often.

Small public accessors:

- `0x140416AB0` / `0x140416AC0`
  - return `owner + 0x1FF0`;
- `0x140416AD0`
  - returns `owner + 0x1FF0 -> +0x70`, or fallback label `0x1428D90A8`;
- `0x140416AF0`
  - copies `owner + 0x1FF0 -> +0x78` into caller storage, or `0` when absent;
- `0x140416B10`
  - returns owner field `+0x236C`, which is populated from selected
    `current_state_object + 0x7C`.

Lifecycle/reset paths:

- `0x14041C7E0`
  - clears owner bits `+0x1D52/+0x1D0B`;
  - calls current-object `virtual +0x30`;
  - clears bit `0x8` in `current_state_object + 0x80`.
- `0x14041C820`
  - tears down nested attachments from `current_state_object + 0x18`;
  - calls current-object `virtual +0x38`;
  - derives one more owner flag from `current_state_object + 0x80`;
  - copies signed id `+0x7C` into `owner + 0x2368`;
  - resets `owner + 0x236C` to `0x20`;
  - clears `owner + 0x1FF0`;
  - clears a mode bit in `owner + 0x1FA8`.
- `0x140416F90`
  - if a current object exists, reports its `+0x70` label and jumps into the
    same teardown helper `0x14041C820`.

Taken together, that is a real lifecycle:

- install current object;
- publish its label/id;
- activate/deactivate it through object virtuals;
- and clear it while synchronizing owner flags.

This strongly supports the reading that `+0x1FF0` is the owner's current
gameplay-state selection object rather than a passive fallback blob.

### `0x140416B20` Installs a Current-State Record Into the Owner Runtime

The former unlabeled vtable slot at `0x140416B20` is no longer just the early
native teleport path from the first prototype. In the current owner/runtime
class it behaves like a state-record install/apply method that bridges:

- current selected object at `owner + 0x1FF0`,
- attached materialized object at `owner + 0x1F80 -> +0x30`,
- nested control family at `owner + 0x15C8/+0x15C0`,
- and owner-visible cached state at `+0x4600..+0x460A` plus transforms
  `+0x4160..+0x4180`.

Confirmed structure:

- if an attached materialized object is present in `owner + 0x1F80 -> +0x30`:
  - and it has state bits `0x1|0x2`, it first receives `virtual +0x68(owner)`;
  - the low 28 bits from `slot+0x10` are copied back into
    `(*(owner+0x1F78))+0xC`;
  - the attachment is cleared.
- owner byte `+0x1D51` then gets `bit 0x2` set.
- next it calls `0x141DE9E60(owner, state_record, mode, flag)`.
- after that it copies the current owner transform from `owner virtual +0x1C8`
  into:
  - `owner + 0x4160`
  - `owner + 0x4170`
  - `owner + 0x4180`

Then the function validates the incoming state record through the global
descriptor table `0x14386FD00` and either:

- logs a failure with a fallback label from `record->virtual +0x40`, or
- asks `record->virtual +0xB0(owner)` for an accept/apply gate.

If `record->virtual +0xB0(owner)` fails, it logs and exits.

If it succeeds:

- the special tag `record_object + 0x60 == 0xAC710201` is checked;
- under that tag, `0x141E181D0(owner + 0x15C0)` can block the rest of the
  install path;
- otherwise the owner cache at `+0x4600..+0x460A` is refreshed from the record:
  - `+0x4600` current descriptor/object id
  - `+0x4604` current mode/state id
  - `+0x4608/+0x4609/+0x460A` three record flag bytes

If the incoming `state_record + 0x8` is nonzero, the cache is populated from
that record directly.

If the incoming `state_record + 0x8` is zero:

- the owner cache is reset to defaults;
- if `state_record + 0xE != 0`, the attached materialized object is cleared via
  `0x1404091A0`;
- then the function dispatches into `0x141DE4F90(owner, record_object,
  byte_0xC, byte_0xD)`.

So `0x140416B20` is best read as an owner-side
`InstallOrApplyCurrentStateRecord`, not as a pure movement helper.

### `0x141DE4F90` Applies a State Record Through Nested Control and Actor Runtime

`0x141DE4F90` turned out to be the heavy apply path behind the zero-state
branch of `0x140416B20`.

Observed behavior:

- it starts from `owner + 0x15B8` and uses `*(owner+0x15B8)+0x1C8` as a scalar
  seed into `0x141E0D530(record_object, ...)`;
- it logs the resolved object name/label;
- it resolves a previously bound control object through
  `(owner + 0x15C0) + 0x2F8 -> 0x141DF35F0`;
- if an old control object exists and differs from the incoming record object,
  it asks the nested-control family `owner + 0x15C8` for a control node by
  `old_object + 0x60` and calls that node's `virtual +0xC0(0)`;
- it then resolves a new control node by `record_object + 0x60` through
  `owner + 0x15C8 -> virtual +0x10`.

If the new control node is found:

- owner transforms `+0x100/+0x120/+0x140` are packed into a stack block;
- `0x141E12CE0(owner + 0x15C8)` runs before the node call;
- the node receives:
  - the incoming record object,
  - a packed owner transform/state block,
  - a float argument,
  - a mode byte,
  through `node->virtual +0xE0(...)`.

If that node-side apply succeeds:

- the record object's transform block at `+0xF0/+0x100/+0x110` is copied into a
  temporary destination;
- owner virtual `+0xC48` is called with that destination and zeroed flags,
  which matches the previously recovered native teleport/apply path;
- `0x141E17530(owner + 0x15C0, record_object)` runs next;
- and, if present, `0x141E0EDF0(owner + 0x15B8, transform_block)` finalizes the
  update.

This is materially stronger than the old `MoveTo`-style prototype:
the zero-state branch already drives a proper nested-control node, then uses
the owner-native `+0xC48` path to commit the record object's transform.

### `0x141DE9E60` Seeds Owner Runtime State From the Incoming Record

The earlier guess that `0x141DE9E60` was only a validator was too weak.
It is a large owner-state seeding routine that runs before
`InstallOrApplyCurrentStateRecord` finishes.

Confirmed early effects:

- logs the incoming record with string block `0x142B82478`;
- validates the record against the global authored table `0x14386FD00` using
  `record[+0x8]` and `record[+0xC]`;
- passes the matching descriptor (or null) into owner virtual `+0x1E8`;
- copies `record + 0x30/+0x40` into owner transforms `+0x130/+0x140`;
- calls owner virtual `+0x208`;
- dispatches `0x141E0ED10(owner+0x15B8, owner, mode)`;
- snapshots record state through `0x140186600(record, ...)` and
  `0x141E0EDF0(owner+0x15B8, snapshot)`.

Then it performs a broad owner-state reset/seed:

- clears and re-seeds transform/state mirrors around
  `+0x17A0..+0x1820`, `+0x19F0`, `+0x175C`, `+0x1768`, `+0x1998/+0x199C`;
- calls `0x141DE4A50(owner)`;
- uses the owner's retained pair `+0xA8/+0xBC` to resolve the live actor again;
- if live actor resolution succeeds, it marks the actor active and invokes its
  `virtual +0xD0` with a large synthesized record block;
- if the caller passed the boolean flag in `r9b`, it also calls
  `0x141E17240(owner + 0x15C0)`;
- finally it sets bit `0x100000` in `(*(owner+0x1A8))`.

So `0x141DE9E60` is best understood as
`SeedOwnerRuntimeFromIncomingStateRecord`, with validation only as its first
phase.

### `0x14041C900` Monitors and Reconciles Current-State Object vs Nested Control

The owner-side monitor around `0x14041C900` is now clear enough to treat as a
current-state reconciliation path.

It operates on two owner bytes:

- `+0x1D51`
- `+0x1D52`

and on two cached state integers:

- `+0x1EC0`
- `+0x1EC4`

Main behavior:

- when `+0x1D51` is negative or `+0x1D52 bit 0` is set, it enters a heavy
  reconcile path;
- if `+0x1D51` is negative, it compares `+0x1EC0` with owner state arrays
  `+0x1A38/+0x1A30` and may log the current `+0x1FF0->+0x70` label before
  clearing the sign bit in `+0x1D51`;
- otherwise it asks nested control `owner + 0x15C8 -> virtual +0x20`,
  then uses that result's `virtual +0x198` to compare against `owner + 0x1EC0`;
  mismatch triggers `0x141DE54A0(owner, old_state_id)`.

Then it reconciles `owner + 0x1EC4`:

- asks `owner + 0x15C8 -> virtual +0x50`,
- if non-null, calls the returned object's `virtual +0xB0`,
- compares that value with `owner + 0x1EC4`.

If the value changed:

- `owner virtual +0x5E8` gates whether nested control should be updated;
- when allowed, `owner + 0x15C8 -> virtual +0x58` receives the previous
  `+0x1EC4` through `virtual +0xA0`;
- then `0x141DE52E0(owner, scalar)` is called;
- otherwise the new value is simply stored into `owner + 0x1EC4`.

If the value matches and there are no pending sign/bit gates:

- it logs the current selected label from `+0x1FF0->+0x70`,
  the textual state name for `+0x1EC4`,
  and the owner-side descriptor entry from `+0x1A38[+0x1EC0]`;
- it calls `current_state_object->virtual +0xB0(owner)`;
- and clears the sign bit in `+0x1D51`.

If the heavy reconcile path does not run, but nested control reports a valid
object through `virtual +0x50 -> virtual +0xC8`, it calls
`owner + 0x15C8 -> virtual +0x58 -> virtual +0xD0()` and returns success
without touching the current-state object.

Finally, when the reconcile path is active, it wraps a call to
`current_state_object->virtual +0x28(owner)` with temporary `+0x1D51 bit 0`
set/cleared.

This makes `0x14041C900` the clearest owner-side bridge so far between:

- current selected state object at `+0x1FF0`,
- nested control family at `+0x15C8`,
- owner state caches `+0x1EC0/+0x1EC4`,
- and the actor-visible state arrays at `+0x1A38...`.

### `0x140417040` Is a Readiness Gate Over Nested Control, `+0x1FF8`, and Owner Mode Bits

The small helper at `0x140417040` is not a generic boolean. It is a compact
owner-side readiness/commit gate that ties together:

- nested control `owner + 0x15C8`,
- a vtable path at owner virtual `+0x8C0`,
- helper object `owner + 0x1FF8`,
- and mode flags inside `owner + 0x1FA8`.

Observed flow:

- first ask `owner + 0x15C8 -> virtual +0x50`;
- if that returns an object and its `virtual +0xC8` succeeds, return success
  immediately;
- otherwise ask owner virtual `+0x8C0` for a deeper object;
- if absent, fail;
- if present:
  - call `owner + 0x1FF8 -> virtual +0xA0()`;
  - call `returned_object->virtual +0x878(0)`;
  - inspect `(*(owner+0x1FA8))+0x1`;
  - and when that signed byte is negative, ask
    `owner + 0x15C8 -> virtual +0x58 -> virtual +0xC0()`.

The function returns `al = 1` only after those gates succeed, so it is best
read as a higher-level owner readiness gate rather than a plain helper.

### `0x14041CBF0` Computes an Actor-State Scalar Through `+0x1FF8`, `+0x1808`, and Nested Control

The larger path at `0x14041CBF0` is a new strong bridge into world-facing
gameplay state.

Inputs/anchors:

- owner virtual `+0xFB8` yields a runtime object;
- owner descriptor pair `+0x108/+0x10C` is validated against the authored
  table `0x14386FD00`;
- owner transform at `+0x120`;
- helper object `owner + 0x1FF8`;
- decision object `owner + 0x1808`;
- nested control family at `owner + 0x15C8`;
- helper cluster `owner + 0x1A0/+0x2388`.

Confirmed behavior:

- the authored descriptor at `0x14386FD00[owner+0x108]` is copied into
  `owner + 0x110` when `+0x10C` matches;
- owner virtual `+0xC10` fills a local transform block;
- the runtime object from `+0xFB8` receives `virtual +0x8C8`, producing an
  orientation block that is combined with owner transforms;
- the function builds a normalized vector/scalar metric from:
  - owner transform,
  - runtime-object orientation,
  - caller vector in `rsi`,
  - and threshold value in `xmm2`.

Then it interacts with three higher-level systems:

- `owner + 0x1808 -> virtual +0x0(owner)` acts as a late-decision gate;
- owner virtual `+0x1018` provides a second gate when the decision object says
  "not now";
- `owner + 0x15C8` is queried through:
  - `virtual +0x28`
  - or `virtual +0x38`
  - or `virtual +0x48`
  depending on `owner + 0x15B8` state;
  and the returned object's `virtual +0x150()` contributes a scalar multiplier.

The function also uses `owner + 0x1A0 -> +0x58 -> +0xCC` and local slots around
`+0x2818`, `+0x2388`, then stores the final scalar into:

- `(*(owner+0x1A0)->+0x58)->+0xDC`
- and, when present, `owner virtual +0xCE8 -> +0x58`

So this is not a camera-only path. It is an owner-side actor-state metric
builder that blends:

- runtime orientation,
- current decision node,
- nested control mode,
- and helper-object scaling.

That is already much closer to a world/action participation layer than to old
NPC locomotion plumbing.

### `0x14041FA90` Confirms That `+0x1FC0/+0x1FD8/+0x1FE8/+0x1FF8` Form a Shared Runtime Helper Bundle

The larger update around `0x14041FA90` confirms that the helper fields after
`+0x1F80` are used together in one owner-runtime bundle.

Observed pattern:

- `0x14048F410(owner + 0x1F80, owner)` is called first, tying the attached
  materialized-state slot back into the owner;
- under owner virtual `+0x5E8`, the function then conditionally touches:
  - `owner + 0x1FC0` via `0x14063AD40`
  - `owner + 0x1FD8` via `0x1403F9B30`
  - `owner + 0x1FE8`, validating its embedded actor handle/id and refreshing it
    through `0x140643970`
  - `owner + 0x1FF8` via `virtual +0x18`

The `+0x1FE8` path is now stronger:

- if its embedded actor handle/id resolves to a live actor, that actor is
  marked active;
- the helper's state id at `+0x10` is refreshed through `0x140643970`;
- if the id changed, the live actor gets `virtual +0xF68()`;
- then `helper + 0x28` is refreshed from `live_actor->+0xE0->+0x10`.

The tail of the same function then:

- asks `owner + 0x1808 -> virtual +0x0(owner)`;
- falls back to owner virtual `+0x1018` and `(*(owner+0x1FA8))+0x8`;
- chooses nested-control branches through `owner + 0x15C8` virtual
  `+0x28/+0x38/+0x48`;
- gets a scalar from the returned object's `virtual +0x150()`;
- multiplies it with helper state under `owner + 0x1A0 -> +0x58`;
- and stores the final result both in that helper object and through owner
  virtual `+0xCE8`.

So the helper family after `+0x1F80` is not a bag of unrelated structs.
It is a coordinated runtime helper bundle that feeds both actor-bound refresh
and higher-level scalar/state updates.

### `ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode` Already Has Cross-System Callers

The first concrete callers of `0x14041CBF0` make its role much stronger.

Confirmed callsites:

- `0x140425C34`
  - first tries a fast "current live object" path through `owner+0x23D8/+0x23E0`;
  - otherwise gates through owner virtual `+0x8D8` and `+0x8E8`;
  - asks `0x141DE6500(owner, &vector)` for a direction/target vector;
  - uses a fixed threshold constant in `xmm2`;
  - then calls `ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode`;
  - success returns immediately as a bool-like gate.

This makes `0x140425BC0..0x140425C44` a higher-level owner gate that already
consumes the actor-state scalar as a decision primitive, not as debug math.

- `0x1406AC5C9`
  - comes from the same broad region as the control-routing / locomotion work;
  - builds a hashed/descriptor block through `virtual +0x2D8` or fallback
    `0x1406AC010`;
  - uses `float [rbx+0xA0]` as the scalar threshold input;
  - then calls `ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode`;
  - on success continues into `0x1406AD290`.

This is the strongest bridge so far between the old control-routing work and
the new actor-state ownership work:

- the scalar path is not isolated;
- it is already consumed by both owner-side decision gates and a deeper
  locomotion/control-routing path.

### `0x14041CED0` Resolves a Reference Transform Through `+0x1FC0`, Selector Gates, or Retained Players

The function beginning at `0x14041CED0` is a new useful bridge around
`owner + 0x1FC0`.

Its output is a 16-byte transform/vector block written to caller storage.

Observed resolution order:

1. zero output;
2. inspect `owner + 0x1FC0`:
   - if present and its `+0x1E8` differs from the global id
     `0x143D4B028`, resolve an object via `0x14063C390`;
3. otherwise try owner helper `0x140426160(owner)` and then
   `0x140426390(owner)`;
4. if a candidate object exists:
   - build a CRC/hash from string block `0x1429195C0`;
   - validate the candidate against the selector registry with
     `0x1417C4800(candidate->+0x30, hash)`;
   - if the selector is enabled, use that candidate;
5. otherwise fall back to retained player slots:
   - first `slot 0` via `0x1410F0D10(0)`;
   - then `slot 1` via `0x1410F0D10(1)`;
   - then extract the chosen live actor transform at `+0x100` through
     `0x14014E240`.

So `+0x1FC0` is not just another opaque helper:
it participates in resolving a reference transform, but can yield to
selector-capable helper objects or to retained player slots when needed.

That further supports the reading that this owner-family already sits on a
shared gameplay/reference-state layer, not on a companion-only island.

### Independent Callers of `ApplyStateRecordThroughNestedControlAndActorRuntime`

The heavy apply path `0x141DE4F90` already has several independent callers,
which means it is a reusable state-application primitive, not a one-off helper.

Confirmed external callsites:

- `0x140416DF8`
  - zero-state branch of `InstallOrApplyCurrentStateRecord`;
- `0x1404380CB`
  - after `candidate->virtual +0xB0(owner)` and `0x1403F41F0`, applies the
    candidate with zero mode bytes and caller-supplied scalar;
- `0x140438159`
  - same family, but after `0x1409DA630(owner, transform)` resolves a
    candidate object;
- `0x1404BAA4B`
  - behind a mode branch keyed by `edx`, after resolving an object through
    `0x141DF35F0`, with `r9b = 1`.

This is enough to treat `0x141DE4F90` as a shared
state-object-to-owner application path. That matters because it means Nadine's
future second-player path does not need to invent a custom bridge if this API
can be driven correctly.

### `+0x1FC0` Is a Locked Owner-Side Collection/Filter Subsystem

The large owner path at `0x14041D0A0` and its helpers make `+0x1FC0` much
clearer.

What is now confirmed:

- `owner + 0x1FC0` has an internal lock/counter at `+0x50`;
- `0x140640360(helper_1FC0, out_list, mode)` walks the helper's entry pool at
  `helper+0x60`, checks per-entry type byte `entry->virtual +0x10 -> +0x190`,
  and appends matching entries into a bounded output array;
- `0x14063CF00(helper_1FC0, retained_pair)` validates whether a retained/live
  pair matches one of those helper entries while filtering out at least two
  disallowed entry kinds (`+0x190 == 2` or `7` are treated specially).

The owner wrapper `0x14041D0A0` then:

- locks `owner + 0x1FC0`;
- asks `0x140640360(..., mode=7)` for a list of candidate entries;
- for each candidate:
  - requires `candidate->virtual +0x38`;
  - extracts a retained/live pair from `candidate->virtual +0x10`;
  - checks that pair against `helper_1FC0` using `0x14063CF00`;
  - if that check fails, builds a large synthesized record block from:
    - candidate metadata `candidate + 0x18`
    - owner retained pair `+0xA8/+0xBC`
    - global hashed string block `0x142937258`
    - current live actor resolution
    - and several zero/default state regions;
  - then feeds that record into a live actor through `live_actor->virtual +0xD0`.

So `+0x1FC0` is not a passive blob. It is a formal owner-side helper that:

- enumerates candidate state objects,
- filters them against retained/live actor identity,
- and when needed synthesizes/publishes actor-facing records.

That is directly relevant to the project goal, because it is already a world/
actor participation path rather than a camera-only or locomotion-only helper.

### `0x14041D0A0` Is an Owner-Side Publish/Refresh Pass Over `+0x1FC0` Candidates

With the helper semantics in place, the outer function `0x14041D0A0` reads as a
publish/refresh pass:

- enumerate helper candidates from `+0x1FC0`;
- reject entries that fail local readiness or retained/live matching;
- for accepted entries, synthesize a large runtime record;
- and push that record into the current live actor through `virtual +0xD0`.

The same function also performs this process twice with two different record
layouts:

- one block rooted around `rbp+0x1E0`
- another around `rbp+0x0B0`

both seeded from:

- candidate identity,
- owner retained pair `+0xA8/+0xBC`,
- global hashed string blocks,
- and the current live actor.

This makes `0x14041D0A0` one of the clearest current bridges between:

- owner helper subsystem `+0x1FC0`,
- retained/live actor identity,
- and actor-facing runtime record publication.

### `ResolveOwnerReferenceTransformViaHelper1FC0OrRetainedPlayers` Has a Real External Caller

The transform resolver `0x14041CED0` is not isolated.

A confirmed external callsite exists at:

- `0x1401779F1`

So the helper/retained-player transform resolution path is already reused
outside the immediate local cluster, which strengthens the reading that it is a
general reference-transform service rather than one local fallback.

### `AdvanceControlRoutingAfterActorStateScalarSuccess` Is Widely Reused

The control-routing continuation `0x1406AD290` is much more central than it
first looked.

Confirmed external callsites include:

- `0x1406AC5D8`
- `0x1406ADE96`
- `0x1406AE161`
- `0x1406AE4DB`
- `0x1406AF405`
- `0x1406AFE3C`
- `0x1406B2B74`
- `0x1406B4E1D`
- `0x1406B595E`
- `0x1406B62D5`
- `0x1406B62F3`
- `0x1406B653C`
- `0x1406B672F`

So the post-scalar continuation is a shared control-routing transition node
across a broad region of the old locomotion/control dispatcher work.

That means the bridge we now have is not narrow:

- owner/runtime actor-state scalar gate on one side,
- broad control-routing transition cluster on the other.

This is currently one of the best structural candidates for moving Nadine from
"companion with special handling" toward a real player-like control path.

### `0x14041DE36` Sits in a Larger Owner Update Loop

The caller around `0x14041DDC0..0x14041DEAF` confirms that
`PublishOwnerRuntimeRecordsFromHelper1FC0Candidates` is not an isolated helper.

In that update path:

- owner virtual `+0xE90` runs first;
- then `0x14041D0A0` is called;
- afterwards the same owner update loop continues into:
  - `owner + 0x1CA0 -> virtual +0x50`
  - owner virtual `+0x8E8`
  - helper `owner + 0x1FC0 -> 0x14063D800`
  - a cluster of further owner methods:
    - `0x1404370C0`
    - `0x140436F20`
    - `0x140439E60`
    - `0x14043A2F0`
    - `0x14043AC60`

So `0x14041D0A0` is embedded in a larger per-owner update pipeline, not called
ad hoc.

### External Consumer of the Reference Transform Resolver

The confirmed external caller at `0x1401779F1` gives semantic weight to
`ResolveOwnerReferenceTransformViaHelper1FC0OrRetainedPlayers`.

That path:

- fetches `CameraManager(0)` and another transform block;
- writes several local/output transforms around `rdi + 0x440/+0x460/+0x4F0`;
- then calls `ResolveOwnerReferenceTransformViaHelper1FC0OrRetainedPlayers`
  with the owner in `r15`;
- stores the returned 16-byte block into `rdi + 0x440`;
- and marks local output flags afterwards.

So the resolver is already used to feed a larger produced-state object, not
just queried for diagnostics.

### `0x14041D710` and `0x14041D820` Add More Readiness/Availability Gates

Two adjacent owner helpers refine the same runtime story.

`0x14041D710`:

- if a global gate byte is enabled, it first tries owner virtual `+0x9B0`;
- from that object it asks `0x14128C910(..., 1)` and `(..., 2)`;
- requires both returned nodes to yield non-null `virtual +0x70` results;
- and also requires `object + 0x1042 != 0`.

If that early path fails, it falls back to nested control:

- `owner + 0x15C8 -> virtual +0x80`
- then `returned->virtual +0x180`
- then `returned->virtual +0x128`

Finally the result is still modulated by:

- a global mode value at `0x146A8FC28`
- owner virtual `+0x4C0`
- owner byte `+0x1D0B`

This makes `0x14041D710` a real multi-source readiness gate spanning:

- a higher owner object from `+0x9B0`,
- nested control,
- global runtime mode,
- and owner local flags.

`0x14041D820`:

- checks `owner + 0x1F90`;
- if present and `(+0x840) != 1`, it asks owner virtual `+0xE20`,
  then passes that object into `0x1405F3F10`;
- success requires the returned object to have `byte +0x189 != 0`.

So `+0x1F90` is not just an old materialized-state neighbor.
It participates in yet another owner-side readiness/availability gate, and it
shares the same `+0x840` field that was already touched from the control-routing
side.

That is another strong bridge between:

- owner gameplay-state cluster around `+0x1F78..+0x1FF8`,
- and the broader control-routing stack.

### `0x14063D800` Is a Post-Pass Over the `+0x1FC0` Helper That Updates Four State Anchors

The helper method `0x14063D800(owner+0x1FC0)` sits immediately after
`PublishOwnerRuntimeRecordsFromHelper1FC0Candidates` in the larger owner update
loop, and it is not a trivial cleanup.

What it does:

- takes the current helper mode from `helper + 0x1F4` and immediately clears it;
- walks the entry pool behind `helper + 0x60` again;
- chooses a preferred candidate by looking at:
  - entry enable flags,
  - entry ordering/priority around `entry + 0x300`,
  - and retained/live identity checks;
- then uses `owner + 0x1FB0 -> +0x10` as a host object containing four anchor
  regions:
  - `+0x460`
  - `+0x4D0`
  - `+0x540`
  - `+0x5B0`

Those four anchors all have:

- byte flags at `+0x6E`
- transform-like data at `+0x38`

The function then:

- computes multiple world-space vectors from the preferred candidate and from
  the current live actor;
- writes those vectors into the four anchor objects via:
  - `0x142143F60`
  - `0x142144030`
- toggles each anchor's `+0x6E bit 0` depending on whether that anchor is
  active/valid;
- and, when a selector/capability path succeeds, seeds two of those anchors
  directly from a second live object resolved through `0x141E9CCE0`.

This is a strong structural result:

- `+0x1FB0` is no longer just a small helper allocation;
- through `+0x10` it leads into a host object with four explicit runtime
  anchors;
- and `0x14063D800` is the pass that refreshes those anchors from the current
  helper/candidate/live-actor state.

### `0x1404370C0` Is a Thin Owner Wrapper Over `0x14063D800`

`0x1404370B0` is a direct thunk:

- `owner + 0x1FC0 -> 0x14063D800`

So when the owner update loop at `0x14041DE8F` calls `0x1404370C0`, it is
simply running the helper post-pass described above.

### `0x140436F20` Syncs Two `+0x1FB0` Host Anchors From the Current Actor and `+0x1FF8`

The first post-pass consumer after `0x14063D800` is `0x140436F20`.

It works off:

- `owner + 0x1FB0 -> +0x10`
- two host anchor regions:
  - `host + 0x620`
  - `host + 0x690`
- current actor from owner virtual `+0xA10`
- helper `owner + 0x1FF8`
- fallback state vectors from `0x141DE6660`

Behavior:

- requires current actor tag `0xAC710200`;
- sets owner byte `+0x1D54 bits 0x20/0x40` depending on whether anchor
  `+0x620` and `+0x690` currently advertise valid state through their `+0x6E`
  flag pattern;
- obtains a base vector either from `owner + 0x1FF8 -> virtual +0x40` or from
  fallback helper `0x141DE6660(owner, slot=0)`;
- if owner byte `+0x1D56 bit 0x20` is set, pulls a second vector from current
  actor `virtual +0x78` and pushes both vectors into anchor `+0x620` using:
  - `0x142144030`
  - `0x142143F60`
- if owner byte `+0x1D56 bit 0x40` is set, does the same with current actor
  `virtual +0x80` for anchor `+0x690`.

So this function is a focused two-anchor sync path:

- current actor
- current/fallback state vector
- two host anchors under `+0x1FB0`
- owner bits `+0x1D54/+0x1D56`

### `0x140439E60` Builds a Geometry/Threshold Gate Around the Current Actor and a Helper-Resolved Object

`0x140439E60` is the first heavy consumer after the helper post-pass.

Inputs:

- owner flag `+0x57C bit 0x2`
- current live actor from retained slot
- nested control base object `owner + 0x15C8 -> +0x8 -> +0x10`
- object resolved through `nested_base + 0x5D0 -> 0x141DF35F0`
- owner transform via virtual `+0x1C8`
- current actor transform/orientation
- world-space helper through `0x141E36060`

It then:

- computes a direction vector from actor/object geometry;
- checks two scalar thresholds;
- builds another projected point from actor transform and current motion basis;
- asks `0x141E0D220` for an additional projection/reference;
- computes a normalized distance-like scalar;
- and if the final scalar is below a threshold, calls:
  - `0x141DE52E0(owner, small_scalar)`
  - current nested-control object `virtual +0x78`.

This function is no longer just “some math”.
It is a real geometry/threshold gate that decides whether to push the owner
deeper into a nested-control action path.

### `0x14043A2F0` Is a Large Action/Targeting Preparation Pass Tied to the Same Owner Stack

`0x14043A2F0` is the heaviest node in this cluster and clearly sits on top of
the same owner/runtime stack.

Confirmed major anchors:

- owner byte `+0x1D59` as an early gate;
- owner virtual `+0x9B0` with `0x14128C910(..., 1/2)` validation;
- owner field `+0x1E84` indexing a large global table at `0x14389A960`;
- current retained/live actor checks via bitset selectors;
- current actor `virtual +0x860`;
- helper `owner + 0x1FC0 -> 0x14063C260`;
- helper `owner + 0x1F90 -> 0x1405F3F10`;
- `owner + 0x1FF0` current-state object hash/selector compare;
- `owner + 0x1FB0` host-backed candidate arrays;
- owner transform at `+0x100`.

The function then:

- validates that the current high-level owner object points back to the same
  owner;
- resolves a candidate object from the `+0x1E84` indexed table;
- checks several capability bits on the current live actor and candidate;
- evaluates proximity and distance thresholds against owner transform;
- checks helper/current-state selector consistency through `+0x1FF0`;
- and uses helper `0x14063C260` plus `+0x1F90` to validate candidate
  availability before continuing.

The later half iterates over a `0x20`-sized candidate array, repeatedly
resolves live actor identities, and applies additional distance and capability
tests before deciding whether to continue.

This is best read as a large action/target-preparation pass over the same
owner stack, not as low-level locomotion.

### `0x14043AC60` Continues the Same High-Level Action/Preparation Family

The next sibling `0x14043AC60` starts with the same owner gate byte
`+0x1D59 bit 0x20`, the same `+0x1E84` indexed table, and owner virtual
`+0x438 / +0x9B0` checks.

The opening phase already shows:

- mode/type discrimination from the owner-side object behind virtual `+0x438`;
- scalar extraction through `0x141B6AC70 / 0x141B6AE20`;
- another current-owner consistency check through virtual `+0x9B0` and
  `0x14128C910(..., 1/2)`;
- a capability-bit test at `0x143EEBF14`;
- and another candidate-array scan with actor identity validation and
  distance/proximity gates.

So `0x14043AC60` is not a separate unrelated subsystem. It is a sibling to
`0x14043A2F0` in the same high-level action/target-preparation family.

### Result for the Requested Cluster

The requested list now reads as one coherent layer:

- `0x14063D800`
  - refreshes four host anchors behind `+0x1FB0`;
- `0x1404370C0`
  - thin wrapper that runs that post-pass from the owner update loop;
- `0x140436F20`
  - syncs two of those anchors from current actor state and `+0x1FF8`;
- `0x140439E60`
  - computes a geometry/threshold gate and can trigger nested-control action;
- `0x14043A2F0`
  - runs a large candidate/action preparation pass over the same owner stack;
- `0x14043AC60`
  - continues that same high-level preparation family.

This is already well above locomotion plumbing. It is a genuine actor/action
participation layer sitting on top of:

- retained/live actor identity,
- helper cluster `+0x1F78..+0x1FF8`,
- nested control `+0x15C8`,
- current-state object `+0x1FF0`,
- and owner-side action/target selection logic.

### `0x141DE6500` Builds the Direction/Reference Vector Fed Into the Actor-State Scalar Path

The helper behind the owner-side caller at `0x140425C24` is now clear enough to
matter.

`0x141DE6500(owner, out_vec)`:

- scans owner state bytes at `+0x1922..+0x1920` to pick the currently active
  directional slot;
- falls back to `owner + 0x18F0` when no slot is marked active;
- otherwise reads one of the per-slot vectors stored in the same
  `+0x18F0/+0x1990` family;
- validates owner descriptor `+0x108/+0x10C` against `0x14386FD00` and copies
  the matching record into `owner + 0x110`;
- combines:
  - the chosen direction vector,
  - owner transform at `+0x120`,
  - and descriptor offset at `+0x110`,
  into the final vector written to caller storage.

So the actor-state scalar path is not consuming an arbitrary caller vector in
that owner-side branch. It is already driven by a synthesized owner-local
direction/reference vector derived from the same `+0x18F0/+0x1990` state family
that the late-decision/update code maintains.

### Control-Routing Success Path After the Actor-State Scalar Gate

The control-routing caller confirms that success in
`ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode` is not cosmetic.

At `0x1406AC5C9`:

- a hashed/descriptor block is built first;
- `float [rbx+0xA0]` is used as the threshold scalar;
- the owner/runtime object in `rdi` and the descriptor block are passed into
  `ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode`;
- and on success the code immediately continues into `0x1406AD290`.

`0x1406AD290` itself:

- clears a bit in `rbx + 0x27D0`;
- resets `rbx + 0x26A4`;
- then transitions into a second stage at `0x1406AD2E0`.

In that second stage:

- it resolves a candidate/current object through `record->virtual +0x8B8`;
- if that succeeds and nested control passes another gate via
  `owner + 0x15C8 -> ... -> virtual +0x150`,
  it reports one status string;
- otherwise it falls back to owner virtual `+0xA10` and reports a different
  status string depending on whether the resolved object has tag `0xAC710200`.

So the actor-state scalar gate is already upstream of a real control-routing
transition, not just a debug probe.

### `0x140417040` Looks Internal to This Owner Family

A raw scan over the executable did not reveal any external direct callsites for
`0x140417040` beyond its own body, which is consistent with it being consumed
primarily through a vtable/dispatch table rather than ordinary direct calls.

That fits the rest of the owner-family work:

- `0x140417040` behaves like a compact readiness gate;
- but its placement suggests it is probably reached through an owner/class
  dispatch slot rather than through many free-function callsites.

### `0x14063C260` Resolves a `+0x1FC0` Candidate Entry by Retained/Live Pair

The helper used repeatedly by `0x14043A2F0` and `0x14043AC60` is now clear
enough to treat as a real semantic node.

`0x14063C260(helper_1FC0, retained_pair)`:

- acquires the helper-side lock/global guard used by this helper family;
- walks the entry pool at `helper + 0x38` with stride `0x400`;
- only considers entries whose state byte `entry + 0x3F0` has bit `0x2`;
- resolves the entry identity through `entry->virtual +0x10`;
- compares the returned `(handle,id)` pair against the input retained/live
  pair;
- returns the first matching helper entry.

There is also a sibling lookup immediately after it:

- `0x14063C390(helper_1FC0, type_id)`
  - scans the same entry pool;
  - matches on `entry + 0x18`;
  - and returns the first enabled entry of that type.

So `0x14063C260` is the direct bridge from owner retained/live identity into
the `+0x1FC0` helper candidate table.

### `0x14040DB30` Caches a Selected Action Target Snapshot Back Into the Owner

The handoff inside `0x14043AC60` is not abstract. The call to `0x14040DB30`
materializes the selected target back into owner state.

`0x14040DB30(owner, selected_actor)`:

- sets `owner + 0x1D55 bit 0x2`;
- pulls a transform-like block from `selected_actor->virtual +0x1C8`;
- copies that into owner caches:
  - `+0x1ED0`
  - `+0x1EE0`
  - `+0x1EF0`
  - `+0x1F10`
  - `+0x1F30`;
- copies actor scalar/id state:
  - `selected_actor + 0xD8 -> owner + 0x1F40`;
  - `selected_actor + 0xA8/+0xBC -> owner + 0x1F48/+0x1F50`.

The sibling helpers immediately below it confirm the meaning of this cache:

- `0x14040DC10`
  - re-resolves the cached retained/live target from `+0x1F48/+0x1F50`;
  - if `+0x1D55 bit 0x2` is set, requires that target to be live, valid, and
    to return a non-null object through virtual `+0x9B0`;
- `0x14040DC90`
  - clears `owner + 0x1D55 bit 0x2`;
- `0x14040DCA0`
  - seeds another owner snapshot at `+0x1F60` from target
    `virtual +0x9A8` and sets `owner + 0x1D5B bit 0x8`.

So `0x14040DB30` is not generic logging. It is a concrete
selected-target/action cache inside the owner.

### `0x141EC4E90` Is a World-Space Probe Between Two Transforms

The fallback branch in `0x14043AC60` calls `0x141EC4E90` when the helper-table
match is absent. That call is world-facing.

`0x141EC4E90(start_vec, end_vec, out_record, config)`:

- validates the two input vectors;
- enters the engine-wide world/probe system;
- packages both transforms plus an external parameter/config record;
- and dispatches the query through `0x1420A7D30`.

The routine is full of spatial normalization and debug/assert scaffolding for
invalid vectors, not selector plumbing or pure data transforms. In context it
is used as a gate before `0x14040DB30` caches the selected actor.

So the fallback in `0x14043AC60` is already performing an actual world-space
probe, not only a table lookup.

### `0x14063E890` Publishes a Prepared State Record Into Helper `+0x1FC0`

The large calls after candidate preparation are also no longer opaque.

`0x14063E890(helper_1FC0, prepared_state_record)`:

- resolves the helper's live actor and marks it active;
- refuses to proceed when helper byte `+0x369` is set or when no live actor is
  available;
- takes the helper-owned lock at `+0xC0`;
- scans a watcher/list structure rooted around `+0x1D8`;
- asks each watcher object whether it accepts the incoming record via virtual
  `+0x28`;
- if none accepts it, allocates a new watcher node, initializes it through
  `0x1406427E0`, links it into the list, and then dispatches the record through
  virtual `+0x30`.

So `0x14063E890` is the helper-side publish/apply path for prepared
owner/action records.

### `0x1406427E0` Initializes the Helper-Side Published Record Object

The initializer used by `0x14063E890` is structured enough to name.

`0x1406427E0(record_obj)`:

- runs a base initializer;
- installs vtable `0x14296FAE0`;
- clears embedded object pairs at:
  - `+0x20/+0x28`
  - `+0x30`
  - `+0x40`
  - `+0xB0`
  - `+0x120`;
- clears pointer/id fields through `+0x1D8`;
- initializes an embedded retained/live pair at `+0x20` via `0x14060FFD0`;
- assigns a unique object id into `record_obj + 0x8`.

This is the helper-side published state object that `0x14063E890` allocates and
feeds with the prepared owner/action record.

### `0x14043AC60` Crosses From Preparation Into Real Action-Target Commitment

With those helpers resolved, the second half of `0x14043AC60` is much clearer.

After it picks the best candidate actor from the helper-backed candidate array,
it:

- checks a capability bit through `0x143D43FA4`;
- keeps the nearest valid candidate by world-space distance to owner transform;
- if the chosen candidate is close enough, asks that candidate's
  `+0x1FC0` helper for a retained/live match through `0x14063C260`;
- if the helper match exists and `candidate_entry->virtual +0x38` succeeds,
  it skips the fallback probe;
- otherwise it runs the world-space probe `0x141EC4E90`;
- and on success it commits the candidate into owner state through
  `0x14040DB30`.

Then it immediately publishes follow-up records:

- `0x14063E890(owner_or_candidate + 0x1FC0, prepared_record)`
- multiple calls to `0x1403CF8B0` with small mode ids and string handles
  around:
  - `0x1429383C0`
  - `0x1429383F0`
  - `0x1429384B8`
  - `0x1429384E0`;
- and finally reads `(*object)[0xE00]` through `0x14123F260`, setting
  `owner + 0x1D59 bit 0x80` when the returned state block carries the required
  flag.

This is no longer just action/target preparation. It is already a real
commit-and-publish layer:

- select candidate,
- validate with capability and world-space gates,
- cache target into owner,
- publish helper records,
- and latch owner action-state bits.

That is materially closer to full gameplay/world participation than the earlier
locomotion-only work.

### The `0x14043A2F0 / 0x14043AC60` Family Is Tied to AI Knowledge / Vox Semantics

The string payloads used in the second half of this family finally expose what
kind of gameplay/action layer this is.

The records passed through the small helper/log path point at these strings:

- `0x1429383C0`
  - `Vox interrupted. Informing '%s' (distraction)`
- `0x1429383F0`
  - `Informed of '%s' vox interrupted (distraction)`
- `0x142938430`
  - `Vox interrupted. Informing '%s' (instant knowledge of player)`
- `0x142938470`
  - `Informed of '%s' vox interrupted (instant knowledge of player)`
- `0x1429384B8`
  - `'%s' sees me in melee with player`
- `0x1429384E0`
  - `I see '%s' in melee with player`

There is also nearby source text around `0x142A67FA8`:

- `melee-attack`
- `MeleeScriptObserver::InitBehaviorInstanceData`
- `MeleeScriptObserver::InitTable`

That materially sharpens the interpretation:

- `0x14043A2F0`
  - is not a generic target-sorter;
  - it is working in the same semantic space as AI knowledge/awareness and
    melee/player perception;
- `0x14043AC60`
  - continues that path and can promote a chosen candidate into owner state,
    then emit the corresponding awareness/vox-style notifications.

So this owner/action family is already sitting in gameplay perception logic,
not merely in locomotion or anonymous helper maintenance.

### `0x1403CF8B0` Is a Tiny Notification/Logging Stub in This Path

The repeated calls after target commit do not themselves contain the logic.

The export-sized body at `0x1403CF8B0` is only a tiny stub/thunk. In the
current release build it does not perform meaningful state mutation by itself.
Its importance here is the payload it carries:

- owner pointer,
- small mode id (`0xA`),
- and the perception/vox strings above.

So in this family:

- real state mutation happens in helpers like
  `0x14040DB30`, `0x14063E890`, `0x14063C260`, `0x141EC4E90`;
- `0x1403CF8B0` is best treated as a lightweight event/log/notification edge.

### `0x14123F260` Behaves Like a Global Knowledge/State-Record Resolver

The final flag latch in `0x14043AC60` goes through `0x14123F260`, and that
resolver is clearly table-driven.

`0x14123F260(record_id)`:

- walks global registries and caches;
- uses CRC/table validation paths over static blobs;
- resolves a record object from global containers via `0x141764BC0` and
  `0x141765030`;
- returns an object whose data block at `+0x50` contains bit flags later tested
  by callers.

In `0x14043AC60` specifically:

- the caller reads `(*object)[0xE00]`,
- resolves it through `0x14123F260`,
- then checks `returned_record->+0x50`,
- and if the required bit is present, sets `owner + 0x1D59 bit 0x80`.

So the end of this family is not arbitrary. It latches owner action/perception
state from a global authored knowledge/state record after the candidate has been
selected and committed.

### `owner + 0x1D59` Is a Compact Perception/Action Bitfield, Not a Misc Flag Blob

Following the setters and freshness checks around `0x14043AC60` tightened the
meaning of this byte substantially.

#### Event bits `0x1` and `0x2`

The larger handler at `0x140421E70` processes an event tag returned through
owner virtual `+0x10D8` and reacts to two concrete hashes:

- `0x73A572E6`
  - sets `owner + 0x1D59 bit 0x1`;
  - snapshots current owner runtime handle into `+0x2190`;
  - rebuilds a derived vector and stores it at `+0x2450`;
- `0x1FE52103`
  - sets `owner + 0x1D59 bit 0x2`;
  - snapshots current owner runtime handle into `+0x2198`;
  - rebuilds a sibling derived vector and stores it at `+0x2460`.

Both paths also refresh the authored descriptor at `+0x108/+0x10C` and copy its
record into `owner + 0x110` before building those vectors.

Those two bits are not permanent state:

- `0x140422100`
  - returns true only while bit `0x1` is set and the current owner runtime
    handle is still within a short time window relative to `+0x2190`;
- `0x140422140`
  - does the same for bit `0x2` and `+0x2198`.

So bits `0x1/0x2` are recent-perception/recent-event latches with freshness
windows, not generic booleans.

#### Bit `0x40`

The path at `0x140404240` uses helper `+0x1FC0`:

- resolves a helper entry through `0x14063C260`;
- requires helper entry kind `+0x190 == 6`;
- requires `entry->virtual +0x8`, `+0x0`, and `+0x48` all to report false;
- then sets `owner + 0x1D59 bit 0x40`.

That bit is immediately read back as a one-bit result in the surrounding code.
So bit `0x40` is another explicit state latch tied to a specific helper-entry
condition, not generic noise.

#### Bit `0x80`

At the tail of `0x14043AC60`:

- `(*object)[0xE00]` is resolved through `0x14123F260`;
- the returned record's flag block at `+0x50` is tested;
- on success the code sets `owner + 0x1D59 bit 0x80`.

So bit `0x80` is authored-record driven and sits above the candidate-selection
and target-commit logic.

Taken together, `+0x1D59` is now best read as a compact owner
perception/action-state byte:

- recent event bit `0x1`
- recent event bit `0x2`
- helper-derived latch `0x40`
- authored-knowledge/action latch `0x80`

This is already squarely in gameplay participation territory.

### `0x14043B380` Derives an Owner Capability Flag From the Authored Descriptor

The function immediately after the `0x14043AC60` cluster is also relevant.

`0x14043B380(owner)`:

- clears `owner + 0x18B0`;
- resolves the authored descriptor at `+0x108/+0x10C`;
- follows `descriptor + 0x68` into another runtime object;
- resolves that object's retained/live actor;
- checks a capability bit via `0x143F067C4`;
- and if present sets `owner + 0x18B0 = 1`.

So the next layer above the `0x14043AC60` family is already consuming the same
authored descriptor to derive a compact owner capability/result flag.

That reinforces the current model:

- authored descriptor
- candidate/action selection
- world/perception gates
- owner perception bitfield
- derived owner capability flags

all belong to the same gameplay-facing owner stack.

### `0x14043B740` Publishes Resolved Knowledge Records Into an External Sink

The next helper after this capability layer is not cosmetic either.

`0x14043B740(owner, sink)`:

- resolves the owner's current knowledge/state id through virtual `+0xCF0`;
- passes that id into `0x141765030`;
- if the registry returns a record, publishes it into the sink through
  `0x1417F9150`;
- then iterates an additional owner-side collection at `owner + 0x2388`,
  resolves each member through `0x14043D730 -> 0x141765030`,
  and publishes each resolved record into the same sink through
  `0x1417F9150`.

`0x1417F9150` itself behaves like a registry-to-sink commit:

- resolves a destination slot through `0x141764B50`;
- writes `record + 0x10` into a per-slot integer array;
- writes the record pointer into the parallel pointer array;
- then calls a small refresh/finalize helper on the sink object.

So this is already a formal publish/broadcast step for resolved
knowledge/perception records.

### `owner + 0x1FA0` Is a Knowledge/Observer Manager

The two tiny wrappers at `0x14043B7F0` and `0x14043B810` jump into a larger
manager behind `owner + 0x1FA0`.

`0x141E63660(manager_1FA0)`:

- iterates a table of entries of size `0x28`;
- only acts on entries with `entry + 0x18 == 1`;
- resolves each entry's retained/live actor;
- builds a compact record using hashed/static templates;
- and dispatches it into the resolved live actor via `virtual +0xD0`.

This is not just local owner bookkeeping. It is an outward-facing manager that
pushes structured records into live actors.

At this point the chain is explicit:

- owner perception/action selection,
- authored knowledge/state resolution,
- target commit,
- sink publication (`0x14043B740` / `0x1417F9150`),
- manager-wide broadcast (`owner + 0x1FA0 -> 0x141E63660`),
- live actor `virtual +0xD0`.

That is a real world/system participation path, not only an internal control
stack.

### `0x141765030 / 0x1417651D0` Form a Global Knowledge-Record Registry Layer

The record helpers used by the perception path are now clearer.

`0x141765030(record_id)`:

- resolves a global knowledge/state record by integer id;
- first tries a sorted registry structure;
- then falls back to a secondary hash/list container;
- returns a pointer to the matching registry entry payload when found.

`0x1417651D0(record_id)` sits one layer above:

- first calls `0x141765030`;
- if the record already exists, returns its payload immediately;
- otherwise enters a creation/assert path with global logging/debug helpers;
- then returns the created/resolved payload.

So these are not owner-local helpers. They are the global knowledge-record
registry used by the owner perception/action family.

### `0x14043B490 / 0x14043B5B0` Resolve `default-anim-state-npc-info`

The twin functions immediately after `0x14043B380` are not random CRC stubs.

Both `0x14043B490` and `0x14043B5B0`:

- work from owner-side runtime objects (`owner + 0x1A0` path);
- build the CRC for the string at `0x1429388F8`:
  - `default-anim-state-npc-info`
- and then feed that id into `0x1417651D0`.

The first variant:

- returns early if an existing nested object already exposes the needed
  subrecord at `+0x68`;
- otherwise resolves/creates the knowledge record via `0x1417651D0`.

So this family is already tied into shared NPC/default animation-state
knowledge, not just melee-only signaling.

### `0x14043B740` Publishes Into a Slot-Based Knowledge Sink

The sink commit helper behind `0x14043B740` is now better understood.

`0x1417F9150(sink, record_payload)`:

- resolves a slot descriptor through `0x141764B50`;
- uses `record_payload + 0xC/+0x10` as the incoming key/value;
- writes the value into `sink->value_array[slot]`;
- writes the record pointer into `sink->record_array[slot]`;
- refreshes the sink through `0x1417FA5A0`.

This is effectively a slot-based knowledge/perception sink, not an opaque
callback.

### `owner + 0x1FA0` Broadcasts Structured Knowledge Records to Live Actors

The manager behind `+0x1FA0` is now more specific than “some observer object”.

`0x141E63660(owner_manager_1FA0)`:

- iterates `0x28`-byte entries;
- for each active entry (`entry + 0x18 == 1`) resolves a live actor;
- constructs a structured hashed record from templates;
- pushes that record into the actor through `virtual +0xD0`;
- temporarily switches an external global object/context while dispatching.

So this is a formal knowledge/perception broadcast manager that already talks
to live actors through a structured record interface.

This matters for the mod goal because it means the owner stack we are tracing
already has a mechanism to:

- resolve authored knowledge state,
- store it in shared registries,
- publish it into sinks,
- and broadcast it to live actor systems.

That is exactly the sort of infrastructure Nadine will need if she is going to
participate in the world as a first-class gameplay actor rather than as a
remote-driven buddy.

### `0x141DF35F0` Is a Runtime Handle/Object Resolver Used by the Owner Control Stack

The helper we kept hitting under nested control is now concrete.

`0x141DF35F0(handle_field)`:

- takes a packed runtime handle/id;
- validates the high bits against the global handle tables;
- resolves the matching runtime object from `0x146A54B80`;
- returns the live object when the handle and object id agree;
- otherwise returns null.

This resolver is used directly in the owner/nested-control family to compare
multiple handle slots and check whether they still point to the same runtime
object.

### `0x141DE5430` Checks That Two Nested-Control Runtime Handles Still Resolve to the Same Object

The helper just above `0x141DE54A0` now reads cleanly.

`0x141DE5430(owner)`:

- resolves `owner + 0x15C0 + 0x2F8` through `0x141DF35F0`;
- resolves `owner + 0x15C0 + 0x258` through `0x141DF35F0`;
- if both resolve and point to the same runtime object,
  returns `owner + 0x15C0 + 0x310`;
- otherwise returns null.

That means the knowledge/perception path is not floating above gameplay state.
It explicitly verifies that multiple nested-control runtime handles still
identify the same live object before using the follow-up control block at
`+0x310`.

### The Current Bridge Into Player-Facing Systems Goes Through Runtime Handles, Not Raw Positions

At this point the useful pattern is stable:

- selection and perception do not act on raw transforms alone;
- they repeatedly resolve packed runtime handles into live objects;
- they compare those resolved objects across nested-control slots;
- and they use those resolved objects to:
  - seed owner caches,
  - publish knowledge records,
  - broadcast through managers,
  - and dispatch into live actors via `virtual +0xD0`.

This is exactly the kind of bridge we need for Nadine:
if we want the world to react to her like it reacts to Chloe, we will probably
need her to participate through the same runtime handle/object layer, not just
through transform ownership or locomotion control.

### A Dedicated `MeleeScriptObserver` Class Family Sits on Top of the Same Runtime/Knowledge Layer

The string block around `0x142A67F80` and `0x142A69600` is now useful beyond
"there is melee stuff nearby".

Confirmed string groups:

- `0x142A67F88`:
  - `melee-attack-behavior-list`
  - `melee-attack`
  - `MeleeScriptObserver::InitBehaviorInstanceData`
  - `MeleeScriptObserver::InitTable`
- `0x142A69600`:
  - `*player-coop-melee-damage*`
  - `*default-melee-lists*`
  - `*online-melee-lists*`
  - `melee-settings`
  - `*dlc-melee-attack-overlay*`
  - `net-melee-no-ap`

The function at `0x1411DE2E0` now looks like the construction/init side of this
family, not a random string user:

- CRCs the string block at `0x1428D8960` (`Player`);
- compares that id against `*(u32*)(runtime_descriptor + 0x38)`;
- on match allocates a `0x1C0` object;
- installs vtable `0x142A69798`;
- seeds multiple runtime/transform/default-state fields;
- then continues through the same actor-facing runtime and knowledge layer.

So this is a player-tagged melee observer family, not generic AI fluff.

### `0x14124AFC0` Is a Melee-Observer Update Path Wired Into Live Actors and the Knowledge Registry

The large method starting at `0x14124AFC0` sits directly under the
`0x142A69798` vtable family and is now clearly gameplay-facing.

It:

- resolves a knowledge/state record through `0x14123F260` using
  `tracked_actor->e00`;
- resolves the live actor from the observer's retained/runtime pair at
  `object + 0xB0/+0xB8`;
- marks that actor active through `+0x72`;
- talks to the actor through virtual slots `+0x5C8`, `+0x9B0`, and `+0xBE0`;
- hashes either the authored melee-settings path or the fallback
  `net-melee-no-ap` path from the string table at `0x142A69660/0x142A69690`;
- packages the resulting data through the helper family at `0x1411E00D0 /
  0x1411E0230`;
- and commits the resolved state id back into `object + 0xC0`.

This is not a static data loader. It is a live update path for a melee-related
observer object that already depends on:

- live actor identity,
- authored knowledge records,
- actor virtual interfaces,
- and per-actor state publication.

### The Same Melee Observer Family Has Readiness / Scalar Methods Tied to the Live Actor

The smaller methods under the same class family are now also readable:

`0x14124C0E0(object)`:

- resolves the live actor from `object + 0xB0/+0xB8`;
- marks it active through `+0x72`;
- resolves `actor + 0x1A0 -> 0x14172B7F0`;
- checks the current actor state through `0x14189E170`;
- returns true only when the actor-side state matches `0x10`.

`0x14124C180(object)`:

- first requires `0x14124C0E0(object) == true`;
- resolves another actor-side node through `object->virtual +0x70`;
- reads `actor_node + 0x168`;
- multiplies that against the observer's base scalar path;
- otherwise falls back to the default scalar at `object + 0xC4`.

`0x14124C060(object, out_transform)`:

- resolves the live actor from `object + 0xB0/+0xB8`;
- requires `0x14124C0E0(object)`;
- then pushes a transform into `0x14189F410` from the actor-side graph.

So this family is not passive metadata. It actively gates and extracts melee-
related state from the live actor/runtime graph.

### Why This Matters for Nadine

This is the first concrete player-facing gameplay family we have pinned down
above the general owner/knowledge infrastructure:

- it is explicitly tagged to `Player`;
- it uses the same runtime handle/object identity layer;
- it resolves authored knowledge/state records;
- it publishes actor-side state back through a dedicated gameplay observer
  object;
- and it exposes readiness/scalar/transform methods tied to a live actor.

That makes the melee observer family a strong candidate for the next bridge
from "Nadine participates in perception/knowledge" to "Nadine participates in
actual player-facing combat/action systems."

### `0x14189E170` and `0x14189F410` Expose Actor-Side State Slots and Transform Extraction for the Melee Observer

The helper pair used by `0x14124C0E0` / `0x14124C060` is no longer opaque.

`0x14189E170(actor_graph, state_id)`:

- first checks a compact table under `actor_graph + 0x140` with count
  `actor_graph + 0x4D4`;
- each entry is `0x100` bytes wide and contributes a one-byte result from
  `entry + 0x14D`;
- if the state id is not found there, falls back to a second compact table at
  `actor_graph + 0x460`;
- each fallback entry is `0x1C` bytes wide and contributes a one-byte result
  from `entry + 0x478`.

So `0x14124C0E0` is not testing an arbitrary enum. It is querying an actor-side
state table and only returns ready when the observer's cached melee state id
maps to byte value `0x10`.

`0x14189F410(actor_graph, out_record)`:

- takes the compact selector in `out_record + 0x8/+0xC`;
- resolves a preferred target from the global live-object table
  `0x14386FD00`;
- if the preferred target matches, copies its first `0x20` bytes back into the
  output record;
- then mixes that with the actor graph's local transform/state fields.

So this helper is a real actor-side transform/target extractor, not just a
copy function.

### `0x1418A5CD0 / 0x14193CD10 / 0x14193CDB0` Continue the Same Melee Target/Selection Path

The next layer under the melee observer family is also consistent.

`0x1418A5CD0(actor_root_plus_0x90, state_id)`:

- seeds a compact selector record through `0x1418A6010`;
- prefers `actor_root->0x38` and falls back to `actor_root->0x40`;
- then dispatches into `0x14193D250`.

`0x14193CD10(container, candidate_ptr, state_id)`:

- recursively walks `0x40`-byte entries;
- follows child containers when flag `0x40` is set;
- otherwise matches the incoming candidate pointer and rejects entries whose
  state id already equals the requested one;
- returns the matching `0x40`-byte entry when found.

`0x14193CDB0(entry, selector_record)`:

- validates selector gating bits at `entry + 0x4`;
- uses `selector_record + 0x2D/+0x2E` as boolean gates;
- then continues a larger state/target resolution path.

This makes the overall picture tighter:

- `MeleeScriptObserver` resolves a live actor;
- asks actor-side state tables whether a melee state is ready;
- extracts a target/transform selector from the actor graph;
- walks a recursive `0x40`-byte container tree to find a matching melee
  candidate entry;
- and only then commits the record back into the observer path.

So the melee observer family is already deep inside actor-side melee
participant selection, not just emitting awareness events.

### `npc-holster-weapon` Confirms That the Same `+0x15C8` Nested-Control Family Also Drives Other Gameplay Actions

The older `npc-holster-weapon` trail is now more useful in light of the melee
observer work.

`0x14055D97A .. 0x14055DA11`:

- builds a command/event record from the local script/runtime context;
- resolves the nested control object through `owner + 0x15C8 -> virtual +0x58`;
- logs or packages the `npc-holster-weapon` selector path;
- then branches:
  - for action-state values `1/2`, dispatches through `0x141DE55E0`
    (`+0x15C8 -> +0x58 -> virtual +0xA0`);
  - otherwise falls back to direct nested-object `virtual +0xB8`.

This is important because it proves the `+0x15C8` family is not a melee-only
oddity. The same nested control object already routes:

- holster actions,
- melee-related readiness/selection,
- and the wider owner-state family we have been mapping.

So the common bridge is getting clearer:

`owner runtime -> nested control object at +0x15C8 -> action-specific virtuals
+0xA0/+0xA8/+0xB0/+0xB8/+0x178/+0x188`

That is a stronger candidate for eventual Nadine promotion than any
locomotion-only hook, because it is already shared across multiple gameplay
action families.

### `interact-command-id / interact-avail / interact-use-advance-update?` Are Confirmed Registry Entries, Not Yet Direct Gameplay Consumers

The old `interact-*` strings are now anchored more carefully.

`0x141358D95 / 0x141358FE2 / 0x141359064`:

- reference:
  - `interact-command-id`
  - `interact-avail`
  - `interact-use-advance-update?`
- but sit in the same startup-style selector registration machinery as the
  previously observed hash-publishing paths.

`0x141C965A0`:

- writes a sequence of global `(selector_id, type, size)` records into
  `0x146C742A8 .. 0x146C742E4`;
- hashes several authored strings through `0x14014D910`;
- explicitly registers `interact-command-id` via the string at
  `0x142A76618`.

So at this point:

- `interact-command-id`, `interact-avail`, and
  `interact-use-advance-update?` are confirmed authored selector ids in the
  shared registry;
- but their direct gameplay consumers are still separate from this init path.

That corrects the earlier ambiguity: these strings matter, but the xrefs found
so far are registry/bootstrap work, not the final player action handlers.

### `0x1408BDF40` Is a Live `interact-hook / interactable-def / rope-holster-offset` Runtime Consumer

The stronger gameplay-facing path is elsewhere.

`0x1408BDF40`:

- hashes `interact-hook` from `0x1429BCD48`;
- resolves a gated live runtime object through `0x141D21E20`;
- reads the live object's retained/runtime pair from `+0xA8/+0xBC`;
- publishes that pair into a large temporary record;
- resolves `interactable-def` from `0x1429BCE90`;
- later resolves `rope-holster-offset` from `0x1429BCEA0`;
- feeds the built records through:
  - owner virtual `+0xE8`
  - `0x141ECEFE0`
  - owner virtual `+0x20`
  - `0x141C9ED50`

This is not a registry helper. It is a real runtime builder that combines:

- `interact-hook`
- `interactable-def`
- rope attachment metadata
- live actor/runtime identity
- and owner-side materialization/state application.

That is the first clear `interact`-adjacent bridge from authored selector ids
into a real gameplay object/update path.

### `0x1408C4A80` Materializes a `winch-hook` Action Family on Top of the Same Runtime Identity Layer

The hook/winch side is similarly concrete.

Relevant strings in this cluster:

- `winch-hook-attach`
- `attach-winch-hook`
- `winch-hook-secure`
- `spawn-rope`
- `interactable-def`

Observed xrefs:

- `0x1408C4B55` -> `winch-hook-attach`
- `0x1408C4DAD` / `0x1408C5B35` -> `winch-hook-secure`
- the same function also consumes `attach-winch-hook`, `interactable-def`,
  and actor/runtime pairs from `+0xA8/+0xBC`

`0x1408C4A80`:

- builds large temporary records by hashing those strings through the shared
  CRC/selector machinery;
- resolves the same live actor/runtime pair shape already used elsewhere;
- materializes structured records through `0x141C9ED50` and `0x141C9EB30`;
- writes resulting state back into owner/runtime fields around:
  - `+0x12F0..+0x1330`
  - `+0x14F8`
  - `+0x1511/+0x1514/+0x1518`
  - `+0x17B8`
- and reuses cached runtime pairs at `+0x978/+0x980` and `+0x1500/+0x1508`.

This is no longer just "rope exists". It is a proper `winch-hook` action/state
family that runs through the same:

- authored selector ids,
- live runtime-object identity,
- and owner/runtime state publication layer

that we already saw for melee and holster.

### `pickup-winch-hook / player-attach-winch-hook / player-grab-winch-hook` Already Feed a Player-Facing Hook State Builder

The larger hook-state path under `0x140E967C0` is also useful now.

Confirmed strings:

- `pickup-winch-hook` at `0x142A28338`
- `player-attach-winch-hook` at `0x142A28420`
- `player-grab-winch-hook` at `0x142A28460`

Observed callsites inside the same function family:

- `0x140E9685A` -> `pickup-winch-hook`
- `0x140E989F6` -> `player-attach-winch-hook`
- `0x140E96AE8` -> `player-grab-winch-hook`

The surrounding code:

- allocates typed action/state objects through `0x1415A0E90`;
- seeds vtable-backed objects for the hook workflow;
- stores live runtime pairs from actor objects into the resulting state
  payloads;
- writes world/transform-derived state into owner fields;
- and commits the resulting objects back through `0x142053820` plus additional
  owner update helpers.

This matters because the verbs are already player-facing, not generic AI:

- pickup hook
- attach hook
- grab hook

So the same general runtime/owner infrastructure already covers a concrete
player-like interaction family beyond melee.

### Practical Status After the Hook/Interact Pass

The current picture is stronger and cleaner:

- `interact-command-id`, `interact-avail`, and
  `interact-use-advance-update?` are confirmed selector-registry entries;
- the real gameplay path is not those init callsites, but the runtime
  consumers that use:
  - `interact-hook`
  - `interactable-def`
  - `spawn-rope`
  - `rope-holster-offset`
  - `winch-hook-attach`
  - `attach-winch-hook`
  - `winch-hook-secure`
  - `pickup-winch-hook`
  - `player-attach-winch-hook`
  - `player-grab-winch-hook`
- those consumers already sit on the same three pillars we care about for
  Nadine:
  1. authored selector ids
  2. live runtime-object identity / retained-runtime pairs
  3. owner/runtime state materialization and publish paths

That makes the hook/interact family the second strong player-facing bridge
after `MeleeScriptObserver`, and a credible route toward "world reacts to
Nadine like it reacts to Chloe" once the ownership/promotion point is found.

### `player-grab-winch-hook` Already Rejoins the Same Nested Action Dispatch Used by Melee and Holster

There is now a direct bridge from the player-facing winch-hook family back into
the shared nested control/action dispatcher.

`0x14124E7C0`:

- resolves a live actor and marks it active through `+0x72`;
- queries actor virtuals `+0x8C0`, `+0xFA8`, and `+0x860`;
- compares the resolved actor-side object against a cached pointer at
  `object + 0x2E8`;
- then calls `0x141DE55E0(object_owner, action_state)` with `edx = 0/1`
  depending on whether the cached actor-side object still matches.

So the path is no longer just:

- player-facing hook strings
- separate hook-only state builders

It now visibly rejoins the same nested action dispatch we already mapped for:

- melee-related action routing
- `npc-holster-weapon`
- other `+0x15C8` action-state families

That is a strong unification point:

`player-grab-winch-hook -> actor-side readiness/object checks -> 0x141DE55E0 -> nested control object at +0x15C8`

This is exactly the kind of bridge we need for Nadine: an interaction family
that is already player-facing, already actor-aware, and already routed through
the common action stack instead of a one-off hook subsystem.

### `0x140484C20` Is a Shared Owner-Side Current-Action Gate Above Nested Control and the Gameplay-State Bundle

The larger owner method at `0x140484C20` is now readable enough to stop
treating it as a random helper.

It starts by:

- resolving the nested control object through `owner + 0x15C8 -> virtual +0x58`;
- checking the cached live runtime pair at `owner + 0x23D8/+0x23E0`;
- falling back to `owner virtual +0xA10` and a special runtime-tag test on
  `object + 0x60 == 0xAC710201`;
- and then checking `owner virtual +0x7D0`.

If none of those short-circuit the path, it:

- resolves `owner + 0x15C8 -> virtual +0x50`;
- asks that object for `virtual +0xA8`;
- and if that gate fails, calls `0x141DE55E0(owner, 1)` to force the common
  nested action-dispatch path.

After that it continues through:

- `nested_object->virtual +0x130`, followed conditionally by `+0x120` or
  `+0x128`;
- helper checks through `owner + 0x1FB0`, `owner + 0x1FF8`, and offsets
  `+0x150` / `+0x1C0` inside the helper payload;
- current-state validation around `owner + 0x2168`;
- readiness checks against `owner + 0x1FA8`;
- `nested_control->virtual +0xC8`;
- `owner virtual +0xFD0`;
- and a transform/state fetch through `owner virtual +0x478`.

So this is already a shared current-action gate that sits:

- above the nested action dispatcher,
- above the `+0x1FB0/+0x1FF8/+0x1FA8` gameplay-state helpers,
- and above owner-side runtime/transform queries.

That makes it a much better candidate for `weapon/aim/holster`-class gating
than any pure locomotion helper, even though the exact authored selector name
for this method is still not pinned.

### `0x14058B900` Is a Combat/Action State Handler That Joins Nested Control to Actor-Graph Selection

The larger method rooted at `0x14058B900` is no longer just "some state
switch".

It:

- resolves a live object through the same retained/runtime identity pattern;
- falls back to an actor/graph object from `owner + 0x1E0`;
- filters a long list of state ids through `object + 0x98`;
- and, when `owner + 0x27D0 bit 0` is set and `owner + 0x24C8 < 3`, forces
  the low-cardinality nested-control state through `0x141DE5A10`.

The body then mixes several already-known layers:

- owner/world transforms through `0x14014E240`;
- packaged record commits through `0x141E0EDF0` / `0x141E0ED10`;
- actor-graph/state helpers through `0x1418B7060`, `0x14189E5A0`,
  and `0x14189CC80`;
- nested-control dispatch through:
  - `owner + 0x15C8 -> virtual +0x48 -> virtual +0x178`
  - `owner + 0x15C8 -> +0xD8 -> virtual +0x170`
  - `owner + 0x15C8 -> +0xF8 -> virtual +0xB0`;
- and owner follow-up through `0x141DEF910` / `0x141DEF9A0`.

This is important because it shows a second strong convergence point after the
melee observer family:

- actor-graph combat/state selection,
- owner-side packaged state commits,
- and the shared nested action object at `+0x15C8`

are already coupled inside one live handler.

So `+0x15C8` is not merely a "dispatch thing". It is already sitting inside
combat/action state handling that talks to the same actor-graph layer used by
`MeleeScriptObserver`.

### `0x14124ED20` Is a Second Actor-Observer Publish Path Built on `0x141DF0000` and the Primary Retained Player

The method region around `0x14124ED20` is now useful even without a perfect
authored name.

Confirmed behavior:

- resolves retained player slot `0` through `0x1410F0D10`;
- calls `0x141DF0000(object, 0)` to fetch the same scalar/query output we have
  already seen in the owner action stack;
- reads world-space transforms from the resolved live actor and from the local
  observer object;
- computes distance/threshold vectors from those transforms;
- and, when the global gate at `0x143073D69` is enabled, packages a structured
  record and publishes it through the sink/manager object at `0x143FDF520`
  via `virtual +0xA8`.

The nearby `.rdata` block at `0x142A36028..0x142A36080` contains:

- `player-is-aiming-firearm-at-npc?`
- `player-is-aiming?`
- `player-is-aiming-grenade?`

and `0x142A76528` contains:

- `is-aiming`

Those strings are not yet directly xref-pinned to this exact method, so the
safe claim is only this:

- `0x14124ED20` belongs to the same combat/player-observer neighborhood as the
  aiming strings,
- and it behaves like another actor-observer publish path built on live actor
  identity, transform thresholds, and the shared `0x141DF0000` scalar query.

So the picture is getting stronger:

- `MeleeScriptObserver` is one concrete combat observer family;
- `player-grab-winch-hook` is a second player-facing action family;
- `0x14058B900` ties actor-graph combat/state data back into nested control;
- `0x14124ED20` adds a second observer-style publish path on top of the same
  live-actor/scalar infrastructure.

That is enough to say the shared owner/runtime stack now clearly reaches
combat/observer publication, not just melee-only logic.

### `0x14124F700` Is a Sibling Actor-Observer Publish Path That Uses the Same Scalar Query but a Different Sink Slot

The nearby method rooted at `0x14124F700` is not a duplicate of
`0x14124ED20`; it is a sibling observer path with a slightly different publish
contract.

Confirmed behavior:

- resolves live actors from two retained/runtime-pair sources and marks them
  active through `+0x72`;
- reads world transforms through repeated `0x14014E240` calls;
- computes a local scalar gate and then calls `0x141DF0000(object, 0)`;
- when the same global gate `0x143073D69` is enabled, packages a structured
  record and publishes it through the same sink object `0x143FDF520`, but this
  time via `virtual +0x90` instead of `+0xA8`;
- then feeds the resulting vector/scalar output into `0x1411DF370`.

So we now have two observer-style publish paths on the same stack:

- `0x14124ED20` -> sink `virtual +0xA8`
- `0x14124F700` -> sink `virtual +0x90`

Both depend on:

- live actor identity,
- world-space transforms,
- the shared scalar query `0x141DF0000`,
- and the same global observer gate.

That is strong evidence that the combat/observer layer above the owner stack is
already structured as a small family of related publish/update paths, not a
single melee-only special case.

### `0x140437F80 / 0x1404380F0` Form a Special-Tag State-Application Helper Family for `0xAC710200 / 0xAC710202`

The special actor/runtime tags seen in the larger combat path are now less
opaque.

`0x140437F80`:

- builds a transform from `owner + 0x100`;
- runs a compact classifier through `0x142145490`;
- if the result is exactly `0x2010`, immediately forwards into
  `0x1404380F0(owner, 0xAC710200, scalar)`;
- otherwise resolves a candidate object from global registries under
  `0x146A53F80 / 0x146A54B80`;
- requires that candidate to accept `owner` through its `virtual +0xB0`;
- and, when it does, commits it through `0x141DE4F90`.

`0x1404380F0`:

- builds the same owner transform;
- resolves a candidate object through `0x1409DA630(tag, transform)`;
- again requires `candidate->virtual +0xB0(owner)`;
- and, on success, also commits through `0x141DE4F90`.

This matters because the earlier combat/action handler at `0x14058B900`
already branches through the sibling tag `0xAC710202` and then falls back into
`0x140437F80`.

So the combat/observer stack now visibly includes a special-tag application
subfamily:

- `0xAC710200`
- `0xAC710202`
- `candidate->virtual +0xB0(owner)`
- final apply through `0x141DE4F90`

That is another sign that the owner/runtime stack is already materializing
specialized actor-state objects for combat/action semantics, not just
publishing generic awareness data.

### Vehicle Entry/Seat Logic Is Anchored Around Authored `drive-event` Strings Rather Than Pure Physics State

The vehicle/passenger pass is now anchored enough to stop treating it as a
black box.

The relevant authored string block at `0x142926E40..0x142927010` contains:

- `drive-event`
- `passenger-entered`
- `add-passenger`
- `set-driver`
- `driver-exiting-vehicle`
- `driver-side`
- `passenger-side`
- `hero-passenger-a-exit-driver`
- `hero-passenger-b-exit-passenger`
- `hero-passenger-a-exit-passenger`
- `hero-jeep-exit-passenger`

Nearby strings also confirm the higher-level controller layer:

- `VehicleCtrl::NotifyPlayerEntering(const DC::VehicleEntryDef *)`
- ` - Vehicle controller: Entered vehicle %s with role %d`
- `vehicle-entry-def`
- `hero-passenger-a-enter-front`

So the seat system is already authored around explicit event/entry strings, not
just around "attach actor to car".

### `set-driver / add-passenger / passenger-entered` Are Built by a Shared Vehicle-Controller Event Path

Three neighboring code paths around `0x140394B20 .. 0x140395560` are now
consistent enough to call out as one family.

Observed string users:

- `0x140394BAB` -> `set-driver`
- `0x14039504D` -> `add-passenger`
- `0x1403954B1` -> `passenger-entered`

All three paths:

- build the same CRC/string record shape from the authored event string;
- capture the target vehicle/runtime pair through `object->virtual +0xF8`;
- package role/index-like values into stack records;
- and continue into the same larger vehicle-controller/event-commit path.

This is the important practical result:

- driver entry is not just "seat index 0";
- passenger entry is not just "seat index N";
- the controller distinguishes them explicitly through authored events:
  - `set-driver`
  - `add-passenger`
  - `passenger-entered`

That means a future Nadine vehicle patch should target this event layer rather
than trying to fake vehicle parenting by transform writes.

### `0x14059D740` Builds a Concrete Passenger-Entry State Around `hero-passenger-a-enter-front`

The path around `0x14059D740` is the strongest concrete passenger-seat lead so
far.

It:

- validates and activates a live target actor through the normal retained/live
  pair shape;
- measures the entry transform against a reference point through
  `0x14014E240` and `0x140186600`;
- hashes the authored string `hero-passenger-a-enter-front`
  (`0x142965D18`);
- stores that hash into controller state at `object + 0xA0`;
- writes a role-like value into `object + 0xB0`;
- writes an entry-state flag bundle into `object + 0x12C`;
- then builds additional transform/entry records from the target actor and
  commits them into controller state around `+0x50`, `+0x70`, and `+0x80`.

This is already much closer to "how the passenger seat is chosen" than the
older locomotion work:

- there is an authored passenger-entry animation/state id,
- it is committed into controller state,
- and it is combined with live actor transforms before the actual entry path
  continues.

So if the immediate goal is "make Nadine sit on the passenger seat", this
passenger-entry builder is currently the best direct runtime hook candidate.

### Script-Level Vehicle Entry Already Exists as `MoveToAndEnterVehicle / TeleportIntoVehicle`

The script layer is also confirmed.

The authored strings:

- `npc-move-to-and-enter-vehicle`
- `MoveToAndEnterVehicle`
- `TeleportIntoVehicle`

are not only static data. They already participate in the live script-skill
layer:

- `0x140053B50` registers `npc-move-to-and-enter-vehicle`;
- `0x140060720` registers `TeleportIntoVehicle`;
- `0x1406B1A76` is a real runtime user of the `TeleportIntoVehicle` string
  block and packages it into a larger scripted path;
- nearby strings confirm the intended semantics:
  - `TeleportIntoVehicle: %s is sitting in the car, so not waiting for him`
  - `MoveToAndEnterVehicle: decided to teleport into vehicle`
  - `MoveToAndEnterVehicle: decided not to teleport because player is not driving the car`

That gives two concrete directions for Nadine vehicle support:

1. passenger-seat entry through the vehicle-controller event/state layer
   (`hero-passenger-a-enter-front`, `add-passenger`, `passenger-entered`);
2. forced/scripted entry through the existing
   `MoveToAndEnterVehicle / TeleportIntoVehicle` skill family.

### Practical Vehicle Conclusion

For vehicle participation we are no longer guessing.

What is now solid:

- the game has a real authored seat/role event layer;
- passenger and driver are distinguished explicitly;
- there is a concrete passenger-entry state builder;
- and there is already a scripted skill family for moving or teleporting a
  character into a vehicle.

So the first realistic Nadine vehicle milestone is not "full driving".
It is:

- getting Nadine through the passenger-entry controller path,
- or routing her through `MoveToAndEnterVehicle / TeleportIntoVehicle`,
- and only after that figuring out whether the driver-specific path can also be
  rebound to her.

### `VehicleCtrl::NotifyPlayerEntering` Publishes a `vehicle-entry-def` Record Into the Live Actor Layer

The larger method at `0x1414115B0` is now strong enough to anchor as the real
`VehicleCtrl::NotifyPlayerEntering(const DC::VehicleEntryDef *)` path.

Confirmed behavior:

- hashes the authored string `vehicle-entry-def` (`0x142926668`);
- resolves controller sub-objects through `0x14142B890(..., 2)` and
  `0x14142B890(..., 3)`;
- allocates/initializes packaged `0x150` records through `0x14159FA50` and
  `0x140242010`;
- copies the incoming `VehicleEntryDef` payload (`+0xA8/+0xBC` plus local
  entry-state fields) into large temporary structures;
- serializes those structures through `0x1417F8B00`;
- and, when a matching live actor is present, pushes the resulting record into
  that actor through `actor->virtual +0xD0`.

That is the missing bridge for vehicle participation:

- the seat/entry layer is not trapped inside `VehicleCtrl`;
- it is published back into the same live actor/runtime path we have already
  been mapping for combat, observer, and action-state systems.

So if Nadine is going to become a valid passenger actor, one credible route is:

`vehicle-entry-def -> VehicleCtrl::NotifyPlayerEntering -> packaged entry record -> live actor virtual +0xD0`

That is a much stronger integration point than raw vehicle parenting or seat
transform hacks.

### The Passenger Entry Builder Also Writes a Stable Role/Entry Record Shape Into Controller State

`0x14059D740` is now a little clearer with more of its tail visible.

Beyond hashing `hero-passenger-a-enter-front`, it also:

- resolves a preferred vehicle/runtime owner via the same bitset-gated lookup
  used elsewhere in the actor stack;
- pulls additional entry data from the selected runtime object at:
  - `+0x15B8`
  - `+0x108/+0x10C`
  - `+0x110/+0x120`
- writes:
  - entry hash to `controller + 0xA0`
  - role-like value to `controller + 0xB0`
  - structured entry transform/state to `controller + 0x50/+0x60/+0x70/+0x80`
  - flag bundle to `controller + 0x12C`
- and packages another small record through `0x1415D23B0` and `0x142177E50`.

So this is not just "pick passenger animation". It already constructs a stable
controller-side entry record with:

- seat/role choice,
- entry animation/state id,
- entry transforms,
- and supplemental serialized data.

That makes it the cleanest current hook point for "put Nadine into passenger
seat" without inventing a new path.

### `0x14059DAB0` Looks Like a Vehicle Exit/Side Selector Sitting on the Same Seat-State Object

The sibling method immediately after the passenger-entry builder is also useful.

`0x14059DAB0`:

- chooses between two authored string paths depending on a binary input flag;
- uses the same vehicle-seat state object (`object + 0x8`, `+0x15C8`,
  `+0xE0`);
- calls into `0x14038DFC0` with authored exit-side strings from the same
  vehicle string cluster;
- then dispatches into `nested_control->(...)->virtual +0xD0`;
- and finally caches a timestamp/value into `object + 0x48`.

This strongly suggests the seat-state object already owns both:

- entry selection (`hero-passenger-a-enter-front`)
- and side/exit selection (`driver-side` / `passenger-side` family)

inside one coherent vehicle-participation controller.

That is relevant because a future Nadine vehicle patch will need both:

- entering the seat,
- and later resolving the correct exit side/animation.

### `0x14059DC30` Is a Tight Seat-State Applicability Gate for One Specific Vehicle Action Hash

The small helper at `0x14059DC30` is worth anchoring because it is much more
specific than a generic utility function.

It:

- requires a non-null actor/runtime object;
- checks capability bit `0x143D43FA4` in that object's bitset;
- requires the incoming action hash `r8d == 0x465123BF`;
- then resolves `owner + 0x15C8 -> virtual +0x58`;
- and, when that succeeds, tail-dispatches into the nested control object's
  `virtual +0xA0` with `edx = 0`.

So this is not just a passive getter. It is a very small
`seat-state applicability/dispatch gate` sitting directly on top of the same
nested action stack that already serves melee, holster, and hook/winch paths.

That makes it a strong candidate for a future "press a button to seat Nadine"
runtime hook, because the logic already expects:

- a live actor identity,
- a concrete action hash,
- and a nested control object that can execute the action.

### `owner + 0x690` Is an Embedded Vehicle Seat-State Subobject, Not a Loose Helper Pointer

The thin wrapper at `0x14059F199` is useful because it binds the seat-state
object into a larger outer owner class.

It does only three things:

- loads `rcx = owner + 0x690`;
- forces `dl = 1`;
- jumps straight into `0x14059DAB0`.

That means:

- the seat-state logic at `0x14059D740 / 0x14059DAB0 / 0x14059DC30`
  is embedded at `owner + 0x690`,
- not discovered ad hoc from some global table or transient pointer.

This materially improves the runtime picture for a future hook, because it
means a vehicle-action owner can carry a stable seat-state controller in-place.

### `0x14059F1E0` Looks Like the Outer Vehicle-Action Owner Update Around the Embedded Seat-State

The larger method starting at `0x14059F1E0` now reads like the surrounding
owner update for that embedded `+0x690` seat-state object.

Confirmed behavior:

- it resolves the current live actor through the normal retained/live pair;
- goes through `owner + 0x15C8 -> virtual +0x58`,
  then immediately calls nested control `virtual +0xB0`;
- stores the resulting low-cardinality value into `owner + 0x69C`;
- checks action/capability bit `0x143D43FA4` again on the live actor;
- if allowed, calls live actor `virtual +0x1140`;
- and then continues into the larger owner/runtime path.

This is a strong structural result:

- the seat-state controller at `+0x690`,
- the shared nested action object at `+0x15C8`,
- and the live actor runtime identity

are already tied together inside one outer vehicle-action owner family.

That is exactly the shape needed for a real authored passenger-entry hook.
It means the likely clean path is not "teleport Nadine into a seat", but:

1. reach the correct vehicle-action owner,
2. drive its embedded seat-state subobject,
3. let the existing nested action/runtime path publish the result into the
   live actor layer.

### `0x14039EB80 / 0x14039EC50` Are Vehicle-Qualification Resolvers, Not Generic Lookups

The two helpers used by the outer vehicle-action owner around `0x14059F1E0`
are now much clearer.

Both `0x14039EB80` and `0x14039EC50`:

- resolve a live actor from a packed retained/live pair;
- mark it active through `actor + 0x72`;
- call actor virtual `+0x298`;
- then gate the result through `CheckSelectorHashEnabledInBitsetRegistry`
  (`0x1417C4800`) against a selector string resolved lazily through the
  registration table.

The selector strings are now concrete:

- `0x14039EB80` -> `RailVehicle`
- `0x14039EC50` -> `DriveableVehicle`

and the nearby string cluster also includes:

- `DriveableBoat`

So this is not just a random object lookup layer. It is an
`actor qualification layer` that decides whether the current live actor can be
treated as a concrete vehicle-participation target.

That is a strong result for the Nadine vehicle MVP because a future
"press button to enter passenger seat" hook now has a principled qualification
path available:

- first find/validate a target actor as `DriveableVehicle` (or sibling vehicle
  kinds),
- then drive the embedded seat-state controller on the vehicle-action owner.

### `is-aiming` Has a Real Runtime Consumer in a Large Player/HUD State Update Path

The earlier `player-is-aiming*` strings were only weakly pinned. That is no
longer true for `is-aiming`.

The direct xref for `is-aiming` leads into the large function starting at
`0x1413571B0`.

What this path clearly does:

- begins from an object with a big state block at `object + 0xF0`;
- reads a boolean-like source from `object + 0x1D88 -> virtual +0x288`;
- writes state bytes into `+0x3B4C` and `+0x3B4D`;
- packages authored scalar records for:
  - `grenade-throw-time`
  - `player-live-time`
  - `is-aiming`
  - `weapon-accuracy`
- pushes those records through repeated calls to `0x140167D70` into
  `object+0xF0 + 0x3B18 / +0x3B30` style runtime slots;
- queries `CameraManager(index)` and `GetCurrentCamera(...)`, then reads
  `camera + 0x1A8` and compares it with a global camera marker before deciding
  what value to publish for `is-aiming`;
- and finally routes the produced values through `object + 0x1D98` using
  virtual `+0xB0` and `+0xB8`.

This is not a dead string registrar and not a generic UI helper. It is a real
runtime path where:

- player state,
- current camera state,
- aiming-related authored scalars,
- and object-local publish sinks

are combined in one update.

So the picture tightened again:

- melee/hook/holster gave us action dispatch through `+0x15C8`;
- vehicle gave us actor qualification and embedded seat-state control;
- now aiming gives us a concrete `player runtime -> camera/HUD state publish`
  path.

For Nadine as a full playable actor, this matters because a second-player stack
will need not only action ownership but also the correct downstream
`is-aiming / weapon-accuracy / live-time` style publishes that existing player
presentation systems already expect.

### `0x14009BC80` Is Only the Registrar for `player-is-aiming-firearm-at-npc?`

The xref on `player-is-aiming-firearm-at-npc?` at `0x14009BC80` is useful only
as a correction:

- it is a registration/init stub through `0x143682D30`,
- not a live gameplay consumer.

So the strong runtime lead on aiming is the larger `0x1413571B0` update path,
not the registrar block.

### `object + 0x1D98` Behaves Like a Reused Presentation/Observer Sink, Not a One-Off Aim Helper

The `is-aiming` path around `0x1413571B0` ended at `object + 0x1D98`, but that
offset is now clearly broader than one aiming-only hook.

Independent consumers confirm that:

- `0x140708E60` resolves a live actor, takes `actor + 0x1D98`, and calls
  `virtual +0x80` before continuing into a larger presentation/update path;
- `0x1407196D0` does the same `actor + 0x1D98 -> virtual +0x80`, then
  continues into a much larger visual/runtime update path and eventually
  publishes colored/flagged records into the global sink at `0x143FDF520`
  through `virtual +0xB8`;
- `0x140161379` takes `owner + 0x1D98`, calls `virtual +0xB0`, and feeds the
  returned scalar/vector data into additional runtime processing.

So the safe structural claim now is:

- `+0x1D98` is a reused presentation/observer-facing sink interface;
- `virtual +0x80` looks like a refresh/reset/readiness-style entry point;
- `virtual +0xB0` and `virtual +0xB8` are used to pull or publish structured
  state into downstream presentation/runtime systems.

This matters for Nadine because it means the aiming/HUD path is not isolated.
It already lands in a broader sink interface that other runtime and visual
systems also consume. That is exactly the kind of downstream surface a true
second-player actor will need in order to feel like Chloe rather than like a
hidden companion with borrowed input.

### `+0x1D98` Sits Inside a Larger Initialized Presentation/Observer Block `+0x1D80..+0x1DE0`

The sink at `+0x1D98` is not a standalone pointer dropped into an otherwise
unrelated class.

Two construction/reset sites now make that much stronger:

- the zero/init region around `0x14090392B..0x140903979` clears or seeds a
  whole contiguous block:
  - `+0x1D80`
  - `+0x1D88`
  - `+0x1D90`
  - `+0x1D98`
  - `+0x1DA0`
  - `+0x1DA8`
  - `+0x1DB0`
  - `+0x1DB8`
  - `+0x1DC0`
  - `+0x1DC8`
  - `+0x1DD0`
  - `+0x1DD8`
  - `+0x1DE0`
- the later setup path around `0x14090AC60..0x14090AD84` explicitly seeds:
  - `+0x1D90`
  - `+0x1D98`
  - `+0x1DA0`
  - `+0x1DA8`
  - `+0x1DB0`
  - `+0x1DB8`
  and then continues into additional object construction.

So the safe claim is now:

- `+0x1D98` belongs to a formal presentation/observer sub-structure,
- not to an incidental one-off field,
- and its neighboring fields likely hold sibling sinks, state records, or
  construction metadata for the same downstream player-facing publish layer.

### The `+0x1D98` Block Currently Resolves Into `BuddyMeleeCoordinator::AttackInternal`, Not a Generic Player Owner

The larger setup/update region around `0x14090AB80..0x14090B120` adds an
important correction to the working model.

That region seeds and uses the same `+0x1D98 / +0x1DA8 / +0x1DB0 / +0x1DC0`
family, but its nearby string cluster is explicit:

- `pAttackList`
- `BuddyMeleeCoordinator::AttackInternal`
- `Buddy Attacking Behavior`
- `buddy-vs-npc-attacks`

and the code path:

- clears the `+0x1D90..+0x1DB8` fields,
- resolves an object into `+0x1D98`,
- seeds sibling records and colors in `+0x1DE0`, `+0x1E20`, `+0x1E70`,
- and publishes them through the global sink `0x143FDF520` via `virtual +0x90`
  and `virtual +0xC8`.

So the current safest interpretation is no longer:

- "`+0x1D98` must belong to a generic player presentation owner"

but instead:

- "`+0x1D98` is a reusable downstream sink block that also appears inside the
  buddy melee coordinator / attack behavior family."

That does not reduce its value for the project. It still proves that:

- the same publish-style sink interfaces are used by rich gameplay behavior,
- they are not UI-only,
- and Nadine's companion-specific systems already touch a downstream publish
  layer that looks structurally compatible with broader player-facing state
  propagation.

But it does mean we should be careful not to overclaim this exact class family
as the final Chloe-equivalent player owner.

### The Same Global Sink `0x143FDF520` Also Appears in a `ghud.cpp`-Anchored Runtime Path

The path immediately before the large `is-aiming` updater adds useful context
for the sink side.

The function region around `0x141356F80..0x141357158` is anchored by nearby
strings and file paths:

- `Camera Inside`
- `GrenadeMove`
- `vis: blind`
- `vis: %s`
- `game\\render\\gui\\ghud.cpp`

and it publishes a structured record into the same global sink object
`0x143FDF520` via `virtual +0x38`.

That gives a cleaner interpretation for the sink family:

- it is not just a buddy-melee artifact;
- it is not just an aiming helper;
- it is reused by HUD/runtime presentation code as well.

So the current safe structural claim is:

- `0x143FDF520` is a broad presentation/runtime publish sink,
- and fields like `+0x1D98` are feeding into a sink family that reaches both
  gameplay behavior and HUD-facing runtime systems.

That is exactly the kind of downstream convergence a second-player Nadine will
eventually need.

### `0x1417F9150` Sits Inside a Formal Slot-Based Sink/Registry Helper Cluster

The helper cluster around `0x1417F9000..0x1417F92xx` is now clearer and it
pushes the sink interpretation upward one layer.

`0x1417F9150(sink_wrapper, record_payload)` is not a generic callback:

- it takes the sink root from `*sink_wrapper`;
- uses `sink_wrapper + 0x10 -> +0x8` plus `0x141764B50` to resolve a slot
  index from `record_payload + 0xC`;
- writes `record_payload + 0x10` into the sink's integer/value array
  (`sink + 0x10`);
- writes the record pointer itself into the parallel pointer array
  (`sink + 0x8`);
- and then calls `0x1417FA5A0(sink)`.

Sibling helpers confirm that this is a small slot-registry API:

- `0x1417F90A0`:
  - resolves a knowledge record id through `0x141765030`;
  - then tail-jumps into `0x1417F9150`;
- `0x1417F9040`:
  - clears one sink slot by key and calls `0x1417FA5A0`;
- `0x1417F90E0`:
  - checks whether a given record/key is currently present in the sink slot;
- `0x1417F91D0`:
  - fetches `record + 0x10` from a resolved sink slot;
- `0x1417F9240`:
  - fetches the record pointer itself from a resolved sink slot.

So the safer structural claim now is:

- `0x143FDF520` is not just "some publish callback object";
- it is a slot-based sink/registry container with key->slot mapping,
  pointer/value arrays, and a finalize/refresh step.

### `0x1417FA5A0` Looks Like Sink Finalize/Refresh, Not a Gameplay-Specific Worker

The helper reached from `0x1417F9150` and `0x1417F9040` is also broader than a
single gameplay feature.

The visible front half of `0x1417FA5A0`:

- seeds a large temporary table from static data;
- folds bytes through a CRC/hash-like loop;
- iterates every populated sink entry (`sink + 0x8`);
- mixes `record + 0xC/+0x10` into that running state;
- then continues through more sink metadata at `sink + 0x18`.

This looks much more like:

- sink checksum / finalize / dirty-refresh,

than:

- a direct gameplay action handler.

So `0x1417F9150 -> 0x1417FA5A0` should be treated as
`commit slot -> refresh/finalize sink state`.

### A Very Early Caller Shows `0x143FDF520` Is Also Used by `Iggy` Alloc/Free Telemetry

An important correction came from the early caller family around
`0x14013A000..0x14013A430`.

That region is anchored by nearby strings:

- `Iggy Alloc`
- `Iggy Free`
- `*** Flash Allocator Overflowed! This would crash on retail ***`
- `*** Iggy will be out of memory soon! Starting to log all allocations... ***`

and it does all of the following:

- builds small structured records on the stack;
- publishes them into `0x143FDF520` via `virtual +0x28`;
- then uses object-side `virtual +0x38` / `+0x90` on a local owner object to
  continue the logging/update path.

So the global sink is broader than the earlier "presentation/runtime publish"
reading:

- it definitely serves gameplay/HUD paths;
- but it also serves allocator/debug/telemetry paths.

The safest updated interpretation is therefore:

- `0x143FDF520` is a broad slot-based runtime telemetry/observer bus;
- gameplay systems, HUD/presentation systems, and at least some debug/allocator
  systems all publish structured records into it.

That weakens the idea that the sink itself is a direct player-ownership node,
but it does not weaken the gameplay work:

- the melee/action/hook/aiming/observer paths still feed real structured data
  into a broad runtime bus;
- the bus simply sits lower and wider in the engine than first assumed.

### The Large `0x14017C580..0x14017C935` Producer Family Also Looks Visual/Debug-Like, Not Like Player Ownership

One more useful correction came from the heavy producer cluster around
`0x14017C580`.

That family repeatedly:

- reads a shared global/source object through `0x143F36BE8`;
- builds packed stack records containing:
  - flag bytes,
  - color-like constants such as `0xFF00FFFF`,
  - one or more world-space vectors,
  - scalar thresholds from constants like `0x143077480`;
- and publishes those records into `0x143FDF520` through multiple sink virtuals:
  - `+0xA0`
  - `+0xC8`
  - `+0x100`
  - `+0x90`

The front helper at `0x14017C990` also computes transform/extent-like vectors
from `runtime + 0xCD0`, `+0xC40`, and broadcast scales from `runtime + 0x10`.

This is much closer to:

- structured debug/visual/runtime vector publication,

than to:

- direct player-control or player-ownership code.

So the current safe split is:

- the sink bus itself is broad and low-level;
- some of its producers are clearly gameplay-facing (`melee`, `hook/winch`,
  `aiming`, observer records);
- other producers, like `Iggy Alloc/Free` and the `0x14017C580` family, are
  generic runtime/debug/visual publishers.

That keeps the reverse on the right track: the bus is useful context, but it is
not the place where Nadine becomes a full second-player actor.

### The Current-Action Gate Now Has Direct Weapon Readiness Failure Reasons

The same current-action family that already touched `+0x15C8` and
`+0x1FF8/+0x1FA8` now has explicit weapon/aim readiness reasons.

Inside the larger gate around `0x140484C20`, the block
`0x1404845F1..0x14048485C` does the following:

- resolves `owner + 0x15C8 -> virtual +0x58`;
- calls nested-control `virtual +0x130`;
- if that fails, packages the string at `0x142940F60`:
  - `weapon-is-down`
- otherwise calls owner virtual `+0x8E8`;
- if that fails, packages:
  - `0x142940F70` -> `not-aiming`
- otherwise checks `owner + 0x1FF8`;
- if that object is absent, packages:
  - `0x142940F80` -> `no-shooting-logic`
- otherwise, when the local mode is not `3`, packages:
  - `0x142940F98` -> `shooting-logic-not-ready`

All of those strings are not passive data here. They are folded through the
same CRC/string-id builder used elsewhere in the authored runtime paths.

This is a strong result because it means we now have a direct bridge from the
known `+0x15C8` action stack into player-like weapon readiness semantics:

- weapon lowered,
- not aiming,
- no shooting logic,
- shooting logic not ready.

That is much closer to a real `weapon/aim/fire` participation path than the
earlier generic sink work.

### `reticle-show-time` and `update-reticle-aspect` Are Published Through the Same Player/HUD Runtime Family

The aiming/HUD runtime family around `0x1413571B0` is now stronger than just
`weapon-accuracy` / `player-live-time` / `is-aiming`.

Two sibling blocks:

- `0x14135AB18`
- `0x14135ABF3`

show that the same family also packages:

- `0x142A76800` -> `reticle-show-time`
- `0x142A767D0` -> `update-reticle-aspect`

In both cases the function:

- builds the same CRC/string-id style record from static tables;
- fills a compact scalar payload on the stack;
- and commits it through `0x140167D70` into local player/HUD state objects.

The surrounding logic also confirms nearby siblings for:

- `0x142A76448` -> `weapon-accuracy`
- `0x142A76510` -> `player-live-time`
- `0x142A76528` -> `is-aiming`
- `0x142A764E0` -> `grenade-throw-time`

So the safe interpretation is now:

- the `0x1413571B0` family is not only an aiming boolean publisher;
- it is a broader player/HUD reticle/aim presentation updater.

For Nadine this matters because a second-player path will need not only action
ownership and shooting readiness, but also the expected reticle timing/aspect
publishes that current HUD systems already consume.

### `fire-weapon-at-target` Is Only a Registrar, but `weapon-fire` Already Has Real Runtime Users

The weapon/fire verb split is now clearer.

`0x14003A380` is just another authored selector/script registration stub:

- it registers the string at `0x142918CF0`:
  - `fire-weapon-at-target`
- through the same startup registry machinery around `0x143682D30`.

So `fire-weapon-at-target` is not yet evidence of a live execution path by
itself.

By contrast, `weapon-fire` already appears in live runtime code, not just in
registration:

- `0x1403EF746`:
  - builds the CRC/string-id for `0x1429336D8` -> `weapon-fire`;
  - compares it against incoming/runtime ids;
  - if the id matches, continues through capability-bit checks on the live
    actor/object behind `r15`;
  - reads additional state from `object + 0x1DB8` through virtuals
    `+0x358`, `+0x1A8`, and `+0x230`;
  - computes and stores a resulting vector/sample into a per-owner buffered
    array around `owner + 0xC00..+0xC30`;
  - also stores packed retained/runtime-pair metadata and flags alongside that
    sample.
- `0x141436EE4`:
  - also builds the CRC/string-id for the same `weapon-fire` token;
  - pushes that id through `0x1405D96B0`, then through nearby helpers
    `0x141432B90` and `0x141897130`;
  - resolves the current retained player slot `0` and checks several live
    gameplay-state gates on the resulting object (`+0x1510`, `+0x1530`,
    `virtual +0x988`);
  - computes a world-space vector from the resolved actor and a target
    transform;
  - and forwards the final result through `0x141434DA0`, toggling a local
    binary state while doing so.

So the current safe distinction is:

- `fire-weapon-at-target` is still only on the script/registration side;
- `weapon-fire` has already crossed into live runtime logic.

That matters because for Nadine the useful bridge is not merely "there exists a
weapon script verb", but "there exists a runtime `weapon-fire` path we can
eventually bind to the same player-like action stack".

The safest structural reading of the two runtime users is now:

- `0x1403EF6D0` behaves like a buffered `weapon-fire` sample/state writer in an
  owner/runtime object;
- `0x141436EA0` behaves like a higher-level `weapon-fire` observer/query path
  that depends on the live retained player and world-space target geometry.

That split is useful for the mod because it suggests two separate combat layers:

- a lower buffered fire-state layer;
- and a higher player-facing observer/qualification layer.

### Owner-Local `weapon-fire` Observer Helpers Sit on Cached Weapon Registry Layers

The next layer under the higher `weapon-fire` path around `0x141436EA0`
resolved cleanly enough to name without over-claiming:

- `0x141432B90`:
  - reads `owner + 0x624` as a low-cardinality/current state id;
  - calls `0x1414FA800`, which is a cached global registry accessor keyed by
    the string family around `*weapon-art-defs-online-hash*`;
  - looks up the current state id through `0x141764B50`;
  - then calls `0x141434E90` and returns the leading float from the resolved
    record pointer;
  - otherwise falls back to the default scalar at `0x142BE8EA4`.
- `0x141434E90`:
  - is the pointer-returning sibling of the same path;
  - it also reads `owner + 0x624`, resolves the cached registry via
    `0x1414FA800`, and returns the matching slot/record pointer from
    `0x141764B50`.
- `0x141434DA0`:
  - reads the same owner-local state id from `owner + 0x624`;
  - if the mode flag is set, it hashes the string at `0x142A854B0`, which is
    `helicopter-igc-mg`, then routes the current state id through
    `0x1414FA380` and `0x141614E40`;
  - if the mode flag is clear, it still resolves the same cached table path but
    skips the extra hash build step.

The supporting cached helpers are also now concrete enough to keep:

- `0x1414FA380` is a cached registry accessor for the `*weapon-art-defs-hash*`
  family.
- `0x1414FA800` is a cached registry accessor for the
  `*weapon-art-defs-online-hash*` family.
- `0x1414FA9A0` is a third sibling cached accessor keyed by the
  `frags-event-weapon-dry-fire` string family.

That is a useful correction: this part of the `weapon-fire` observer path does
not yet look like direct fire execution. It looks like an owner-local
weapon-state/observer layer built on cached weapon definition registries and a
current state id at `owner + 0x624`.

So the safer structural reading becomes:

- `0x141436EA0` is still a higher retained-player/world-geometry observer path;
- `0x141432B90` / `0x141434E90` resolve owner-local weapon observer state from
  cached weapon registries;
- `0x141434DA0` updates or qualifies that owner-local observer state by a
  mode/toggle flag, with one branch using the authored token
  `helicopter-igc-mg`.

### The Owner-Local Weapon Observer Layer Has a Normal `id -> record` Table and a Small Record Layout

The lower support path under those helpers is now clear enough to describe as a
real table layout instead of opaque runtime glue:

- `0x141614E40` is a generic id lookup over a registry object with:
  - `record_count` at `registry + 0x0`
  - `id_table` at `registry + 0x8`
  - `record_ptr_table` at `registry + 0x10`
  - open-addressing style probing on `id % record_count`
  - final return value `record_ptr_table[matched_slot]`

That matters because it confirms `0x1414FA380` / `0x1414FA800` are not special
gameplay dispatchers by themselves. They are cached registry roots, and the
interesting semantics live in the returned records.

The `*weapon-art-defs-online-hash*` path already exposes a compact getter
family:

- `0x1414FAC10` -> returns `record + 0x0`
- `0x1414FAC50` -> returns `record + 0x8`
- `0x1414FAC90` -> returns `record + 0x10`
- `0x1414FACD0` -> returns `record + 0x18`
- `0x1414FAD10` -> returns `record + 0x20`
- `0x1414FAD50` -> returns `record->ptr0 + 0x2B0` when `record + 0x0` is valid

So the current safe reading of that observer record is:

- it is at least a small tuple of pointer-sized fields at offsets
  `0x0/0x8/0x10/0x18/0x20`;
- the first field can itself point at a larger object with useful state at
  `+0x2B0`.

This strengthens the interpretation of `0x141432B90`:

- it is not just a random float fetch;
- it is a scalar accessor over a normal owner-local weapon observer record
  family selected by `owner + 0x624`.

### The Tail of `0x141436EA0` Reuses the Owner-Local Weapon Observer Twice Before Publishing Final State

The rest of the higher `weapon-fire` path is also much less ambiguous now:

- after the retained-player/world-space qualification gates, the function:
  - computes a normalized vector from target and actor transforms;
  - toggles the local binary state byte;
  - calls `0x141434DA0(owner, toggled_flag)` once as a state update;
  - then calls `0x141434DA0(owner, toggled_flag)` again and reads
    `result->+0xB8->+0xC4` as a follow-up state id.
- that follow-up state id, plus the derived vector and owner-local flags, are
  then packed into a larger published state block around:
  - `rbp + 0x130..0x178`
  - `rbp + 0xF0`
  - and an additional hashed token path rooted at `0x142918C50`

So `0x141436EA0` now reads as:

1. compute/qualify a `weapon-fire` world-space observer vector;
2. update owner-local weapon observer state through `0x141434DA0`;
3. extract a follow-up observer/state id from the resulting record;
4. publish a larger structured state block for downstream consumers.

That is a stronger bridge than the earlier coarse reading. The higher
`weapon-fire` path is not just "look at the retained player and do math"; it
explicitly round-trips through an owner-local weapon observer record family
before packaging the final published state.

### The Final Published Token in the Higher `weapon-fire` Path Is `UsingJointDifference`

The tail of `0x141436EA0` also stopped being anonymous:

- the final hashed token rooted at `0x142918C50` is `UsingJointDifference`;
- after the observer round-trip through `0x141434DA0`, the path feeds:
  - the derived vector,
  - the toggled mode byte,
  - the follow-up observer/state id,
  - and the current owner/runtime pair
  into `0x141520750`.

`0x141520750` is best described as a structured state-record builder:

- it clears a destination record header;
- copies two input vectors into record fields around `+0x20/+0x30/+0x40/+0x70`;
- seeds several fixed/default fields around:
  - `+0x50/+0x54/+0x5C`
  - `+0x88/+0x89`
  - `+0x8C`
  - `+0x90`
  - `+0x98/+0xC0/+0xD0`
  - `+0xE0/+0xF0/+0x100/+0x110`
  - `+0x120/+0x124`
- copies the current actor/runtime pair (`object + 0xA8/+0xBC`) into the front
  of the produced record when a source actor is present;
- and resolves a downstream record pointer through `0x1417651D0`, storing it at
  `produced_record + 0x90`.

So the end of the higher `weapon-fire` path is no longer just "observer math".
It ends in:

- a concrete owner-local observer/state update,
- then a produced state record tagged by `UsingJointDifference`,
- then downstream consumers that can read the current actor pair and the
  precomputed vectors from one place.

### `UsingJointDifference` Is a Reused Produced-State Type, Not a One-Off `weapon-fire` Blob

The next pass made the reuse visible enough to tighten the model:

- `0x142918C50` is not just an opaque token; the nearby string is
  `DcCalcStartPhaseUsingJointDifference`.
- there are several real callsites to `0x141520750`, including:
  - the already-mapped `weapon-fire`/observer family around `0x14143725B`;
  - a sibling family around `0x141438061` with the same
    `weapon-art-defs(-hash)` round-trip and the same `UsingJointDifference`
    publish tail;
  - a separate family around `0x141472277` that again computes a vector,
    resolves the same owner-local weapon-art observer layer, and then builds
    the same `UsingJointDifference` record;
  - a simpler wrapper at `0x140A40A30`.

The simple wrapper `0x140A40A30` is useful because it strips away a lot of
noise:

- it forwards directly into `0x141520750`;
- then stores one extra pointer at `wrapper_owner + 0x130`;
- clears/defaults fields around `+0x140/+0x150/+0x160/+0x170`.

That makes `0x141520750` look less like a combat-special builder and more like
a shared "produced state object with vector + actor pair + downstream record"
constructor used by multiple owners.

The sibling family around `0x141438061` strengthens the same point:

- it computes a derived vector;
- resolves the current state id through `0x1414FA380 -> 0x141614E40`;
- extracts `record->+0xB8->+0xC4` as a follow-up state id;
- builds a `UsingJointDifference` record through `0x141520750`;
- then continues into another `UsingJointDifference`-tagged publish branch.

And the family around `0x141472277` repeats the same structural pattern from a
different owner layout:

- derive vector;
- resolve current state through the cached weapon-art registry;
- build `UsingJointDifference` record via `0x141520750`;
- then continue with the same token family and downstream state id logic.

So the safe correction now is:

- `UsingJointDifference` is not a local `weapon-fire` record type;
- it is a shared produced-state record family that the higher weapon observer
  path happens to use.

That is better for the co-op project, because it suggests the downstream state
format is reusable across multiple owner families instead of being welded to one
combat-only code path.

### The Shared `UsingJointDifference` Record Already Feeds a Start-Phase / Locomotion-Flavored Consumer

One more pass downstream makes the reuse even stronger:

- the heavy consumer at `0x141520890` is not a generic serializer;
- it is keyed by nearby authored tokens:
  - `calc-start-phase-using-joint-diff`
  - `DcCalcStartPhaseUsingJointDifference`
  - `calc-start-phase-range`
  - `calc-start-phase-dist`
  - `calc-start-phase-max-frames`
  - `calc-start-phase`

Structurally, `0x141520890`:

- accepts the record built by `0x141520750`;
- reuses the current actor/runtime pair and query path through `virtual +0x530`;
- checks or derives another small pair record via `0x141E7D510`;
- hashes `Select Spawner By Name...` and additional authored tokens into local
  CRC/string ids;
- builds another descriptor through `0x1417C32A0 / 0x1417C3790`;
- and then continues through a larger produced-state/application path.

The exact high-level name still needs care, but the safe conclusion is already
useful:

- the `UsingJointDifference` record family is not only reused across multiple
  combat-ish owners;
- it also feeds a downstream subsystem that looks start-phase / locomotion /
  placement flavored.

That is a strong sign that this produced-state format sits in a broader actor
state-transition layer, not in a single weapon-only feature silo.

### The `calc-start-phase` Family Drives `UsingJointDifference` Through a Local Multi-Branch Transition Cluster

The first real caller cluster of `0x141520890` is now clear enough to record:

- there are four nearby callsites into `0x141520890`:
  - `0x1415215FB`
  - `0x141521627`
  - `0x141521724`
  - `0x141523542`
- the local family around `0x141521520..0x141521E90` is not a tiny wrapper.
  It is a multi-branch transition/update cluster that:
  - conditionally invokes `0x141520890` for several state slices;
  - iterates local tables/arrays;
  - resolves current actor/runtime objects through `0x1410F0D10`;
  - computes distances and normalized deltas against `actor + 0x100`;
  - queries `0x1417651D0` records repeatedly;
  - and publishes or applies follow-up state through several downstream calls.

The authored anchors in this family are now strong:

- `calc-start-phase-using-joint-diff`
- `DcCalcStartPhaseUsingJointDifference`
- `calc-start-phase-range`
- `calc-start-phase-dist`
- `calc-start-phase-max-frames`
- `calc-move-mode-transition-phase-max-frames`

This makes the family look like a genuine state-transition/start-phase subsystem
over actor/runtime data, not just a record serializer.

There is also a simpler helper around `0x140A40AB0` that is worth separating
from the transition logic:

- it is not another `UsingJointDifference` builder;
- it searches spawner-related tables/arrays, including a fallback through
  `0x14162C830`;
- it returns success plus a resolved index/owner slot.

So the safe split is now:

- `0x140A40AB0` looks like a spawner/group selection helper;
- `0x141520890` and its callers are the real `calc-start-phase` transition
  family that consumes `UsingJointDifference` records.

### The `calc-start-phase` Family Already Publishes to Actor `virtual +0xD0` and Feeds a Heavy Runtime Consumer

Two downstream helpers under that family are now clear enough to pin down:

- `0x1417F8910`:
  - takes a destination block and a target object;
  - if the target is valid and not blocked by `target + 0x40 bit 0x1`, it
    acquires/releases guards through `0x1416A2E80 / 0x1416A2ED0`;
  - then forwards the structured block through the target object's
    `virtual +0xD0`;
  - otherwise it writes the default fallback block at `dest + 0x18`.

That is already actor-facing publish/apply behavior, not just state packing.

- `0x1402EF0A0`:
  - starts from a `0x1417651D0`-resolved record id plus a vector payload;
  - packages several actor/runtime pairs (`+0xA8/+0xBC`) and a large payload
    block around `rsp + 0xA0..0x1D5`;
  - resolves live actor identity and marks `+0x72`;
  - then forwards the result into the heavier handler `0x1402EF420`.

The neighboring authored tokens make the phase domain stronger, not weaker:

- `set-and-return-player-sync-actor-start-phase`
- `wait-and-get-player-sync-actor-start-phase`
- `*player-move-mode-transitions*`
- `move-mode-transition`
- `anim-state-start-phase-func`
- `calc-new-phase-from-speed-tree`

So the safest update now is:

- `UsingJointDifference` is not only a reusable produced-state record family;
- it already feeds actor-facing publish/apply code and a heavier runtime phase /
  move-mode transition consumer.

### The Heavy Runtime Consumer `0x1402EF420` Already Applies Branch-Dependent Phase State Into Actor-Facing Transition Helpers

The next layer under `0x1402EF0A0` is now concrete enough to pin down without
guessing:

- `0x1402EF420` is not a formatter or logging stub.
  It is a heavy runtime apply path that:
  - begins with `0x1402EE710`;
  - gates on globals around:
    - `0x143F051C8`
    - `0x1430439AC`
    - `0x1430439A4`
  - samples the current world transform through repeated `0x140186600` calls;
  - chooses a branch/state id from authored ids stored in the incoming record
    at:
    - `+0x90`
    - `+0x94`
    - `+0x9C`
    - `+0xA0`
    - `+0xA8`
    with the selection influenced by the scalar input carried in `xmm6`;
  - converts the retained/runtime pair and several transform blocks into a
    larger phase-application basis.

- the same path then uses:
  - `0x1415D23B0`
  - `0x142177E50`
  - `0x1401781E0`
  as a coherent math/basis-building chain:
  - `0x1415D23B0` normalizes and cross-combines two incoming vectors into a
    packed orientation/phase basis block;
  - `0x142177E50` converts that packed block into a piecewise interpolated or
    ranged vector record using thresholds at offsets like `+0x14/+0x18/+0x20/+0x24/+0x28`;
  - `0x1401781E0` resolves a retained/runtime object through
    `0x14386FD00`, copies its cached transform at `+0x10`, and emits a world-
    space basis vector from the object's orientation/translation.

- `0x1402EF420` then runs that basis through several actor-facing transition
  helpers:
  - `0x141BE20C0`
  - `0x141BE2D90`
  - `0x1417423B0`
  - `0x141BE4EA0`

The helper roles are now distinguishable enough to record:

- `0x141BE20C0`
  - consumes an actor/runtime pair plus a small instruction array;
  - iterates `0x20`-byte entries with type-like values `0/1/2`;
  - for type `1` it recurses into itself on nested instruction records;
  - for type `2` it delegates to `0x141BE1E60`;
  - that makes it a real transition-instruction dispatcher over a resolved live
    actor, not a math helper.

- `0x141BE2D90`
  - resolves the target actor through the same runtime-pair validation path;
  - sets bit `0x8` in the resolved actor-side record;
  - writes three floats to `+0x50/+0x54/+0x58`.
  - safest interpretation: actor-side transition target-position writer.

- the nearby sibling writers under the same family strengthen that reading:
  - `0x141BE2E70` writes a scalar to `+0x5C`;
  - `0x141BE2F50` writes a scaled short/integer to `+0x2C`.
  So this family is already updating a concrete actor-side transition state
  object, not just pushing observer data.

- `0x1417423B0`
  - takes two object-backed transforms plus scalar parameters;
  - rebuilds another world-space basis/orientation block;
  - and returns that derived block through `0x141742290`.
  This is part of the same phase/transition application family rather than a
  detached utility.

- `0x141BE4EA0`
  - consumes the rebuilt phase payload plus live actor/runtime data;
  - resolves and copies actor transform state;
  - performs another orientation/basis rebuild;
  - validates the resulting vectors through a debug/assert path;
  - queries two actor-side objects through `virtual +0x168`;
  - then finalizes a structured transition record through `0x141BDFE70`.
  This is the strongest current candidate for the real actor-side phase-
  transition commit stage below `UsingJointDifference`.

There is one more concrete execution detail at the end of `0x1402EF420`:

- after the larger phase/basis build it loops over retained player slots `0`
  and `1`;
- resolves each slot through `0x1410F0F60`;
- computes a normalized direction from the current world position to the
  resolved actor transform;
- then calls `0x141F84640` with the slot index.

So `0x1402EF420` is already doing all of the following:

1. selecting authored phase ids,
2. rebuilding phase/orientation basis,
3. dispatching transition instructions,
4. writing actor-side transition fields,
5. finalizing a structured transition record,
6. and fanning follow-up logic across retained player slots `0/1`.

That is well past "observer state". It is a real runtime state-application
path sitting in the same broader family as `UsingJointDifference`,
`calc-start-phase`, and player move-mode transitions.

### Below the Phase Apply Path, `0x141BDFE70` Stays in Transition Resolution While `0x141F84640` Fans Out Into World-Effect Logic

The next split under the heavy phase-apply family is also clear enough now:

- `0x141BDFE70`
  - is not a raw world-effect launcher;
  - it behaves like a transition-result resolver/finalizer over the current
    actor owner:
    - early-outs with explicit result codes `2`, `7`, `4`, `3` depending on
      owner flags and resolved state availability;
    - consults owner state around:
      - `+0x987`
      - `+0x990`
      - `+0x998`
      - `+0xA28`
      - `+0x960`
    - queries authored/knowledge-style state via:
      - `0x141764B50`
      - `0x141765030`
      - `0x14209A8B0`
      - `0x14209A740`
    - recurses into itself or into sibling `0x141BDF510` for multi-entry
      transition selection;
    - and keeps producing `(ptr,id,status)`-style result objects in the output
      block.
  - safest interpretation: this is still part of actor-side transition
    resolution/selection, not yet the final world-effect execution layer.

- `0x141F84640`
  - sits on the slot-fanout tail of `0x1402EF420`;
  - is not another state resolver for player ownership;
  - it immediately shows effect/presentation anchors instead:
    - the downstream path is associated with
      `big4/src/game/process-explosion.cpp`
    - nearby debug strings include:
      - `IsFinite(transform)`
      - `IsFinite(bf)`
      - `Unhandled ParticleError enum %d!`
  - structurally it:
    - gates on a global runtime object at `0x143FE1070`;
    - checks per-slot state and thresholds in the incoming block;
    - packages structured records through `0x1417C3790`;
    - computes bounded scalar ranges from fields like `+0x4/+0x8/+0xC/+0x10/+0x14`;
    - hashes local tokens and then calls `0x141BEB0F0`.
  - safest interpretation: this is a retained-player-slot follow-up path that
    has already crossed from transition state into world-effect / particle /
    presentation-side processing.

This split matters:

- `0x141BDFE70` is still interesting for Nadine because it remains inside the
  actor-side phase/transition decision machinery.
- `0x141F84640` looks more like downstream world-feedback execution and is less
  likely to be the place where Nadine becomes a valid second player by itself.

So the current best ownership-facing focus below the `UsingJointDifference`
family is:

1. `0x141BDFE70`
2. sibling `0x141BDF510`
3. and only then the effect-side branch around `0x141F84640 / 0x141BEB0F0`

That keeps the reverse effort on gameplay-state qualification before diving too
deep into particles/effects.

### The Sibling `0x141BDF510` Is Actually Particle-Root Allocation Under This Branch, While `0x141F84640 -> 0x141BEB0F0` Is Camera-Shake / Explosion-Shake Follow-Up

The next step down makes that split even cleaner:

- `0x141BDF510`
  - still starts from `owner + 0x58` and computes a slot-like index/offset;
  - still probes bitsets around:
    - `+0x3B0`
    - `+0x430`
    - `+0x4B0`
    - `+0x630`
  - but the associated debug/assert strings now make its real role much
    clearer:
    - `ParticleManager::AllocRootData`
    - `ParticleRootData %d is marked allocated!`
    - `ParticleRootData %d has non-zero particle count (%s)!`
    - `ParticleRootData %d is marked active!`
    - `ParticleRootData %d is marked retiring!`
  - so this helper is not the core player-ownership allocator.
  - it is a particle-root/effect-root allocation path that the transition
    family can descend into.

That changes the safe split:

- `0x141BDFE70` remains useful as actor-side transition resolution.
- `0x141BDF510` is already an effect-side particle-root allocation helper.
- `0x141F84640 -> 0x141BEB0F0` remains a later camera-shake / explosion-shake
  follow-up path.

The effect-side branch is now more specific than before:

- `0x141F84640`
  - no longer just "world-effect / particle flavored";
  - it clearly belongs to a `camera-shake` family:
    - nearby strings:
      - `camera-shake`
      - `CameraShake::GetArrayOfNameIds`
      - `shoot-shake`
      - `shoot-shake-rumble-settings`
      - `explosion-shake`
    - source path:
      - `shared/src/gamelib/camera/camera-shake.cpp`
  - it gates on per-slot thresholds and then packages a camera-shake style
    record before calling `0x141BEB0F0`.

- `0x141BEB0F0`
  - is the concrete camera-shake/explosion-shake follow-up dispatcher under
    that branch;
  - it resolves one of several effect records (`rsp+0x60`, `+0x68`, `+0x50`,
    `+0x58`);
  - then routes into:
    - `0x141BE8FF0`
    - `0x141BE9AE0`
    - `0x141BE9B90`
  - using global effect registries rooted around:
    - `0x146921700`
    - `0x146922710`
  - and helper strings/settings associated with `shoot-shake` / camera-shake.

- `0x141F83CC0`
  - which sits immediately under `0x141F84640`,
  - looks like the queue/update layer for those camera-shake entries:
    - maintains capped per-slot arrays (count cap `0x32`);
    - updates or appends records in `0x146AA5A20/0x146AA5A28`;
    - and writes timing/strength/source fields into those entries.

This is a useful correction to the larger player-like map:

- the `UsingJointDifference -> calc-start-phase -> actor transition` family is
  still promising for Nadine because it resolves gameplay-state and actor-side
  transition slots;
- but one visible downstream consumer of that family is already only
  presentation feedback (`camera-shake` / `shoot-shake` / `explosion-shake`),
  not player ownership itself.

That means the next ownership-facing work should stay on:

- `0x141BDFE70`
- the actor-side state/knowledge resolvers it queries
- and the branches before the particle-root allocator and camera-shake follow-up

and not over-invest in either:

- the particle-root branch under `0x141BDF510`
- or the camera-shake branch under `0x141F84640 / 0x141BEB0F0`

except as supporting evidence that the transition family already reaches world
feedback systems.

### `0x14209A740 / 0x14209A8B0` Are Owner-Local Dual-Hash State Resolvers Under `0x141BDFE70`

The actor-side resolver path in `0x141BDFE70` is a little cleaner now:

- `0x141765030` remains the global knowledge/state-record registry layer.
- But the two other helpers used there:
  - `0x14209A740`
  - `0x14209A8B0`
  are not arbitrary gameplay branches.

Both functions have the same core shape:

- primary hash table:
  - bucket count/stride fields on the owner (`+0x90` or `+0x128`)
  - linked-list buckets via `owner->[table]`
  - record key compare at `node + 0x10`
- secondary/side validation:
  - optional linked object at `node + 0x18`
  - second hash family via owner fields:
    - `+0x398`
    - `+0x428`
  - secondary key compare through `linked + 0x28` or `linked + 0x0C`

So the safe interpretation is:

- `0x14209A740`
  - resolves an owner-local authored state node by id through the primary hash
    rooted at `owner + 0x90`;
  - then validates or cross-resolves its linked side-node through the second
    hash family at `owner + 0x398/+0x428`;
  - returns either the resolved linked object or null.

- `0x14209A8B0`
  - is the sibling resolver over the owner hash rooted at `owner + 0x98/+0x128`;
  - performs the same secondary validation through `owner + 0x398/+0x428`;
  - and returns the validated linked object at `node + 0x18` when the pair is
    coherent.

That matters because it shows `0x141BDFE70` is not only switching on status
codes. It is resolving authored state nodes through a proper owner-local
registry layer and only then deciding whether to continue into:

- actor-side transition result construction,
- particle-root allocation,
- or camera-shake/explosion-shake follow-up.

### External Consumers Pin `node + 0x30` as a Long Mask Layer and `node + 0x12 bit3` as a Short Enable Gate

Two external consumers of the owner-local authored-state resolvers make the
node layout more concrete.

- `0x141EF59C0`
  - walks an authored-state id list under the current owner/runtime object;
  - resolves each id through `0x14209A740`;
  - then filters the resolved node by testing `node + 0x30` against two long
    mode-specific masks:
    - `0x200000000000`
    - `0x100000000000`
  - only after that does it build the larger downstream payload objects through
    `0x14036BD80`, `0x1415A0E90`, and `0x14163F930`.

This is strong evidence that `node + 0x30` is a long authored-state
capability/category mask used for mode-specific filtering before observer or
publish payloads are materialized.

- `0x141F564E0`
  - canonicalizes an authored-state id through `0x141764B50`;
  - resolves it through `0x14209A8B0`;
  - reads `BYTE PTR [node + 0x12]`, extracts `bit3`, and uses that as an
    early boolean gate before continuing into the larger observer/publish path.

That makes the short field at `node + 0x12` look like a compact authored-state
flag byte, with at least `bit3` behaving as an enable/override gate.

The current safe interpretation is therefore:

- `node + 0x30`
  - long capability/category masks for mode-specific authored-state filters;
- `node + 0x12 bit3`
  - compact enable/override gate checked before observer publish/update logic.

This keeps the reverse on the ownership-facing side of the pipeline and away
from already-late particle or camera-shake branches.

### The Direct `0x141BDFE70` Consumers Form a Structured Transition-Result Family

Walking the seven direct callsites on `0x141BDFE70` made the usage pattern much
clearer.

- `0x141BDCB4A` sits in an actor-local consumer that:
  - passes a structured input block into `0x141BDFE70`;
  - expects the returned `XMMWORD` to behave like `(ptr, id, status)`;
  - validates the returned object/id pair;
  - sets `WORD PTR [resolved_object + 0x4] |= 0x10`;
  - caches the transition result back into `consumer + 0x20`;
  - mirrors a live-entity handle/id pair into `consumer + 0x30`, with a
    fallback through `0x141D22420` if the current cached pair is missing.

So this path is not just "asking a question". It consumes a transition result,
marks the resolved object, and caches both the transition record and the
corresponding live entity identity.

- `0x141BE0022` and `0x141BE00BA`
  - are looped siblings over authored-state id arrays;
  - repeatedly invoke `0x141BDFE70` with the same structured input shape;
  - then update local owner state around:
    - `+0x984`
    - `+0x990`
    - `+0x998`
    - `+0xA28`

This matches the earlier ownership-facing reading of `0x141BDFE70`: it is part
of an actor-side transition qualification/update family, not a one-off effect
helper.

- `0x141BE083E` and `0x141BE08E3`
  - are global/singleton-flavored siblings of the same pattern;
  - use the same `0x141BDFE70` structured call shape, but through the global
    owner context rooted at `0x146920CA0`;
  - then update global state around:
    - `0x146921624`
    - `0x146921630`
    - `0x146921638`
    - `0x1469216C8`

So the same transition-result family exists in both actor-local and global
singleton forms.

- `0x141BE4E4C` and `0x141BE51E9`
  - sit in a larger composite builder;
  - first call `0x141BDFE70` to seed one result slot;
  - later derive extra entity/runtime state through validated handle pairs and
    `entity->virtual +0x168`;
  - then feed that derived state back into a second `0x141BDFE70` call.

This is the strongest evidence so far that `0x141BDFE70` is part of a proper
transition-result composition API: callers do not just branch on its status,
they build layered result records, mark resolved objects, and mirror live actor
identity alongside the returned state.

### Correction: `0x146920CA0 / 0x146920CF0 / 0x146920CF8` Belong to `ParticleManager`, Not a Generic Transition Manager

The previous reading of the `0x146920CA0 / 0x146920CF0 / 0x146920CF8` globals
as a generic transition-manager family was too broad.

The hard evidence is now explicit:

- `0x141BD4DD0`
  - anchors to strings:
    - `ParticleManager::Init`
    - `shared/src/gamelib/render/particle/particle.cpp`
  - and initializes the same global trio:
    - `0x146920CA0`
    - `0x146920CF0`
    - `0x146920CF8`

So the safe interpretation is:

- `0x146920CA0`
  - a large `ParticleManager`-owned context object;
- `0x146920CF0`
  - a separate manager-owned authored-state/event resolver object used by the
    singleton particle-event paths;
- `0x146920CF8`
  - a compact helper used by the same particle-event family when rebuilding
    short-id keyed records.

Likewise, the big consumer at `0x141BD8BE0` is not a generic ownership manager
update. It sits in the `ParticleManager::UpdateEvents` side of the graph:

- strings nearby resolve to:
  - `ParticleManager::UpdateEvents`
  - `particle-kill`
  - `particle-settings`
- and the function:
  - iterates entries out of the `0x146920CA0` manager-owned arrays;
  - resolves authored-state ids through `0x146920CF0 -> 0x14209A740`;
  - filters them by the same `node + 0x30` mask layer;
  - then feeds structured results through `0x141BE4EA0`.

This does not make the authored-state work useless. It still confirms that the
same owner-authored-state node/mask layer is shared by runtime systems outside
the immediate actor-local path. But it does mean the `0x146920CA0` branch
should now be treated as a particle/event subsystem, not as the core player
ownership stack.

One helper from that branch is still structurally useful:

- `0x141A81B80`
  - takes a manager/context object and a resolved authored-state node;
  - reads policy bits `8` and `9` out of `context + 0x130`;
  - tests them against the same long authored-state masks at `node + 0x30`:
    - `0x100000000000`
    - `0x200000000000`
  - and returns a boolean gate.

So even though this branch is ParticleManager-owned, it gives a clean reusable
statement of the policy model:

- context-side mode bits live in a compact word at `+0x130`;
- node-side authored-state capabilities live in the long mask at `+0x30`;
- the runtime gate is literally "context policy bits vs node capability masks".

### `0x141BDC640` Looks Like an Actor-Local Pending Transition Entry Processor Above `0x141BDFE70`

Separate from the ParticleManager singleton branch, the large local function
around `0x141BDC640` still looks ownership-facing.

What it does:

- walks a local pending-entry/list structure rooted at:
  - `owner + 0x668`
  - `owner + 0x670`
  - `owner + 0x6F0`
  - `owner + 0x6F8`
- resolves authored-state ids through the actor-local path:
  - entry-derived id -> `owner_context + 0x50` -> `0x14209A8B0`
- if that fails, falls back into:
  - `0x141D21F90`
  - `0x141D231E0`
  to build a live-entity query/transform record;
- then feeds the resulting structured input into `0x141BDFE70`;
- consumes the returned transition record exactly like the other actor-local
  consumers:
  - validates `(ptr,id,status)`
  - sets object bits
  - mirrors/caches the live entity pair
  - or clears and unlinks the pending entry from the local list.

This makes `0x141BDC640` a stronger ownership-facing anchor than the global
particle branch. It already sits on:

- local pending transition entries,
- actor/runtime identity,
- authored-state resolution,
- transition-result application/caching.

So the best remaining upward route is now likely through the owner/class that
owns this `+0x668/+0x670/+0x6F0` pending-entry cluster.

### The Pending Transition Cluster Lives Inside a Larger Actor-Local Runtime Owner Family

Pushing a little higher above `0x141BDC640` shows that the `+0x668/+0x670/+0x6F0`
cluster is not floating on its own. It is embedded in a larger actor-local
runtime owner family with several internal subobjects.

The clearest anchor is `0x141BDCED0`, which builds and refreshes a large
runtime subobject rooted at `owner + 0x30`:

- allocates/initializes a `0x280` owned object;
- copies a broad state image out of owner fields spanning roughly:
  - `+0x994 .. +0xA2C`
- stores the built object at:
  - `owner + 0x30`
  - plus sibling pointers like `owner + 0x1E0/+0x1F8/+0x200`
- then derives internal booleans/counters:
  - `+0x987`
  - `+0x988`
  - `+0x989`
  - `+0x98A`
  - `+0x98B`
  - `+0x98C`
  - `+0x9C1`

So the actor-local family around this code already has:

- a built runtime object at `owner + 0x30`;
- mirrored local state bytes/counters around `+0x987..+0x9FF`;
- and the pending-entry/list cluster at:
  - `+0x668`
  - `+0x670`
  - `+0x6F0`
  - `+0x6F8`

That is a much better candidate for a real gameplay owner than the
ParticleManager singleton branch.

Related supporting paths:

- `0x141BDD400`
  - constructs the large `owner + 0x30` runtime subobject and seeds
    `owner + 0x1E0/+0x1F8/+0x200`;
- `0x141BDCED0`
  - refreshes/commits the owner-local state image into that runtime object;
- `0x141BDC640`
  - then consumes local pending transition entries against the same owner and
    runtime object, using `0x141BDFE70` as the transition-result resolver.

This is now the strongest local class-family path toward Nadine promotion:

- owner-local runtime object at `+0x30`
- owner-local state bytes/counters around `+0x987..+0x9FF`
- pending transition cluster at `+0x668/+0x670/+0x6F0/+0x6F8`
- transition-result resolution through `0x141BDFE70`

rather than any of the already-demoted singleton particle/event branches.

### `0x141BDDbe0` Is the Central Refresh/Commit Wrapper for This Local Owner Family

Following the one-step wrappers above `0x141BDD400` / `0x141BDCED0` tightens the
picture further.

`0x141BDD3E0` is not a vtable slot for the owner itself. It is a compact
callback adapter:

- reads:
  - `record + 0x0` -> owner pointer
  - `record + 0x8` -> runtime/context pointer
  - `record + 0x10` -> byte flag
- then tail-calls straight into `0x141BDDbe0`.

Two places build the same callback-record shape:

- `0x140164420`
  - allocates a small `0x18` record,
  - stores:
    - owner/context at `+0x0`
    - shared runtime/context object at `+0x8`
    - byte flag at `+0x10`
  - and publishes callback target `0x141BDD3E0`
- `0x141BDE160`
  - builds the same callback-record form locally and again stores
    `0x141BDD3E0` as the invoked target

So `0x141BDD3E0` is best treated as a queued/invoked callback entry point for
this owner refresh path, not as the owner's final semantic identity.

The real center of gravity is `0x141BDDbe0`:

- acquires the owner-local guard at `owner + 0x10`
- derives local booleans:
  - `+0x987`
  - `+0x988`
  - `+0x989`
- calls `0x141BDD400` and stores the returned runtime subobject at `owner + 0x30`
- clears the local block at `owner + 0x530`
- calls the already-known refresh path `0x141BDCED0`
- conditionally runs follow-up owner methods:
  - `0x141BD74B0`
  - `0x141BDE250`
  - `0x141BD5FB0`
  - `0x141BDB0E0`
  - `0x141BDBB20`
  - `0x141BD6CD0`
  - `0x141BD62C0`
  - `0x141BDB6B0`
  - `0x141BD8020`
  - `0x141BD79D0`
- then commits data from the built runtime object back into the outer shell
  object at `owner->0`:
  - writes `outer + 0xA0 = owner + 0x30`
  - writes `outer + 0x148 = (owner + 0x30)->0x1E0`
  - updates the shell's `+0x80/+0x88`, `+0x20/+0x30/+0x40/+0x50/+0x60/+0x70`
    staging fields
  - rebuilds `outer + 0x130` through `0x141A7FEC0`
  - finalizes through `0x141A81300`, then mode-selects between
    `0x141A81570` and `0x141A817A0`

This is stronger than the earlier view of `0x141BDCED0` alone. The real local
ownership-facing stack now looks like:

- queued callback record -> `0x141BDD3E0`
- central owner refresh/commit -> `0x141BDDbe0`
- runtime subobject builder -> `0x141BDD400`
- state-image refresh -> `0x141BDCED0`
- pending transition processing -> `0x141BDC640`

So `0x141BDDbe0` is the current best candidate for the central actor-local
promotion/rebind wrapper that bridges:

- local owner state
- runtime subobject construction
- transition resolution
- and the outer shell object that the rest of the game sees.

### The Same Owner Family Has Explicit Authored-State Bitset Banks Under `+0x3B0/+0x430/+0x4B0/+0x530/+0x630`

Pushing one layer down inside the same class family shows that this local
owner is not just a generic wrapper around `owner + 0x30`. It also owns a very
explicit banked authored-state/slot subsystem.

The strongest helpers here are:

- `0x141BDE250`
- `0x141BD79D0`
- `0x141BD8020`
- `0x141BD74B0`

`0x141BDE250` looks like a local reset/rebuild pass for these banks:

- clears/swaps outer-shell staging pointers around:
  - `owner->0 + 0x30`
  - `owner->0 + 0x60`
  - `owner->0 + 0xE0`
- zeroes:
  - `owner + 0x630` (`0x800` bytes)
  - `owner + 0x430` (`0x80` bytes)
- iterates bitset banks rooted at:
  - `+0x3B0`
  - `+0x4B0`
- calls `0x141BD7D00` while rebuilding missing entries
- then continues into list/queue state around:
  - `+0x798`
  - `+0x818`
  - `+0x828`

So this is not effect-side cleanup. It is an owner-local authored-state bank
rebuild/reset.

`0x141BD79D0` looks like the corresponding state-slot materialize/promote path:

- scans the bank at `owner + 0x5B0` for the next active authored-state slot
- cross-checks corresponding presence bits in:
  - `+0x3B0`
  - `+0x430`
  - `+0x4B0`
  - `+0x530`
- pulls a live slot record through `owner + 0x58 -> 0x1421FCDD0`
- validates/repairs that record
- writes back:
  - slot state/timestamps
  - updated bank bits in `+0x530` and `+0x4B0`
- sets:
  - `owner + 0x98E = 1`

That reads much more like "authoritative slot/state-bank update" than like any
render or particle helper.

`0x141BD8020` is the heavier derived-batch builder over the same subsystem:

- starts from `owner + 0x3B0`
- allocates helper storage blocks (`0x28`, `0x98`, and a `0x10 * count` list)
- checks consistency between:
  - bit presence in `+0x430`
  - counter table at `+0x630`
- logs/asserts on mismatches
- resolves live slot records through `owner + 0x58`
- writes compact `(slot_id, packed_size/alignment, slot_ptr)` tuples into the
  produced list

So `0x141BD8020` is best read as a "build resolved authored-state slot batch"
helper for the same owner family.

Finally, `0x141BD74B0` looks like an earlier "pick next active authored-state
slot" gate:

- starts from bank `+0x4B0`, with fallback into `+0x528`
- derives a candidate slot index
- checks corresponding bits in `+0x3B0`
- then advances into the next slot/update pass

This tightens the interpretation of the whole family:

- `0x141BDDbe0` is the central local owner refresh/commit wrapper
- `0x141BDE250` resets and rebuilds authored-state banks
- `0x141BD79D0` materializes/promotes active slot records
- `0x141BD8020` builds a resolved batch from those slot banks
- `0x141BDC640` consumes pending transition entries against the same owner
- `0x141BDFE70` resolves the resulting transition state

At this point the best semantic reading is no longer "generic actor helper".
It is a local authored-state/phase owner that:

- maintains banked authored-state slots,
- resolves them to runtime records,
- consumes transition results,
- and commits the result into the outer gameplay shell.

### Correction: This Whole `0x141BDC640 / 0x141BDDBE0 / 0x141BDE250` Family Is Still ParticleManager-Owned

The next layer down resolves the ambiguity.

Once the sibling helpers under this family are opened, they stop looking like a
generic gameplay owner and start looking like a `ParticleRootData /
ParticleManager` state owner with shared authored-state machinery.

Hard evidence:

- `0x141BD79D0` hits assertions/messages anchored on:
  - `ParticleManager::FreeRootData`
  - `ParticleRootData %d being freed wasn't allocated`
  - `ParticleRootData %d being freed isn't active`
  - `ParticleRootData %d being freed is marked newly retiring`
  - `ParticleRootData %d being freed is marked retiring`
  - `ParticleRootData %d being freed already has id == 0`
- `0x141BD8020` is anchored on:
  - `ParticleManager::CreateRenderData`
  - `ParticleRootData %d has state (%s) that does not match its particle count`
- `0x141BD74B0` is anchored on:
  - `ParticleManager::UpdateFreeRetiredRootData`
- nearby sibling paths also reference:
  - `ParticleManager::UpdateBlocks`
  - `ParticleManager::UpdateRootData`
  - `ParticleManager::UpdateRetireRootData`

So the more accurate reading is:

- `0x141BDDbe0`
  - central refresh/commit wrapper for a local ParticleManager root-state owner
- `0x141BDD400`
  - builder for its runtime/root subobject at `+0x30`
- `0x141BDCED0`
  - refresh path for that same root-state image
- `0x141BDC640`
  - pending transition entry processor inside this same particle-root owner
- `0x141BDE250`
  - rebuild/reset of ParticleManager root-state banks and queues
- `0x141BD79D0`
  - `FreeRootData`-side state/bank promotion path
- `0x141BD8020`
  - `CreateRenderData`-side resolved batch builder

This does not make the reverse useless. It still confirms that:

- authored-state ids,
- transition-result resolution,
- banked slot state,
- and live-entity keyed records

are shared into particle root systems.

But it does demote this family as the primary route toward Nadine promotion.

The reason it looked ownership-facing at first is that the particle-root owner
still:

- stores live entity pairs,
- marks live entities active (`+0x72`),
- consumes authored-state transition results,
- and commits a built runtime shell object.

That is strong shared infrastructure, but not the player/Chloe ownership layer
we actually need.

### The Remaining Direct `0x141BDFE70` Cluster Around `0x146920CA0/CF0` Still Looks Particle-Root Oriented

Checking the remaining high callsites in the same neighborhood did not rescue
this branch back into gameplay ownership.

In particular:

- `0x141BE4E4C`
- `0x141BE51E9`

still feed `0x141BDFE70` from structures rooted at:

- `0x146920CA0`
- `0x146920CF0`

and continue to use the same local gating/result style as the already-demoted
ParticleManager branch.

Those wrappers still:

- validate live entity handles and mark `entity + 0x72 = 1`
- build structured transform/query blocks
- pass them into `0x141BDFE70`

which explains why they originally looked gameplay-facing.

But after the `ParticleRootData` strings in:

- `0x141BD74B0`
- `0x141BD79D0`
- `0x141BD8020`

that same pattern is now better understood as particle-root state reacting to
live entities, not as the player ownership stack itself.

So the current working conclusion is:

- `0x141BDFE70` remains useful as shared authored-state / transition
  infrastructure,
- but the direct consumer cluster we have mapped so far around
  `0x141BDC640 .. 0x141BE51E9`
  should now be treated as particle-root flavored,
- and therefore demoted as the primary route to Nadine promotion.

### Pivot Back to Explicit Player-Facing Transition Strings: `move-mode-transition` and Melee Button Verbs

After demoting the `ParticleManager`-owned branch, the cleanest live gameplay
anchors are the explicit player/melee transition strings:

- `*player-move-mode-transitions*`
- `move-mode-transition`
- `player-enter-balance-mode`
- `transition-player-into-appropriate-grapple-reaction`
- `melee-r1-hold`
- `melee-r1-release`
- `melee-lmouse-release`
- `melee-rmouse-hold`
- `melee-rmouse-release`

The useful xrefs so far are:

- `0x140EF6ED0`
  - hashes `move-mode-transition`
  - immediately pairs it with `player-enter-balance-mode`
  - then builds/publishes a small structured runtime record through
    `0x143682D30` and `0x1417D5F50`
- `0x140F7D28A`
  - repeats the same `move-mode-transition` + `player-enter-balance-mode`
    pairing inside a different local owner/indexed path
- `0x141271D20 .. 0x14127241F`
  - is a real gameplay-facing matcher over melee button/transition strings,
    not a registrar
  - checks hashed names including:
    - `melee-r1-hold`
    - `melee-r1-release`
    - `melee-lmouse-release`
    - `melee-rmouse-hold`
    - `melee-rmouse-release`
    - `transition-player-into-appropriate-grapple-reaction`

That already makes this branch a much better ownership/action candidate than the
demoted particle-root transition family.

### `0x141273B20` Is a Real Melee Button / Grapple-Reaction Command Dispatcher

The branch around `0x141273B20` is now clear enough to treat as a live gameplay
dispatcher, not just a hash comparer.

It:

- probes the current active button/transition set through `0x141E39D50`
- on match, pushes command entries into a local queue through `0x141273FA0`
- separately updates scalar/pressure-like values for existing entries through
  `0x1412743F0`

Confirmed command names on this path:

- `melee-r1-release`
- `melee-r1-hold`
- `melee-lmouse-press`
- `melee-lmouse-hold`
- `melee-lmouse-release`
- `melee-rmouse-hold`
- `melee-rmouse-release`
- `melee-r1`
- `melee-circle`
- `transition-player-into-appropriate-grapple-reaction`

The important practical point is that this is already a concrete
`player-facing action input -> queued gameplay command` path.

### `0x141273FA0` / `0x1412743F0` Maintain a 16-Slot Melee Command Queue, Not a Loose Helper Blob

The helper pair under `0x141273B20` turned out to be very structured:

- `0x141273FA0`
  - manages a 16-entry queue of `0x40`-byte command slots
  - optional fast path:
    - if the caller passes a byte flag
    - and the same command id already exists with scalar `slot+0x34 == 0`
    - it updates that slot in place instead of rotating the queue
  - otherwise it shifts older entries backward in bulk and inserts the new
    command at the front
  - every slot stores:
    - command id at `+0x0`
    - payload vectors at `+0x10` and `+0x20`
    - scalar/pressure values at `+0x30/+0x34`
    - small byte state at `+0x38`

- `0x1412743F0`
  - walks the same 16-slot queue
  - finds a matching command id
  - updates only the scalar at `slot+0x34`

So this is already a formal queued input/action structure, not an incidental
temporary buffer.

### `0x141274470` Is the Constructor for the Large Melee Command Queue Owner

`0x141274470` is the construction path for the owner object behind that queue.

Hard evidence:

- its nearby literal banner is:
  - `============== Melee Command Queue ==============`
- it also references:
  - `*melee-button-names*`

Structurally it:

- installs vtable `0x142A6C2E8`
- clears a large `0x860`-ish owner object
- leaves the 16-entry queue state rooted around:
  - `+0x40 .. +0x438`
- seeds multiple outer control/status fields around:
  - `+0x560`
  - `+0x5E0`
  - `+0x660`
  - `+0x680`
  - `+0x7A0`
  - `+0x7D0`
  - `+0x860`

This is important because it gives us a concrete player-melee owner object,
instead of yet another generic authored-state utility.

### `0x141272860` Is a Debug/Inspection Path Over the Same Melee Command Queue Owner

The large function at `0x141272860` is not the core dispatcher, but it anchors
the same owner family:

- starts by printing/anchoring the banner:
  - `============== Melee Command Queue ==============`
- references:
  - `*melee-button-names*`
  - `INVALIDATED`
  - formatting strings used to dump button/scalar state

This makes it a reliable secondary anchor for the same queue owner family and
confirms the semantics of the `0x141273FA0 / 0x1412743F0` structure.

### Updated Working Direction

At this point the current best gameplay-facing route is no longer the
particle-root transition family.

The stronger path is:

- explicit player movement transition record producers:
  - `move-mode-transition`
  - `player-enter-balance-mode`
- explicit melee/grapple command matcher/dispatcher:
  - `0x141271D20`
  - `0x141273B20`
- formal melee command queue owner:
  - `0x141274470`

This is a much better bridge toward:

- real Chloe/Nadine action ownership,
- concrete player verb execution,
- and later promotion/rebind experiments,

because it is already operating on named player/melee commands instead of
shared generic authored-state infrastructure.

### The Melee Command Queue Owner Reattaches into the Existing `+0x1F60/+0x1F68/+0x1FF8` Gameplay Cluster

The next useful step was raising the queue owner one level higher, and that
paid off immediately.

The only clean external callsite found so far is:

- `0x1410CB4F3`
  - allocates `0x870`
  - calls `0x141274470`
  - stores the result into `owner + 0x1F60`
  - then immediately calls the object's virtual `+0x8` with:
    - `rdx = owner`
    - `r8  = owner + 0x3BD0`

The same outer build path also constructs:

- another sibling object into `owner + 0x1F68`
- an earlier sibling into `owner + 0x1FF8`

and applies the same attach pattern.

That matters because it ties the explicit melee command queue back into the
same broad gameplay-state zone we already mapped around:

- `+0x1F78`
- `+0x1F80`
- `+0x1FB0`
- `+0x1FB8`
- `+0x1FC0`
- `+0x1FF0`
- `+0x1FF8`

So the melee command queue is not a detached minigame helper. It is being
mounted into the same actor/gameplay-state owner region we were already
treating as one of the strongest player-like participation candidates.

### `0x1412746B0` Anchors to `ProcessNpcMeleeManager` and Seeds Concrete Melee State Ids

The large constructor/initializer at `0x1412746B0` gives the first concrete
owner-family anchor above the queue layer.

Hard evidence:

- nearby string:
  - `ProcessNpcMeleeManager`

And during init it resolves and caches several authored ids straight into the
owner:

- `player-attacks`
- `player-throws`
- `*camera-melee-settings*`
- `*camera-melee-settings-net*`
- `stealth-edge`

It also:

- stores an external object/reference at `owner + 0x108/+0x110/+0x130`
- seeds cached pair/state into `+0x138`
- writes the resolved authored ids to globals/owner fields like:
  - `0x143073D08`
  - `0x143073D0C`
  - `0x143073DC0`
  - `owner + 0x858/+0x85C`
- builds another owned runtime object at `owner + 0x820`
- zeros/initializes a wide local state block around:
  - `+0x6C0`
  - `+0x6D0`
  - `+0x6F0`
  - `+0x7B8`
  - `+0x840`
  - `+0x854`
  - `+0x860`

So this owner family is already much closer to a real melee/gameplay manager
than to a generic registry wrapper.

### Updated Read on the Current Best Path

The current strongest gameplay-facing route is now:

1. explicit player movement transition producers
   - `move-mode-transition`
   - `player-enter-balance-mode`
2. explicit melee/grapple command matcher/dispatcher
   - `0x141271D20`
   - `0x141273B20`
3. formal melee command queue owner
   - `0x141274470`
4. larger melee manager / actor-state owner family
   - `0x1412746B0`
   - mounted into the outer actor at `+0x1F60/+0x1F68/+0x1FF8`

This is the first time the `player-melee command` path and the older
`+0x1Fxx gameplay-state cluster` have met cleanly in one owner region.

### `ProcessNpcMeleeManager` Is a Real Mounted Owner Family, with Its Own Teardown and Cached Actor-Pair Helper

The neighboring functions around the same vtable strengthened the class-family
reading:

- `0x141274E20`
  - is a teardown/reset path on the same `0x142A6C2E8` family
  - releases/clears:
    - owned object at `+0x130`
    - owned runtime object at `+0x820`
  - then falls back to a smaller common base layout
- `0x141283A20`
  - is a freeing-dtor style sibling on the same family
  - reuses the same release sequence for `+0x130` and `+0x820`
  - then optionally frees the outer `0x870` object when requested

That means the melee command queue owner / `ProcessNpcMeleeManager` side is a
formal mounted owner family with:

- constructor
- init path
- attach path through outer actor slots
- teardown/dtor paths

not an ephemeral helper.

There is also a small cached-actor helper in the same family:

- `0x141274EC0`
  - reads cached pair/state at `+0x138/+0x140`
  - if the cached object id matches,
  - marks the resolved live entity active via `entity + 0x72 = 1`

That is a useful sign for the Nadine goal: this family is already operating on
live gameplay actor identity, not just command names.

### The Outer Builder Around `0x1410CB2BA..0x1410CB8F0` Looks Like a Large Player-Melee Control Subgraph

Raising the scope one level higher made the ownership picture much clearer.

The large build sequence around:

- `0x1410CB2BA`
- through at least `0x1410CB8F0`

mounts a whole family of owned gameplay subobjects into one outer actor object.

Confirmed mounted slots on that outer owner:

- `+0x1D80`
- `+0x1DD8`
- `+0x1DF8`
- `+0x1E08`
- `+0x1E20`
- `+0x1E50`
- `+0x1E58`
- `+0x1F60`
- `+0x1F68`
- `+0x1F70`
- `+0x1FF8`

Every one of those follows the same pattern:

1. allocate a concrete subobject
2. construct/init it
3. store it into the outer owner slot
4. immediately call a virtual attach method on the subobject with:
   - `rdx = outer owner`
   - usually `r8 = outer owner + 0x3BD0`

Inside that same builder:

- `+0x1F60` receives the `ConstructMeleeCommandQueueOwner` object
- `+0x1F68` receives another melee-family sibling object
- `+0x1FF8` receives an earlier melee/gameplay-state sibling
- additional large mounted subobjects are created at:
  - `+0x1D80`
  - `+0x1E08`
  - `+0x1E50`
  - `+0x1E58`
  - `+0x1E20`

This is the strongest structural evidence so far that the `+0x1Fxx` region is
not just a bag of helpers. It is a formal mounted gameplay-state / melee /
player-control subgraph.

### The Outer Mounted Graph Is Very Likely in the `player-melee-control` Layer

Two string anchors now line up with that reading:

- `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\melee\\npc-melee-manager.cpp`
- `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\melee\\player-melee-control.cpp`

And the mounted subobject family itself is now explicitly tied to:

- `ProcessNpcMeleeManager`
- `player-attacks`
- `player-throws`
- `stealth-edge`
- `*camera-melee-settings*`
- `*camera-melee-settings-net*`

So the current best interpretation is:

- the outer owner is sitting in the `player-melee-control` stratum,
- one of its mounted children is `ProcessNpcMeleeManager`,
- another mounted child is the explicit melee command queue,
- and the older `+0x1Fxx` gameplay-state cluster is therefore much closer to a
  real player-control graph than to a generic NPC utility layer.

### `+0x1F68` and `+0x1F70` Are Separate Mounted Class Families, Not Anonymous Buffers

The sibling mounted subobjects beside the melee command queue now read as
distinct class families with their own tiny ctor/dtor clusters.

For `owner + 0x1F68`:

- build site in the outer owner:
  - installs vtable/data table `0x142A6A500`
  - object size `0x140`
- sibling ctor/dtor-like helpers:
  - `0x14125C430`
  - `0x14125C4A0`
  - `0x14125C4D0`

For `owner + 0x1F70`:

- build site in the outer owner:
  - installs vtable/data table `0x142A6A930`
  - object size `0x130`
- sibling ctor/dtor-like helpers:
  - `0x14125E440`
  - `0x14125E4B0`
  - `0x14125E4E0`

So at minimum the outer player-melee owner mounts:

- one large `ProcessNpcMeleeManager` family
- one explicit melee command queue family
- at least two additional mounted gameplay/melee class families at
  `+0x1F68` and `+0x1F70`

This is useful because it means the `+0x1Fxx` region is not one monolithic
blob. It is a composed graph of mounted runtime classes, which is exactly the
shape we would expect from a real player-combat/controller subsystem.

### `+0x1F68` and `+0x1F70` Already Behave Like Mounted Melee State Selectors, Not Passive State Holders

The next useful result is behavioral, not just structural.

The `+0x1F68` family is already in a live decision/presentation loop:

- in the large path around `0x14125C600`
  - the outer owner reads `owner + 0x1F68`
  - calls its virtual `+0x48`
  - builds/publishes a structured record through:
    - `GetCameraManager(0)` / `0x141C06420`
    - `0x141C0DFD0`
  - and later uses `owner + 0x1F68 -> virtual +0x60` as a gate

That means `+0x1F68` is already participating in a
`melee state -> camera/presentation / record publish` path.

The `+0x1F70` family is already in a sibling state-selection branch:

- in the large path around `0x14125E650`
  - the outer owner reads `owner + 0x1F70`
  - calls virtuals:
    - `+0x40`
    - `+0x48`
    - `+0x38`
    - `+0x30`
  - the result competes with:
    - `owner + 0x1F68`
    - `owner + 0x1D98`
    - `owner + 0x1F60`

In particular this path:

- can fall through to `owner + 0x1F60 -> virtual +0x210`
- or switch into `owner + 0x1F70 -> virtual +0x30`

So `+0x1F70` is not just another mounted helper. It already looks like a
runtime state/transition selector that participates directly in melee/control
flow.

### Stronger Current Reading of the Mounted `+0x1Fxx` Region

At this point the mounted region is best read as:

- `+0x1F60`
  - explicit melee command queue / command emission owner
- `+0x1F68`
  - mounted melee state object participating in camera/presentation publish and
    gating
- `+0x1F70`
  - mounted sibling state/transition object participating in control-flow
    selection
- `+0x1FF8`
  - earlier mounted gameplay sibling in the same outer graph

This is now much closer to a real player-combat state machine than to a bag of
miscellaneous gameplay helpers.

### The Same Mounted Graph Also Spans Traversal and Drag-Rope Player Control

The next structural step is that the outer mounted graph around
`+0x1D80..+0x1FF8` is not melee-only.

The same builder around `0x1410CB2BA..0x1410CB8F0` also creates:

- `owner + 0x1DF8`
  - allocated size `0x560`
  - constructed by `0x140DE5CC0`
  - installs vtable/table anchor `0x142A1F690`
- `owner + 0x1E08`
  - allocated size `0x220`
  - installs vtable/table anchor `0x142A19ED8`
- `owner + 0x1E50`
  - allocated size `0x550`
  - installs vtable/table anchor `0x142A227A0`
- `owner + 0x1E58`
  - allocated size `0x710`
  - constructed by `0x140E14490`
- `owner + 0x1E20`
  - allocated size `0x2C0`
  - constructed by `0x140FCCD20`

The `+0x1E08` mounted object is explicitly anchored to
`player-drag-edge.cpp` through the strings around `0x142A19ED8`:

- `drag-drop-left`
- `drag-drop-bw`
- `climb`
- `drag-drop-right`
- `drag-climb-left`
- `drag-climb-bw`
- `drag-drop-fw`
- `drag-climb-right`
- `fall`
- `kEdgeRightArmIKFadeTarget`
- `kEdgeIsClimbing`
- `climb-up-rh`
- `player-climb-up`
- `kEdgeLeftArmIKFadeTarget`
- `kEdgeArmIKFadeTarget`
- `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-drag-edge.cpp`

The `+0x1E50` mounted object is explicitly anchored to
`player-drag-rope.cpp` through the strings around `0x142A227A0`:

- `PlayerDragRopeAction::sWorkData::UpdateApRef`
- `rope-drag-impact-haptic-effect`
- `drag^free-rope-2`
- `no-aim`
- `rope-climb`
- `drag-rope-idle`
- `drag-rope-enter`
- `drag-rope-loop-haptic-effect`
- `drag-rumble-settings`
- `enter-drag-rope`
- `rope-climb-up`
- `move-to-drag-rope`
- `free-rope-to-drag-rope`
- `wall-rope-jump`
- `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-drag-rope.cpp`

The `+0x1DF8` mounted object now looks like a sibling
`player-fall/free-rope` state family, not a generic helper. Its constructor
`0x140DE5CC0` installs `0x142A1F690` and seeds hashed/state data from strings
near:

- `player-fall.cpp`
- `DiveFall`
- `fall-land-roll-hard-run`
- `run^fall-high`
- `fall-land-roll-run`
- `player-fall-death-safe-landing`
- `desired-fall-state`
- `grapple-grip-layer`
- `jump-land`
- `fall-high`
- `free-rope-grip`
- `player-fall-death`
- `time-in-air`

The `+0x1E58` mounted object looks like a broad edge/rope/staff state-set
owner, not a single verb helper. Its constructor `0x140E14490` installs
`0x142A216C8` and the associated table contains authored state strings such as:

- `*edge-right-right*`
- `*edge-right-left*`
- `*edge-right-forward*`
- `*edge-right-back*`
- `*edge-left-left*`
- `*edge-left-aim*`
- `*edge-left-back*`
- `*edge-left-right*`
- `*short-right-back*`
- `*short-right-right*`
- `*short-left-left*`
- `*short-right-forward*`
- `*short-left-back*`
- `*edge-right-aim*`
- `*short-left-forward*`
- `*corner-left-back*`
- `*corner-left-right*`
- `*wall-rope*`
- `*corner-left-forward*`
- `*staff-plant*`
- `*free-rope*`

The `+0x1E20` mounted object now looks traversal/motion-facing as well, not
combat-only. Its constructor `0x140FCCD20` installs `0x142A3C490`, seeds a
mounted `0x130` child, and the surrounding data is anchored to:

- `water-current-settings`
- `water-current`
- `slide-barrier`
- `player-slide-barrier-settings`
- `slide-attractor`
- `*default-water-forces*`
- telemetry strings such as `speedInWaterDir`, `water force`, `netAccel`,
  `Desired steer angle`, `water speed`

So the best current reading of the outer mounted graph is no longer
`player-melee-control only`.

It now looks like a broader `player-control` graph which already bundles:

- melee command emission and melee state selection
- `ProcessNpcMeleeManager`
- drag-edge / climb-edge traversal
- drag-rope / wall-rope / free-rope traversal
- fall / dive-fall / free-rope grip state
- slide / water-current / traversal-force state

That is a major shift for the co-op goal, because it means the same actor-local
mounted graph is already spanning both combat and traversal verbs that a real
playable character needs.

### The Outer Mounted Owner Is Broader Than Combat+Traversal and Already Pulls In Named Player Subsystems

The same outer build region around `0x1410CAE00..0x1410CBD7B` now looks like a
real player owner assembly pass, not just a local melee/traversal helper.

Before it reaches the already-mapped `+0x1DF8/+0x1E08/+0x1E20/+0x1E50/+0x1E58`
block, it also mounts several named player-facing subobjects:

- `owner + 0x1D60`
  - allocated size `0x1DF0`
  - installs root table/vtable `0x142A268C0`
  - initializes a large embedded block at `+0x120` via `0x140E4ECA0`
- `owner + 0x1D68`
  - allocated size `0x2D0`
  - installs `0x142A288D8`
- `owner + 0x1DC0`
  - allocated size `0x4560`
  - constructed by `0x141F9D610`
- `owner + 0x1DC8`
  - allocated size `0x2B0`
  - installs `0x1428ED7B0`
- `owner + 0x1DD0`
  - allocated size `0x540`
  - constructed by `0x14024A090`
- `owner + 0x1E28`
  - allocated size `0x1E0`
  - installs `0x142A0E148`
- `owner + 0x1E30`
  - allocated size `0x120`
  - installs `0x142A2D680`
- `owner + 0x1E38`
  - allocated size `0x460`
  - installs `0x142A2D550`
- `owner + 0x1E48`
  - allocated size `0x2B0`
  - installs `0x142A32C08`

The named string anchors for these neighbors are already clearly player-facing:

- `0x142A2D550`
  - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-look-at-ik.cpp`
- `0x142A2D680`
  - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-menu.cpp`
  - `PlayerMenuObserver::PlayerMenuObserver`
  - `WeaponMenuObserver::WeaponMenuObserver`
- `0x142A32C08`
  - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-perch.cpp`
  - `climb^perch`
  - `perch-point`
  - `waist-grab-2`
- `0x142A0E148`
  - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-bump-bend-align.cpp`
  - `sam-bump-react-*`
  - `suppress-evade-controller`

The root `+0x1D60` owner is especially important:

- its root table `0x142A268C0` lives in the same `rdata` neighborhood as
  `player-melee-control.cpp`
- its embedded initializer `0x140E4ECA0` seeds a large mounted block from
  `0x142A26CB0`
- that surrounding region is clearly in the same `player-melee-control` family
  as the later mounted combat/traversal objects

So the best current reading is no longer merely:

- `there is a melee/traversal cluster around +0x1Fxx`

but rather:

- the outer actor owner already assembles a broad player subsystem graph
  including:
  - melee control
  - look-at IK
  - menu/weapon menu observers
  - perch control
  - bump/bend-align control
  - drag-edge / drag-rope / fall / slide / water-current state

That is much closer to a real Chloe-class player owner than anything found in
the earlier NPC/control-routing work.

### `0x1410CA720` Now Reads Like a Real Player Owner Assembly Pass

The broader assembly pass now has a clean prologue at `0x1410CA720`, and it is
no longer reasonable to treat it as just "the builder that happened to create
our melee cluster".

It is a large outer owner assembly function that mounts, attaches, and wires a
long run of player-facing subobjects in one pass. So far the pass covers at
least:

- `+0x1D48`
  - size `0x690`
  - built by `0x140F187B0`
- `+0x1D50`
  - built by `0x14100D5A0`
- `+0x1D58`
  - size `0x130`
  - table `0x142A236B0`
  - anchored by `player-anim-settings`
- `+0x1D60`
  - root `0x1DF0` owner
  - table `0x142A268C0`
  - embedded core initialized by `0x140E4ECA0`
- `+0x1D68`
  - size `0x2D0`
  - table `0x142A288D8`
- `+0x1D70`
  - size `0x140`
  - table `0x142A351A8`
- `+0x1D88`
  - built by `0x1410B4B10`
- `+0x1D90`
  - size `0x930`
  - built by `0x140CAE670`
- `+0x1D98`
  - size `0x3E0`
  - table `0x142A23568`
  - this is the same presentation/observer sink block already seen later in
    aiming/combat publish paths
- `+0x1DA0`
  - size `0x52C0`
  - built by `0x141301710`
- `+0x1DA8`
  - built by `0x140E91A00`
- `+0x1DB0`
  - size `0x1A0`
  - table `0x142A28BC8`
  - hard anchors to player investigate/inspect strings:
    - `read-mode-enter`
    - `investigate-*`
    - `hero-inspect-additive`
    - `ProcessInvestigateItem`
    - `PlayerInvestigateHandIkAction::UpdateProcedural`
- `+0x1DB8`
  - size `0x15C0`
  - built by `0x140D64BF0`
- `+0x1DC0`
  - size `0x4560`
  - built by `0x141F9D610`
- `+0x1DC8`
  - size `0x2B0`
  - table `0x1428ED7B0`
- `+0x1DD0`
  - size `0x540`
  - built by `0x14024A090`
  - anchored to `player-look-at-ik.cpp`
- `+0x1DE0`
  - size `0x6090`
  - built by `0x1411A65C0`
- `+0x1E28`
  - size `0x1E0`
  - table `0x142A0E148`
  - anchored to `player-bump-bend-align.cpp`
- `+0x1E30`
  - size `0x120`
  - table `0x142A2D680`
  - anchored to `player-menu.cpp`
- `+0x1E38`
  - size `0x460`
  - table `0x142A2D550`
  - anchored to `player-look-at-ik.cpp`
- `+0x1E48`
  - size `0x2B0`
  - table `0x142A32C08`
  - anchored to `player-perch.cpp`
- `+0x1E50`
  - drag-rope
- `+0x1E58`
  - edge/rope/staff state-set
- `+0x1E60`
  - size `0x220`
  - table `0x142A237D0`
  - hard anchors to:
    - `player-flame.cpp`
    - `PlayerFlameController::KickCollisionProbes`
    - `PlayerFlashlightController::*`
- `+0x1E68`
  - size `0x340`
  - built by `0x14105E220`
- `+0x1F60/+0x1F68/+0x1F70/+0x1FF8`
  - mounted melee/traversal/control graph already mapped above

So the current strongest conclusion is:

- `0x1410CA720` is assembling a broad player owner / player subsystem graph
  for a live actor,
- not a narrow melee-only helper,
- and this is currently the best structural candidate for the Chloe-class
  ownership layer that Nadine eventually needs to join.

### The `+0x1D60` Root Owner Now Has a Direct In-Place Constructor and a Higher-Level Caller

The `+0x1D60` root owner family is now stronger than a mere vtable guess.

There is a direct in-place constructor:

- `0x140E5DDF0`
  - installs `0x142A268C0`
  - immediately calls `0x140E4ECA0` on the embedded `+0x120` core
  - then seeds the same `+0x1AE0..+0x1DE0` block that later appears in the
    large outer assembly pass

So the `+0x1D60` object can now be treated as a formal class family with:

- a standalone ctor/in-place init path
- a large embedded `player-melee-control`-region core
- subsequent mounting into the broader outer owner

More importantly, the large assembly pass `0x1410CA720` now has a confirmed
higher-level caller:

- `0x1410DD0FC -> call 0x1410CA720`

The surrounding `0x1410DCE80..0x1410DD27C` function is already operating on a
much larger outer runtime object `rsi`, and before the call it seeds:

- `+0x3BD8`
- `+0x2018`
- `+0x4308`
- `+0x3C20`
- many high offsets like `+0x32F8`, `+0x3318`, `+0x3330`, `+0x3360`, `+0x33C8`

Then it invokes `0x1410CA720`, and immediately after that:

- stores another object at `outer + 0x1DF0`
- calls through its vtable
- performs additional runtime initialization on the same large owner

That means:

- `0x1410CA720` is not a leaf helper
- it is one stage inside a higher-level player-runtime assembly chain
- and `0x1410DD0FC` is currently the best upward lead toward the real
  Chloe-class owner/factory path

This is the first direct upward caller above the broad player-owner assembly
graph, so it becomes the current strongest promotion/rebind candidate for
future Nadine work.

### The Higher-Level Caller Is Now Anchored Directly to `player.cpp`

The caller region above `0x1410CA720` is no longer just "some larger runtime
owner".

The debug/descriptor anchors used immediately after the call now point straight
at:

- `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player.cpp`
- `Player::Player(void)`
- `Player::Init(const ProcessSpawnInfo &)`

Those strings sit in the same anchor block used by the post-assembly caller
around `0x1410DD4EE..0x1410DD538`, where the function packages state and emits
debug/structured output using:

- `0x142A4A2F0`
- `0x142A4A558`

So the upward chain now reads:

- high-level `player.cpp`-anchored init path
- caller at `0x1410DD0FC`
- broad player-owner assembly pass at `0x1410CA720`
- root player-control owner at `+0x1D60`
- mounted melee / traversal / inspect / flame / look-at / perch / menu graph

This is the strongest result so far for the actual target architecture:

- we are no longer merely near a player-like subsystem,
- we are inside a real `Player` construction / initialization chain.

For the co-op objective that matters a lot:

- promotion/rebind for Nadine is now most plausibly a `Player`-class
  construction / init problem,
- not just an NPC-control, camera, or isolated action-stack problem.

That sharply narrows the search for a future runtime patch:

- the next upward work should stay on the `player.cpp`-anchored caller chain,
- while the next downward work should test which mounted player subsystems are
  actually hard-gated to Chloe/player identity.

That same `player.cpp`-anchored initialization neighborhood also already
contains explicit net/co-op flavored player state, not only generic
single-player strings. Confirmed nearby anchors include:

- `hero-default`
- `player-1`
- `local-player-1`
- `*net-player-sprint-settings*`
- `*player-sprint-settings*`
- `*net-player-swim-sprint-settings*`
- `*camera-melee-settings-net*`
- `*net-player-anim-art-groups*`
- `*net-player-weapon-mods-coop*`
- `net-player-coop-id`

Implication:

- the caller chain around `0x1410DD0FC -> 0x1410CA720` is not merely "a big
  player-flavored control graph";
- it is already sitting in a real `Player` init layer that knows about
  player-slot identity and net/co-op flavored player settings;
- this further strengthens it as the best current promotion/rebind candidate
  for lifting Nadine into a Chloe-class gameplay owner path.

Two more concrete structural details now reinforce that reading:

- before the call into `0x1410CA720`, the same larger init block allocates an
  owner-local object at `outer + 0x4308` using the anchored token
  `*player-ik-arm*`
- and the tail of `0x1410CA720` confirms that the mounted graph does not stop
  at melee / rope / edge states:
  - after `+0x1E58`, it still mounts `+0x1E20`
  - then immediately mounts `+0x1E28`

So the active structure is not just "combat + traversal helpers near Player".
It is a broad Player subsystem graph that already includes player-IK and other
owner-local mounted state in the same init chain.

The tail of `0x1410CA720` also confirms that this assembly pass keeps mounting
ordinary Player-side subsystems after the combat/traversal-heavy region:

- `+0x1E30`
  - direct vtable anchor `0x142A2D680`
  - matches the previously observed `player-menu.cpp` family
- `+0x1E38`
  - direct vtable anchor `0x142A2D550`
  - matches the previously observed `player-look-at-ik.cpp` family
- `+0x1E48`
  - direct vtable anchor `0x142A32C08`
  - matches the previously observed `player-perch.cpp` family

And the same tail continues further into still-unresolved mounted owner slots:

- `+0x1E98`
- `+0x1EB0`
- `+0x1EB8`
- `+0x1EE0`

Implication:

- `0x1410CA720` is not a narrow combat/traversal builder;
- it is a broad Player subsystem assembly pass that keeps wiring menu,
  look-at-IK, perch, and additional mounted owner state into one outer Player
  runtime object.

The first pass over those later mounted owners already gives two useful
provisional directions:

- `+0x1E98`
  - constructed by a very large in-place builder at `0x1410484F0`
  - size on the mounting path is `0x1B50`
  - layout includes many embedded subrecords and repeated initialized policy
    blocks, so this is a major mounted Player owner rather than a tiny helper
  - nearby player-side strings in the same broader address neighborhood are
    dense with `player-investigate` / `read-mode-enter` / `investigate-*`
    anchors
  - current cautious reading: this is a strong candidate for a mounted
    `player-investigate` or broader interaction/presentation owner
  - this owner is also definitely live at runtime:
    - `0x14015E271` calls its virtual `+0x430`
    - `0x14016060C` calls its virtual `+0x438`
    - `0x140160C8D` calls its virtual `+0x220`
    - `0x1402052E8` calls its virtual `+0x208`
    - `0x1401EFE96` also calls its virtual `+0x208`
  - those calls sit inside broader per-player loops over retained player slots
    `0/1`, not in one-off construction-only code
  - at least one of those `+0x208` gates is already tied to the player-labelled
    descriptor block `0x1428D8960` (`Player`)
  - the same broader string neighborhood is now pinned much harder:
    - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-investigate.cpp`
    - `read-mode-enter`
    - `investigate-flip`
    - `investigate-exit`
    - `investigate-zoom`
    - `--player-investigate--`
    - `player-investigate-set-extra-sounds`
    - `player-investigate-set-camera-indices`
    - `player-investigate-set-hud`
    - `player-investigate-make-readable`
    - `player-investigate-force-zoom`
  - current stronger reading:
    this is no longer just "interaction/presentation-like", it is now the best
    mounted `player-investigate / readable-object / read-mode` owner candidate
    in the Player graph
  - implication: `+0x1E98` is a real mounted Player subsystem participating in
    recurring frame/update passes and player-tagged gameplay gating, not a
    dormant state blob

- `+0x1EB0`
  - constructed by `0x140DCB1A0`
  - size on the mounting path is `0x4B0`
  - vtable anchor is `0x142A1DBB8`
  - this is now directly confirmed in the real Player assembly pass:
    - `0x1410CB9CF` calls `0x140DCB1A0`
    - and immediately stores the result into `outer_owner + 0x1EB0`
  - immediate runtime consumers later dereference `owner + 0x1EB0` as a live
    subobject and call its virtual `+0x38`
  - string ids pushed into the same local path include:
    - `use-surface-type-probe`
    - `monkey-base`
    - `frags-event-dodging-rolling`
  - nearby authored strings in that same mounted-owner region include:
    - `cover-low-l^evade-r`
    - `cover-r^evade-right-run`
    - `cover-low-r^evade-r`
  - the global string neighborhood is now much stronger too:
    - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-cover.cpp`
    - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-cover-actions.cpp`
    - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-cover-states.cpp`
    - `d:\\perforce\\code\\big4final-tll-release\\big4\\src\\game\\player\\player-evade.cpp`
    - `*player-cover-settings*`
    - `*player-roll-settings*`
    - `evade-roll`
    - `player-roll`
    - `cover-evade`
  - current stronger reading:
    this is a mounted Player-side `cover / evade / dodge-roll` owner rather
    than an unnamed generic helper

- `+0x1EE0`
  - is also dereferenced immediately by downstream runtime code from the same
    mounted owner family
  - and the broader Player assembly pass now confirms it is mounted by a
    dedicated constructor path:
    - `0x1410CBD3D` calls `0x140CEDCA0`
    - and stores the result into `outer_owner + 0x1EE0`
  - e.g. `0x140DCB3B4` and `0x140DCB871` call deep virtuals on
    `owner + 0x1EE0`
  - both activation/deactivation-style paths over the same family now show a
    stable pair:
    - `0x140DCB3B4`
      - first calls `owner + 0x1EE0 -> virtual +0x1C8`
      - then calls `owner + 0x1EB0 -> virtual +0x38`
      - then continues through `owner + 0x1D98`, `owner + 0x1DC0`, and later
        Player-side state publication
    - `0x140DCB871`
      - first calls `owner + 0x1EE0 -> virtual +0x1D0`
      - then tail-calls `owner + 0x1EB0 -> virtual +0x40`
  - current stronger reading:
    `+0x1EE0` is a live activation/synchronization sibling for the same
    Player cover/evade family, not a passive cache slot

Implication:

- the `player.cpp`-anchored Player init chain now clearly extends beyond
  combat/traversal and into interaction/presentation-flavored mounted owners;
- that makes it an even stronger long-term promotion target for Nadine than a
  combat-only or locomotion-only rebinding path.

The exact mount order in the same `Player::Init` assembly pass is now also
confirmed, not inferred:

- `0x1410CB9CF` mounts `+0x1EB0` via `0x140DCB1A0`
- `0x1410CBA12` mounts `+0x1E98` via `0x1410484F0`
- `0x1410CBC02` mounts `+0x1EB8` via `0x140F49300`
- `0x1410CBD40` mounts `+0x1EE0` via `0x140CEDCA0`

Implication:

- the investigate/read-mode owner (`+0x1E98`) and the cover/evade owner
  (`+0x1EB0`) are peers in the same real Player subsystem assembly pass;
- `+0x1EE0` is a sibling mounted directly beside them, not a side-cache built
  elsewhere;
- so the Player graph is already grouping interaction/read-mode and
  cover/evade families under the same outer gameplay owner that we eventually
  need for Nadine.

`+0x1EB8` is now at least structurally pinned as its own mounted Player
subsystem, even though the exact gameplay name is still unresolved.

- it is mounted directly in the same real Player assembly pass:
  - `0x1410CBC02` calls `0x140F49300`
  - then stores the result into `outer_owner + 0x1EB8`
- `0x140F49300` is not a trivial blob ctor:
  - installs vtable `0x142A34158`
  - seeds a long run of owner fields out to roughly `+0x498`
  - creates an owned nested helper object at `+0x450`
    - nested helper vtable anchor `0x142AFC890`
    - later methods call its virtuals `+0x8` and `+0x10`
- the same family has follow-up methods beginning right after the constructor:
  - `0x140F49730` tears down / resets the nested helper at `+0x450`
  - `0x140F497A0` does a relocate-style pass over `+0x108/+0x110/+0x450`
  - `0x140F49850` and siblings are compact boolean gates over bytes
    `+0x120/+0x121/+0x122` and time/delta field `+0x448`
- current disciplined reading:
  - `+0x1EB8` is a mounted Player subsystem with its own nested timed/gated
    helper object
  - but there is still not enough direct evidence to call it inspect,
    flashlight, perch, or another concrete verb owner
  - so it should stay slot-level for now while `+0x1E98`, `+0x1EB0`, and
    `+0x1EE0` already carry stronger semantic reads

Several neighboring mounted Player slots in the same assembly pass are now also
structurally pinned, even if their final semantic names are still open:

- `+0x1E60`
  - mounted from a `0x220` allocation
  - ctor path is inlined in `0x1410CA950..0x1410CA996`
  - installs vtable `0x142A237D0`
  - creates an owned helper block at `+0x120` via `0x141EC5DF0`
  - stores another cross-link/pointer at `+0x210`
  - current read: mounted Player subsystem with a small owned helper, still
    unresolved semantically

- `+0x1E68`
  - mounted from `0x14105E220` with size `0x340`
  - direct store is `0x1410CA9D4 -> outer_owner + 0x1E68`
  - current read: separate mounted Player subsystem peer in the same
    investigate/interaction-side region

- `+0x1DA0`
  - mounted from `0x141301710` with size `0x52C0`
  - direct store is `0x1410CAC6C -> outer_owner + 0x1DA0`
  - this is one of the largest mounted Player-side owners in the graph and is
    almost certainly not a helper
  - current stronger read:
    major Player `HUD / read-mode / investigate-presentation` owner candidate
  - reasons:
    - the surrounding Player-side string space is dense with:
      - `*player-hud-scripts*`
      - `general/net/player-hud:*`
      - `hud-show-read-mode`
      - `hud-hide-read-mode`
      - `hud-show-investigate-hud`
      - `hud-hide-investigate-hud`
      - `enable-inspect-reticle`
      - `disable-inspect-reticle`
      - `read-mode-enter`
      - `read-mode-exit`
    - the constructor itself is huge, seeds many large structured blocks, and
      immediately allocates several owned subrecords/helpers
  - still not pinned enough for a final semantic rename, but the direction is
    now much narrower than "generic presentation owner"

- `+0x1DB8`
  - mounted from `0x140D64BF0` with size `0x15C0`
  - direct store is `0x1410CAD8D -> outer_owner + 0x1DB8`
  - current read: substantial mounted Player-side action/qualification owner
    adjacent to the same broad interaction/combat/presentation region
  - reasons:
    - ctor seeds a long run of repeated state blocks and several owned helper
      tables via `0x141EC5DF0`
    - downstream consumer `0x140D650A7` already calls
      `owner + 0x1DB8 -> virtual +0xB0` as part of a larger actor/world gate
      that also consults `owner + 0x1D88` and owner virtual `+0x8C0`
  - implication:
    this looks closer to a real gameplay qualification/execution-side owner
    than to a passive data blob, but the exact verb family is still open

Implication:

- the promotion target is not one narrow owner plus a few helpers;
- `Player::Init` is mounting a dense band of medium/large runtime owners around
  `+0x1DA0..+0x1EB8`, with investigate/read-mode, cover/evade, menu/look-at/
  perch, and still-unresolved peers all living side-by-side;
- that makes the outer Player owner itself, not a single subobject, the more
  credible long-term promotion/rebind target for Nadine.

### `+0x1DB8` Now Sits Inside a Concrete Player-Side Qualification / Execution Gate Cluster

The earlier claim that `+0x1DB8` looked like a "substantial Player-side
action/qualification owner" is now much stronger. The live code at
`0x140D65030..0x140D6536C` shows two compact gate functions that repeatedly
consult the same mounted owner band:

- `owner + 0x1D88`
- `owner + 0x1D98`
- `owner + 0x1DB8`
- `owner + 0x1E50`
- owner virtual `+0x8C0`

The first gate (`0x140D65030`) currently reads best as:

- early global/runtime singleton suppression via `0x143ECEA78/+0xBC`;
- `owner + 0x1D88 -> virtual +0x250` must succeed first;
- then `owner + 0x1DB8 -> virtual +0xB0` acts as a boolean-like
  qualification gate;
- if that gate is true, the code also requires
  `owner + 0x1D88 -> virtual +0xB0(0)` to return a non-null object;
- then it queries owner virtual `+0x8C0` and compares the resulting
  `object + 0x860` against two hashed/action-id families;
- if neither hash family matches, it falls through into `0x1410F75C0`;
- and only if that final fallback also fails does the gate return success.

That makes this path much more than a helper: it is a real Player-side
qualification/selection gate that uses mounted owners plus owner-global state
to decide whether some action/state path should be permitted.

The second gate (`0x140D65290`) is simpler but reinforces the same picture:

- it requires `owner + 0x1D88 -> virtual +0x118 == false`;
- requires `owner + 0x1E50 -> virtual +0x70(0) == false`;
- again checks `owner + 0x1DB8 -> virtual +0xB0`;
- if that gate is true, again requires a non-null
  `owner + 0x1D88 -> virtual +0xB0(0)`;
- then requires all of:
  - `owner + 0x1D98 -> virtual +0xF0 == true`
  - `owner + 0x1D88 -> virtual +0x2C0 == false`
  - `owner + 0x1D88 -> virtual +0x2A0 == false`
  - `owner + 0x1D88 -> virtual +0x1D0 == true`

This is already enough to say the `+0x1DB8` family is not passive. It is part
of a formal Player-side execution/qualification cluster that also talks to:

- a presentation/observer sink family (`+0x1D98`);
- another mounted Player-side owner (`+0x1E50`);
- and a deeper owner-global selector via `+0x8C0`.

Current disciplined read:

- `+0x1DB8` is a mounted Player-side qualification / execution owner;
- `+0x1D88` looks like a sibling state/control owner with several readiness/
  blocker virtuals (`+0x118`, `+0x1D0`, `+0x250`, `+0x2A0`, `+0x2C0`);
- `+0x1D98` continues to behave like a broader presentation/observer sink;
- and this whole band now looks much closer to real Player verb gating than to
  generic companion infrastructure.

### The `+0x1DB8/+0x1D88` Gates Sit Inside a Broader Player-Side Spatial / Cooldown Check Family

The same local function band immediately below the qualification gates shows
that they are part of a larger Player-side execution family rather than
standalone helpers.

`0x140D654F0` is a compact aggregate checker:

- it calls `0x140D65940`;
- then `0x140D65540`;
- then `0x140D65740`;
- then `0x140D65B20`;
- and only returns success if all four checks return false.

The first three helpers are strongly convergent:

- they all resolve a transform/quaternion-like block through
  `owner->0x1A0 -> 0x14172B7F0 -> +0x4F8 -> +0x10 -> 0x1401781E0`;
- they all combine that with actor state at:
  - `actor + 0x3A30`
  - `actor + 0x3A1C`
- and they all compare the resulting scalar against different hard-coded
  thresholds.

Current disciplined read:

- `0x140D65940`, `0x140D65540`, and `0x140D65740` are sibling Player-side
  spatial/state threshold tests;
- `0x140D654F0` is the aggregate "all threshold checks must remain clear"
  gate above them.

`0x140D65B20` is structurally different but still belongs to the same family:

- it hashes a known action family token set and resolves a scalar record via
  `0x1417651D0`;
- multiplies that scalar by a constant;
- compares it against the delta between:
  - `owner->0xE0->0x10`
  - and `owner-local cached value at +0x8`;
- and returns true when that derived cooldown/window test is exceeded.

That cooldown/window path is now paired with a prep helper right above the
qualification gates:

- `0x140D64FB0` stores an owner pointer into a tiny local helper object;
- resolves current actor-root state through `owner->0x1A0 -> 0x14172B7F0`;
- pushes that through `0x140D66220`;
- and, when the probe fails or actor scalar `+0x3A1C` is still below a
  threshold, refreshes the cached baseline at helper `+0x8` from
  `owner->0xE0->0x10`.

So the paired reading is now:

- `0x140D64FB0` seeds or refreshes a baseline timestamp/anchor for the local
  Player action helper;
- `0x140D65B20` checks whether enough scaled time/window has elapsed since that
  baseline.

So the whole band now reads like:

- mounted qualification / blocker owners (`+0x1D88/+0x1DB8/+0x1E50/+0x1D98`)
- plus spatial threshold gates
- plus a hashed cooldown/window gate

all participating in one Player-side execution policy family.

### The Same Qualification Family Is Reused Well Outside One Local Method

The cluster is also clearly reused outside the immediate local caller around
`0x140D186E5`.

A nonlocal Player-side caller around `0x140F60963` now confirms that:

- it indexes an owner-local table by `actor + 0x4010`;
- initializes an auxiliary record through `0x140D64FB0`;
- then runs both:
  - `0x140D65030`
  - `0x140D65290`
- and, if both stay clear, additionally queries
  `owner + 0x1DB8 -> virtual +0x3B8`;
- later in the same path it runs `0x140D65030` again and then
  `owner + 0x1DB8 -> virtual +0x50`;
- and finally routes the result through `owner + 0x1FB0`.

That same nonlocal caller also shows the mounted Player graph converging in one
place:

- if the early `+0x1DB8` gates clear, it uses deeper `+0x1DB8` virtuals
  (`+0x3B8`, later `+0x50`);
- otherwise it branches through:
  - `+0x1E98 -> virtual +0x130`
  - `+0x1E50 -> virtual +0x70`
  - `+0x1EB0 -> virtual +0x50`
  - and finally `+0x1FB0` dispatch
- with owner virtuals `+0xC78 / +0xC98 / +0xCB8` acting as outer commits.

This is a stronger convergence point than the earlier isolated melee-only
tracks: one outer Player-side caller is already orchestrating investigate-like,
cover/evade-like, `+0x1DB8` qualification, and `+0x1FB0` dispatch under a
single owner.

### `+0x1D88` and `+0x1DE0` Are Now Structurally Pinned Inside the Same Player Assembly Pass

The central sibling owners in the qualification cluster are no longer just
offsets.

`+0x1D88`:

- is mounted directly in the real Player assembly pass at
  `0x1410CA921..0x1410CA93A`;
- comes from `0x1410B4B10`;
- allocates size `0x4B0`;
- installs vtable `0x142A47F90`;
- seeds two internal helper blocks via `0x1401B7600` at:
  - `+0x2C0`
  - `+0x380`
- and carries a long run of repeated `(ptr, int)` entry storage from
  roughly `+0x140` through `+0x2B0`, plus later state around `+0x440..+0x4A0`.

Current disciplined read:

- `+0x1D88` is a mounted Player-side state/control owner with internal
  repeated-entry tables and helper blocks;
- that structure matches its heavy use as a multi-virtual readiness/blocker
  object in the `+0x1DB8` qualification family.

`+0x1DE0`:

- is mounted later in the same Player assembly pass at
  `0x1410CADD7..0x1410CADFB`;
- comes from `0x1411A65C0`;
- allocates size `0x6090`;
- is not a tiny helper: its ctor seeds multiple large internal tables and
  subregions, including:
  - `+0x1AD0`
  - `+0x3460`
  - `+0x4140`
  - `+0x5DC0`
- and the ctor itself calls `0x1410F75C0`, the same known mounted-player
  action-family hash checker already used by the `+0x1DB8` gate layer.

That is valuable because the nonlocal caller around `0x140F60B6F` already uses
`owner + 0x1DE0 -> virtual +0x28` in the same outer Player-side branch that
also touches:

- `+0x1E98`
- `+0x1E50`
- `+0x1EB0`
- `+0x1DB8`
- `+0x1FB0`

So `+0x1DE0` is now firmly part of the same real Player action-owner band, not
an unrelated side structure.

### The Large Outer Caller Around `0x140F60680` Already Looks Like a Real Player Verb Orchestrator

The broader function band around `0x140F60680..0x140F61459` is now the best
single convergence point above the mounted subsystems.

What it already does:

- talks to `owner + 0x1FB0` heavily through virtuals:
  - `+0x38`
  - `+0x40`
  - `+0xB0`
  - `+0xC0`
  - `+0x108`
  - `+0x110`
  - `+0x118`
  - `+0x120`
  - `+0x130`
- uses outer owner commit-style virtuals:
  - `+0xBD0`
  - `+0xC78`
  - `+0xC98`
  - `+0xCB8`
- and branches through multiple mounted siblings under the same outer owner:
  - `+0x1DE0 -> virtual +0x28`
  - `+0x1E98 -> virtual +0x130`
  - `+0x1E50 -> virtual +0x70`
  - `+0x1EB0 -> virtual +0x50`
  - `+0x1DB8 -> virtual +0x3B8`, later `+0x50`, and another path `+0x110`

It also still depends on the earlier policy family:

- local helper seed `0x140D64FB0`
- qualification gates `0x140D65030 / 0x140D65290`
- readiness/cooldown gates `0x140D654F0 / 0x140D65B20`

Current disciplined read:

- this is no longer "some caller that happens to touch many subsystems";
- it already looks like a real outer Player verb orchestrator;
- the mounted owners under `+0x1D88..+0x1FB0` behave here as verb-specific
  policy, state, and dispatch participants under one Player-class owner;
- and the owner virtuals `+0xBD0/+0xC78/+0xC98/+0xCB8` now look like strong
  promotion/rebind anchors, because they sit above the mounted subsystem
  choices rather than inside one particular verb owner.

That matters because it shows:

- this is not a one-off check for one melee codepath;
- the `+0x1DB8/+0x1D88` qualification family is a reused Player-side action
  gate;
- and `+0x1DB8` exposes deeper verb-specific virtuals (`+0x3B8`, `+0x50`)
  once the generic gating layer clears.

### Additional `Player::Init` Mounts Around `+0x1D48..+0x1DF8` Now Broaden the Promotion Map

The same `Player::Init` assembly pass around `0x1410CA720` now yields a larger,
more disciplined mounted-owner map immediately adjacent to the already pinned
`+0x1D88/+0x1DB8/+0x1DE0` cluster.

Confirmed mounts:

- `+0x1D48`
  - alloc size `0x690`
  - ctor `0x140F187B0`
- `+0x1D50`
  - direct builder `0x14100D5A0`
- `+0x1D58`
  - alloc size `0x130`
  - vtable `0x142A236B0`
- `+0x1D60`
  - alloc size `0x1DF0`
  - inline ctor path with vtable `0x142A268C0`
- `+0x1D68`
  - alloc size `0x2D0`
  - inline ctor path with vtable `0x142A288D8`
- `+0x1D70`
  - alloc size `0x140`
  - vtable `0x142A351A8`
- `+0x1D78`
  - alloc size `0x4B0`
  - secondary vtable at `+0x120`:
    - outer vtable `0x142A441A0`
    - inner/helper vtable `0x142A441D8`
- `+0x1D80`
  - alloc size `0x470`
  - ctor `0x141072B20`
- `+0x1D90`
  - alloc size `0x930`
  - ctor `0x140CAE670`
- `+0x1D98`
  - alloc size `0x3E0`
  - inline ctor path with vtable `0x142A23568`
- `+0x1DA0`
  - alloc size `0x52C0`
  - ctor `0x141301710`
- `+0x1DA8`
  - direct builder `0x140E91A00`
- `+0x1DB0`
  - alloc size `0x1A0`
  - inline ctor path with vtable `0x142A28BC8`
- `+0x1DB8`
  - alloc size `0x15C0`
  - ctor `0x140D64BF0`
- `+0x1DC8`
  - alloc size `0x380`
  - inline ctor path with vtable `0x142A0CC60`
- `+0x1DD0`
  - alloc size `0x540`
  - ctor `0x14024A090`
- `+0x1DD8`
  - alloc size `0x380`
  - inline ctor path with vtable `0x142A0CC60`
- `+0x1DE0`
  - alloc size `0x6090`
  - ctor `0x1411A65C0`
- `+0x1DE8`
  - direct builder `0x140FF5560`
- `+0x1DF8`
  - alloc size `0x560`
  - ctor `0x140DE5CC0`

This matters because the mounted Player graph is now broader and denser than
the earlier "melee / cover / investigate" sketch. The promotion target for
Nadine is increasingly not a single helper, but a structured Player owner band
covering `+0x1D48..+0x1FF8`.

### `+0x1D78` Already Looks Like a Nested Mounted-Policy Helper With Its Own Inner Interface

The mount at `+0x1D78` is not just another flat subobject.

Its construction path:

- allocates `0x4B0`,
- seeds outer vtable `0x142A441A0`,
- initializes an inner interface block at `+0x120`,
- and assigns a second vtable `0x142A441D8` to that inner block.

Immediately after mount, `Player::Init` feeds the current actor/runtime pair
into `(+0x1D78 + 0x120) -> virtual +0x20`.

Disciplined read:

- `+0x1D78` is likely a mounted policy/helper owner with its own nested runtime
  interface;
- it already participates in actor-qualified setup during `Player::Init`;
- and it belongs in the same promotion map as `+0x1DB8/+0x1DE0`, not in a
  throwaway helper bucket.

### `+0x1DA0`, `+0x1D98`, and `+0x1DB0` Strengthen the Player Presentation / Policy Layer

Three adjacent mounts in the same pass are now structurally stronger:

- `+0x1DA0`
  - alloc size `0x52C0`
  - ctor `0x141301710`
  - this reinforces the earlier read that the `player-investigate /
    readable / read-mode` family is not a side feature but a heavyweight Player
    subsystem.
- `+0x1D98`
  - alloc size `0x3E0`
  - vtable `0x142A23568`
  - this sits immediately next to the known presentation/observer sink role.
- `+0x1DB0`
  - alloc size `0x1A0`
  - vtable `0x142A28BC8`
  - compact sibling in the same Player-side action/presentation band.

These mounts matter because the outer Player verb orchestrator is not selecting
among isolated gameplay verbs only. It sits above a larger ownership band that
already contains:

- presentation / read-mode-like owners,
- policy / qualification owners,
- compact helper-dispatch siblings,
- and the heavier `+0x1DE0` action family.

### `+0x1EB8` Is Now Structurally Pinned As Another Real Mounted Player Subsystem

The previously mounted `+0x1EB8` object is now structurally pinned more
cleanly:

- builder `0x140F49300`
- inline vtable `0x142A34158`
- repeated paired state slots running from roughly `+0x130` through `+0x408`
- extra state around `+0x430..+0x498`

Even without final semantics, this is clearly not a tiny cache or transient
blob. It is another formal mounted runtime class inside the same Player graph.

### A Small Callback Family at `0x142A35090` Already Routes Authored Events Into `+0x1FB0`

The code immediately following the outer verb orchestrator band is now useful in
its own right.

`0x142A35090` is a compact callback/listener vtable whose main members include:

- `0x140F614E0`
- `0x140F615B0`
- `0x140F61610`

The key dispatch method `0x140F614E0`:

- reads an authored event id from `record->+0x0`,
- keeps the outer Player owner through `this->+0x8`,
- and routes directly into `owner + 0x1FB0`.

Observed routes:

- one event family jumps to `+0x1FB0 -> virtual +0x128`
- another jumps to `+0x1FB0 -> virtual +0x100`
- three smaller event ids just toggle bytes on the callback object:
  - `this + 0x28 = 1`
  - `this + 0x29 = 1`
  - `this + 0x29 = 0`

The sibling helper `0x140F61490` is also already tied to the same dispatch
object:

- it resolves an owner-side object through `owner->0x2010 -> virtual +0x28`,
- then asks `owner + 0x1FB0 -> virtual +0x78`,
- and if present tail-calls `owner + 0x1FB0 -> virtual +0x48`.

Finally, `0x140F615B0` is the allocating constructor for the callback object:

- alloc size `0x40`
- vtable `0x142A35090`
- stores outer Player owner at `object + 0x10`

Disciplined read:

- this is a real authored-event ingress into the Player verb graph;
- it does not sit above `+0x1FB0`, but it clearly feeds into it;
- and it strengthens the case that `+0x1FB0` is the central outer dispatch hub
  beneath the larger verb orchestrator.

### `Player::Init` Mounts The Central `+0x1FB0` Dispatch Hub Through `0x140F5F920`

The late `Player::Init` band around `0x1410CCEE1..0x1410CD01B` now gives the
direct mount path for the central dispatch object.

Confirmed mounts in this band:

- `+0x1F98 <- 0x140FE92B0`
- `+0x1FA0 <- 0x140E40EB0`
- `+0x1FA8 <- 0x140F632B0`
- `+0x1FB0 <- 0x140F5F920`
- `+0x1FB8 <- 0x141004610`
- `+0x1FC8 <- 0x140CD3640`
- `+0x2010 <- 0x140D80440`
- `+0x1FD8 <- 0x140FDA6E0`
- `+0x1FE0 <- 0x140CEACD0`
- `+0x1FE8 <- 0x140DA6920`
- `+0x1FF0 <- 0x140E3E660`

That is already enough to treat this region as a late mounted
`dispatch/observer/action-support` band under the same outer Player owner.

### `0x140F5F920` Is The Allocating Entry For The Real `+0x1FB0` Dispatch Object

`0x140F5F920` itself is not a random helper. It is the allocating constructor
entry for the object mounted into `outer_owner + 0x1FB0`.

What it does:

- allocates `0x3E0`,
- clears the block,
- then tail-calls the in-place initializer at `0x140F5F980`.

The in-place initializer `0x140F5F980`:

- seeds vtable `0x142A34E10`,
- initializes multiple state bands across:
  - `+0x120/+0x128`
  - `+0x170..+0x210`
  - `+0x288..+0x2F8`
  - `+0x300..+0x3D8`
- stores notable anchors:
  - `+0x2A8 = 0x142AFC890`
  - `+0x2F8 = 0x142B0E8C8`
- and seeds default scalars/flags for the same object later used by:
  - the outer verb orchestrator `0x140F60680`
  - the authored-event callback family at `0x142A35090`

Current disciplined read:

- `+0x1FB0` is no longer just "a dispatch object by behavior";
- it is now structurally pinned as a real mounted runtime class;
- and the surrounding late mounts `+0x1F98..+0x2010` are best treated as its
  adjacent support band inside the outer Player owner graph.

### Outer Owner Commits `+0xBD0 / +0xC78 / +0xC98 / +0xCB8` Are Not Symmetric

The broad call-count on the outer owner virtuals was already known, but a few
concrete callsites now make their local role clearer.

#### `+0xBD0` looks like the generic owner-side refresh/fallback commit

Representative callsites:

- `0x140F606CE`
- `0x140F60777`
- `0x140CCD F78`
- `0x140D6AE47`

Observed pattern:

- it is used after branch failure or after a neutral/default path is chosen;
- it tends to happen before tail cleanup/return rather than after a successful
  specialized verb commit;
- and it appears both in the outer Player verb orchestrator and in smaller
  callback/fallback paths.

Current disciplined read:

- `+0xBD0` is the best current candidate for a generic outer-owner
  `refresh / settle / fallback-commit` hook.

#### `+0xCB8` follows accepted `+0x1FB0` / presentation-qualified branches

Concrete examples:

- in `0x140F60680`, after `+0x1FB0 -> virtual +0x40`
- in `0x140CFF8DB..0x140CFF8EE`, after
  - `+0x1DB8 -> virtual +0x48`
  - `+0x1D98 -> virtual +0x50`

That is stronger evidence that `+0xCB8` is not the generic fallback.
It sits after a branch that already cleared mounted policy/presentation checks.

Current read:

- `+0xCB8` is a strong candidate for an outer-owner
  `accept / finalize specialized verb branch` commit.

#### `+0xC78` follows a successful spatial/action execution branch

Representative path:

- `0x140CFFB3B..0x140CFFB45`

There the flow:

- builds a transform/state candidate through `+0x1DE0`,
- tests a success return from `+0x1DE0 -> virtual +0x68`,
- sets `actor + 0x35 = 1`,
- and only then calls outer owner `+0xC78`.

That is consistent with:

- a success-side execution commit,
- or a "branch accepted and now advance owner execution state" hook.

#### `+0xC98` is reused as a broader owner-side finalize/publish path

Representative paths:

- `0x140501406`
- `0x140D6B1A2`

In these callers `+0xC98` appears when a higher-priority object/state branch is
already present, rather than when a new branch is being freshly accepted.

Current disciplined read:

- `+0xC98` is broader than `+0xCB8`;
- it looks closer to an outer-owner
  `publish / finalize active state / handoff-complete` path.

These reads are still semantic rather than definitive, but they are already
good enough to keep the outer Player owner focus narrow:

- `+0xBD0` = likely generic fallback/refresh commit
- `+0xCB8` = accepted specialized branch commit
- `+0xC78` = successful execution-side branch commit
- `+0xC98` = broader active-state finalize/publish commit

### `0x1410DC160` Is The Larger Top-Level `Player` Construction / Init Pass Above `0x1410DD0FC`

The region around `0x1410DC160` is now clearly above the previously isolated
`0x1410DD0FC -> 0x1410CA720` edge.

What is now confirmed:

- `rcx` is the large outer Player object (`rsi = rcx` for the whole pass).
- the function stores and seeds broad owner state directly across the large
  `Player` layout:
  - `+0x1B50..+0x1B80`
  - `+0x1C10..+0x1CC8`
  - `+0x2040..+0x2068`
  - `+0x3360..+0x3FD8`
  - `+0x4308`
  - `+0x4850`
- inside the same pass it:
  - creates the owner-local object at `+0x3BD0` through `0x1410DE520`
  - calls the mounted graph assembly step `0x1410DD0FC -> 0x1410CA720`
  - allocates `+0x4308`
  - creates `+0x1DF0`
  - and later drives additional finalize/setup work through `+0x3BD0`,
    `+0x1DF0`, `+0x1A8`, `+0x1B10`, and `+0x3D98`

This is the strongest current evidence that:

- `0x1410DD0FC` is only one embedded assembly phase;
- while `0x1410DC160` is the larger top-level Player construction/init pass
  that owns the real promotion surface.

### `0x1410DE520` Builds The Owner-Local `+0x3BD0` Runtime Object Used Before And After Mount Assembly

`0x1410DE520` is now pinned as the in-place initializer for the object stored
into `outer_player + 0x3BD0`.

Confirmed from the `0x1410DC160` pass:

- allocation size before the call is `0x190`
- the result is stored at `outer_player + 0x3BD0`
- later in the same top-level pass the object is reused through:
  - `0x1410DCCBB`
  - `0x1410DCCC7`

The initializer itself:

- seeds vtable `0x142A4BFD8`
- builds two repeated state bands:
  - first around `+0x20..+0x108`
  - second around `+0x128..+0x178`
- each band contains:
  - pointer/state fields
  - a `0x1000` capacity-like field
  - flag/count bytes around `+0xB0/+0xB4` and `+0x140`

Disciplined read:

- this is not a trivial helper;
- it is an owner-local runtime/control object created very early in the
  top-level Player init pass;
- and because the same outer Player then keeps reusing it before and after the
  mounted graph assembly, `+0x3BD0` is now one of the best promotion/rebind
  anchors above the lower mounted subsystem map.

### `0x1410DE740` Looks Like A Player Finalize / Spawn-Info Bridge Above The Mounted Graph

The helper at `0x1410DE740` now matters because it sits directly beside the
`+0x3BD0` initializer cluster and immediately operates on the outer Player
owner plus external state.

What it does:

- logs through the same `outer_player + 0x3C20` channel used elsewhere in the
  Player init chain;
- then calls `0x140CC1450(rcx = outer_player, rdx = external_state)`;
- returns the produced object/state pointer.

This is useful for the promotion problem because:

- it sits above the mounted Player graph rather than inside one subsystem;
- it already consumes external state adjacent to Player construction;
- and it looks like a real finalize/spawn handoff bridge instead of a narrow
  combat or traversal helper.

Current disciplined read:

- the real Nadine promotion target is not merely `+0x1FB0`;
- it is the larger Player owner built by `0x1410DC160`;
- and the most promising upper anchors now are:
  - `0x1410DC160` itself,
  - the owner-local `+0x3BD0` object built by `0x1410DE520`,
  - and the nearby finalize/spawn bridge `0x1410DE740`.

### `0x140CC1450` Applies `ProcessSpawnInfo`-Driven Transform / Identity State Into The Top-Level Player Owner

The bridge called from `0x1410DE740` is now strong enough to pin as a real
top-level Player apply/finalize path rather than a generic helper.

What `0x140CC1450` does on entry:

- `rcx = outer_player_owner`
- `rdx = external_state` passed down from `0x1410DE740`
- reads owner state from:
  - `+0x164`
  - `+0x108/+0x10C`
  - `+0x110/+0x120/+0x130/+0x140`
  - `+0x18`
- uses a lookup table at `0x14386FD00`
- calls `0x14177F7F0`
- validates the resulting record against owner ids at `+0x108/+0x10C`
- copies the accepted record back into `outer_player + 0x110`

Then it:

- builds a transform block on the stack from that owner-local state,
- hands it into `0x1418C4840(outer_player, external_state, transform_block, 2)`,
- and continues through a second stage rooted at `owner + 0x18`.

Two practical conclusions now hold:

- `0x1410DE740 -> 0x140CC1450` is already a real `ProcessSpawnInfo`-facing
  Player bridge, not just a post-init logger;
- and `0x140CC1450` is one of the first places where external spawn/process
  state is clearly folded into the top-level Player owner above the mounted
  subsystem graph.

That makes this path a high-priority promotion candidate for Nadine:

- if Nadine is to be rebound into a real Player ownership path,
- the `0x1410DC160 -> 0x1410DE740 -> 0x140CC1450` chain is currently the
  strongest upper-layer place to look.

### `0x14177F7F0` Is A Generic Global Runtime-State Slot Allocator, Not A Player-Only Helper

The helper used by `0x140CC1450` is now pinned more tightly from direct
disassembly and from three additional independent callsites:

- `0x1408F99B0`
- `0x1409C81C0`
- `0x141BBA060`

What `0x14177F7F0` does structurally:

- takes a compact integer/type argument in `edx`
- takes an optional retained/runtime identity bundle in `r8`
- resolves a free slot index through the global bitset families rooted at:
  - `0x1466CE200`
  - `0x1466D0200`
- materializes a `0x380`-byte runtime-state object from that global pool
- resets it through `0x1418C4AD0`
- copies the optional retained/runtime pair into `object + 0x1A8`
- stores the incoming type/context id into `object + 0x208`
- zeroes `object + 0x20C`
- optionally allocates an auxiliary array/object into `object + 0x210`

That makes the function read as a generic global runtime-state slot allocator
with retained-pair seeding, not as a Player-only helper.

This matters for promotion because:

- the top-level Player path `0x140CC1450` is not inventing a private Player
  object from scratch;
- it is claiming one of the same generic global runtime-state slots used by
  other subsystems;
- so Nadine likely needs to enter this generic runtime-state layer first and
  only then ride the higher Player owner apply path.

### `0x1418C4840` Applies External State And A Transform Block Into That Runtime-State Slot

The partner helper reached from `0x140CC1450`, `0x1408F99B0`, `0x1409C81C0`,
and `0x141BBA060` is now pinned as a generic apply/commit path over those
`0x380` runtime-state slots.

On entry:

- `rcx = runtime_state_slot`
- `rdx = external_state / source owner`
- `r8  = transform block`
- `r9d = mode / variant` (`2` in the Player promotion path)

What it commits:

- source links from `external_state + 0x40/+0x48` into:
  - `slot + 0x20`
  - `slot + 0x130`
  - `slot + 0x138`
- one more source link from `external_state + 0x58` into:
  - `slot + 0x138`
- owner/source links into:
  - `slot + 0x18`
  - `slot + 0x1A0`
- allocates/initializes an auxiliary subobject at `slot + 0x200`
- writes state defaults at:
  - `slot + 0x140`
  - `slot + 0x280`
  - `slot + 0x2A8`
  - `slot + 0x2C0`
  - `slot + 0x2D4`
  - `slot + 0x2E0..+0x320`
- converts the incoming transform block into the committed spatial block at:
  - `slot + 0x1C0..+0x1F0`

Current disciplined read:

- `0x14177F7F0` claims or allocates a generic runtime-state slot
- `0x1418C4840` applies external/source state plus a transform block into it
- `0x140CC1450` is the higher Player-specific bridge that drives those two
  generic primitives from `ProcessSpawnInfo`-style state

Useful practical refinement:

- `external_state` is no longer just “some ProcessSpawnInfo payload”
- at minimum, the apply path is reading:
  - `external_state + 0x40`
  - `external_state + 0x48`
  - `external_state + 0x58`
- and using those links to seed the generic runtime-state slot before the
  higher Player-level post-apply work runs

### `0x1417CD200` / `0x1417CD3D0` Form The Upper Authored-State Resolve/Finalize Pair Used By `0x140CC1450`

The `proto`-state follow-up in `0x140CC1450` is now more concrete.

`0x1417CD200`

- takes a hashed authored token
- searches the upper authored-state registry rooted at:
  - `0x1466EC480`
  - `0x143862600`
- returns the matched upper state record object

`0x1417CD3D0`

- takes that resolved record
- uses its `+0x24` field plus internal registry state
- finalizes/activates the record for use by the current Player

So the late `proto` branch inside `0x140CC1450` is not merely:

- compare hash
- store pointer

It is:

- authored hash -> resolve upper state record -> store into `player + 0x1A0`
- then finalize/activate that state record through `0x1417CD3D0`

That makes `player + 0x1A0` look less like a dumb pointer cache and more like
an upper authored-state slot that is actively rebound during the apply path.

### `0x1410DE430` Builds A Sibling Owner-Local Fixed-Entry Object Beside `+0x3BD0`

The nearby helper at `0x1410DE430` is now useful enough to pin as a sibling
upper-layer owner object rather than dead noise beside `0x1410DE520`.

What it does:

- zeroes pointer fields every `0x40` bytes from `+0x0` through `+0x790`
- stores `self + 0x20` into `object + 0x10`
- stores `0x1E` into `object + 0x18`

That layout strongly suggests a fixed-entry owner-local table/list with:

- header at `+0x0..+0x18`
- entries beginning at `+0x20`
- `0x1E` slots
- effective stride around `0x40`

This is not yet the promotion hook itself, but it matters because the
`+0x3BD0` registry is not alone: the same top-level Player band clearly owns
multiple sibling upper-layer runtime containers above the mounted `+0x1Fxx`
graph.

### `0x1410DC160` / `0x1410DE740` Sit In The Same Upper Virtual Family Block As The `+0x3BD0` Object

The upper Player layer is now tighter than a plain "caller chain" story.

What is now confirmed from the assembly pass around `0x1410DCFFB` and the
`.rdata` block near `0x142A4BFC0`:

- `0x1410DE660` is a tiny vptr stamper that writes `0x142A4BFC0`
- the surrounding assembly pass allocates an object, stores that vptr, and
  then places the object at `outer_owner + 0x2018`
- the same `.rdata` block contains:
  - `0x1410DC160`
  - `0x1410DE740`
  - several nearby sibling methods
- `0x1410DE520` seeds the `+0x3BD0` object with a neighboring vptr inside the
  same block family (`0x142A4BFD8`)

Current disciplined read:

- `0x1410DC160` and `0x1410DE740` are not floating free helpers;
- they belong to the same upper virtual family block as the neighboring
  owner-local runtime objects;
- and the `+0x3BD0` registry object already sits in that same upper class
  family band.

This is useful for promotion because it narrows the search:

- the rebind/promotion hook may live in an upper virtual owner family rather
  than in one plain direct caller of `0x1410DC160`;
- and `outer_owner + 0x2018` is now a concrete additional anchor beside
  `+0x3BD0`, `+0x3D98`, and the mounted `+0x1Fxx` graph.

### `outer_owner + 0x2018` Is A Formal Upper-Layer Runtime Object With Getter, Relocate, And Teardown Paths

The new `+0x2018` anchor is now structurally stronger than "some sibling
pointer allocated near the top-level Player init".

Confirmed pieces:

- `0x1410C6BA0` is a trivial getter that returns `outer_owner + 0x2018`
- the relocation-style pass around `0x140F64F36`:
  - calls `(+0x2018)->virtual +0x8`
  - then conditionally rebases the stored pointer itself
- the teardown/cleanup path around `0x1410CDA22`:
  - resolves `outer_owner + 0x2018`
  - calls `(+0x2018)->virtual +0x40` with `edx = 1`
  - and only then continues with sibling cleanup on `+0x1EC8` and `+0x1F28`

This is enough to treat `+0x2018` as a formal upper-layer owner object in the
same lifecycle tier as the `+0x3BD0` registry object:

- it has a dedicated getter
- it participates in relocation/rebase
- it participates in teardown/final cleanup

So the current upper promotion map is now tighter:

- `+0x2018` = upper virtual-family object
- `+0x3BD0` = sibling upper owner-local registry object
- both already live in the same top-level Player lifecycle band above the
  mounted gameplay graph

### `+0x2018` Already Looks Like An Upper Player Action / Policy Object, Not Just A Lifecycle Node

Beyond lifecycle, the `+0x2018` object already exposes live query surfaces in
Player verb paths.

Confirmed uses:

- `0x141099175`:
  - first seeds local verb state through `0x141099520`
  - then tail-calls `(+0x2018)->virtual +0x20`
  - passes `rdx = local_state + 0x50`
  - passes `r8d = 0`
- `0x14109BE19`:
  - calls `(+0x2018)->virtual +0x18`
  - feeds it two local blocks on the stack
  - consumes the returned pointer as:
    - `qword[0]` -> scalar/double-like value
    - `dword[+0x8]` -> integer/state id

And the same broader caller around `0x1410991A0` already ties that object to
the surrounding Player graph:

- reads and checks `outer_owner + 0x1DF0`
- uses `outer_owner + 0x1D78 + 0x120`
- updates through `outer_owner + 0x3BD0`
- then asks `+0x2018` for verb/policy output

Current disciplined read:

- `+0x2018` is no longer just a sibling lifecycle container;
- it is already participating in upper Player action/policy evaluation;
- and it is one of the best current candidates for the place where Nadine must
  be accepted as a real Player owner before lower verbs become valid.

### `0x141099520 / 0x141099175` Form A Live Upper `PlayerGesturePermit` / Action-Policy Path

The earlier narrow read of `+0x2018` as a pure `PlayerWeaponAction` owner was
too aggressive. The newer pass shows a broader upper `PlayerGesturePermit` /
action-policy layer sitting above the mounted graph and already consuming real
Player verb state.

What is confirmed from `0x141099520`:

- it starts from the same outer Player owner that already carries the mounted
  graph;
- it gates on live mounted siblings:
  - `outer_owner + 0x1E50`
  - `outer_owner + 0x1E58`
- it then pulls owner-local runtime state from `outer_owner + 0x4320`;
- it consults:
  - `outer_owner + 0x1DF0`
  - `outer_owner + 0x1D78 + 0x120`
  - `outer_owner + 0x3BD0`
- and it emits a prepared local gesture/action context block instead of going
  directly into a lower verb subsystem.

What is confirmed from `0x141099175`:

- it first calls `0x141099520`;
- then resolves `outer_owner + 0x2018`;
- then feeds the prepared local block at `local + 0x50` into that upper object.

The surrounding `.rdata` and nearby reason strings now matter more than the
raw vtable assumption:

- `PlayerGesturePermit::GestureLockedOut(...)`
- `player has two-handed weapon in hands`
- `player is aiming`
- `current player movemode does not have :enable-gestures #t`
- `Player State does not permit gestures`
- `Player Grapple does not permit gestures`
- `PlayerWeaponAction`
- `PlayerWeaponActionWait`
- `PlayerReloadLoopWeaponAction`
- `PlayerRechamberWeaponAction`
- `ReloadWeapon`
- `RechamberWeapon`
- `weapon-hand-changed`
- `kStateReloading`
- `kStateAiming`
- `kStateWeaponOut`
- `kStateDraw`
- `kStateHolster`

This is the disciplined read now:

- `+0x2018` belongs to a broader upper `PlayerGesturePermit` / action-policy
  family;
- that family clearly touches weapon-action states, but it is not yet safe to
  call it weapon-only;
- and it already sits above `+0x1DF0`, `+0x1D78`, and `+0x3BD0` in real
  Player verb qualification logic.

That is a better promotion target for Nadine than a lower weapon-only surface,
because it looks like one of the layers that decides whether the top-level
Player state permits a verb family at all.

### The `0x142A4BFC0` Upper Family Table Is Not A Clean Function-Only Vtable Read

The small stamper `0x1410DE660` still writes `0x142A4BFC0` into the sibling
upper-layer object at `outer_owner + 0x2018`, but the raw table can no longer
be treated as a plain function-only vtable without qualification.

Reason:

- some qwords in that table resolve into `.text`
- while others resolve into `.rdata`

So the safe interpretation is:

- `0x142A4BFC0` is definitely part of the upper Player family block used by
  `+0x2018`;
- but the family layout is nontrivial enough that individual slot semantics
  must be inferred from live callers, not only from raw table offsets.

This matters because it avoids overcommitting on a false "virtual +0x18 /
+0x20 = exact gameplay methods" story before the family is fully normalized.

### `0x1410DC120` Already Looks Like A Small Upper Player State-Tag Classifier

The short helper directly above `0x1410DC160` is now useful enough to pin.

It:

- reads `DWORD [incoming_record + 0x4]`
- compares that tag against four authored/state hashes
- returns:
  - `0`
  - `1`
  - `2`
  - `3`
  - or `-1` on no match

So it should be treated as a compact upper Player state-tag classifier sitting
right on top of the same top-level Player family that later runs
`0x1410DC160`.

### `+0x3BD0` Already Participates In The Outer Player Relocate / Rebase Lifecycle

The relocation-style pass around `0x140F63F80..0x140F64056` confirms that the
`+0x3BD0` object is not a passive lookup blob.

Confirmed behavior in that pass:

- the outer owner relocates/rebases several late-band pointers together:
  - `+0x1F78`
  - `+0x3FE0`
  - `+0x3BD0`
  - `+0x3C18`
  - `+0x3C70`
  - `+0x3C78`
  - `+0x3C80`
- for `+0x3BD0`, it first dispatches through the object's first virtual slot
  before rebasing the stored pointer itself

That is strong evidence that:

- `+0x3BD0` is a real owner-owned runtime object with its own relocate/update
  method;
- and the upper Player band around `+0x3BD0/+0x3C18/+0x3D98/...` is a formal
  runtime lifecycle region, not just setup-time scaffolding.

### `+0x3BD0` Already Behaves Like An Owner-Local Hashed Runtime Registry

The early guess that `+0x3BD0` was just "some owner-local runtime object" is
now stronger and narrower.

In the consumer around `0x14102F6B0`:

- `outer_player + 0x3BD0` is loaded first
- `DWORD [object + 0xB0]` is used as the divisor / bucket count
- `QWORD [object + 0x20]` is used as the bucket table
- linked entries are walked by:
  - `entry->+0x10` = hashed id
  - `entry->+0x0` = next pointer

That is already enough to treat the object as a real hashed owner-local
registry rather than a passive state blob.

This matters for promotion because:

- it means the top-level Player owner already owns a stable local runtime
  registry layer above the mounted subsystem graph;
- and any Nadine rebinding that wants to look like a real Player will likely
  need to satisfy or populate this same owner-local lookup surface, not only
  the visible `+0x1Fxx` mounted subsystems.

### `0x1410DB040` / `0x1410DBF10` Now Look Like A Player Preflight Validator Over `ProcessSpawnInfo`-Style Input

The pair around `0x1410DB040` is now useful enough to keep in the top-layer
promotion map.

What is confirmed:

- `0x1410DBF10` is a compact wrapper:
  - takes a small incoming record at `r8`
  - calls `0x1410DB040`
  - if the returned code is below the record's current threshold/index, it
    copies the produced reason pointer out through `record->+0x10`
  - otherwise it reports failure through its boolean return

- `0x1410DB040` itself is a broader Player-side preflight validator:
  - consumes the current outer Player owner and a `ProcessSpawnInfo`-style
    input/state bundle
  - checks several high-level Player gates:
    - live actor / retained identity
    - `outer_player + 0x1A0`
    - `outer_player + 0x1D88`
    - `outer_player + 0x3D98`
  - emits structured failure reasons with pointers to strings such as:
    - `player has two-handed weapon in hands`
    - `player is aiming`
    - `current player movemode does not have :enable-gestures #t`
    - plus a later reason object at `0x142938320`

This is not yet the final promotion hook, but it matters because it sits above
the mounted graph and already decides whether the current top-level Player
state permits a given action/context at all.

Current disciplined read:

- `0x1410DB040` is a top-layer Player preflight validator over
  `ProcessSpawnInfo`-adjacent input;
- and `0x1410DBF10` is the small adapter that exposes that validation result to
  an incoming record object.

### `0x14109BE19` Publishes Upper Gesture / Weapon-Action Policy Output Back Into `+0x3BD0`

The upper `+0x2018` path is now stronger because the larger caller around
`0x14109BE19` does more than ask the upper family for a result. It publishes
that result back into the owner-local registry at `+0x3BD0`.

Confirmed sequence:

- it prepares a hash/key in `edx`;
- calls into `outer_owner + 0x2018`;
- consumes the returned compact record as:
  - `qword[0]` -> scalar/double-like result
  - `dword[+0x8]` -> small state id
- stores those into local verb state;
- then, when the local gating byte at `local + 0x5C` is still clear, it hashes
  a named upper action token and writes a one-byte payload into
  `outer_owner + 0x3BD0`.

The named tokens on this path are already concrete:

- `ReloadWeapon`
- `weapon-hand-changed`
- `PlayerRechamberWeaponAction`
- and sibling reload-loop strings nearby:
  - `s_weapon-reload-loop-start`
  - `s_weapon-reload-loop-mirror-start`
  - `s_weapon-drop`

This makes the upper path more precise:

- `+0x2018` is part of a gesture / action-permit family that also touches
  weapon-action state;
- and `+0x3BD0` is not just a passive hashed lookup table, but an owner-local
  publish surface for keyed upper Player policy flags.

### `0x140CB5050` Writes Keyed One-Byte Records Into The `+0x3BD0` Owner Registry

The helper reached from `0x1410993B8` is now useful enough to pin.

Confirmed behavior:

- input:
  - `rcx = outer_owner + 0x3BD0`
  - `edx = hashed key`
  - `r8  = pointer to one-byte payload`
- it resolves/allocates storage through the object's internal tables
  starting at `object + 0xB8`;
- copies a single byte from `*r8` into that keyed entry;
- then dispatches a follow-up update/commit through `0x142052250`.

Nonlocal callers are already spread beyond one site:

- `0x1410993B8`
- `0x141099EE8`
- `0x14101090B`
- `0x141014B3E`
- `0x140DCF121`
- `0x141332758`

Those callers already span more than one narrow weapon case. Concrete keys now
seen on the callsites include:

- `ReloadWeapon`
- `RechamberWeapon`
- `PlayerReloadClipWeaponAction`
- `kRopeClimb`
- `piton-stab-fx`

So the disciplined read now is:

- `+0x3BD0` is an owner-local keyed byte/flag registry for upper Player policy
  and action-state publication;
- not just a lookup cache;
- and not weapon-only;
- and one realistic promotion requirement for Nadine is likely to survive the
  same upper policy publications into this registry before lower mounted verbs
  behave correctly.

### `0x142A4BF50` Is A Small Upper Player Bridge Object Wrapped Around The Real Owner

The tiny object family at `0x142A4BF50` is now useful enough to pin because it
already exposes upper-owner getters into the same promotion-relevant band.

What is confirmed:

- `0x1410DBFA0` initializes the tiny wrapper:
  - `object->vptr = 0x142A4BF50`
  - `object->+0x8 = outer_player_owner`
- `0x1410DBFC0` returns `wrapper->owner->+0x2018`
- `0x1410DC020` returns `wrapper->owner + 0x3C20`
- `0x1410DBFE0` resolves one of three `owner + 0x1A0` subentries through the
  chain `owner + 0x1A0 -> +0x58 -> +0x318 + 0x30*index`

So this is not a random helper. It is a small upper bridge over the real outer
Player owner that already exposes:

- the upper action/policy object at `+0x2018`
- the logging/finalize band at `+0x3C20`
- and indexed substate from `+0x1A0`

That is useful for promotion work because it means the upper Player family is
already being passed around through a compact wrapper object, not only through
the raw top-level owner pointer.

### The `0x142A4BF50` Wrapper Is Constructed During The Same Upper Player Init Region

The larger init region around `0x1410DCA60..0x1410DE3D6` now shows that the
small wrapper above is not an isolated side helper.

Confirmed sequence:

- the outer Player owner at `rsi` already has `+0x3FD8`
- a small `0x10` wrapper object is allocated
- `0x1410DCA93` stamps `0x142A4BF50`
- `wrapper->+0x8 = outer_player_owner`
- the code then:
  - asks the real owner for a value through `owner->virtual +0x370`
  - passes that plus the wrapper into `(*(owner+0x3FD8))->virtual +0x20`
  - continues the same upper init/finalize band

This is strong evidence that:

- the wrapper is part of the formal upper Player initialization/finalization
  path;
- and `owner + 0x3FD8` is another nontrivial upper-layer consumer that already
  accepts a bridge object tied to the real outer Player owner.

### `outer_owner + 0x3FD8` Is A Large Upper Player Manager, Not A Passive Pointer

The object at `outer_owner + 0x3FD8` is now strong enough to pin as another
formal upper-layer manager in the same top Player band.

What is confirmed:

- getter:
  - `0x1410C6B90` returns `outer_owner + 0x3FD8`
- constructor path:
  - `0x141E65C60` allocates size `0x5E70`
  - then jumps into `0x141E66090`
- in-place init at `0x141E66090`:
  - stamps vtable `0x142B91B60`
  - stores the outer owner in `object + 0x10`
  - seeds large internal state bands and tables
  - zeros late object links like `+0x830/+0x838/+0x840`
  - ends with high-water fields around `+0x5E50/+0x5E60`
- teardown path at `0x141E66260`:
  - swaps to another vtable
  - releases linked children at `+0x18`, `+0x20`, `+0x830`, `+0x838`
  - and supports freeing on flag

The same manager is already wired into the upper Player lifecycle:

- in the init region around `0x1410DCA60..`
  - the bridge wrapper `0x142A4BF50` is created
  - `owner->virtual +0x370` produces an input value
  - then `(*(owner + 0x3FD8))->virtual +0x20` consumes that value plus the
    wrapper object
- in cleanup around `0x1410D8D80..`
  - `(*(owner + 0x3FD8))->virtual +0x138` is called before the slot is cleared

So the disciplined read now is:

- `+0x3FD8` is a real upper Player manager that already accepts a compact
  bridge object tied to the outer owner;
- it sits above the mounted graph;
- and it is now one of the best candidates for the place where a promoted
  Nadine would have to be registered or rebound before upper Player policy and
  verbs behave correctly.

### `+0x3FD8::virtual +0x20` Already Looks Like A Real Bridge-Registration Path

The manager-side method at `0x141E668C0` is now strong enough to treat as a
real registration/setup path rather than a plain setter.

Confirmed behavior:

- input shape:
  - `rcx = upper Player manager at +0x3FD8`
  - `rdx = owner-derived value produced from owner->virtual +0x370`
  - `r8  = small bridge wrapper object 0x142A4BF50`
- on entry it stores:
  - `manager + 0x18 = rdx`
  - `manager + 0x20 = wrapper`
- then it calls `wrapper->virtual +0x18`
- stores that returned pointer/value into `manager + 0x840`
- then calls `manager->virtual +0x38`

That `virtual +0x38` method is now also interpretable:

- `0x141E65BE0` reads `manager + 0x840`
- and returns `DWORD [that_object + 0x0]`

So the current disciplined read is:

- `wrapper->virtual +0x18` yields a compact bridge/result object or descriptor
- `manager->virtual +0x38` turns that into an entry count
- and `0x141E668C0` uses that count to allocate internal arrays and per-entry
  handler objects

The rest of `0x141E668C0` confirms it is building real manager-side state:

- allocates array storage into:
  - `manager + 0xE0`
  - `manager + 0xD8`
- iterates entries
- classifies entry tags through at least two authored/state hashes:
  - `0x675650B0`
  - `0x7339D08C`
- allocates per-entry objects like:
  - small object with vtable `0x142BD3B30`
  - larger object with vtable `0x142BD4068`
- stores the original wrapper at `object + 0x18` or `+0x288`
- and finally snapshots outer owner identity from `manager + 0x10` into
  `manager + 0x190`

This is one of the strongest promotion-facing results so far:

- `+0x3FD8::virtual +0x20` is not passive lifecycle glue;
- it is already a real upper registration path that turns a compact bridge to
  the outer Player owner into structured manager-side entry state.

### `wrapper->virtual +0x18 -> 0x141E52590` Builds A Compact Upper Gesture/Joint Descriptor

The bridge wrapper method used by the `+0x3FD8` manager is no longer opaque.

Confirmed from `0x1401B8420 -> 0x141E52590`:

- it allocates a compact header object;
- seeds:
  - `DWORD [obj + 0x0] = 0`
  - `DWORD [obj + 0x4] = 2`
  - `QWORD [obj + 0x8] = secondary array allocation`
- then builds two parallel hashed integer arrays with overflow guards/debug
  checks;
- nearby strings tie this builder to upper gesture/joint policy:
  - `gesture-read-joints-layer`
  - `*gesture-space-joints*`
  - `*** Begin %s gesture log ***`

This fits the manager-side registration path:

- the tiny upper bridge wrapper does not hand `+0x3FD8` a raw owner pointer;
- it hands back a compact descriptor object;
- `+0x3FD8::virtual +0x20` then uses that descriptor to derive entry count and
  build manager-side per-entry handler state.

Current disciplined read:

- the upper promotion path is already passing through a formal
  `gesture/joint/action` descriptor layer before lower mounted verbs execute;
- this is exactly the kind of upper structural gate that Nadine will likely
  need to satisfy to look like a real Player rather than a controlled buddy.

### `0x141E52590` Returns A Two-Layer Gesture/Joint Descriptor With Fixed Hash Families

The descriptor builder behind `wrapper->virtual +0x18` is now pinned more
tightly than a generic "compact object".

Confirmed from the full body at `0x141E52590`:

- it allocates a `0x10`-byte header object and seeds:
  - `DWORD [obj + 0x0] = 0`
  - `DWORD [obj + 0x4] = 2`
  - `QWORD [obj + 0x8] = secondary array pointer`
- then it computes and appends two fixed CRC/hash families:
  - a 4-byte sequence from `0x1428D9008`
  - a 7-byte sequence from `0x142B909F0`
- both hash families are appended into the descriptor's secondary array with
  overflow/debug guards identical to other authored-string hash builders
- nearby strings still anchor the layer to upper gesture/joint policy:
  - `gesture-read-joints-layer`
  - `*gesture-space-joints*`
  - `*** Begin %s gesture log ***`

So the disciplined read is now:

- `wrapper->virtual +0x18` returns a fixed-count (`count = 2`) upper
  `gesture/joint/action` descriptor
- and the `+0x3FD8` manager is not deriving entries from arbitrary owner data,
  but from two authored hash families already normalized into that descriptor

### `+0x3FD8::virtual +0x60` Looks Like A Per-Entry Reset/Invalidate Pass

The method reached from the upper Player update path at `0x1410E9294` through
`(*(owner + 0x3FD8))->virtual +0x60` is now interpretable.

Confirmed at `0x141E69AA0`:

- early-outs if `BYTE [manager + 0x8] == 0`
- clears repeated per-entry counters across the large manager body:
  - `+0x1C8`, `+0x298`, `+0x368`, `+0x438`
  - `+0x508`, `+0x5D8`, `+0x6A8`, `+0x778`
- clears repeated pointer pairs:
  - `+0x250/+0x258`
  - `+0x320/+0x328`
  - `+0x3F0/+0x3F8`
  - `+0x4C0/+0x4C8`
  - `+0x590/+0x598`
  - `+0x660/+0x668`
  - `+0x730/+0x738`
  - `+0x800/+0x808`
- then iterates `manager->virtual +0x38` entries and calls each registered
  per-entry handler through `handler->virtual +0xD8`, passing:
  - outer owner at `manager + 0x10`
  - upper owner snapshot at `manager + 0x838`
  - manager-side state block at `manager + 0x170`
  - scratch/output band at `manager + 0x850`

This is not a passive "tick". It looks like a manager-wide reset/invalidate
phase before the next upper Player action-policy frame is rebuilt.

### `+0x3FD8::virtual +0x68` Rebuilds An Eight-Slot Aggregate And Pushes It Back Into Handlers

The sibling method reached immediately after `virtual +0x60` in the same upper
Player update path is also now pinned.

Confirmed at `0x141E69BD0`:

- early-outs on the same `BYTE [manager + 0x8] == 0` gate
- resets the shared state band at `manager + 0x170` via `0x1420F3100`
- allocates a temporary aggregate object:
  - header object size `0x18`
  - secondary buffer size `0x80`
  - seeds `QWORD [agg + 0x8] = 8`
- loops over 8 upper slots:
  - queries per-slot state via `0x1420F2DC0(manager+0x170, slot_index, owner+0xF0, manager+0x850)`
  - if a slot exists, appends `(slot_index, slot_payload_ptr)` into the
    temporary aggregate through `0x1420F37A0`
- then iterates registered manager handlers and calls
  `handler->virtual +0xE0(owner, aggregate)`

This is a strong signal that `virtual +0x68` is the aggregate rebuild/publish
phase that follows the invalidate/reset pass from `virtual +0x60`.

### The Per-Entry Handler Families Are Structured Upper Slot Objects, Not Tiny Flags

The two handler families allocated by `0x141E668C0` are now a bit clearer.

For the larger family at vtable `0x142BD4068`:

- size `0x290`
- stores:
  - slot index at `+0x10`
  - wrapper bridge at `+0x288`
  - owner snapshot at `+0x8`
  - manager-side link at `+0x20`
- seeds multiple local state bands:
  - `+0x1C0..+0x210`
  - `+0x270..+0x284`
  - `+0x248/+0x258`
  - `+0x1A8/+0x1AC/+0x1B0`
- uses helper builders `0x140242010` and `0x141685C30`

For the smaller family around vtable `0x142BD3B30`:

- the manager allocates it in the tag branch for `0x7339D08C`
- an external ctor at `0x1420F736D` shows it is a compact `0x8`-byte object
  whose primary payload is just that vtable

This sharpens the split:

- one tag branch produces a trivial compact handler shell
- the other produces a heavy per-slot upper state object with embedded local
  aggregate/presentation bands and a direct back-reference to the bridge wrapper

### The Upper Player Update Path Already Calls The `+0x3FD8` Manager As A Two-Phase Rebuild

The callsite around `0x1410E9294` is now strong enough to treat as a real upper
Player update stage rather than random bookkeeping.

Confirmed sequence:

- optional pre-step through `owner + 0x1A0 -> +0x20`
- `(*(owner + 0x3FD8))->virtual +0x60`
- other upper-owner work:
  - `owner + 0xC0 -> ... -> virtual +0x30`
  - `0x140CC21C0(owner)`
  - owner virtual `+0xEA8`
  - registry-side action on `owner + 0x3BD0` with selector `2`
  - owner virtual `+0xEB8`
  - `owner + 0x1E98 -> virtual +0x60`
- `(*(owner + 0x3FD8))->virtual +0x68`
- then broader finalize/update continues

So the upper `Player` layer is already using `+0x3FD8` as a formal two-phase
manager:

1. invalidate/reset previous upper slot state
2. rebuild/publish the current aggregate back through registered handlers

This is much closer to a true promotion surface than lower mounted verbs.

### The `0x1410CF114` Init Callsite Already Binds `+0x3FD8` Into The Top-Level Player Bringup

The upper manager is not only present in the object graph; it is already part
of a concrete top-level Player initialization step.

Confirmed from the callsite region around `0x1410CF114`:

- the same init pass builds a compact owner-local packet/builder object in `r15`
- that object accumulates multiple typed entries with explicit type fields:
  - one entry tagged `0x10`
  - one entry tagged `0x08`
  - one entry tagged `0x04`
- after these typed packets are queued, the pass calls:
  - `rcx = outer_player + 0x3FD8`
  - `rdx = r15` (the compact packet/builder object)
  - `r8d = 0xC8`
  - `r9  = outer_player + 0x4260`
  - `(*(outer_player + 0x3FD8))->virtual +0x0`

That makes the disciplined read stronger:

- `+0x3FD8` is not only consuming the tiny bridge wrapper from the upper init
  region;
- it also participates directly in the top-level Player bringup with a typed
  init packet/builder plus an owner-local state region at `+0x4260`;
- this is the closest thing so far to a real upper bind/registration stage for
  a full Player owner.

### The `0x1410E9220` Update Callsite Uses `+0x3FD8` As A Formal Two-Phase Upper Player Stage

The broad upper Player update path around `0x1410E9220` is now strong enough to
treat as a real orchestration stage, not just a loose bundle of helper calls.

Confirmed order:

- optional pre-step through `owner + 0x1A0 -> +0x20`
- `(*(owner + 0x3FD8))->virtual +0x60`
- sibling upper work:
  - `owner + 0xC0 -> ... -> virtual +0x30`
  - `0x140CC21C0(owner)`
  - owner virtual `+0xEA8`
  - keyed registry action on `owner + 0x3BD0` with selector `2`
  - owner virtual `+0xEB8`
  - `owner + 0x1E98 -> virtual +0x60`
- `(*(owner + 0x3FD8))->virtual +0x68`
- then broader transform/presentation publication continues:
  - writes back into `owner + 0x1720/+0x1730`
  - publishes presentation/telemetry records through `0x143FDF520`

This means the current upper Player lifecycle already looks like:

1. init/bind the upper manager with typed packet state
2. each update frame:
   - invalidate/reset manager slot state
   - run sibling upper Player owners
   - rebuild/publish the upper manager aggregate
   - then push presentation/runtime outputs outward

That is a much narrower and more concrete promotion surface for Nadine than the
older lower-level `companion` or `NPC` paths.

### `+0x3FD8::virtual +0x0` Now Looks Like The Upper Manager Bind/Register Pass

The manager method hit directly from the top-level Player init callsite around
`0x1410CF114` is no longer just an opaque vtable slot.

Confirmed at `0x141E66D50`:

- input shape matches the init callsite:
  - `rcx = upper manager +0x3FD8`
  - `rdx = compact typed packet/builder object`
  - `r8d = numeric mode/count input` (the init callsite uses `0xC8`)
  - `r9  = owner-local state region at outer_player + 0x4260`
- the method resolves multiple authored hash families before doing anything
  with handlers:
  - one 8-byte family through `0x141764C40`
  - one 6-byte family through `0x141FF63B0`
  - one 0x19-byte family through `0x1417292A0`
- it builds and stores a compact derived object into `manager + 0x838`
- then iterates registered per-entry handlers and calls
  `handler->virtual +0x10(packet_builder, mode_plus_one, owner_4260_state)`

This is a stronger interpretation than before:

- `+0x3FD8::virtual +0x20` registers the upper bridge/descriptor side
- `+0x3FD8::virtual +0x0` then binds init-time typed packet state and pushes
  it into every registered handler

So the upper manager is already split into two meaningful surfaces:

1. bridge/descriptor registration
2. top-level Player init binding with typed packet state

That combination is very close to the kind of upper ownership hook Nadine will
have to satisfy for a real promotion into the Player path.

### Direct rel32 Caller Checks Keep The Upper Chain Narrow

A raw `.text` rel32 scan over `tll.exe` gives one more useful constraint:

- `0x1410DB040` has a direct rel32 caller at `0x1410DBF40`
  (`0x1410DBF10` wrapper)
- `0x140CC1450` has a direct rel32 caller at `0x1410DE781`
  (`0x1410DE740` finalize/apply wrapper)
- no other direct rel32 calls to `0x1410DC160` or `0x1410DE740` surfaced in
  the raw scan

This does not prove there are no higher callers at all, but it does reinforce
the current shape of the upper chain:

`0x1410DBF10 -> 0x1410DB040`

and

`0x1410DE740 -> 0x140CC1450`

remain the cleanest direct upper entry points above the broad `Player::Init`
layer.

### RTTI Now Pins `0x142A4BFE8` As The Real `Player` Vtable And `0x1410DC160` As A True `Player` Virtual Slot

Running the local RTTI/vtable helper against `tll.exe` now gives a hard anchor
instead of string-only inference:

- `Player`:
  - complete-object locator / descriptor chain resolves to
    `vtable_rva = 0x2A4BFE8`
  - final VA:
    - `0x142A4BFE8`
- `ProcessSpawnInfo`:
  - final vtable VA:
    - `0x14291CF98`

Dumping qwords from `0x142A4BFE8` shows:

- `Player_vftable + 0x50 -> 0x1410C6FC0`
- `Player_vftable + 0x58 -> 0x1410DC160`
- `Player_vftable + 0x60 -> 0x1410D3440`

This resolves an old ambiguity:

- `0x1410DC160` is not just an upper-family helper near Player state;
- it is a real virtual method of the RTTI-backed `Player` class at slot
  `+0x58`.

That also explains why raw rel32 scans never found ordinary direct callers for
`0x1410DC160`: the path is reached through virtual dispatch.

### `0x1410C8FD0` Is The Real In-Place `Player` Constructor And `0x1410C6F00` Is Its Alloc-Ctor Wrapper

Scanning for code that writes the real `Player` vtable `0x142A4BFE8` yields a
clean constructor pair.

At `0x1410C8FD0`:

- calls base ctor `0x140CBFB30`
- writes `0x142A4BFE8` into `[rbx]`
- zero-initializes the broad `Player` layout, including:
  - upper owner bands
  - `+0x1D48..+0x1DF8`
  - multiple mounted subsystem slots already mapped earlier

The direct wrapper at `0x1410C6F00` then behaves as:

- if `rdx != 0`, jump straight into `0x1410C8FD0`
- otherwise allocate `0x49E0` bytes
- then call `0x1410C8FD0`

So the upper construction chain is now pinned much harder:

`0x1410C6F00 -> 0x1410C8FD0 -> Player vftable 0x142A4BFE8`

and the real `Player` object size is:

- `0x49E0`

This is a better promotion anchor for Nadine than any lower companion/NPC path,
because it is now tied to the true top-level `Player` class rather than a
player-like family guess.

### `0x1410F14C0` Seeds A Real `ProcessSpawnInfo` Packet Before Committing The Retained Player Pair

The retained spawn commit path is now stronger than the earlier
`CommitResolvedPlayerSpawn` read.

Inside `0x1410F14C0`:

- code explicitly seeds a local object with vtable:
  - `0x14291CF98` (`ProcessSpawnInfo`)
- then routes that packet through the usual validation/build helpers such as:
  - `0x1417C32A0`
  - `0x1417C3790`
- and only after that resolves/commits the retained/live player pair into:
  - `g_RetainedPlayerSlotTable` at `0x143EEBE80`

This means the retained spawn path is not only “some retained slot writer”.
It already passes through a real `ProcessSpawnInfo`-class object model before
the live player pair is accepted.

### `0x1413C04A0` Is A Second Upper Runtime Caller Of The Retained Spawn Path

`0x1410F1910` is no longer only reachable from the obvious script-side
`spawn-player` path.

Direct call scanning now shows an independent second caller:

- `0x1413C04A0`

That function:

- builds a local retained-spawn state block on the stack
- calls `0x1410F1910`
- validates the returned retained/live pair
- and on success dispatches:
  - `outer_owner->virtual +0x240`

So the retained spawn result is already fed back into a higher runtime owner
layer, not only stored into the global retained slot table.

This is important for Nadine because it shows a second upper ownership-facing
surface above retained spawn:

`retained spawn -> validated retained/live pair -> outer-owner virtual +0x240`

which is much closer to a real promotion/rebind path than the earlier
companion-only stacks.

### `0x1400AA8C0` Registers The Real `Player` Alloc-Ctor As A Startup Factory Pointer

The real `Player` alloc-ctor wrapper `0x1410C6F00` still has no ordinary direct
rel32 callers. That ambiguity is now resolved one layer up.

At `0x1400AA8C0`:

- two startup lookup/helper calls seed temporary ids
- then the function loads:
  - `r9  = 0x1410C6F00`
  - `rcx = 0x143EEBF00`
- and calls:
  - `0x1417C4650`

So `0x1410C6F00` is not dead or private; it is published as a constructor /
factory pointer through a startup registration helper into the global slot
`0x143EEBF00`.

The adjacent startup helper `0x1400AA940` also seeds a sibling global family:

- `0x143EEBF90`
- `0x143EEBFA0`
- `0x143EEBFA8`
- `0x143EEBFAC`

with a second Player-adjacent helper pointer:

- `0x1410C6F60`

That makes the upper promotion story stronger:

- the real `Player` ctor path is not only reachable;
- it is explicitly registered into global startup tables.

### `0x143EEBF00` Already Participates In Live Gameplay Gates

The global slot seeded by the startup registration path is already used outside
startup.

At `0x140410F34`:

- a live runtime object is resolved first
- then `rdx = 0x143EEBF00`
- and `0x1401696B0` is called

`0x1401696B0` itself is a compact bitset-membership test over:

- `rcx + 0x30` bitset storage
- `rdx + 0x14` selector/id

So `0x143EEBF00` is already acting like a real runtime-discriminator/registry
entry in gameplay logic, not merely an inert startup record.

### The Sibling Global Player Slot Family At `0x143EEBF90` Also Feeds Back Into Live Player State

The neighboring startup registration helper at `0x1400AA940` is not dead data
either.

It publishes:

- helper factory `0x1410C6F60`
- into the global family rooted at:
  - `0x143EEBF90`
  - `0x143EEBFA0`
  - metadata `0x143EEBFA8/0x143EEBFAC`

And `0x1410C6FC0` already consumes that family in live `Player` state:

- checks `player + 0xC0`
- asks that subobject about `0x143EEBF90` through virtual `+0x198`
- on success stores the global slot pointer back into `[player+0xC0]+0x8`
- and sets local flags at `player + 0x3D/+0x3E`

So the startup-registered Player global families now look like real upper
ownership / capability surfaces, not just one-time registrars.

### `0x1410DBF10` And `0x1410DE740` Sit In Compact `.rdata` Upper-Player Function Tables Beside Other Known Upper Helpers

The upper wrappers are now tied together structurally, not only by behavior.

Raw qword inspection around their `.rdata` anchors shows:

- near `0x142A4A7A8`:
  - `0x1410DC020`
  - `0x1410DBF10`
  - `0x1410DBF80`
  - `0x1410DC120`
  - `0x1410DE6F0`
- near `0x142A4AED8`:
  - `0x1410DE740`
  - `0x1410CF370`
  - plus nearby upper Player helpers in the same compact band

This matters because it reinforces that:

- `0x1410DBF10`
- `0x1410DB040`
- `0x1410DC120`
- `0x1410DC160`
- `0x1410DE740`
- `0x1410CF370`
- `0x1410CF470`

are not just loosely adjacent free functions. They are already grouped into a
real upper `Player` dispatch surface in `.rdata`, which is exactly the kind of
layer we need for Nadine promotion/rebind.

### `0x1410CF370` Is The Named `Player::InitializeDrawControl` Stage

The upper Player init chain now has a directly named stage rather than only a
large anonymous block.

Inside `0x1410CF370`:

- the embedded diagnostic string is:
  - `Player::InitializeDrawControl(const ProcessSpawnInfo &, const ResolvedLook &)`
- the function logs through:
  - `outer_player + 0x3C20`
- then calls:
  - `0x141BBACC0`
- then updates the companion/behavior object at `outer_player + 0x198`

This is strong enough to treat `0x1410CF370` as a real upper Player init phase,
not a random helper near the `Player` graph.

### `0x1410CF470` Is The Named `Player::InitializeCompositeBody` Stage And Is Called Directly From `0x1410DC160`

The neighboring upper phase also resolves cleanly now.

Inside `0x1410CF470`:

- the embedded diagnostic string is:
  - `Player::InitializeCompositeBody(void)`

And direct call scanning shows:

- `0x1410DC160` calls it directly at:
  - `0x1410DCF6F`

So the top-level `Player` init chain is now starting to decompose into named
stages:

- ctor:
  - `0x1410C6F00 -> 0x1410C8FD0`
- upper virtual init:
  - `0x1410DC160`
- named upper phases under it:
  - `0x1410CF370` (`InitializeDrawControl`)
  - `0x1410CF470` (`InitializeCompositeBody`)

This makes the future Nadine promotion patch more concrete, because the target
is no longer “some broad Player init blob” but a staged upper `Player`
construction path.

### `Player::InitializeCompositeBody` Also Has A Second Upper Rebuild Caller Beyond `0x1410DC160`

`0x1410CF470` is not only called from the top-level virtual init pass
`0x1410DC160`.

Direct call scanning shows a second caller:

- `0x1410FF199`

The surrounding upper path is not a trivial helper. It already touches:

- `player + 0x4308`
- `player + 0x1DF0 -> virtual +0x130`
- `player + 0x1D88 -> virtual +0x68`
- `player + 0x1F80 -> virtual +0x78`
- `player + 0x3FD8 -> virtual +0x10`

and then revisits `Player::InitializeCompositeBody`.

So `InitializeCompositeBody` now has two separate upper entry paths:

1. initial top-level bringup under `0x1410DC160`
2. a second upper rebuild/revalidation path around `0x1410FF199`

That is useful for Nadine because the eventual promotion patch may not need to
replay the whole ctor/init chain if this upper rebuild line can be reused after
state/ownership is rebound.

### `0x1410DBF10` Exposes The Exact Shape Of The Upper Player Preflight Packet

The small wrapper above `0x1410DB040` is now concrete enough to treat as a
stable adapter layer rather than a generic helper.

Inside `0x1410DBF10`:

- the incoming object at `r8` contains at least:
  - `[+0x0]` -> live actor / upper Player target pointer
  - `[+0xC]` -> threshold or minimum accepted result index
  - `[+0x10]` -> optional output pointer for the failure-reason/result token
- the wrapper pushes:
  - `rcx = &incoming_record_dword`
  - `rdx = &local_reason_ptr`
  - `r9 = incoming_object[0]`
  into `0x1410DB040`
- if the validator returns `-1`, or if the threshold in `incoming[+0xC]`
  rejects the returned result, the wrapper returns `1`
- otherwise it copies the produced reason/result pointer to
  `*incoming[+0x10]` and returns `0`

So this is not just “a tiny wrapper near Player init”. It is already a real
upper `Player` verb/admission adapter over a compact packet shape.

### The Tiny Bridge Wrapper At `0x142A4BF50` Is Now Pinned As A Real Upper Surface

The bridge wrapper around `0x1410DBFA0` now has enough semantics to stop
treating it as a nameless glue object.

Confirmed methods:

- `0x1410DBFA0`
  - seeds the wrapper and stores the real outer `Player` owner in `object+0x8`
- `0x1410DBFC0`
  - returns `wrapper->owner->+0x2018`
- `0x1410DBFE0`
  - for indices `0..1` returns a compact substate entry under
    `wrapper->owner->+0x1A0 -> +0x58 -> +0x318 + 0x30*index`
- `0x1410DC020`
  - returns `wrapper->owner + 0x3C20`
- `0x1410DC030`
  - relocates `wrapper->owner` if it falls inside a supplied
    `[old_base, old_end)` window
- `0x1410DC050`
  - destroys/frees the 0x10-byte wrapper
- `0x1401B8420`
  - jumps into `0x141E52590`
- `0x1401B8430`
  - trivial `false` return

That makes `0x142A4BF50` a formal upper `Player` bridge surface, not just one
getter for `+0x2018`.

### `0x141E52590` Builds A Fixed Two-Entry Upper Authored Descriptor

The bridge-wrapper virtual at `0x1401B8420` is now concrete:

- it allocates a compact descriptor object
- seeds:
  - `count = 2`
  - `secondary_array = new uint32_t[2]`
- then computes and appends two fixed authored ids from short baked byte
  sequences before returning the descriptor

This explains why manager-side registration around `+0x3FD8` can treat the
bridge as a stable source of a small authored descriptor family rather than as
an opaque callback.

The important part for the project is not the exact ids yet. The important part
is that the upper `Player` bind surface already expects a compact
descriptor-producing bridge object, which is much closer to a promotion/rebind
hook for Nadine than the old lower action/NPC paths.

### `0x141E668C0` Builds Two Different Upper Entry Handler Families From The Bridge Descriptor

The upper manager registration path at `0x141E668C0` is now stronger than a
generic “stores wrapper and allocates arrays” description.

Confirmed behavior:

- stores:
  - owner-derived value in `manager+0x18`
  - bridge wrapper in `manager+0x20`
- calls:
  - `wrapper->virtual +0x18` -> `0x141E52590`
- caches the returned descriptor in:
  - `manager+0x840`
- derives entry count through:
  - `manager->virtual +0x38`
- allocates:
  - `manager+0xE0` -> compact per-entry state array
  - `manager+0xD8` -> per-entry handler pointer array

Then it walks the descriptor ids and builds at least two distinct handler
families:

- one branch allocates a trivial compact shell
- another branch allocates a heavier `0x290` upper slot object which stores:
  - the bridge wrapper at `+0x288`
  - owner-derived state across `+0x1C0..+0x284`

So `+0x3FD8` is already doing real upper `Player` bind work from a compact
bridge descriptor, not just pass-through bookkeeping.

### `player + 0xC0` Now Looks Like A Live Player-Side Helper Registry

The bridge from the startup helper family back into live `Player` state is
clearer now.

Already known:

- `0x1410C6FC0` checks `player + 0xC0`
- asks that subobject about the global family rooted at `0x143EEBF90` through
  virtual `+0x198`
- on success writes that global slot pointer into `[player+0xC0]+0x8`
- and sets local flags at `player + 0x3D/+0x3E`

New confirmation from the real `Player` ctor body:

- later in `0x1410C8FD0`, a live path reads `player + 0xC0`
- dereferences:
  - `[player+0xC0]`
  - `-> +0x10`
  - `-> +0x18`
- and compares that stored id against a freshly hashed authored token derived
  from the short string block at `0x14292C03C`
  - the surrounding bytes resolve to strings like:
    - `crouch`
    - `stealth`

That makes `player + 0xC0` look like a real live helper/state registry rooted
in the upper `Player` object, not one-shot construction storage.

### Current Narrowest Promotion Surface

With the bridge and manager layers pinned harder, the current best Nadine
promotion chain is now:

`startup Player factory`
-> `0x1410C6F00 -> 0x1410C8FD0`
-> `0x1410DBF10 -> 0x1410DB040`
-> bridge wrapper `0x142A4BF50`
-> upper surfaces `+0x2018 / +0x3C20 / +0x1A0`
-> upper manager `+0x3FD8`
-> `0x1410DE740 -> 0x140CC1450`
-> named upper init/rebuild stages below

This is materially better than the earlier “player-like” picture. The top of
the stack now already looks like a real upper `Player` bind surface with
descriptor publication, per-entry handler registration, and owner-local helper
registries.

### `0x1410DBF80` Is A Small Wildcard Gate Over Bridge-Resolved Upper Substate

The neighboring helper in the same compact upper function band is now clearer.

`0x1410DBF80`:

- checks `incoming_record + 0xC`
- if it equals `-1`, returns success immediately
- otherwise tail-calls `wrapper->virtual + 0x10`

And for the current bridge family at `0x142A4BF50`, that virtual resolves to
`0x1410DBFE0`, which returns one of the compact indexed upper substates under:

- `wrapper->owner->+0x1A0 -> +0x58 -> +0x318 + 0x30*index`

So this helper is already a real bridge-aware gate between an incoming upper
packet and concrete `Player` substate, not just another anonymous wrapper.

### `0x1410CF114` Is The Most Concrete Upper Manager Bind Packet Builder So Far

The init path around `0x1410CF114` is now strong enough to treat as a likely
runtime patch surface.

What it does before the manager bind call:

- uses `outer_player + 0x4260`
- prepares a compact local packet block with:
  - leading mode/flag words at `+0x20..+0x2C`
  - pointer/state fields at `+0x30..+0x56`
- calls:
  - `+0x3FD8->virtual +0x0`
  with:
  - `rcx = outer_player+0x3FD8`
  - `rdx = local packet builder object`
  - `r8d = 0xC8`
  - `r9 = outer_player + 0x4260`

Immediately after that, the same path allocates and appends typed packet
records into the same builder:

- one record with type `0x08`
  - vtable `0x142B352E0`
  - payload includes:
    - `builder+0x78`
    - current owner-derived object pointer
    - owner state id
- one record with type `0x04`
  - vtable `0x142B35140`
  - same `builder+0x78`
  - same owner-derived object pointer/state id
  - plus one extra pointer field at `+0x48`

So `0x1410CF114` is no longer just “some init code near +0x3FD8”. It is a
concrete typed upper-manager bind packet builder over:

- `outer_player + 0x4260`
- a local packet list with typed entries
- and the upper manager at `+0x3FD8`

That makes it one of the best practical candidate surfaces for a future Nadine
promotion patch without replaying the whole ctor chain.

### The `0x1410CF114` Bind Builder Appends Typed `0x08` And `0x04` Entries Into A Real Packet List

The packet shape under the same callsite is now firmer.

After the initial `+0x3FD8->virtual +0x0` bind call, the builder:

- keeps a packet-list object in `r15`
  - count at `r15+0x28`
  - entry storage at `r15+0x30`
  - aggregate flags at `r15+0x48`
- appends a first typed record:
  - `type = 0x08`
  - vtable `0x142B352E0`
  - payload:
    - `+0x10 = builder + 0x78`
    - `+0x18 = owner-derived object pointer`
    - `+0x20 = owner state id`
    - `+0x40 = owner state id mirror`
- then appends a second typed record:
  - `type = 0x04`
  - vtable `0x142B35140`
  - payload:
    - same `builder + 0x78`
    - same owner-derived object pointer/state id
    - one extra pointer/value field at `+0x48`
- and raises aggregate bit `0x8` in `builder+0x48`

So the upper bind surface is now not just “manager plus packet”. It is a real
typed packet-list protocol with at least two known entry kinds already wired
into top-level `Player` bringup.

### `0x1410E9220` Uses `+0x3FD8` And `player+0xC0` As Formal Update Stages

The broad upper Player update path now reinforces the same surface.

At the top of `0x1410E9220`:

- if `outer_player + 0x3FD8` exists:
  - calls `manager->virtual +0x60`
- then, if `player + 0xC0` exists:
  - dereferences its inner object
  - calls inner `virtual +0x30`
- then continues into broader owner update work:
  - `0x140CC21C0`
  - owner `virtual +0xEA8`
  - lookup/use of `outer_player + 0x3BD0`

So both of the newly pinned layers:

- upper manager `+0x3FD8`
- live helper/state registry `player + 0xC0`

already participate in the formal upper Player update pipeline, not only in
construction.

### `0x1410F14C0` Continues The Retained Spawn Path Into Upper Publish/Apply Work

The retained commit path now reads even less like a terminal helper and more
like the front half of an upper Player bind/apply pipeline.

Confirmed continuation inside `0x1410F14C0`:

- it seeds a real `ProcessSpawnInfo` packet with vtable `0x14291CF98`
- then seeds a second compact object with local vtable `0x1428D5D60`
- then, when no external follow-up record is supplied, resolves a fallback
  record through `0x1417651D0`
- finally it continues into `0x141C88050` with:
  - one owner/runtime-selected string/value in `rdx`
  - one sibling selected string/value in `r8`
  - a boolean-like flag in stack arg `+0x20`
  - and a fixed upper string block at `0x142A4AF60` in `rcx`

The exact authored names on this branch still need pinning more carefully, but
the structural point is already useful:

`0x1410F14C0` does not stop at “copy retained pair into ProcessSpawnInfo”.
It keeps going into an upper published/apply record path, which keeps it in the
same promotion surface as the top-level `Player` bind chain rather than in a
spawn-only side branch.

### The Typed `0x08` / `0x04` Entries Under `0x1410CF114` Are `AnimSnapshotLayer::Setup` Variants

The two typed packet-entry classes under the upper `+0x3FD8` bind path now
have a hard semantic anchor from their `.rdata` blocks:

- `0x142B35140`
- `0x142B352E0`

Those blocks carry strings like:

- `AnimSnapshotLayer::Setup`
- `This variant of AnimLayer::Setup() is not intended to be used on AnimSnapshotLayer`
- `anim-snapshot-layer.cpp`

So the two entry vtables under the `0x1410CF114` bind packet are no longer
best read as abstract upper-Player packet records. They are concrete
`AnimSnapshotLayer` setup variants used by the upper Player init path.

The class-local helpers now line up cleanly:

- `0x141A77430`
  - relocates embedded pointers for the `0x04` entry
- `0x141A774B0`
  - reports whether the `0x04` entry has any attached sources
- `0x141A774E0`
  - assert/dispatch wrapper that tail-calls entry `virtual +0x58`
- `0x141A77550`
  - seeds the `0x04` entry with one primary source pointer
- `0x141A77580`
  - seeds the `0x04` entry with one secondary source pointer

and for the `0x08` variant:

- `0x141A77A20`
  - relocates embedded pointers
- `0x141A77A80`
  - readiness check on the attached source pointer
- `0x141A77AA0`
  - assert/dispatch wrapper that tail-calls `virtual +0x58`
- `0x141A77B50`
  - seeds the entry with its primary source pointer
- `0x141A77B90`
  - resolves a source-side authored record and appends it into the destination
    packet writer as a compact `0x0A000C`-shaped chunk

This materially changes the interpretation of `+0x3FD8`:

- it is still an important upper Player surface,
- but it now looks like an upper `AnimSnapshotLayer` / animation snapshot
  manager,
- not the final direct ownership/promotion manager for Nadine.

So `+0x3FD8` remains relevant for full player bringup and presentation, but the
primary promotion focus should move back toward:

- `0x1410DBF10 / 0x1410DB040`
- `0x1410DC160`
- `0x1410DE740 -> 0x140CC1450`
- retained/player integration through `0x1410F14C0 / 0x1410F1910 / 0x1413C04A0`

### `0x141E66D50 / 0x141E69AA0 / 0x141E69BD0` Are The Bind / Reset / Rebuild Phases Of The Upper `AnimSnapshotLayer` Manager

With the typed handlers identified, the three `+0x3FD8` manager phases are
also clearer.

`0x141E66D50`

- consumes the typed packet built around `0x1410CF114`
- resolves the fixed two-entry descriptor from `0x141E52590`
- builds manager-local handler arrays
- dispatches typed init work through `handler->virtual +0x10`

`0x141E69AA0`

- clears repeated per-entry counters and source pointers
- iterates the registered handlers in `manager + 0xD8`
- calls `handler->virtual +0xD8`

`0x141E69BD0`

- rebuilds a shared owner-derived 8-slot aggregate
- then pushes that aggregate back into handlers through `handler->virtual +0xE0`

So the upper `+0x3FD8` surface should now be read as:

- bind/register animation snapshot handlers during Player init,
- reset/invalidate them during the upper update loop,
- rebuild/publish their aggregate state during the second upper update phase.

This is still important for turning Nadine into a full Player, but it is a
presentation/animation layer, not the first place to force promotion.

### `0x1410DE740` Is Still The Strongest Direct `Player` Apply Bridge Above The Animation-Snapshot Layer

The higher finalize/apply wrapper remains cleaner and more ownership-facing
than the `+0x3FD8` animation manager.

Confirmed again from `0x1410DE740`:

- it first validates `outer_player + 0x3C20` through `0x1415A17E0`
- then calls:
  - `0x140CC1450(outer_player, external_state)`
- and finally does a trailing cleanup/leave call through `0x1415A1B80`

That makes `0x1410DE740 -> 0x140CC1450` the best current upper Player apply
surface once the animation-specific `+0x3FD8` branch is peeled away.

### `0x1413C04A0` Really Does Notify The Outer Owner Through `virtual +0x240` After A Valid Retained-Spawn Pair

The second retained-spawn caller is now fully confirmed in current disassembly.

Inside `0x1413C04A0`:

- it builds and publishes upper records through `0x141C88050`
- then calls `0x1410F1910`
- checks that the returned pair is non-null
- checks that `pair->id` matches the live object at `[pair->object]+0xBC`
- and only then calls:
  - `outer_owner->virtual +0x240`

So `0x1413C04A0` is not just “another caller of retained spawn”.
It is already a real upper notification bridge from a validated retained/live
pair back into an outer owner.

That keeps the retained integration track alive as one of the better places to
introduce Nadine into the real Player path without replaying the whole ctor
chain.

### The Retained Notify Bridge Now Clearly Sits On The `TaskManager::SpawnPlayer` / `TrySpawnPlayerAtContinue` Side Of The Upper Spawn Path

The upper retained branch is now better anchored by nearby literal strings.

For `0x1413C04A0`:

- the local publish path uses strings at:
  - `0x142A7DDD0` -> `TaskManager::SpawnPlayer(): playerPersisentState CONTAINS ...`
  - `0x142A7DD70` -> `TaskManager::SpawnPlayer`
  - `0x142A7DEB0` / `0x142A7DF40` -> `task-manager.cpp` checkpoint/task text

For `0x1410F1910`:

- one obvious string anchor is:
  - `0x142A4AF30`
  - `SpawnPlayer - Failed to find player spawner %s`
  - `TrySpawnPlayerAtContinue(): m_pistol=%s, m_longGun=%s, m_rope=%s, useCheckpointed=%d`

So the retained path is no longer best read as generic spawn glue. The current
upper split looks like this:

- `0x1410F1910` / `0x1413C04A0`
  - `TaskManager::SpawnPlayer` / continue-point side
- `0x1410DE740 -> 0x140CC1450`
  - real `Player`-side apply/finalize side

That is useful because it gives the future Nadine promotion patch a much more
concrete seam:

- either inject before Player bringup on the `TaskManager::SpawnPlayer` side,
- or inject after retained resolution on the direct Player apply side.

### `0x1410DE740` Is Now More Firmly A `player.cpp`-Side Finalize/Apply Wrapper

The wrapper itself now has stronger source-side anchors too.

Its validation literals point into:

- `0x142A4A2F0`
  - `player.cpp`
  - `Player::Player(void)`
- `0x142A4B580`
  - `Player::AllocateAnimData(...)`
  - nearby player-side authored/action strings

This does not change the current interpretation of the function, but it does
make the boundary much clearer:

- `0x1410F1910` / `0x1413C04A0` are on the TaskManager / continue / retained
  side
- `0x1410DE740 -> 0x140CC1450` are already on the real `Player` side

That boundary is now one of the most practical promotion seams in the whole
project.

### `0x140CC1450` Conditionally Promotes A `proto`-Tagged Upper State Through `player + 0x1A0`

One late branch in the top-level apply path is now concrete enough to keep.

After the main `0x1418C4840` apply/commit and the first follow-up refresh:

- `0x140CC1450` calls `outer_player->virtual +0x300`
- hashes the authored token at `0x1428D56DC`
- that token resolves to:
  - `proto`
- compares the resulting hash against the current value returned through the
  `+0x300` path
- and if it differs:
  - resolves a new record through `0x1417CD200`
  - stores it in `player + 0x1A0`
  - finalizes it through `0x1417CD3D0`

So `0x140CC1450` is not only:

- validating retained/live identity,
- copying accepted state into `player + 0x110`,
- and rebuilding transform blocks,

it also conditionally pushes a concrete authored upper state tagged by
`proto` into `player + 0x1A0`.

That is stronger evidence that `0x140CC1450` is a real upper Player apply path,
not a shallow transform-copy helper.

### `0x14291CF98` Is The Real `ProcessSpawnInfo` Vtable And `0x1410F14C0` Seeds A Concrete Packet Of That Type Before Upper Apply

RTTI now hard-pins the packet vtable used by the retained upper path.

`msvc_rtti_vtable.py` against `ProcessSpawnInfo` resolves:

- `ProcessSpawnInfo` vtable = `0x14291CF98`

Current raw layout around that vtable shows a compact method table beginning at:

- `0x14035C820`
- `0x14035C830`
- `0x14035C840`
- `0x14035C850`

That matters because `0x1410F14C0` does not merely copy a pair into an opaque
blob:

- it seeds a local object at `rbp-0x60`
- writes `vtable = 0x14291CF98`
- initializes it through `0x1417C32A0`
- later builds a second local packet layer and resolves a fallback through
  `0x1417651D0`
- and finally writes the resolved `(handle,id)` pair both:
  - into the global retained slot table at `0x143EEBE80`
  - and into the output pair at `[rdi] / [rdi+0x8]`

So the `external_state` consumed later by `0x1410DE740 -> 0x140CC1450` is no
longer best described as a vague “spawn/process state”. The strongest current
read is:

- real `ProcessSpawnInfo`-family packet,
- template-seeded inside `0x1410F14C0`,
- then handed into the upper Player apply path.

This is the first upper packet shape that already looks realistic for a future
promotion patch.

### `0x1413C04A0` Belongs To A Real Upper Retained-Notify Owner Family, Not A One-Off Helper

The `virtual +0x240` caller now looks less ad hoc when compared against the
nearby family around `0x1413BC7E0`.

From direct disassembly:

- `0x1413C04A0`
  - uses owner fields `+0x100`, `+0x1C9`
  - builds a large local retained/runtime snapshot around `rsp+0x50`
  - publishes it through `0x141C88050`
  - calls `0x1410F1910`
  - validates `pair->object`, `pair->id`, and `[pair->object]+0xBC`
  - then dispatches:
    - `outer_owner->virtual +0x240`

- sibling `0x1413BC7E0`
  - uses the same owner field `+0x100`
  - writes owner flags at `+0x1F0` and `+0x1E0`
  - calls:
    - `outer_owner->virtual +0x238`
    - `outer_owner->virtual +0x258`

So the current best reading is that this is not a random upper callback. It is
an upper retained/runtime owner family with at least three real notify/update
slots:

- `virtual +0x238`
- `virtual +0x240`
- `virtual +0x258`

That makes `0x1413C04A0 -> virtual +0x240` even more interesting as a future
promotion bridge:

- it is already above raw retained spawn,
- it already validates live runtime identity,
- and it already enters a formal outer-owner virtual surface instead of a
  one-off helper path.

### The TaskManager-Family `virtual +0x238 / +0x240 / +0x258` Surface Is Real, But It Is Not The Direct Player-Promotion Hook

RTTI and direct vtable inspection now pin the outer-owner family much more
precisely:

- `TaskManager` vtable = `0x142904C78`
  - `virtual +0x238` -> `0x1413C3310`
  - `virtual +0x240` -> `0x1413BF7B0`
  - `virtual +0x258` -> `0x14029C6B0`
- `TaskManagerPlayMovie` vtable = `0x142A7CB70`
  - `virtual +0x238` -> `0x1413C3310`
  - `virtual +0x240` -> `0x1413BF7B0`
  - `virtual +0x258` -> `0x1413BCAE0`

This confirms that the `0x1413C04A0` family really is sitting on a formal
TaskManager-family upper virtual surface, not an accidental callback. But the
slot semantics are narrower than “general player promotion”.

#### `virtual +0x240` -> `0x1413BF7B0`

Direct disassembly shows that this slot:

- consumes the incoming packet field at `incoming + 0x14`
- reads the global retained slot table at `0x143EEBE80`
- validates live actor ids through `[object] + 0xBC`
- hashes the authored token:
  - `spawn-flashlight`
- later hashes a second authored token:
  - `turn-on-flashlight`
- publishes structured records into live actors through `virtual +0xD0`

So this is best read as a TaskManager-side retained/live flashlight publish
path. It sits on the same upper retained surface we care about, but it is not
itself the direct promotion entry for turning Nadine into a second Player.

#### `virtual +0x238` -> `0x1413C3310`

This sibling slot:

- one-time gates itself through process-global byte latches
- hashes the authored tokens:
  - `menu`
  - `main-menu`
- resolves/creates manager-side records through the `0x141CBA1B0 /
  0x141CBAC10 / 0x141CBACC0 / 0x141CBAE10` family
- then publishes a compact packet through `0x141CB95B0`

So `virtual +0x238` is better read as a one-time TaskManager menu/main-menu
bootstrap/publish path, not as a Player-ownership binder.

#### `TaskManagerPlayMovie virtual +0x258` -> `0x1413BCAE0`

The `TaskManagerPlayMovie` override at `+0x258` is also now concrete enough to
demote from “ownership candidate” to “play-movie/camera-side helper”:

- it touches global state around `0x143FE10D8`
- it drives `0x1417D5F50`
- it calls `0x1410F3DD0`
- and it sets `this + 0x1F0 = 1`

That makes it look like a specialized play-movie / retained-camera / overlay
activation path, not a general Player promotion hook.

#### Practical correction

This changes the seam analysis in one important way:

- `0x1413C04A0 -> virtual +0x240` still matters because it validates retained
  live identity on the TaskManager side,
- but the direct promotion candidate is no longer “whatever `virtual +0x240`
  does”,
- it is still the Player-side apply/finalize chain:
  - `0x1410DE740 -> 0x140CC1450`
  - plus the real `ProcessSpawnInfo` packet path
  - plus retained handoff through `0x1410F1910`

So the current best seam stays:

- TaskManager / retained / continue side:
  - `0x1410F14C0`
  - `0x1410F1910`
  - `0x1413C04A0`
- Player-side promotion/apply side:
  - `0x1410DE740`
  - `0x140CC1450`

### `0x1410DE740` Is A Thin, Stable Player-Side Finalize Wrapper Around The Real Apply Seam

Direct disassembly sharpens `0x1410DE740` further.

It does only three meaningful things before returning:

- logs/validates through `outer_player + 0x3C20` using:
  - `0x142A4A2F0` (`player.cpp` / `Player::Player(void)` block)
  - `0x142A4B580` (`Player::AllocateAnimData(...)`)
  - line/id `0x1291`
- calls:
  - `0x140CC1450(outer_player, external_state)`
- immediately runs the paired cleanup/finalize:
  - `0x1415A1B80`

So `0x1410DE740` is now best treated as a thin, stable Player-side
finalize/apply wrapper whose whole value is that it already sits on the real
Player side and feeds the stronger seam below it.

### `0x140CC1450` Now Exposes Concrete Owner-Side Preconditions And Concrete `ProcessSpawnInfo` Links

The top-level apply bridge is no longer just “accept some spawn state”.

#### Owner-side preconditions and selectors

Before it commits anything it uses several real Player fields:

- `player + 0x164`
  - shifted left by 2 and passed as the runtime-state slot kind to
    `0x14177F7F0`
- global gate `0x143E07473`
  - if set, and `player + 0x161A bit0` is set, the slot kind is forced to:
    - `0x80`
- `player + 0x108`
  - indexes the lookup table at `0x14386FD00`
- `player + 0x10C`
  - must match the candidate record field at `record + 0x50`

Only if that candidate table entry is present and the `+0x50` id matches
`player + 0x10C` does the function copy the accepted state into:

- `player + 0x110`

That means the upper apply seam is already identity-sensitive in a concrete
way. Nadine will eventually need valid upper ids here, not just a transform.

#### Concrete external packet links

The consumed `external_state` is also now firmer.

When `0x140CC1450` calls:

- `0x1418C4840(slot, external_state, built_transform_block, 2)`

the callee immediately consumes:

- `external_state + 0x40`
- `external_state + 0x48`
- `external_state + 0x58`

and seeds them into the runtime-state slot as:

- `slot + 0x20 <- external_state + 0x40`
- `slot + 0x130 <- external_state + 0x48`
- `slot + 0x138 <- external_state + 0x58`
- `slot + 0x18 <- external_state`
- `slot + 0x1A0 <- external_state`

Then it seeds the spatial block:

- `slot + 0x1C0 .. +0x1F0`

from the temporary transform block built by `0x140CC1450`, and initializes a
subobject at:

- `slot + 0x200`

So the seam is now concrete enough to phrase as:

- accepted top-level Player identity/state from `player + 0x108/+0x10C`
- plus three packet links at `external_state + 0x40/+0x48/+0x58`
- plus the rebuilt transform/state block

rather than “some vague ProcessSpawnInfo payload”.

#### Practical promotion implication

This makes the future runtime patch target narrower:

- the TaskManager side only has to hand us a valid retained/live pair and real
  `ProcessSpawnInfo`-family packet;
- the actual promotion problem on the Player side is making Nadine satisfy the
  upper owner ids/flags used by `0x140CC1450`, then feeding the same packet
  links that Chloe’s path already consumes.

### `0x1410F14C0 / 0x1410F1910` Already Seed Concrete Player-Facing Packet Layers, Not Just A Generic Spawn Blob

The retained side is also more concrete now.

#### `0x1410F14C0`

Direct disassembly now shows that the lower retained-commit helper:

- seeds a real stack object with vtable:
  - `0x14291CF98` (`ProcessSpawnInfo`)
- hashes the authored token:
  - `Player`
- initializes that packet through:
  - `0x1417C32A0`
- and, when the direct incoming branch is absent, resolves a fallback through:
  - `0x1417651D0`
  - using the authored token:
    - `*player-health-settings*`

So the first retained-side packet layer is already explicitly Player-facing and
already knows about Player health state, not just raw retained identity.

#### `0x1410F1910`

The higher retained-side caller then adds another concrete authored layer:

- it still delegates core packet seeding to:
  - `0x1410F14C0`
- but the surrounding path also resolves/publishes:
  - `*player-starting-weapons*`
- and carries the exact continue/checkpoint log string:
  - `TrySpawnPlayerAtContinue(): m_pistol=%s, m_longGun=%s, m_rope=%s, useCheckpointed=%d`

Together this means the upper retained branch is already assembling at least
three real Player-facing packet concerns before the Player-side apply seam:

- `Player`
- `*player-health-settings*`
- `*player-starting-weapons*`

That is a stronger shape for a future promotion patch than “some spawn blob”.
It suggests the TaskManager side is already packaging the same categories of
state we would need Nadine to inherit if we want a true second Player path.

### `0x1410DB040 / 0x1410DBF10` Are Now Concrete Upper Gesture/Verb Permit Gates, Not Just Generic Preflight

The upper validation pair can now be read much more concretely.

#### Incoming record shape

`0x1410DBF10` now exposes the exact small adapter packet:

- `record + 0x0`
  - live actor pointer
- `record + 0xC`
  - minimum accepted result threshold, or `-1` as a wildcard
- `record + 0x10`
  - optional out pointer for the chosen failure-reason token

It calls `0x1410DB040`, and:

- accepts immediately if the validator returns `-1`
- accepts if `record + 0xC` is `-1`
- otherwise accepts only when:
  - `returned_result >= record + 0xC`
- and on rejection writes the chosen reason token back through
  `record + 0x10`

So this is not a vague helper anymore. It is a real upper permit adapter for a
Player action/gesture gate.

#### Concrete blocker reasons

Direct disassembly of `0x1410DB040` now yields the actual reasons it can
publish while evaluating the current Player owner:

- `cinematic is playing`
- `player has two-handed weapon in hands`
- `player is aiming`
- `current player movemode does not have :enable-gestures #t`
- `gesture does not have :allow-during-run #t`
- `Player State does not permit gestures`
- `Player Grapple does not permit gestures`
- `Player Weapon Switch does not permit gestures`
- `Strafe Set doesn't allow idle gesture`
- `player has weapon in hands`
- `player has flashlight/torch/flare`
- `player is in an IGC`
- `player not in a good locomotion state`
- `player is crouched`
- `character is in melee`

And the gating itself is no longer generic either. It inspects concrete mounted
Player subsystems across at least:

- `player + 0x1A0`
- `player + 0x1D88`
- `player + 0x1D50`
- `player + 0x1D48`
- `player + 0x1E58`
- `player + 0x1FF0`
- `player + 0x3D98`

This matters for Nadine because it reframes the problem:

- promotion is not only about getting her into the top-level Player apply seam;
- she also has to pass the same upper verb/gesture blockers that Chloe does;
- and now those blockers are concrete enough to use as a checklist for future
  runtime validation.

### `0x140CBFB30` Is The Shared Base Ctor Beneath The Real `Player` Vtable, And It Still Does Not Seed `player+0x108/+0x10C`

Direct disassembly of the base ctor called from the real in-place `Player`
constructor tightens one important constraint.

`0x1410C8FD0` begins by calling:

- `0x140CBFB30`

and only after that stamps the real `Player` vtable `0x142A4BFE8`.

Inside `0x140CBFB30`:

- the temporary base vtable is `0x142A0B960`;
- the broad shared gameplay/action band is zeroed, including:
  - `+0x15B4`
  - `+0x15C8`
  - `+0x15D0`
  - `+0x15D8`
  - `+0x15F0`
  - `+0x1600`
  - `+0x1680/+0x1684`
  - `+0x1748`
  - `+0x1808`
  - `+0x1B10`
- the embedded object at `+0x1A20` is also initialized and `+0x1B10` is
  explicitly cleared again after that.

What it does **not** do in any direct visible way is seed:

- `player + 0x108`
- `player + 0x10C`

That removes one more false lead:

- the upper descriptor/id pair consumed later by `0x140CC1450` is not coming
  from the visible `Player`-only ctor prologue;
- it has to arrive through a different shared/base or retained/apply path.

### `0x140CBFF50` Already Consumes A Small Upper Init Packet And Seeds Shared Player Policy State

The next shared/base-stage init helper below the visible `Player` init is now
much more concrete.

Inside `0x140CBFF50`:

- the incoming third argument is dereferenced at `arg3 + 0x28`;
- the resulting small record is copied into shared Player state:
  - `record + 0x0 -> player + 0x15A4`
  - `record + 0x0 -> player + 0x15A8`
  - `record + 0x4 -> player + 0x15AC`
  - `record + 0x8 -> player + 0x15B0`
  - `record + 0xC bit0 -> merged into player + 0x161A`
- this same stage also allocates/builds `player + 0x1B10`;
- then runs the shared follow-up through:
  - `0x141E994C0`

The same path also seeds or refreshes broader shared/base state around:

- `+0x16A0`
- `+0x1680`
- `+0x16AC`
- `+0x16D0`
- `+0x1A8/+0xBC`

So this helper already looks like a real shared upper/base init stage that
eats a compact incoming packet and turns it into Player policy/state, not just
dead ctor scaffolding.

### Practical Constraint: The Visible `Player` Stages Still Don’t Directly Write `+0x108/+0x10C`

Across the currently visible top-level chain:

- `0x1410C8FD0`
- `0x1410DC160`
- `0x1410CF370`
- `0x1410CF470`

the authored descriptor/id pair later consumed by `0x140CC1450` at:

- `player + 0x108`
- `player + 0x10C`

still does not appear as a direct, explicit Player-only write in the visible
disassembly.

That sharpens the next search:

- stop expecting a simple `mov [player+0x108]` inside the visible `Player`
  stages;
- treat `+0x108/+0x10C` as a shared descriptor/id pair that is probably seeded
  either:
  - before the visible `Player`-only init stages, through the shared/base path
    around `0x140CBFF50 / 0x141E994C0`,
  - or through the retained/apply handoff that eventually feeds
    `0x1410DE740 -> 0x140CC1450`.

### `0x140CBFF50` Seeds The Upper Player Band From A Shared External Runtime Source, But Still Not `player+0x108/+0x10C`

Direct disassembly of the first block inside `0x140CBFF50` sharpens the shared
base picture further.

When the global gate `0x143E07473` is enabled, the helper reads a shared source
record through:

- `0x143FE1088`

and copies a concrete slice of that source into the upper Player band:

- `source + 0xD8  -> player + 0x18B0`
- `source + 0xDC  -> player + 0x18B4`
- `source + 0xE0  -> player + 0x18B8`
- `source + 0xE8  -> player + 0x18C0`
- `source + 0xF0  -> player + 0x18C8`
- `source + 0xF4  -> player + 0x18CC`
- `source + 0xF8  -> player + 0x18D0`
- `source + 0x100 -> player + 0x18D8`
- `source + 0x108 -> player + 0x18E0`
- `source + 0x114 -> player + 0x18EC`
- `source + 0x118 -> player + 0x18F0`
- `source + 0x120 -> player + 0x18F8`
- `source + 0x128 -> player + 0x1900`
- `source + 0x138 -> player + 0x1910`

Then it continues through:

- `0x140BB1210`

and only after that returns to the compact packet work around
`player + 0x15A4/+0x15A8/+0x15AC`.

This matters because it eliminates another easy misread:

- the shared/base init really is pulling upper runtime/profile state from an
  external source;
- but even here the source field at `+0x108` goes to `player + 0x18E0`, not to
  the later apply pair at `player + 0x108/+0x10C`.

So the hidden pair consumed by `0x140CC1450` is still not explained by the
visible copy work inside `0x140CBFF50`.

### The Nearby `+0x108` Writes Found So Far Belong To Packet/Subobject Layouts, Not The Real Top-Level `Player`

The direct `+0x108` writes found around the upper promotion band are now pinned
well enough to classify them.

They are **not** the top-level `Player` pair that `0x140CC1450` later reads.

1. `0x1410DE520`

- zeroes the sibling owner-local runtime object stored at `outer_player+0x3BD0`;
- its local field at `object + 0x108` belongs to that fixed owner-local object,
  not to the top-level `Player`.

2. `0x140CC1080/0x140CC117E` region

- builds a large local structured packet on the stack;
- zeros local fields at `rbp + 0x108`, `+0x118`, `+0x128`, `+0x138`, etc.;
- publishes it through:
  - `0x1417F8B00`
  - and actor-side `virtual +0xD0`
- therefore this is packet layout, not a write into the live `Player` object.

3. `0x1410F21AF/0x1410F2334` region

- builds another large local structured packet at `rbp + 0xC0`;
- zeros local fields at `rbp + 0x108`, `+0x118`, `+0x128`, `+0x138`, etc.;
- this path sits directly above:
  - `0x1410F14C0`
  - `0x1417F8910`
- so it is retained/continue packet shaping, not a direct write into the live
  `Player`.

That narrows the search again:

- the visible `+0x108` writes near the upper Player/rebind area are false
  friends;
- the real `player + 0x108/+0x10C` pair used by `0x140CC1450` still appears to
  arrive through a more indirect handoff than a simple nearby field store.

### `player+0x108/+0x10C` Is A Shared Upper Authored-Record Key, Not A One-Off Field Used Only By `0x140CC1450`

The same lookup/refresh pattern used by `0x140CC1450` now shows up repeatedly
through live upper Player code.

Confirmed repeated readers include at least:

- `0x1410D10D5`
- `0x1410D171D`
- `0x1410D3575`
- `0x1410D4F45`
- `0x1410D505C`
- `0x1410D5123`

All of them do the same core sequence:

1. read:
   - `player + 0x108`
   - `player + 0x10C`
2. resolve a shared record from:
   - `0x14386FD00`
3. validate:
   - `record + 0x50 == player + 0x10C`
4. on success, copy:
   - `record[0..0x1F] -> player + 0x110`

So the pair is not just a local `ProcessSpawnInfo` apply detail. It is a
shared upper authored-record key that many later Player helpers rely on before
doing their own spatial/math/update work.

That makes the promotion requirement stricter:

- Nadine does not merely need to survive one call to `0x140CC1450`;
- she needs the same stable `+0x108/+0x10C` key that the rest of the upper
  Player helper family expects to keep re-resolving afterward.

### `0x1410F14C0` Resolves A Retained/Live Pair Through `0x1417C32A0 -> 0x1417C3790`, So The Final Player `external_state` Likely Lives In That Materialized Object Family

The retained side is now sharper too.

Inside `0x1410F14C0`:

- a local `ProcessSpawnInfo`-class packet is stamped with vtable:
  - `0x14291CF98`
- then initialized through:
  - `0x1417C32A0`
- and later resolved/materialized through:
  - `0x1417C3790`

`0x1417C32A0` now reads as a real compact packet initializer:

- `packet + 0x10 <- incoming r8`
- `packet + 0x18 <- global default`
- `packet + 0x20 <- incoming r9`
- `packet + 0x28 <- stack arg`
- `packet + 0x30 <- stack arg`
- `packet + 0x38 <- 0`
- `packet + 0x48 <- selector/hash id`
- `packet + 0x50 <- resolved selector/capability entry`
- `packet + 0x40 <- derived mask/limit from that selector entry`

`0x1417C3790` then validates and materializes from that packet:

- requires `packet + 0x50`
- checks selector/capability through `0x1417C4800`
- builds a returned object family
- stores back-links and manager links
- repeatedly copies packet payload blocks from:
  - `packet + 0x20...`
  - `packet + 0x40...`
  into the returned object
- and returns an object whose:
  - `+0xA8`
  - `+0xBC`
  are exactly the retained/live pair later harvested by `0x1410F14C0`

That changed the retained/apply picture, but only provisionally. The next pass
sharpened it further.

### The Extended Wrapper Family At `0x1417C35A0 / 0x1428D5D60` Matches The Upper `external_state` Shape Better Than The Returned Object From `0x1417C3790`

Direct disassembly now makes the packet/object split clearer.

#### `0x1417C35A0`

The constructor/initializer at `0x1417C35A0`:

- stamps local vtable `0x1428D5D60`;
- immediately calls `0x1417C32A0` for the shared packet base;
- then seeds wrapper-local fields at:
  - `+0x58`
  - `+0x60`
  - `+0x68`
  - `+0x70`
  - `+0x78`
  - `+0x80`
  - `+0x88..+0x98`

This matters because the inherited base still carries the exact upper packet
band that `0x1418C4840` later consumes:

- `+0x40`
- `+0x48`
- `+0x58`

So the extended wrapper family is now the best direct structural match for the
Player-side `external_state` contract.

#### `0x1417C3790`

The retained/materialize path still matters, but it now reads more narrowly:

- `0x1417C3790` consumes the selector-backed packet/wrapper;
- performs selector/capability gating;
- allocates/builds a materialized runtime object;
- writes:
  - `returned + 0x28`
  - `returned + 0x30`
  - `returned + 0x38`
  - `returned + 0xA0`
  - `returned + 0xA8`
  - `returned + 0xB8`
  - `returned + 0xC0`
  - `returned + 0xE0`
- and only then exposes retained/live identity through:
  - `returned + 0xA8`
  - `returned + 0xBC`

So the returned object is now better read as a materialized runtime companion
to the packet, not the final upper Player apply packet itself.

#### `0x1415FC800 / 0x1415FC9F0`

The helpers called directly inside `0x1417C3790` are now classified too:

- `0x1415FC800`
  - inserts or relinks intrusive nodes into an indexed owner-local container;
  - updates owner fields around `+0x70/+0x80/+0x98/+0xA8`;
- `0x1415FC9F0`
  - unlinks the same nodes and decrements that same owner-local count.

These are internal bookkeeping helpers for the materialized runtime object
family, not direct Player promotion/apply helpers.

#### Cross-check Against `0x1418C4840`

The upper generic apply path at `0x1418C4840` still consumes:

- `external_state + 0x40`
- `external_state + 0x48`
- `external_state + 0x58`

and stores them into the runtime-state slot at:

- `slot + 0x20`
- `slot + 0x130`
- `slot + 0x138`

That shape matches the extended wrapper family from `0x1417C35A0` much better
than the materialized returned object from `0x1417C3790`.

#### Updated Promotion Reading

So the retained/apply picture is now:

- `0x1410F14C0` is still primarily a retained/live pair resolver/committer;
- the returned object from `0x1417C3790` is still important, but mostly as a
  materialized runtime object whose pair at `+0xA8/+0xBC` is harvested;
- the strongest current candidate for the eventual upper Player
  `external_state` family is now the extended wrapper packet family at:
  - `0x1417C35A0`
  - `vtable 0x1428D5D60`

That is a better runtime-patch target than the tiny final pair written back by
`0x1410F14C0`, and a better direct shape match for
`0x1410DE740 -> 0x140CC1450 -> 0x1418C4840` than the returned object produced
by `0x1417C3790`.

### The Same `0x1417C35A0` Wrapper Is Already Accepted By The Generic Entity Runtime Command Interface

The next pass tightened this further.

`0x141D21C80` (`DispatchRetainedEntityRuntimeCommand_0x538`) now reads as:

1. validate retained/runtime pair at `object + 0x60`
2. resolve the live entity
3. build a temporary runtime descriptor through:
   - `0x1417C32A0`, or
   - `0x1417C35A0`
4. call live-entity `virtual +0x538` with that descriptor

The extended wrapper path is explicit:

- local vtable is switched to `0x1428D5D60`
- wrapper-local fields are zeroed/initialized
- then the wrapper is passed directly into entity `virtual +0x538`

So `0x1417C35A0` is not only structurally similar to the upper Player
`external_state` family. It is already a real accepted packet on the generic
entity-side runtime command interface.

Two additional callsites strengthen that reading:

- `0x1413BF090`
  - resolves runtime state through `0x141B39750`
  - builds a `0x1417C35A0` wrapper
  - immediately calls `0x141D21C80`
- `0x1414D1DD2`
  - in its live-entity branch, builds the same `0x1417C35A0` wrapper
  - then continues through `0x1414CE220`

That changes the practical conclusion again, in a good way:

- the returned object from `0x1417C3790` still matters for retained/live pair
  materialization;
- but the strongest current runtime-patch surface above that seam is now the
  extended wrapper family `0x1417C35A0 / 0x1428D5D60`, because:
  - it matches the `0x1418C4840` field shape better,
  - and it is already accepted by generic entity runtime verb dispatch.

### Correct `Player` Vtable Mapping Tightens The Upper Promotion Surface

A proper PE-backed read of the real `Player` vtable `0x142A4BFE8` corrected a
few stale slot assumptions.

Confirmed slots now are:

- `+0x58  -> 0x1410DC160`
- `+0x300 -> 0x141BB8BC0`
- `+0x6F0 -> 0x1410DE740`
- `+0x710 -> 0x1410CE260`
- `+0x718 -> 0x1410CF370`
- `+0x738 -> 0x140CC18D0`
- `+0x850 -> 0x1410DE890`

This matters because the old quick slot read had started to smear together a
few unrelated helpers. The promotion search is now anchored to the actual
upper `Player` virtual surface, not to guessed proxy targets.

### `Player::InitializeDrawControl` (`0x1410CF370`, vtable `+0x718`) Is The First Clear Upper `Player`-Side Consumer Of The Extended Wrapper

Direct disassembly shows `0x1410CF370` taking:

- `rcx = Player *`
- `rdx = ProcessSpawnInfo-like state/result pointer`
- `r8  = extended wrapper packet`
- `r9  = draw/control context`

The function:

- links `player + 0x1B10` to the incoming context through `0x141E60E80`;
- logs via `player + 0x3C20`;
- immediately forwards the same `(player, state, wrapper, context)` tuple into
  `0x141BBACC0`;
- ORs companion masks `0x200` and `0x800` through the player companion object
  at `+0x198`;
- clears local draw-control bands `+0x3B0/+0x3B8/+0x3C0`;
- calls `0x141BCCED0`;
- returns the incoming second argument.

So the extended wrapper family `0x1417C35A0 / 0x1428D5D60` is no longer only:

- a retained-side candidate for `0x140CC1450`, or
- a generic entity-runtime command packet through `0x141D21C80`.

It is already a real upper `Player`-side packet consumed from the real
`Player` vtable.

### `0x141BBACC0` Is A Real `Player`-Side Wrapper/Context Binder, Not Just A Companion Helper

The same pass sharpens `0x141BBACC0`.

Fed from `Player::InitializeDrawControl`, it:

- treats the third argument as a wrapper packet and checks
  `wrapper + 0x10` / `wrapper + 0x30` through `0x1417C4800`;
- reads mode/count/state from the fourth argument context
  (`context + 0x15`, `+0x18`, `+0x30`, `+0x130`);
- mutates player companion/focking flags through `player + 0x198`;
- iterates context-driven entries and dispatches child-node flag changes
  through `0x141754460 / 0x141754940`.

That makes it an upper `Player` behavior binder over the wrapper/context pair,
not merely a local companion camera-rig helper.

### `Player` Virtual `+0x300` Is A Compact Current-State Getter, Not The Main Apply Hook

The actual `Player` vtable slot `+0x300` is `0x141BB8BC0`.

It does only this:

- if `player + 0x190 != 0`, resolve the current live actor through
  `0x1417D1170`;
- read `live_actor->+0x18->+0x10`;
- write that dword to the caller-provided output;
- otherwise write zero.

This is important because `0x140CC1450` calls `player->virtual +0x300` before
the late `proto` path, but that call is only harvesting a compact current
upper-state id. It is not itself the top-level promotion/apply seam.

### `Player` Virtual `+0x738` Is A Narrow Context Seed Into `player + 0x15B4`

The real slot `+0x738` is `0x140CC18D0`.

It is tiny:

- if the incoming context pointer is non-null, copy `context + 0x58E0` into
  `player + 0x15B4`;
- otherwise clear `player + 0x15B4`.

So the `+0x738` call from `0x1410FE840` is a narrow upper-context seed helper,
not a broad promotion/apply hook.

### `Player` Virtual `+0x850` Is An Upper Rebuild Path That Publishes A Retained Pair Into `player + 0x3828`

The real slot `+0x850` is `0x1410DE890`.

It is a heavy upper rebuild/update path that:

- checks action-policy state through `player + 0x1D88`;
- drives sibling `Player` owners such as `+0x1E50`;
- resolves authored record families through the weapon-art registry;
- builds a small wrapper packet;
- runs it through `0x1417C3790`;
- writes the resulting retained/live pair back into `player + 0x3828`.

This makes `+0x850` relevant to full upper `Player` rebuild/state refresh, but
still downstream of the main direct apply seam:

- `0x1410DE740 -> 0x140CC1450`.

### Real `Player` Upper Rebuild Order Now Separates Context Seeding, State Materialization, And Wrapper Binding

Direct disassembly of the real `Player` rebuild path `0x1410DE890`
(`vtable +0x850`) is now concrete enough to separate three upper stages that
had previously started to blur together:

1. it seeds narrow upper context through the real `Player` slot `+0x738`
   (`0x140CC18D0`);
2. it resolves and attaches the active upper state object through the real
   `Player` slot `+0x6F0` (`0x1410DE740 -> 0x140CC1450`), storing the result
   into `player + 0x1A8`;
3. only after that it builds an extended wrapper packet
   `0x1417C35A0 / vtable 0x1428D5D60` and feeds it into the real `Player`
   slot `+0x718` (`0x1410CF370`).

That is a useful correction for the promotion search:

- `+0x718` is a real upper `Player` wrapper/context bind phase, but not
  enough by itself;
- the rebuild path still needs the active state object brought up through
  `+0x6F0`;
- the direct apply seam `0x1410DE740 -> 0x140CC1450` remains the only pinned
  place where the stable authored pair `player + 0x108/+0x10C` is consumed.

So a real Nadine promotion patch will likely need to satisfy at least:

- the upper preflight gate,
- the active-state materialization path around `+0x6F0`,
- and the later wrapper/context bind path around `+0x718`.

### A Sibling Upper Consumer Also Uses `+0x718` Output To Validate The Same Authored Pair Table `0x14386FD00`

The upper caller around `0x14133939D` is not yet pinned to a clean public
class name, but its behavior is already useful:

- it calls an upper slot `+0x718` on the owner at `+0x1DA0`;
- then pulls a pair into local stack state through sibling virtual helpers;
- then validates that pair through the same authored-record table:
  - `0x14386FD00`
  - `record + 0x50`

That matters because `0x140CC1450` uses the same table and id match against
the real `Player` pair `+0x108/+0x10C`.

So even though this sibling caller is not itself the real `Player` apply seam,
it strengthens the model that the upper wrapper/bind family and the stable
authored pair table are part of one coherent upper state system, not two
unrelated subsystems.

### A Direct `.text` Writer Scan Still Shows No Simple Top-Level Writer For `player + 0x108/+0x10C`

A direct instruction scan over `.text` for memory-destination writes at
displacements `+0x108` and `+0x10C` still does not surface a clean top-level
`Player` writer in the upper init/apply family.

The direct hits remain dominated by:

- local stack temporaries;
- unrelated object families writing their own `+0x108/+0x10C` fields;
- and vtable `call [obj + 0x108]` sites unrelated to the real `Player`
  authored pair.

This strengthens the existing conclusion:

- the real `Player` pair consumed later by `0x140CC1450` is probably seeded
  through a deeper shared/base handoff or packet copy,
- not by an obvious one-line field assignment inside the visible
  `Player`-only init stages.

### Real `Player` Slots `+0x700/+0x708/+0x710` Form A Distinct Upper State-Build / Publish Band Around `player+0x1A0/+0x1A8`

Direct disassembly now makes the band above the already-pinned `+0x6F0` apply
seam much clearer.

The real `Player` slot `+0x700` is `0x141BBA490`.

It does not merely allocate a helper:

- allocates a `0x2B0` object through `0x1415A0E90`;
- initializes it through `0x141728690`;
- stores it into `player + 0x1A0`;
- links `player + 0x1A8 -> +0x10 = player + 0x1A0`;
- optionally gates through `player->virtual +0x760`;
- if the gate passes, queries `player->virtual +0x708`;
- then feeds:
  - `player + 0x1A0`,
  - `player + 0x1A8`,
  - `player + 0x160`,
  - and the `+0x708` result
  into `0x141728720`;
- and only after that calls the real `Player` slot `+0x710`.

So `+0x700` is better read as:

- the entry point of an upper `Player` state-build/publish band,
- not as a one-off allocator.

The real `Player` slot `+0x708` is still trivial:

- `0x141E994B0` simply returns `0x3C`.

But in context that return value matters because it is consumed immediately by
`0x141728720` as part of the `player + 0x1A0` build.

### `0x141728690` / `0x141728720` Build The Upper State Owner Stored At `player + 0x1A0`

The `0x2B0` object stored at `player + 0x1A0` is now concrete enough to treat
as a formal upper state owner.

`0x141728690`:

- zeroes the owner bands through `+0x70`;
- allocates a `0x200` scratch / entry region at `owner + 0x78`;
- clears the late tail through `+0x2AC`.

`0x141728720` then populates that owner:

- stores the active upper-state object in `owner + 0x20`;
- mirrors an optional retained/live pair into `owner + 0x38`;
- copies classifier/count fields from the incoming active-state descriptor into
  `owner + 0x214/+0x218`;
- synchronizes a live runtime object through `0x141780990`;
- forwards a compact state summary through `0x1418BBEB0`;
- allocates the per-entry table at `owner + 0x30` using the classifier/count
  passed from the `+0x708` stage.

This means `player + 0x1A0` is not just a late state blob:

- it is a built owner with its own entry table,
- fed from the active upper state at `player + 0x1A8`,
- and explicitly prepared before the heavy upper publish stage.

### Real `Player` Slot `+0x710` Publishes Overlay / Reference / Read-Joint State Before The Animation-Snapshot Stage

The real `Player` slot `+0x710`, `0x1410CE260`, now has a readable high-level
shape.

Early in the path it resolves and publishes authored state through the global
knowledge/state registry:

- resolves ids with `0x1417651D0`;
- resolves concrete records with `0x141765030`;
- publishes them through `0x1417F9150` into the sink at
  `[player + 0x1A8] + 0x60`.

The authored tokens on this stage include:

- `anim-net-player`
- `*player-overlay-priorities*`
- `*player-default-overlays-count*`
- `*player-default-overlays*`
- `apReference`
- `align`
- `run-chloe`
- `base-read-joints-layer`

It also builds / refreshes the upper owner at `player + 0x4260` and seeds
typed records through:

- `0x141728D90`
- `0x14172A720`
- `0x14172ACD0`

Only after that band is prepared does control continue into the already-known
animation-snapshot stage at:

- `player + 0x3FD8`.

So `+0x710` should now be treated as:

- a real upper overlay / reference / read-joint publish stage,
- still part of the top-level `Player` bringup,
- and structurally between the `+0x6F0` active-state materialization and the
  later `+0x718` wrapper/context bind.

### `+0x700/+0x708/+0x710` Are A Separate Upper Player Band, Not Yet Proven To Sit Inside The Real `+0x850` Rebuild Order

A direct vcall-site scan over `.text` produces an important correction:

- `Player +0x710` appears only once as a direct vcall, at `0x141BBA554`,
  inside the `Player +0x700` implementation itself;
- `Player +0x700` appears at two direct vcall sites:
  - `0x1413086BC`
  - `0x141BB4BEE`
- `Player +0x718` still appears at:
  - `0x1410FEC2F`
  - `0x14133939D`
  - `0x141BB4D13`

So the new upper state-owner band around:

- `player + 0x1A0`
- `player + 0x1A8`
- `player + 0x4260`
- `Player +0x700/+0x708/+0x710`

is very real, but it should not yet be folded into the already-pinned
`Player +0x850` rebuild ordering.

The conservative read is now:

- `+0x850` still gives the best proven rebuild order:
  - `+0x738`
  - `+0x6F0`
  - `+0x718`
- while `+0x700/+0x708/+0x710` form a separate upper Player state-build/publish
  band that reuses the same `+0x1A0/+0x1A8` region and is invoked by other
  upper callers.

That is still useful for Nadine promotion:

- the direct apply seam remains `0x1410DE740 -> 0x140CC1450`;
- but there is now a second concrete upper bringup band that may also need to
  be satisfied before overlays/reference-joint/presentation behavior matches a
  real Player.

### A Sibling Upper Orchestrator Already Orders `+0x738 -> +0x6F0 -> +0x700 -> +0x718` In One Place

The function around `0x141BB4AA0` is now worth treating as a real upper
orchestrator, even though its public class name is still not pinned.

What it does in order:

- calls `player->virtual +0x738`;
- caches the resulting compact state dword into `player + 0x170`, and may seed
  `player + 0x190`;
- resolves an authored record through `0x1417CD080`;
- calls `player->virtual +0x6F0` and stores the result into `player + 0x1A8`;
- immediately calls `player->virtual +0x700`;
- then later calls `player->virtual +0x718`.

So even though the already-pinned `Player +0x850` rebuild path is still only
proven to order:

- `+0x738`
- `+0x6F0`
- `+0x718`

we now have a separate upper caller that explicitly threads the larger band:

- `+0x738`
- `+0x6F0`
- `+0x700`
- `+0x718`

through one continuous upper path.

This is operationally useful because it gives a second concrete place to study
how the active-state materialization, upper state-owner build, and later
wrapper/context bind are chained together above the mounted graph.

The broader owner block around that sequence is also now less abstract:

- it is surrounded by authored tokens such as:
  - `ndgo-look-resolve`
  - `ndgo-anim-control`
  - `ndgo-draw-control`
  - `ndgo-draw-control-finalize`
  - `splasher-set`
  - `*splasher-init*`
  - `allow-player-to-target`
- and it sits beside the source-path string:
  - `NdGameObject::Init(const ProcessSpawnInfo &)`

So this sibling lifecycle block is not floating free. It already sits inside a
broader `NdGameObject` / `ProcessSpawnInfo`-anchored upper stage, which makes
it more promising as a future promotion/rebind patch surface than a random
mid-level helper chain would be.

### `Player +0x700` Is Reused By Another Upper Consumer Outside That Sibling Orchestrator

A second direct vcall site at `0x1413086BC` confirms that `Player +0x700` is
not only an internal helper of one upper orchestrator.

That second caller:

- invokes `player->virtual +0x700`;
- immediately invokes `player->virtual +0x708`;
- then continues into `player +0x1E98 -> virtual +0x190`.

This matters because it links the upper `+0x700/+0x708` state-owner band back
into the already pinned mounted Player subsystem at:

- `player + 0x1E98`

which we had earlier mapped as a strong investigate/read-mode-style Player
owner.

So the new upper state-owner band is not isolated:

- it is reused by at least two separate upper callers,
- and one of them already continues into a mounted Player verb subsystem.

### The Same Sibling Upper Lifecycle Continues Through `+0x6F8`, `+0x768`, And `+0x4A8`

The block around `0x141BB4AA0` does not stop at:

- `+0x738`
- `+0x6F0`
- `+0x700`
- `+0x718`

It continues into additional real `Player` slots:

- `+0x6F8 -> 0x141BBA2A0`
- `+0x768 -> 0x1401D48D0`
- `+0x4A8 -> 0x140CC17A0`

These are now concrete enough to matter.

`Player +0x6F8`:

- consumes an incoming upper payload record;
- copies vector/scalar fields into `player + 0x1A8` across
  `+0x150..+0x194`;
- derives a delta vector;
- checks a selector/capability through `player + 0x30` via `0x1417C4800`;
- and on success raises the caller-side helper flag at `+0x2D0`.

So `+0x6F8` looks like an upper active-state payload applicator, not a generic
post-hook.

`Player +0x768`:

- is just a trivial `return true` gate.

`Player +0x4A8`:

- writes an effective upper state-kind dword to the caller-provided output;
- in the ordinary case returns `player + 0x174`;
- under the global gate at `0x143E07473`, instead hashes the `Player` token
  and returns that override.

This sharpens the sibling upper lifecycle:

1. seed narrow context through `+0x738`
2. materialize/attach active upper state through `+0x6F0`
3. build/publish the upper `+0x700/+0x708/+0x710` state-owner band
4. bind wrapper/context state through `+0x718`
5. apply upper active-state payload through `+0x6F8`
6. run later permit/state-kind gates through `+0x768` and `+0x4A8`

This is not yet a full Nadine promotion recipe, but it is already much closer
to a concrete upper runtime patch sequence than the older “find one hook”
model.

### Real `Player` Slot `+0x740` Is A Draw-Control Ambient-Occluder Resolver, Not A Promotion Surface

Hex-Rays on `0x141BCD340` and direct callsite disassembly now pin real
`Player` slot `+0x740` much more tightly.

The function:

- hashes and resolves authored ids around:
  - `character-ambient-occluder-array`
  - `ambient-occluders`
  - `*characters-ambient-occluders-map*`
- stores the selected result in `player + 0x180`;
- can refresh the draw-control object rooted at `player + 0x198` through
  `0x1417567C0`;
- then scans draw-control mesh records and updates ambient-occluder usage
  through:
  - `0x141756570`
  - `0x141755B10`

The three currently pinned callsites also fit this reading:

- `0x140DE249D`
  - asks `player+0x1DA0 -> virtual +0x740` for a local result block, then
    continues in a larger visual/gameplay state path;
- `0x14132FDEC`
  - asks the same slot for a small temporary block before building a later
    runtime object;
- `0x141BB50D8`
  - calls `+0x740` from inside the sibling upper lifecycle after `+0x6F8`.

So `+0x740` is no longer a plausible first-class promotion seam.
It is a real upper Player slot, but it belongs to draw-control / ambient
classification and mesh refresh, not to ownership bind.

### The Sibling Upper Orchestrator At `0x141BB4AA0` Lives Inside A Broader Player Draw-Control Finalize Stage

The block at `0x141BB4AA0` is not a standalone function entry.
IDA now confirms it sits inside:

- `0x141BB4280 .. 0x141BB5DA0`

The beginning of that containing function shows a broader Player-side setup
stage that:

- seeds authored ids at:
  - `player + 0x178`
  - `player + 0x17C`
- using tokens:
  - `look-collection-randomizer`
  - `look-randomizer`
- then threads the upper lifecycle through:
  - `+0x238`
  - `+0x738`
  - `+0x6F0`
  - `+0x700`
  - `+0x718`
  - `+0x6F8`
  - `+0x4A8`
  - `+0x740`

This containing stage also owns the point where the `+0x740` ambient-occluder
resolver is applied back into the Player draw-control region.

The practical consequence is important:

- `0x141BB4AA0` remains a strong upper lifecycle anchor;
- but it is now best treated as a block inside a broader Player draw-control
  finalization stage, not as an isolated ownership hook.

### The Block At `0x141308680` Lives Inside A Concrete Upper Verb Stage, Not A Generic Promotion Path

IDA now confirms the second upper consumer block at `0x141308680` lives inside:

- `0x141308220 .. 0x1413095B9`

The containing function hashes and compares authored tokens including:

- `edge-aim-reload`
- `idle-reach0-stamina`
- `idle-reach0`

Then it:

- gates through the Player qualification owner at `player + 0x1D88`;
- reuses the owner-local registry at `player + 0x3BD0`;
- drives:
  - `Player +0x700`
  - `Player +0x708`
- and only after that continues into the mounted Player subsystem at:
  - `player + 0x1E98`

So this second caller is valuable, but for a narrower reason:

- it proves the `+0x700/+0x708` upper state-owner band is reused by concrete
  upper Player verbs;
- it does **not** move the primary promotion target away from the top-level
  Player apply path.

### Current Promotion Focus Narrows Further

After these corrections, the upper Player map is cleaner:

- `+0x740` is draw-control / ambient-occluder work;
- `0x141BB4280` is a broader draw-control finalization stage containing the
  sibling lifecycle block;
- `0x141308220` is a concrete upper verb stage using `+0x700/+0x708`, but is
  still downstream of true promotion.

That leaves the main practical promotion surfaces still centered on:

- `0x1410DE740 -> 0x140CC1450`
- `+0x6F0`
- `+0x718`
- and the retained / `ProcessSpawnInfo` packet family above them

rather than on `+0x740` or the verb-specific upper consumers. This is useful
because it removes another plausible but incorrect upper hook from the short
list before runtime patching starts.

### `0x141BBACC0` Is A Downstream Upper Behavior / Draw-Control Binder, Not A Player-Identity Source

Direct disassembly of `0x141BBACC0` makes its role narrower than the earlier
"first clear upper Player-side consumer" phrasing.

In the real Player path, the function:

- receives the extended wrapper packet that `Player +0x718` passes in;
- selector-gates the wrapper through:
  - `wrapper + 0x10 -> +0x30`
  - `CheckSelectorHashEnabledInBitsetRegistry`
- mutates the companion/draw-control owner at:
  - `player + 0x198`
- toggles/propagates child state through:
  - `0x141754460`
  - `0x141754940`
- builds late owner arrays around:
  - `player + 0x188`
  - `player + 0x208`

So this stage clearly consumes upper Player wrapper state, but it does so as a
downstream behavior/draw-control binder.

The important negative result is:

- this is not where the stable authored id pair at `player+0x108/+0x10C` is
  born;
- and it is not a better promotion hook than `0x1410DE740 -> 0x140CC1450`.

### `Player +0x718` Is Now Better Classified As A Downstream Draw-Control / Behavior Bind Stage

`Player::InitializeDrawControl` (`0x1410CF370`) is still a real upper Player
stage, but the combined evidence is now tighter:

- it links `player + 0x1B10` to the incoming context through `0x141E60E80`;
- it forwards the extended wrapper packet into `0x141BBACC0`;
- it applies companion-object masks `0x200` and `0x800`;
- it clears local draw-control bands at:
  - `player + 0x3B0`
  - `player + 0x3B8`
  - `player + 0x3C0`

That means `+0x718` remains necessary in the upper Player lifecycle, but as a
downstream draw-control/behavior bind stage, not as the first place to inject
Nadine into real Player identity.

### `0x140CBFF50 -> 0x141E994C0` Is A Stronger Shared/Base Upper Handoff Than `+0x718`

The new strongest upstream correction is around the shared/base handoff:

- `0x140CBFF50`
- `0x141E994C0`

What `0x140CBFF50` now demonstrably does:

- copies compact packet state into:
  - `player + 0x15A4`
  - `player + 0x15A8`
  - `player + 0x15AC`
  - `player + 0x15B0`
- merges packet bit0 into `player + 0x161A`
- allocates/builds `player + 0x1B10`
- copies shared external state from `0x143FE1088` into:
  - `player + 0x18B0..+0x1910`
  - including `source + 0x108 -> player + 0x18E0`
- then immediately hands off into `0x141E994C0`

And `0x141E994C0` now reads as:

- seeding fixed upper defaults around:
  - `player + 0x1510`
  - `player + 0x1530`
  - `player + 0x1550`
- invoking `0x141BB4280` as the broader draw-control / upper-lifecycle stage
- continuing through `player->virtual +0x788`
- materializing:
  - `player + 0x4A8`
- allocating a fresh `0x2B0` upper owner family

This is important because it shifts the real upstream attention again:

- `+0x718` is downstream bind;
- `+0x740` is draw-control ambient classification;
- `0x140CBFF50 -> 0x141E994C0` is a stronger shared/base upper handoff into
  the real Player lifecycle.

That still does not directly expose the hidden `player+0x108/+0x10C` writer,
but it narrows the search region further upward and away from the
draw-control-specific stages.

### `0x140CBFF50` Really Is The Last Strong Shared/Base Handoff Before The Broader Upper Player Lifecycle

Another direct pass over `0x140CBFF50` makes its role firmer.

Before it calls `0x141E994C0`, it already:

- seeds the compact upper packet into:
  - `player + 0x15A4`
  - `player + 0x15A8`
  - `player + 0x15AC`
  - `player + 0x15B0`
- merges packet bit0 into:
  - `player + 0x161A`
- allocates/builds:
  - `player + 0x1B10`
- copies shared external state from `0x143FE1088` into:
  - `player + 0x18B0..+0x1910`
  - including `source + 0x108 -> player + 0x18E0`

Then it immediately hands off to:

- `0x141E994C0`

So the practical upper layering now reads as:

1. compact shared/base packet copy (`0x140CBFF50`)
2. broad upper Player handoff (`0x141E994C0`)
3. later draw-control / behavior bind (`+0x718 -> 0x141BBACC0`)
4. even later draw-control ambient classification (`+0x740`)

This is a better ordering than the earlier blended picture.

### `0x141E994C0` Starts The Broader Upper Player Lifecycle After The Shared/Base Packet

Direct disassembly of `0x141E994C0` is now strong enough to pin its place in
the upper stack.

The function:

- seeds fixed upper defaults around:
  - `player + 0x1510`
  - `player + 0x1530`
  - `player + 0x1550`
- calls `0x141BB4280`, which we already pinned as the broader draw-control /
  upper-lifecycle stage containing the sibling block `0x141BB4AA0`
- on the success path, continues through:
  - `player->virtual +0x788`
- later materializes:
  - `player + 0x4A8`
- and allocates a fresh `0x2B0` upper owner family

So `0x141E994C0` is not just “some helper after base init”.
It is the point where the compact shared/base packet is expanded into the
broader upper Player lifecycle.

This matters for Nadine because it puts the likely promotion neighborhood in a
tighter order:

- first the compact packet/base handoff must be valid;
- then the broader upper Player lifecycle starts;
- only after that do draw-control/behavior bind stages make sense.

That is a better patching model than trying to force Nadine directly into
`+0x718` or any later mounted verb owner.

### `player+0x108/+0x10C` Belong To An Embedded Authored-Descriptor Subobject Rooted At `player+0x100`

The hidden pair used by the upper Player apply seam is now structurally
clearer.

It is not best read as two loose top-level `Player` integers.
Instead:

- there is an embedded subobject rooted at:
  - `player + 0x100`
- `0x1410C9F20` is a small in-place initializer for that subobject:
  - writes vtable `0x142AB08C8` to `player + 0x100`
  - clears late fields around `+0x110/+0x122/+0x128`
  - but does **not** seed the authored key at `+0x108/+0x10C`

The early real `Player::Init` path around `0x1410DC99C` then does:

- `rdx = incoming_packet + 0x20`
- `rcx = player + 0x100`
- call `0x1416860A0`

`0x1416860A0` now reads concretely as:

- reads the authored key from:
  - `[player+0x100] + 0x8  -> player + 0x108`
  - `[player+0x100] + 0xC  -> player + 0x10C`
- resolves the matching record through:
  - `0x14386FD00`
- only if that pair is already valid:
  - copies the matched `0x20` bytes into `player + 0x110`
- then derives follow-up vector/transform state into:
  - `player + 0x130`
  - `player + 0x140`

So this helper does **not** seed the hidden pair.
It consumes an already-existing authored key inside the embedded
`player+0x100` subobject.

That is the strongest narrowing so far:

- the promotion work should stop treating `player+0x108/+0x10C` as a flat
  mystery field;
- and instead treat `player+0x100` as the real upper authored-descriptor
  container that must be initialized correctly for Nadine.

Another useful correction:

- `0x1416860A0` is not a one-off helper only used by `Player::Init`
- direct rel32 scans now show broad gameplay reuse across many unrelated
  runtime functions

So `player+0x100` is not merely a local spawn/apply scratch object.
It is much more likely a shared authored-descriptor/state carrier that other
systems already know how to consume.

### The Earliest Visible Real `Player::Init` Use Of The Embedded `+0x100` Subobject Happens Before The Shared/Base Upper Handoff

This matters for patch ordering.

Inside the real `Player::Init` pass `0x1410DC160`, the sequence is now:

1. prepare several upper/runtime owner bands
2. call the embedded authored-descriptor refresh:
   - `lea rcx, [player+0x100]`
   - `mov rdx, [incoming_packet+0x20]`
   - `call 0x1416860A0`
3. continue through:
   - `player->virtual +0x208`
   - `+0x3BD0`
   - `+0x3FD8`
   - `0x140CBFF50`

So the embedded authored-descriptor object is used *before* the later shared
upper handoff:

- `0x140CBFF50`
- `0x141E994C0`

This is a useful correction to the promotion model:

- the hidden authored pair is probably not born deep inside the late draw/
  control stages;
- it likely arrives already seeded in the incoming top-level Player bringup
  path, or in an even earlier base/packet init stage that feeds
  `player+0x100`.

### `0x1417C32A0` / `0x1417C35A0` Now Explain Why Early `Player::Init` Reads `incoming_packet + 0x20`

Direct disassembly of the packet initializers now tightens the wrapper model.

`0x1417C32A0` (`InitializeSelectorBackedProcessSpawnPacket`) explicitly stores:

- `arg4 / r9` into `packet + 0x20`
- selector/hash id into `packet + 0x8`
- additional base links into `+0x28/+0x30/+0x38/+0x48`

Then `0x1417C35A0` (`InitializeExtendedSelectorBackedRuntimeStatePacket`) does:

- call `0x1417C32A0` for the shared base
- preserve the same base `+0x20` source link
- store the incoming source object again at wrapper `+0x58`
- clear the late wrapper-only state at `+0x60/+0x68/+0x70/+0x78/+0x80/+0x88..+0x98`

That matters because the real early `Player::Init` step under `0x1410DC160`
does exactly:

- `mov rdx, [incoming_packet + 0x20]`
- `lea rcx, [player + 0x100]`
- `call 0x1416860A0`

So the earliest embedded authored-descriptor refresh is not fed by an opaque
"spawn blob". It is fed by the shared `+0x20` source-link field preserved by
the extended wrapper family itself.

This makes `0x1417C35A0` even more important:

- it already matches the later upper apply shape at `0x1418C4840`
- and now it also explains the earliest upper `player+0x100` refresh input

### `0x141D21C80` Selects Between A Base Packet And The Extended Wrapper Before Calling Entity Virtual `+0x538`

The retained-runtime dispatcher is now concrete enough to matter to the
promotion search.

Inside `0x141D21C80`:

- it validates the retained/live pair
- builds a temporary packet on the stack
- if the mode flag is nonzero, it initializes the base packet through
  `0x1417C32A0`
- if the mode flag is zero, it initializes the extended wrapper through
  `0x1417C35A0`
- then it calls the live entity's virtual `+0x538` with that packet

So the extended wrapper is not just "compatible with" the upper `Player`
surface. It is already a real accepted runtime-command packet on the generic
entity-side interface.

### `0x1413BF090` Is A Strong Retained-Side Producer Of The Same Extended Wrapper Family

The upper retained helper at `0x1413BF090` now provides a useful upstream
bridge.

It:

- resolves an object through the global retained/runtime registry
- builds a local stack wrapper through `0x1417C35A0`
- immediately dispatches that wrapper through `0x141D21C80`

This is a stronger bridge than the older generic retained-spawn narrative:

- `0x1410F14C0` still seeds the real `ProcessSpawnInfo` packet family
- but `0x1413BF090` shows a retained-side path that already produces the exact
  extended wrapper family later matched by the upper `Player` lifecycle

### `0x141E7C5F0` Is A Stub; The Real Body Starts At `0x141E7C600`

One earlier assumption was wrong and is now corrected.

The exact call target used by early `Player::Init` after `0x1416860A0` is
currently:

- `0x141E7C5F0`

Direct byte decoding shows that address is only:

- `ret 0`

The real nontrivial body starts at:

- `0x141E7C600`

and compares/logs two descriptor carriers rather than seeding the embedded
authored pair.

So the active early `Player::Init` work remains:

- refresh through `0x1416860A0`
- then continue into `player->virtual +0x208`

and not a hidden mutating post-refresh helper at `0x141E7C5F0`.

### `PromoteRetainedPlayerSlot` Also Builds The Same Base Packet Plus Extended Wrapper Family

The retained-slot promotion layer is now more relevant to the real Player
promotion search than before.

Inside `0x1410F3B20`:

- it hashes `Player`
- builds the base `ProcessSpawnInfo` packet through `0x1417C32A0`
- then immediately overwrites the local packet vtable with `0x1428D5D60`
- seeds the same wrapper-style local state on the stack
- and passes that state into the upper retained manager path

So `0x1410F3B20` is not only:

- "write retained slot table entry"

It is also a concrete retained-side builder of the same packet/wrapper family
that already lines up with:

- early `Player::Init` through `incoming_packet + 0x20`
- later upper apply through `0x1418C4840`
- and retained runtime dispatch through `0x141D21C80`

That makes `0x1410F3B20` one of the strongest concrete promotion-adjacent
retained entrypoints found so far.

### `0x1410F3B20` Routes That Wrapper Into A Real Two-Slot `NetGameManager` Tracker Family

The next layer under `PromoteRetainedPlayerSlot` is now much clearer.

The helper at `0x140A90E80` is no longer an anonymous manager accessor.

Its own assert strings read:

- `NetGameManager::GetMeNetPlayerTracker`
- `screenIndex > -1`
- `screenIndex < 2`

and the code returns:

- `manager + 0x3A0 + screenIndex * 8`

So the object around `0x143DD70C0` is a real two-slot net-player tracker
owner, not a generic retained helper.

This matters because `0x1410F3B20`:

- builds the same base packet plus extended wrapper family
- then routes that state into the `0x143DD70C0` / `GetMeNetPlayerTrackerByScreenIndex`
  layer

That is the strongest built-in "second player representation" signal found so
far:

- it is screen-indexed
- it is explicitly `NetGameManager`-named
- and it sits directly below a retained-slot promotion path that already
  builds the same wrapper family used by the upper Player lifecycle

### `0x140B31F60` Is Real `NetPlayerTracker::SpawnPlayer`, Not Just A Random Retained Consumer

The single direct caller chain above `PromoteRetainedPlayerSlot` is now much
tighter.

Direct rel32 scan shows:

- only one direct tail-call into `0x140B31F60`
- from `0x140B346B0`

and the large body at `0x140B31F60` is now concrete enough to identify.

Inside `0x140B31F60`:

- assert strings explicitly mention `NetPlayerTracker::SpawnPlayer`
- it resolves the current screen slot through `GetMeNetPlayerTrackerByScreenIndex`
- it pulls `Player`-facing authored state such as:
  - `start-on-edge`
  - `start-in-cover`
  - `start-in-swim`
  - `*player-starting-weapons-online*`
  - `*player-beast-weapons-online*`
- it then calls `PromoteRetainedPlayerSlot`
- and after that it immediately consumes the returned real `Player` object
  through mounted fields such as `player+0x1D88`

So this is not merely:

- "some retained helper that happens to touch promotion"

It is a real net-player spawn path that already crosses:

- two-slot `NetGameManager` tracker ownership
- retained slot promotion
- and direct post-promotion access to the real `Player` object

That makes the current strongest built-in co-op/promotion chain:

- `NetPlayerTracker::SpawnPlayer`
- `PromoteRetainedPlayerSlot`
- `GetMeNetPlayerTrackerByScreenIndex`
- upper `Player` apply/finalize through `0x1410DE740 -> 0x140CC1450`

This is materially stronger than the earlier broad "find the hidden writer for
player+0x108/+0x10C" framing, because it exposes a real engine-owned
screen-indexed second-player bringup surface.

### `0x140A90F40` Is The Wider Tracker Lookup Around The Same Two-Slot Family

The sibling helper at `0x140A90F40` is also no longer anonymous.

Its body:

- hashes `Player`
- scans the same tracker-owner family around `manager+0x3A0`
- compares tracker-side runtime player object identity against the supplied
  object

and it has a very wide callsite set across gameplay code.

So the tracker family now splits cleanly into:

- `0x140A90E80`
  - direct indexed accessor for screen `0..1`
- `0x140A90F40`
  - wider lookup by runtime player object and `Player`-hash identity

This is useful for Nadine because a successful promotion likely needs both:

- a concrete slot/indexed home
- and a tracker lookup path that lets the rest of runtime systems discover the
  promoted player object again by identity

### The Manager-Side Chain Above That Tracker Family Is Now Concrete Too

The tracker path is no longer floating without a named owner.

String xrefs now pin:

- `0x140A9AAD0`
  - `NetGameManager::RespawnPlayer`
- `0x140B347C0`
  - `NetPlayerTracker::NetRespawn`
- `0x140B31F60`
  - `NetPlayerTracker::SpawnPlayer`
- `0x140B346B0`
  - same tracker family under the broader `NetPlayerTracker::Spawn` string

The practical chain now reads like this:

- `NetGameManager::RespawnPlayer`
- `NetPlayerTracker::NetRespawn`
- `NetPlayerTracker::StartOrQueueSpawnPlayer`
- `NetPlayerTracker::SpawnPlayer`
- `PromoteRetainedPlayerSlot`
- `GetMeNetPlayerTrackerByScreenIndex`
- upper real `Player` bringup below that

What the intermediate tracker wrappers do:

- `0x140B347C0`
  - seeds tracker-side respawn flags and optional transform payload
  - touches timing/pending fields around `+0x4C0/+0x4C8/+0x4D8/+0x4F8/+0x4FC`
- `0x140B346B0`
  - clears or updates deferred-spawn bits
  - checks tracker readiness through virtual `+0x18`
  - either tail-calls the heavy `SpawnPlayer` body
  - or queues deferred state in tracker fields around `+0xA00/+0xA04`

This matters because the "best built-in promotion surface" is no longer just:

- retained slot promotion by itself

It is now a real named net-player pipeline owned by:

- `NetGameManager`
- `NetPlayerTracker`
- and then the real `Player` object

That is the strongest co-op-facing engine path found so far.

### `NetGameManager::AddOrUpdateNetPlayer` Seeds The Exact Slot That Later Reaches `PromoteRetainedPlayerSlot`

The manager-side tracker setup is now strong enough to be operationally useful.

In the heavy `NetGameManager::AddOrUpdateNetPlayer` helper at `0x140A90550`:

- the manager scans its `0x43D0` tracker array
- allocates or reuses a tracker object
- writes incoming spawn/loadout state into that tracker
- and crucially stores the effective player/screen index into:
  - `tracker + 0x1E0`

Later, inside `NetPlayerTracker::SpawnPlayer` at `0x140B31F60`:

- the same field is read back as:
  - `ecx = [tracker + 0x1E0]`
- and passed directly into:
  - `PromoteRetainedPlayerSlot`

So the current best practical reading is:

- `tracker + 0x1E0` is the real bridge from manager-side net-player setup
  into retained-slot promotion

This is a much stronger runtime patch surface than the earlier "find some
hidden Player-only writer" framing.

It implies a realistic built-in route to second-player promotion:

1. create/update a `NetPlayerTracker`
2. make sure it carries the correct local slot index at `+0x1E0`
3. drive the existing `RespawnPlayer -> NetRespawn -> SpawnPlayer` chain
4. let `PromoteRetainedPlayerSlot` promote that slot into a real `Player`

If this holds at runtime for slot `1`, Nadine may not need a bespoke upper
Player ctor replay at all. The engine may already own the intended second-slot
bringup path.

### The Respawn/Event Layer Above That Tracker Pipeline Is Now Named Too

The tracker pipeline is no longer isolated from its transport layer.

String xrefs now pin:

- `0x140BDB2E0`
  - `NetEventRespawnPlayer::Fill`
- `0x140BDB440`
  - `NetEventRespawnPlayer::HostFill`
- `0x140BDB620`
  - `SendNetEventRespawnPlayer`
- `0x140BDBF70`
  - `SendNetEventSignalRespawnPlayer`

And direct calls already connect that event family back into tracker bringup:

- `0x140A9CD25 -> NetPlayerTracker::NetRespawn`
- `0x140BC0425 -> NetPlayerTracker::NetRespawn`
- `0x140BDB23F -> NetPlayerTracker::NetRespawn`

So the chain is now visible one layer higher:

- send/fill respawn event
- tracker-side `NetRespawn`
- tracker spawn/start path
- retained promotion
- real `Player`

That matters because the future Nadine patch no longer has to start at the
lowest retained seam. There is now a plausible higher control surface at the
respawn-event / tracker-event layer if slot-1 bringup proves easier there than
inside pure `Player` apply code.

### `0x140A90A00` Is The Cinema-Flavored Wrapper, Not The Generic Public Net-Player Entry

The label above `0x140A90550` needed one correction.

String xrefs now show:

- `0x140A90BFC` inside `0x140A90A00`
  - `NetGameManager::AddOrUpdateNetPlayerCinema`
- `0x140A906BC` inside `0x140A90550`
  - `NetGameManager::AddOrUpdateNetPlayer`

So:

- `0x140A90550`
  - remains the heavier generic manager-side net-player tracker worker
- `0x140A90A00`
  - is the cinema-flavored wrapper above it

That means `0x140A90A00` is still useful because it seeds:

- `tracker+0x1E0`
- `tracker+0x1E4`

but it should no longer be treated as the single generic public patch surface.

### `NetGameManager::AddOrUpdateNetPlayer` Still Exposes The Strongest Manager-Side Tracker Surface

The manager-side public wrapper above the heavy tracker worker is now clearer.

`0x140A90A00` sits directly over `0x140A90550` and:

- finds or creates the tracker object
- seeds tracker spawn/loadout payload
- stores `tracker+0x1E0 = 0` in the local-player case
- derives a secondary state id/hash into `tracker+0x1E4`
- returns the tracker pointer

This makes the manager-side pair:

- `0x140A90550`
- and, where relevant, the cinema wrapper `0x140A90A00`

cleaner patch surfaces than dropping straight into:

- `NetPlayerTracker::SpawnPlayer`
- or lower retained promotion internals

because it still sits above tracker creation, but already feeds the exact
fields later consumed by the spawn/promotion chain.

### The Net-Event Handler Above `RespawnPlayer` Exists, But It Is Not The Best First Hook

The event/transport layer tightened one step more:

- `0x140B6BB80` resolves a tracker by id from the manager tracker array
- then calls `NetGameManager::RespawnPlayer`

So the chain can be seen one level higher as:

- incoming net event
- resolve tracker by id
- call `RespawnPlayer`
- tracker `NetRespawn`
- tracker `SpawnPlayer`
- retained promotion
- real `Player`

That is useful structurally, but it also clarifies the tradeoff:

- the event handler is a transport-facing seam
- the manager wrapper `0x140A90A00` and manager respawn path `0x140A9AAD0`
  are better first runtime patch surfaces for a local experiment

In other words, the refined shortlist is now:

- `0x140A90550`
- `0x140A90A00` for the cinema/local-player flavored path
- `0x140A9AAD0`

not the lower tracker internals and not the higher event transport layer.

### One Generic Caller Already Drives `AddOrUpdateNetPlayer` For Slots `0` And `1`

The strongest new practical clue is in one of the generic callers above
`0x140A90550`.

Inside the path around:

- `0x140A392EC`
- `0x140A3936A`

the same function calls `NetGameManager::AddOrUpdateNetPlayer` twice:

- once with `r9d = 0`
- once with `r9d = 1`

and uses the same source object in both calls.

That means the binary already contains a real manager-side dual-slot bringup
pattern above the tracker layer, not just a two-slot storage structure.

This is stronger than the earlier evidence because it shows:

- not only that the tracker array has multiple slots
- but that one real gameplay path actively iterates slot `0` and slot `1`
  through the generic manager-side add/update worker

So the "second player" path is now visible at three levels:

- storage (`manager+0x3A0`, tracker array)
- promotion (`PromoteRetainedPlayerSlot`)
- and a real dual-slot manager caller above `AddOrUpdateNetPlayer`

### The Public Callers Above `AddOrUpdateNetPlayer` Already Split Into Two Useful Shapes

The next layer above `0x140A90550` is now sharper.

1. Around:

- `0x140A392EC`
- `0x140A3936A`

the same function calls `NetGameManager::AddOrUpdateNetPlayer` twice with the
same source object:

- once with `r9d = 0`
- once with `r9d = 1`

and then immediately pushes the returned tracker through the same follow-up
state path.

This is the strongest current manager-side dual-slot bringup pattern.

2. Around:

- `0x140A581FD`
- `0x140A583F5`
- `0x140A58494`

the caller first tries to match an existing tracker by:

- `tracker+0x1DC`
- `tracker+0x1E0`

and only falls back to `AddOrUpdateNetPlayer` when no matching tracker is
already live.

That second path is still useful, but it looks more like:

- tracker reuse / update / late attach

than the cleaner dual-slot bringup shape above.

So the manager-side priority is now:

- first: the dual-slot caller around `0x140A392EC / 0x140A3936A`
- second: the reuse/update caller around `0x140A581FD / 0x140A583F5`

### `0x140A58310` Already Behaves Like An External Net-Player Record Bridge

The broader caller around `0x140A58310` is now tighter than the earlier
"reuse/update" label.

Its input record uses:

- `record+0x18`
- `record+0x1C`

as the exact identity pair that first probes live trackers through:

- `tracker+0x1DC`
- `tracker+0x1E0`

If a live tracker already matches that pair, the function reuses it.

If not, it splits:

- cinema/local-record flavored path:
  - builds a small `0x40` packet on the stack
  - calls `0x140A90A00`
- generic path:
  - resolves a source object from the 12-entry object array by matching
    `record+0x18` against `source+0xE8`
  - then calls `0x140A90550` with `r9d = record+0x1C`

After the tracker exists, the same function copies the wider state block:

- `record+0x20..0x3C`

into tracker fields:

- `+0x86C..+0x884`

So `0x140A58310` is already a useful higher seam:

- it accepts an external player-like record
- maps that record onto tracker identity
- reuses or creates the tracker
- then pushes the wider state payload into the tracker

This makes it a more structured runtime patch surface than dropping directly
into lower tracker internals.

One more anchor makes that bridge less generic than it first looked:

- both `0x140A58190`
- and `0x140A58310`

build their local packet through the authored token:

- `late-joiner-%d`

So these two bridges are best read as late-join flavored tracker bringup /
reuse paths, not arbitrary net-player helpers.

### `0x140A90A00` Seeds A Fixed `NetCharacter-` Family Id Into `tracker+0x1E4`

The cinema/local wrapper at `0x140A90A00` is now tighter too.

New concrete facts:

- it copies the incoming `0x40` payload into tracker-local bands:
  - `tracker+0x7C0`
  - `tracker+0x0D9`
  - `tracker+0x119`
- it writes:
  - `tracker+0x1DC = incoming_r8d`
  - `tracker+0x1E0 = 0`
- then it hashes the literal token:
  - `NetCharacter-`
- resolves the hashed record through `0x1417AAA70`
- and writes the resulting id into:
  - `tracker+0x1E4`

So `tracker+0x1E4` is not just arbitrary spill from the caller. For this
wrapper path it is derived from a fixed `NetCharacter-` family token.

That is useful for later runtime work because it means the cinema/local path
already seeds a stable tracker-side family id, not only slot/index state.

### The Cinema Wrapper Isolated Further: Only Four Direct Raw Callers Remain

Raw rel32 scanning over `.text` now shows only four direct callers into
`0x140A90A00`:

- `0x140A4DDA0`
- `0x140A4E7DC`
- `0x140A58190`
- `0x140A58310`

The split is useful:

1. Two broader bridges:

- `0x140A58190`
- `0x140A58310`

2. Two thin wrappers:

- `0x140A4DDA0`
  - requires the same global gate as the cinema path
  - only needs `record+0x18 != 0`
  - builds the local `0x40` packet
  - calls `0x140A90A00`
  - then stamps the same tracker flags
- `0x140A4E7DC`
  - iterates a compact 10-entry id array
  - for each entry builds the same local `0x40` packet
  - calls `0x140A90A00`
  - stamps the same tracker flags

So the cinema wrapper is now clearly isolated behind a tiny caller family.

That matters because it gives a sharper tradeoff:

- the generic net-player worker `0x140A90550` still has the broader built-in
  second-player surface
- but the cinema path now has two much thinner local seams above it if we
  need a narrower first patch than the full external-record bridge

Raw rel32 scans also add one important constraint:

- neither `0x140A4DDA0`
- nor `0x140A4E7DC`

has any direct code caller in `.text`.

So those two wrappers are most likely table-dispatched entries, not clean
public code seams. That drops their practical priority below the explicit
record bridges:

- `0x140A58190`
- `0x140A58310`

The token layer also sharpens the split:

- `0x140A4E7DC` builds its local packet through:
  - `cinema-player-%d`
- while `0x140A58190` / `0x140A58310` build through:
  - `late-joiner-%d`

So the two thin wrappers and the two broader bridges already separate into
distinct authored identity families:

- cinema-player
- late-joiner

The same raw caller check now closes the loop on the late-joiner side too:

- `0x140A58190`
- `0x140A58310`

also have no direct `.text` code callers.

So the whole cinema / late-joiner helper band is now better treated as
table-dispatched machinery under the manager layer, not as explicit public
runtime seams.

Practically that leaves the strongest direct public surfaces at:

- `0x140A90550`
- `0x140A9AAD0`

### The Broader Generic Callers Above `0x140A90550` Sit In Matchmaking / Session Flows

The heavier direct callers above `0x140A90550` are no longer anonymous either.

Literal tokens now tie them to session / matchmaking flow:

- `joined-tournament-match`
- `return-to-search`
- `map-chosen`
- `matchmaking-failed`
- `timed-out`
- `start-game-failed`

That means the broader callers such as:

- `0x140AD1D50`
- `0x140C07F70`

are useful as proof that the PC binary still carries a live multiplayer/session
layer, but they are worse first local patch seams than the narrower:

- `late-joiner-%d` bridges
- and `cinema-player-%d` wrappers

### `NetGameManager::RespawnPlayer` Is The Clean Explicit Public Seam On This Layer

The direct manager respawn path at `0x140A9AAD0` is now tighter too.

New concrete facts:

- unlike the cinema / late-joiner helper band, this one is not just
  table-dispatched machinery underneath another layer;
- it is a direct explicit manager seam that already works on a live tracker
  object and then hands control into:
  - `NetPlayerTracker::StartOrQueueSpawnPlayer`

The important split inside `0x140A9AAD0` is:

1. active path
   - stages fresh tracker state into:
     - `tracker+0x500..+0x540`
     - `tracker+0x840/+0x844`
   - then calls:
     - `0x140B346B0`

2. deferred path
   - if the tracker-side gate does not accept the immediate bringup,
     it queues deferred respawn through:
     - `tracker+0xA00`
     - `tracker+0xA04`
   - and seeds timing through:
     - `tracker+0x4C0/+0x4C8`

So `0x140A9AAD0` is now the clearest explicit public seam on the whole
manager/tracker layer:

- above the lower tracker spawn internals
- but below the broader session/matchmaking machinery

That makes it the strongest non-table-dispatched runtime patch surface found
so far alongside `0x140A90550`.

One more correction tightens this layer:

- the earlier event-handler label should live on:
  - `0x140B6B970`
- not on the interior block:
  - `0x140B6BB80`

And the direct-caller picture is now exact:

- `0x140A9AAD0` has only one direct `.text` caller:
  - `0x140B6B970`

So the manager respawn seam is explicit, but it is only reached directly
through that one higher event-style function.

Separately, the deferred queue consumer is now pinned higher too:

- `0x140BC0425` sits inside:
  - `0x140BBFD70 .. 0x140BC119D`

and that larger function:

- calls `NetPlayerTracker::NetRespawn`
- then, if `tracker+0xA00` is set,
- uses `tracker+0xA04` through `0x140B33CF0`
- clears the pending byte
- and refreshes the live tracker object again

So the split above tracker respawn is now cleaner:

- `0x140B6B970` -> explicit event-style path into `NetGameManager::RespawnPlayer`
- `0x140BBFD70` -> direct tracker-side consumer of `NetRespawn` plus deferred queue resume

### Manager/Tracker Net-Player Seams Now Have Concrete Minimum State Fields

Two upper seams are now concrete enough to reason about a first slot-`1`
runtime patch without guessing at random tracker bytes.

For `0x140A90550`:

- the worker writes the incoming net-player payload into the tracker itself,
  not only into temporary locals;
- new concrete stores are:
  - `tracker+0x7C0..+0x7DF <- incoming_record+0x00..+0x1F`
  - `tracker+0x7E0 <- incoming_record+0x20`
  - `tracker+0x7B8 <- incoming_record+0xE4`
  - `tracker+0x1DC <- incoming_record+0xE8`
  - `tracker+0x7BC <- incoming_record+0xF0`
  - `tracker+0x1E0 <- requested slot / player index`
- it then derives `tracker+0x1E4` through the hashed `NetCharacter-` token
  family and copies a `0x40`-byte presentation/state block into
  `tracker+0x119`;
- on the local-player branch it also links the selected tracker back into
  manager arrays at:
  - `manager+0x3A0 + slot*8`
  - `manager+0x3B0`

For `0x140A9AAD0`:

- the function begins from a live tracker object and stages immediate respawn
  data directly into:
  - `tracker+0x500..+0x540`
  - `tracker+0x840 <- incoming r8d`
  - `tracker+0x844 <- incoming r9d`
- it then calls tracker virtual `+0x18`;
- if that gate accepts, it calls `0x140B346B0` with
  `edx = tracker+0x4F8`;
- if not, it defers through:
  - `tracker+0xA04 <- incoming r9d`
  - `tracker+0xA00 <- 1`
  - `tracker+0x4C8 <- global time`
  - `tracker+0x4C0 <- global time - const`

`0x140B346B0` is also sharper now:

- it clears:
  - `tracker+0xA7C`
  - `tracker+0x814`
- if `edx != 0`, it immediately proceeds into the tracker virtual `+0x18`
  gate;
- if `edx == 0` and the gate accepts, it tail-calls `0x140B31F60`;
- otherwise it creates the same deferred respawn state as the manager seam:
  - `tracker+0xA04 <- edx`
  - `tracker+0xA00 <- 1`
  - `tracker+0x4C8 / +0x4C0 <- deferred timer`

And inside `0x140B31F60` the tracker fields that matter earliest are now
concrete too:

- the real retained slot comes from:
  - `tracker+0x1E0`
- the function immediately indexes:
  - `g_RetainedPlayerSlotTable`
- later upper authored/runtime record production consumes:
  - `tracker+0x1E4`
- before `PromoteRetainedPlayerSlot`, the spawn path also snapshots:
  - `tracker+0x840 -> tracker+0x848`
  - `tracker+0x844 -> tracker+0x84C`
- then clears:
  - `tracker+0x840`

This is enough to stop treating the net-player path as a black box. The two
strong seams now differ clearly:

- `0x140A90550` is the cleaner tracker-population seam;
- `0x140A9AAD0` is the cleaner immediate-vs-deferred respawn seam above
  `StartOrQueueSpawnPlayer`.

One more practical refinement:

- `0x140A9AAD0` does not meaningfully use `rcx`;
- the real live arguments are:
  - `rdx = tracker`
  - `r8d -> tracker+0x840`
  - `r9d -> tracker+0x844`

So the upper respawn seam is even narrower than it looked: the runtime patch
surface is effectively tracker-centric.

The event packet layer above it is also clearer now:

- `NetEventRespawnPlayer::Fill` stores:
  - `event+0x18 <- tracker virtual +0x0`
  - `event+0x1C <- has_transform`
  - `event+0x20..+0x60 <- optional transform block`
  - `event+0xC8 <- final byte flag`
- `NetEventRespawnPlayer::HostFill` stores:
  - `event+0xB0 <- global time`
  - `event+0xB8 <- low-nibble stage from tracker+0x55C`
  - `event+0xBC <- derived state id`
  - `event+0xC0 <- derived count / mode`
  - `event+0xC4 <- optional timing override`

That means the event layer is useful as a reference for packet semantics, but
it is still a worse first local patch surface than the tracker-centric pair:

- `0x140A90550`
- `0x140A9AAD0`

Another practical refinement:

- the immediate respawn path is not driven only by `+0x840/+0x844`;
- `0x140B44EC0`, which feeds the staged block at `tracker+0x500..+0x540`,
  first tries to resolve a live actor pair from:
  - `tracker+0x8`
  - `tracker+0x10`
- if that pair is valid, it copies world-space data from:
  - `live_actor+0x100`
  - `live_actor+0x120`
  - `live_actor+0x140`
- otherwise it falls back to:
  - `tracker+0x20`
  - through `0x141685C30`

So for a first successful immediate slot-`1` patch, the tracker probably needs
not only the net-player payload and slot fields, but also one valid transform
source:

- either a valid live actor pair in `tracker+0x8/+0x10`
- or a fallback source object through `tracker+0x20`

There is also one useful narrowing below `RespawnPlayer`:

- `0x140B411F0` and `0x140B412D0` are just post-spawn integer selectors for
  two Player fields:
  - `player+0x854`
  - `player+0x858`
- if those fields are zero, they fall back to hashed defaults

That means the first hard blocker is still tracker bringup and transform
source validity, not some hidden mandatory nonzero `+0x840/+0x844` pair.

New practical narrowing:

- `NetPlayerTracker::NetRespawn (0x140B347C0)` already has a direct bypass for
  the transform-source problem.

Its live argument shape is now concrete:

- `rcx = tracker`
- `edx = respawn mode / state id`
- `r8b = has_prebuilt_transform_block`
- `r9  = optional 0x50 transform block`

When `r8b != 0`, the function does **not** call
`BuildNetPlayerTrackerRespawnTransformBlock`.
Instead it copies:

- `r9+0x00..0x1F -> tracker+0x500..+0x51F`
- `r9+0x20..0x3F -> tracker+0x520..+0x53F`
- `r9+0x40..0x4F -> tracker+0x540..+0x54F`

and then continues normal respawn/spawn flow.

That means the first built-in slot-`1` patch no longer has to solve
`tracker+0x8/+0x10` first if it can provide a valid `0x50` block directly.

Two live callers already use this bypass path:

- `0x140BC0425`
- `0x140BDB23F`

In both cases the caller passes:

- `r8b = 1`
- `r9  = prebuilt_0x50_block`

The build pattern above `0x140BC0425` is also concrete now:

- the caller prepares a compact default seed record on the stack
- calls `0x141685C30`
- then calls `NetPlayerTracker::NetRespawn`

That seed record shape is now concrete:

- `+0x0  = 0x7FCCCCCC`
- `+0x4  = 0x7FCCCCCC` (stored as the qword tail covering `+0x4..+0xB`)
- `+0xC  = respawn mode / state id`
- `+0x10 = zero vec4`
- `+0x20 = default vec4 from 0x1428805D0`

And the live caller uses:

- `rdx = tracker + 0x20`

as the reference-transform carrier for `0x141685C30`.

So `0x141685C30` is now the practical helper to care about for the first
local second-player patch, not `0x140B44EC0`.

Related clarification:

- `0x140A90550` does **not** seed:
  - `tracker+0x8`
  - `tracker+0x10`
  - `tracker+0x20`
- it only seeds the higher tracker payload / slot / hash fields already mapped
  earlier.

The new shortest patch chain is therefore:

- build/populate tracker through `0x140A90550`
- build a direct `0x50` respawn block through `0x141685C30` or equivalent
- call `0x140B347C0` with `r8b=1`

One important correction below that seam:

- `tracker+0x20` is **not** a compact actor-pair record.
- `0x141685C30` only consumes the first `16` bytes at `rdx`:
  - `vmovups xmm2, [rdx]`
  - then it never reads the reference source again
- everything else in the produced `0x50` block comes from:
  - the compact seed record at `r8`
  - optional authored override lookup through `0x14386FD00`

So `tracker+0x20` is best read as a `16-byte reference-position carrier`,
not as pair/object identity.

That also means the first local patch does **not** have to seed `tracker+0x20`
specifically if it can call `0x141685C30` directly with any valid
`actor+0x100`-style world-position source.

Related clarification inside `NetPlayerTracker::NetRespawn`:

- after the prebuilt block is copied into `tracker+0x500..+0x540`,
  the later optional live-actor activation reads a separate `16-byte` record
  from `tracker+0x60`
- that block is the one treated as `(object_ptr,id)` pair:
  - low qword consumed as object pointer
  - dword at `+0x68` consumed as runtime id

So the tracker already has two different early helper bands:

- `+0x20` = reference position for respawn-block building
- `+0x60` = optional live actor pair used later by the post-copy activation
  path

One more narrowing from the same focused net-player slice:

- no writer into `tracker+0x60` showed up in the current bringup chain
  around:
  - `0x140A90550`
  - `0x140B31F60`
  - `0x140BC0425`
- and the compact pair helper family:
  - `0x140B45030`
  - `0x140B45040`
  - `0x140B45100`
  is not called from those focused tracker bringup slices

So `tracker+0x60` should currently be treated as an optional later-quality
band, not as a proven first blocker for the built-in slot-`1` patch.

Another useful minimization from the same focused slice:

- in the dumped `NetPlayerTracker::SpawnPlayer` body
  (`0x140B31F60 .. 0x140B32D20`), the only one of the freshly mapped
  `AddOrUpdateNetPlayerTracker` fields that keeps showing up directly is:
  - `tracker+0x1E0`
- no direct reads from that focused `SpawnPlayer` body showed up for:
  - `tracker+0x7C0..+0x7E0`
  - `tracker+0x7B8/+0x7BC`
  - `tracker+0x1E4`

So inside the immediate built-in bringup path, `tracker+0x1E0` is still the
only clearly first-order field for real retained-slot promotion; the wider
payload seeded by `0x140A90550` currently looks more like later-quality or
downstream session state than the first hard requirement for slot-`1` spawn.

One more practical cutoff:

- `0x140A90A00` is now clearly a local-player wrapper, not a good slot-`1`
  seam
- it hard-forces:
  - `tracker+0x1E0 = 0`
- and then seeds:
  - `tracker+0x7BC`
  - `tracker+0x1DC`
  - `tracker+0x7B8`
  - `tracker+0x7C0..+0x7E0`
  - plus three extra `0x40` copies into tracker-local bands around:
    - `+0x159`
    - `+0xD9`
    - `+0x119`
- then it derives `tracker+0x1E4` from a `NetCharacter-` token family

So `0x140A90A00` is still useful as a payload template, but it should now be
treated as excluded from the first slot-`1` patch shortlist because it
hardcodes the local-player slot.

One more direct runtime anchor is now concrete:

- `0x143DD70C0` is a real global `NetGameManager` owner object, not just a
  symbolic anchor from strings
- raw callers repeatedly do:
  - `lea rcx, 0x143DD70C0`
  - then call:
    - `0x140A90E80`
    - `0x140A910B0`
    - `0x140A962F0`
    - and other net-player manager helpers
- `GetMeNetPlayerTrackerByScreenIndex (0x140A90E80)` then resolves tracker
  pointers directly from:
  - `manager + 0x3A0 + screen_index*8`

Practical effect:

- an injected DLL does not need a separate memory scan to find
  `NetGameManager`
- the first built-in slot-`1` patch can reach the real manager directly as:
  - `module_base + 0x3DD70C0`

### Disabled Runtime Scaffold Now Exists For The Built-In Slot-1 Bringup Path

The host DLL now carries a disabled-by-default scaffold for the built-in
slot-`1` net-player path inside `scanner/src/teleport_hook.cpp`.

What is already wired:

- direct manager pointer:
  - `module_base + 0x3DD70C0`
- direct slot-`1` tracker lookup through:
  - `0x140A90E80`
- direct prebuilt respawn-block builder through:
  - `0x141685C30`
- direct tracker respawn seam through:
  - `0x140B347C0`

What the scaffold intentionally does **not** do yet:

- it does **not** call `0x140A90550`
- it does **not** attempt tracker creation
- it does **not** force-enable the experiment at runtime

Reason:

- the direct slot-`1` respawn seam is now concrete enough to encode safely;
- the remaining high-risk unknown is still the exact `AddOrUpdateNetPlayerTracker`
  ABI and minimum creation payload for a tracker that does not already exist.

So the current code state is:

- safe direct manager/tracker/respawn wrappers exist in the DLL;
- the first unresolved blocker is now sharply narrowed to:
  - creating or populating a valid slot-`1` tracker through `0x140A90550`.

### `0x140A90550` Now Has A Concrete Call Shape From Two Real Public Callers

Direct disassembly against retail `tll.exe` now pins the real call shape much
harder.

Both the dual-slot caller around:

- `0x140A392EC`
- `0x140A3936A`

and the late-join bridge caller around:

- `0x140A58287`
- `0x140A58494`

invoke `0x140A90550` as:

- `rcx = module_base + 0x3DD70C0`
  - real global `NetGameManager`
- `rdx = module_base + 0x3E08C80`
  - shared net-player context/global
- `r8 = source_object`
  - a `0x1C0`-sized source entry from the same manager-facing net-player
    object families
- `r9d = slot_index`
- stack arg 5 / `r12b`
  - `0` on the generic slot bringup path
  - influences matching/linking behavior and later lands in `tracker+0x1E8`

That means the low worker no longer has a vague ABI. The remaining unknown is
not the call shape, but which `source_object` is safe for a first Nadine
experiment.

New exact internal consequences of that call:

- on success it returns the live tracker pointer;
- hashes a temporary `NetCharacter-`-family packet and stores the result in:
  - `tracker+0x1E4`
- stores a produced `0x40` block into:
  - `tracker+0x119`
- when the fifth argument is `0`, it conditionally links the tracker back into:
  - `manager+0x3A0 + slot*8`
  - `manager+0x3B0`

This is enough to justify a new disabled scaffold step in the DLL:

- if slot `1` tracker is missing,
- try `0x140A90550(module_base+0x3DD70C0, module_base+0x3E08C80, sourceA[0], 1, 0)`
- then continue through the already pinned direct respawn path.

One important correction from the same retail disassembly:

- `0x140A90550` is **not** the whole tracker-bringup sequence by itself
- both real caller families do additional post-call initialization on the
  returned tracker

Observed post-call init bands:

1. Dual-slot caller around `0x140A392EC / 0x140A3936A`
   - writes `tracker+0x220 = -1`
   - toggles `tracker+0x455`
   - calls `0x140A59540`
   - conditionally refreshes another object path through `0x14194E510`

2. Late-join bridge around `0x140A58494`
   - writes `tracker+0x220 = -1`
   - clears/sets:
     - `+0x455`
     - `+0x88C`
     - `+0x850`
     - `+0x817`
   - conditionally seeds:
     - `+0xA10`
     - `+0x814`
     - `+0x88D/+0x88E/+0x88F`
   - then copies the wider external record and calls:
     - `0x140B3D7D0`

So the P0 blocker is now sharper:

- not just “which `source_object` is safe”
- but also “which post-call init band must follow `0x140A90550` for a stable
  slot-`1` tracker”

### `0x140A58310` External Record Layout Is Now Much More Concrete

Retail disassembly also pins the wider late-join bridge record beyond the
earlier `+0x18/+0x1C/+0x20..+0x3C` summary.

Confirmed structure use inside `0x140A58310`:

- `record+0x18`
  - source/net identity
- `record+0x1C`
  - target tracker slot/index
- `record+0x20..+0x3C`
  - copied into tracker:
    - `+0x86C`
    - `+0x870`
    - `+0x874`
    - `+0x878`
    - `+0x87C`
    - `+0x880`
    - `+0x884`
    - `+0x888`
- `record+0x40`
  - start of a 19-entry `WORD` array
- `record+0x66`
  - start of a paired 19-entry signed-byte array
- `record+0x7C`
  - `tracker+0x40C8`
- `record+0x80`
  - `tracker+0x1FC`
- `record+0x84`
  - `tracker+0x208` with minimum clamp to `1`
- `record+0x88`
  - `tracker+0x200`
- `record+0x89`
  - `tracker+0x201`
- `record+0x8C`
  - `tracker+0x560`
- `record+0x90`
  - `tracker+0x454`
- `record+0x91`
  - `tracker+0x204`
- `record+0xB4`
  - `tracker+0x450`
- `record+0xC0`
  - `tracker+0x1F0`
- `record+0xC8`
  - `tracker+0x1F8`

This does not replace the lower `0x140A90550` seam yet, but it makes
`0x140A58310` a much more concrete fallback if direct tracker creation from a
source object turns out unstable.

### Fresh Runtime Result: `slot1_probe.dll` Reached A Live Tracker Before The Crash

On a fresh campaign launch, a dedicated injected probe DLL (`tll_slot1_probe.dll`)
logged the following before the game crashed:

- live Nadine object resolved inside the process:
  - `nadine = 0x10E12F8600`
- live Player object also resolved in the same probe run:
  - `player = 0x10E1060900`
- upper globals were live:
  - `manager = 0x7FF68C6E3900`
  - `context = 0x7FF68C6E61F0`
- most importantly, slot `1` already had a non-null tracker before any
  `AddOrUpdate` call:
  - `initial tracker = 0x7FF689ED4D80`

The probe log stopped immediately after that `initial tracker` line and the
game crashed. That narrows the crash window to the remainder of the probe
sequence after tracker acquisition.

This is a strong runtime correction:

- the first blocker is no longer "can we create a slot-1 tracker at all?";
- on this launch, the engine already had a live slot-`1` tracker;
- the next staged probe should therefore avoid calling
  `AddOrUpdateNetPlayerTracker` and should first validate:
  1. tracker field reads;
  2. `BuildRespawnTransformBlock`;
  3. only then `NetPlayerTracker::NetRespawn`.

### Fresh Runtime Result: External Object Scanner Confirms Nadine As A Viable Surrogate For Low Worker Fields

The live object scanner wrote a fresh Nadine snapshot during the same run:

- `nadine_object.bin` updated at `2026-06-10 21:09`
- matching live candidate from `live_runtime_objects.json`:
  - `0x10E12F8600`

The low fields that matter to the current `0x140A90550` theory remained
compatible with the known Player snapshot at the key offsets we already track:

- `+0xE4 = 0x11`
- `+0xE8 = 0`
- `+0xF0 = 0`
- `+0x100 = 0x7FCCCCCC7FCCCCCC`
- `+0x108 = 0`
- `+0x10C = 0`
- `+0x110 = 0`
- `+0x120 = 0`

So the runtime evidence still supports Nadine as a plausible surrogate source
object for the low tracker worker. What changed is not the source-object theory,
but the order of risk:

- first validate the already-live slot-`1` tracker path;
- only then revisit forced tracker creation if the pre-existing tracker turns
  out unusable.

### Runtime Correction: `0x140A90E80` Is Not Safe To Treat As A Tracker Getter Yet

A staged runtime probe on a fresh launch reached a non-null pointer from the
assumed slot-`1` getter path, but the follow-up field reads were clearly code
bytes rather than tracker state:

- returned pointer: `0x7FF689ED4D80`
- sampled `+0x1E0/+0x1E4/+0x220/+0x840/+0x844/+0x500` decoded into instruction-like values
  such as:
  - `0x245C8948`
  - `0xA139EB05`
  - `0xC3C28B480289018B`

The probe log stopped after `BuildRespawnTransformBlock` was entered and the
process then crashed.

That means the old assumption
`0x140A90E80 == GetNetPlayerTrackerByScreenIndex(manager, slot)` is no longer
safe enough for runtime patching.

Immediate correction:
- remove `0x140A90E80` from the short safe-runtime seam list;
- prefer direct slot storage validation through `manager + 0x3A0 + slot*8` on the
  next staged probe;
- keep `BuildRespawnTransformBlock` isolated from `NetRespawn` until that direct
  tracker path is validated.

### Runtime Correction: Direct `manager + 0x3A0 + slot*8` Also Produced The Same Code Pointer

A second staged probe (`slot1_probe2.dll`) removed the old callable getter and
read the slot pointer directly from the manager's slot array:

- live addrs:
  - `nadine = 0x10E1305580`
  - `player = 0x10E1061C80`
- `manager = 0x7FF68C6E3900`
- direct slot-`1` pointer from `manager + 0x3A0 + 8`:
  - `0x7FF689ED4D80`

That is the same value previously returned by the assumed callable getter.
So the previous crash was not caused by the getter helper alone; the manager's
slot storage itself already held that code-like pointer on this run.

The probe then reached:
- `slot1 probe2 calling BuildRespawnTransformBlock`

and the game crashed again.

So the current runtime ordering is now even tighter:
1. validate what `0x7FF689ED4D80` really is before treating slot `1` storage as a tracker
2. keep `BuildRespawnTransformBlock` disabled until that slot object is typed correctly
3. do not call `NetRespawn` before both earlier points are resolved

### Runtime Result: The Real Slot-`1` Storage Is The Inline Manager Band At `+0x3C0 + idx*0x43D0`

A safe probe using the inline manager formula completed without crashing and
logged:

- `manager = 0x7FF68C6E3900`
- `slot1 inline object = 0x7FF68C6E8090`
- sampled fields:
  - `+0x1E0 = 0x41524950`
  - `+0x1E4 = 0x435F4554`
  - `+0x4FC = 0x61`
- `BuildRespawnTransformBlock` was skipped on this pass
- the process stayed alive

This is the first runtime-safe confirmation that the real per-slot band is the
inline manager object computed by the helper at `0x140A90E50`:

`manager + 0x3C0 + idx*0x43D0`

and not the old code-like pointer `0x7FF689ED4D80`.

Immediate correction:
- treat `0x140A90E50` / the inline-object formula as the real slot object seam;
- treat `0x140A90E70` / `0x140A90E80` as different upper helpers, not tracker
  storage;
- continue staging on the inline slot object only.

### Runtime Result: `BuildRespawnTransformBlock` Is The First Confirmed Crash Surface Even On The Correct Inline Slot Path

A follow-up staged probe (`slot1_probe4.dll`) used the corrected inline slot
object:

- `manager = 0x7FF68C6E3900`
- `slot1 inline object = 0x7FF68C6E8090`
- sampled fields remained stable:
  - `+0x1E0 = 0x41524950`
  - `+0x1E4 = 0x435F4554`
  - `+0x4FC = 0x61`

The probe then called `BuildRespawnTransformBlock` and the process crashed
again immediately after logging that call.

That is the strongest runtime narrowing so far:

- the old getter/callable seam was not the root cause;
- the inline slot path itself is stable enough for reads;
- the first confirmed crashing action is the direct call into
  `0x141685C30` (`BuildRespawnTransformBlock`) when fed from
  `Nadine + 0x100` in the current runtime context.

So the next runtime direction is no longer "find the tracker". It is now one
of these two narrower tasks:

1. determine the real source object/context expected by `BuildRespawnTransformBlock`
   instead of raw `Nadine + 0x100`;
2. bypass that builder entirely and seed `tracker+0x500..+0x540` with a known-good
   block copied from a live built-in caller path.

### Runtime Result: `BuildRespawnTransformBlock` Still Crashes With The Correct Live Caller Source Pointer

A final staged probe (`slot1_probe5.dll`) switched the source pointer from
`Nadine + 0x100` to the same shape used by the live caller at `0x140BC03FF`:

- source passed to `BuildRespawnTransformBlock`:
  - `inline slot object + 0x20`
- other observed runtime state remained unchanged:
  - `slot1 inline object = 0x7FF68C6E8090`
  - `+0x1E0 = 0x41524950`
  - `+0x1E4 = 0x435F4554`
  - `+0x4FC = 0x61`

The process still crashed immediately after the probe logged:
- `slot1 probe5 calling BuildRespawnTransformBlock`

So the remaining blocker is no longer the source-pointer shape. The crash must
now be inside the builder's remaining contract, most likely one of:

- the exact seed record contents (`r8`)
- the surrounding slot/tracker lifecycle state expected before the call
- or another hidden caller-side context that the probe still does not supply

This is the narrowest runtime result so far:
- slot lookup fixed
- source pointer fixed
- builder still crashes
- next step must focus on the precise seed/context contract rather than more
  blind runtime retries


### Static Result: The Seed Record We Used For `BuildRespawnTransformBlock` Was Wrong

The live caller at `0x140BC03CF..0x140BC0410` does **not** pass the seed layout
that the earlier probes used.

Real caller-side seed construction on the stack is:

- `+0x00` : `DWORD 0x7FCCCCCC`
- `+0x04` : `QWORD 0x000000007FCCCCCC`
- `+0x0C` : `DWORD edi` (respawn mode / state id)
- `+0x10` : `vec4 zero`
- `+0x20` : `vec4 default` from `0x1428805D0`

That means the old probe record was wrong at least at `+0x08`:
- the probe wrote a second full `DWORD 0x7FCCCCCC` there;
- the live caller leaves the upper half of the `QWORD` zero instead.

So the runtime crashes seen so far still do not disprove the builder path.
They only prove that our hand-built seed record was not faithful enough.

Immediate next step:
- rebuild the probe with the exact live seed layout;
- keep the correct source pointer `inline slot + 0x20`;
- still keep `NetRespawn` disabled.

### Runtime Result: Exact Live Seed Layout Still Does Not Make `BuildRespawnTransformBlock` Safe

A final builder-only probe (`slot1_probe6.dll`) used all currently-corrected
inputs at once:

- real inline slot object:
  - `0x7FF68C6E8090`
- source pointer matching the live caller:
  - `inline slot + 0x20`
- exact live caller seed layout:
  - `DWORD 0x7FCCCCCC`
  - `QWORD 0x000000007FCCCCCC`
  - `DWORD mode`
  - zero vec4
  - default vec4 from `0x1428805D0`
- `NetRespawn` still disabled

The probe still crashed immediately after logging:
- `slot1 probe6 calling BuildRespawnTransformBlock`

So the remaining missing contract is narrower still:
- not slot lookup
- not source pointer shape
- not seed layout
- but some pre-call state or lifecycle requirement of the inline slot object
  itself, most likely around the live contents of `slot + 0x20` and nearby
  fields

The next runtime step should therefore be read-only:
- dump `slot + 0x20/+0x30/+0x40` and adjacent state
- do not call the builder again until that state is understood

### Runtime Result: Global Source Band `0x143DD7480 + idx*0x43D0` Is Distinct From The Inline Slot Object And Slot-1 Source Is Mostly Empty

A read-only probe dumped the global source band used by the live caller loop.
The relevant entries on this run were:

- `source0 = 0x7FF68C6F1450 + 0*0x43D0`
- `source1 = 0x7FF68C6F1450 + 1*0x43D0`

Observed state:

- `source0`
  - `+0x00 = 0x7FF68C6F1450`
  - `+0x10 = 0xEF68060B00000000`
  - `+0x20 = 0x0`
  - `+0x38 = 0xEF68060B00000000`
  - `+0xE8 = 0x0`
- `source1`
  - `+0x00 = 0x7FF68C6F1450`
  - `+0x10 = 0x0`
  - `+0x20 = 0x0`
  - `+0x38 = 0x0`
  - `+0xE8 = 0x0`

This is the first runtime-safe evidence that:

- the global source band is a separate object family from the inline slot band;
- slot `1` source entry is almost entirely uninitialized in the current
  campaign state;
- so a slot-`1` crash in the respawn builder path may simply be because the
  engine never prepared a valid source object for that slot.

That sharply raises the value of the next builder-only test:
- keep the exact live seed layout;
- but use the partially initialized `source0 + 0x20` source pointer instead of
  slot-`1` state.

### Runtime Result: Partially Initialized `source0 + 0x20` Still Does Not Make The Builder Safe

A builder-only probe (`slot1_probe9.dll`) switched to the global source band
identified from the live caller loop:

- source pointer:
  - `0x143DD7480 + 0x20` (`source0 + 0x20`)
- exact live seed layout preserved
- `NetRespawn` still disabled

The process still crashed immediately after:
- `slot1 probe9 calling BuildRespawnTransformBlock`

So the current runtime evidence is now:
- slot lookup fixed
- inline slot typing fixed
- seed layout fixed
- alternate global source band tested
- builder still crashes

That leaves one high-confidence remaining gap: the caller-side lifecycle or
preparation step that produces a valid source band before `0x141685C30` is ever
reached in the built-in path.

Further runtime retries should wait until that lifecycle is reconstructed from
`0x140BBFD70..0x140BC0425` and its surrounding helpers.

### Runtime Correction: `0x140A90E50` Returns An Inline Per-Slot Session Band, Not The Materialized Tracker

The read-only slot probes established a stronger reclassification of the
`0x140A90E50` seam.

The object at:

- `manager + 0x3C0 + idx*0x43D0`

is stable and readable, but its live contents on slot `1` are:

- `+0x20 = 0x5F50414D5F54454E`
- `+0x28 = 0x524F465F4E494152`
- `+0x30 = 0x5F5049565F545345`
- `+0x38 = 0x00000000004D4454`
- `+0x40 = 0x5F50414D5F54454E`
- `+0x48 = 0x53544E414E4D4552`

Decoded as little-endian ASCII, this is the session/map/mode band:

- `NET_MAP_`
- `RAIN_FOR`
- `EST_VIP_`
- `TDM`
- `NET_MAP_`
- `REMNANTS`

So the corrected interpretation is:

- `0x140A90E50` returns an inline per-slot session/source band;
- it is not the materialized tracker object used by the built-in respawn path;
- and the old idea of treating that object as a tracker/respawn carrier should
  stay retired.

This also explains why `slot+0x20` was a bad builder source despite the seam
itself being readable and stable.

### Tooling Update: External Read-Only Code Dumper Prepared For The Next Session

Because:

- the IDB ranges around `0x140BBFD70`, `0x140B44EC0`, and `0x141685C30` are
  still corrupted enough to degrade pseudocode,
- and blind injected builder probes have stopped yielding new signal,

a new external read-only helper was added:

- `scanner/src/runtime_code_dump.cpp`
- target: `tll_runtime_code_dump.exe`
- follow-up disassembler helper: `scripts/disasm_runtime_code_bins.sh`

Its job is to dump live process code bytes for:

- `0x140BBFD70`
- `0x140B44EC0`
- `0x141685C30`
- `0x140B42310`
- `0x141C8D740`

into binary blobs under `reports/`.

The current session ended before this tool could be run on a live `tll.exe`,
but it is now the preferred next step for reconstructing the missing
caller-side lifecycle above `BuildRespawnTransformBlock`.

### Runtime Correction: The Builder Was Fine; The C++ Seed Packing Was Wrong

The repeated `BuildRespawnTransformBlock` crashes were finally traced to the
probe itself, not to the engine helper.

The earlier probe structs placed:

- `u32`
- then `u64`

in a normal C++ aggregate, which inserted alignment padding and therefore did
**not** reproduce the live stack layout:

- `+0x0  dword 0x7FCCCCCC`
- `+0x4  qword 0x000000007FCCCCCC`
- `+0xC  dword mode`
- `+0x10 vec4 zero`
- `+0x20 vec4 default`

After switching to a raw byte seed with explicit offsets, the helper
`0x141685C30` (`BuildRespawnTransformBlockFromReferenceTransformAndSeedRecord`)
became stable.

Confirmed live runtime success:

- `source0 + 0x20`
- `source1 + 0x20`
- `Nadine + 0x100`

all return a valid `0x50` block without crashing.

That removes the builder contract itself from the blocker list.

### Runtime Result: `manager+0x3A0..+0x3C0` Is Still Not The Actual `NetRespawn` Receiver

A read-only pointer-band probe dumped the manager region:

- `manager+0x380 = 0x7FF689ED4DB0`
- `manager+0x388 = 0x7FF689ED4DA0`
- `manager+0x390 = 0x7FF689F20F10`
- `manager+0x398 = 0x7FF689F20F00`
- `manager+0x3A0 = 0x7FF689ED4D90`
- `manager+0x3A8 = 0x7FF689ED4D80`
- `manager+0x3B0 = 0x7FF689ED4D70`
- `manager+0x3B8 = 0x7FF689ED4D60`
- `manager+0x3C0 = 0x7FF689ED4D50`

Dereferencing them does not yield a normal tracker/runtime object shape. The
first qwords are poison/code-like values such as:

- `0xCCCCCCCCCCC301B0`
- `0xC00000016B81B60F`

So this whole manager pointer band remains suspect as an upper indirection
layer, not a confirmed `NetPlayerTracker` object family.

### Runtime Result: `manager+0x3A8` Enters `NetRespawn` But Does Not Return

With the builder fixed, the next probe used:

- respawn block built from `Nadine + 0x100`
- `rcx = *(manager + 0x3A8)`
- `edx = 0`
- `r8b = 1`
- `r9 = &respawn_block`

The log reached:

- `slot1 probe13 calling NetRespawn`

and never logged a return.

That is enough to demote `manager+0x3A8` as a likely wrong receiver for
`0x140B347C0`.

So the current blocker is now narrower and different:

- the respawn block builder works;
- the remaining unknown is the real receiver object for `NetRespawn`, not the
  block format.

### Runtime Result: `AddOrUpdateNetPlayer(slot=1, source0, flag=0)` Does Not Return

`slot1_probe14` called:

- `manager = *(0x143DD70C0)`
- `context = 0x143E08C80`
- `source0 = 0x143DD7480`
- `slot = 1`
- `flag = 0`

and reached:

- `slot1 probe14 calling AddOrUpdateNetPlayer(slot1, source0, flag0)`

but never logged a return.

This is the first direct runtime result on the top manager seam itself. It
means the blocker has moved again:

- `BuildRespawnTransformBlock` is no longer the issue;
- `manager+0x3A8` is not enough by itself;
- the direct `AddOrUpdateNetPlayer` path still needs a correct `source_obj`
  contract or caller-side pre-initialization before slot `1` can be brought up
  safely from a probe.

### Static Correction: `0x140A90550` Expects A Source Entry, Not The `0x143DD7480` Base

Retail caller disassembly around:

- `0x140A392EC / 0x140A3936A`
- `0x140A58287 / 0x140A58494`

now makes the `r8` contract concrete enough to explain `probe14`.

The function does **not** want the raw global base `0x143DD7480`.

Observed real callers pass:

- an actual source entry pointer already loaded into a register (`rdi`/`rsi`)
- not the array base itself

The late-join bridge also shows the relevant source family shape directly:

- base: `0x143DD7480`
- stride: `0x43D0`

So `probe14` was wrong in a narrow but important way:

- it called `AddOrUpdateNetPlayer(slot=1, source0_base, flag=0)`
- while the real contract is `AddOrUpdateNetPlayer(slot=1, source_entry, flag=0)`

The next prepared runtime step is now `slot1_probe15`, which uses:

- `source_entry1 = 0x143DD7480 + 0x43D0`

instead of the raw base pointer.

### Static Follow-Up: The Dual-Slot Caller Gets `source_obj` From `0x14194D5F0`, Not Directly From The Raw Source Array

`slot1_probe15` confirmed that the naive array entry is still empty in campaign:

- `source_entry1 + 0x1DC = 0`
- `source_entry1 + 0x1E0 = 0`
- `source_entry1 + 0xE8  = 0`

and `AddOrUpdateNetPlayer(slot=1, source_entry1, flag=0)` still did not
return.

The missing piece is now pinned by the retail caller around `0x140A392A2`:

- it does **not** pull the `r8` source entry directly from `0x143DD7480`
- it first calls `0x14194D5F0(0x143E08C80, out_list, 0)`
- then iterates the produced pointer list from `rsp+0x58`

Important consequences from `0x14194D5F0`:

- fast path (`context+0x18 == 0`):
  - materializes a single source entry at `context + 0x1D00`
  - stores that pointer into the output list
- enumerating path:
  - scans a `0x1C0`-stride family at `context + 0x800`
  - only emits entries whose `+0xE0` gate is set

So the current best source candidate for the built-in slot-`1` patch is no
longer:

- `0x143DD7480 + 0x43D0`

but the exact entry pointer produced by:

- `0x14194D5F0(0x143E08C80, ...)`

with the campaign fast-path likely yielding:

- `0x143E08C80 + 0x1D00`

`slot1_probe16` confirmed that simply pointing at `context+0x1D00` is still
too weak if the producer step is skipped:

- `context+0x18 = 0`
- `source_fast + 0x1DC = 0`
- `source_fast + 0x1E0 = 0`
- `source_fast + 0xE8  = 0`

and `AddOrUpdateNetPlayer(slot=1, source_fast, flag=0)` still does not return.

That leaves one sharper runtime seam:

- call `0x14194D5F0(context, out_list, 0)` first
- use the produced `entry0`
- only then call `0x140A90550`

`slot1_probe17` is prepared exactly in that form.

### Runtime Result: `BuildNetPlayerSourceList` Also Needs The Upstream Context Preflight

`slot1_probe17` reached:

- `context+8  = 0`
- `context+18 = 0`
- `calling BuildNetPlayerSourceList(context, out, 0)`

and did not return.

That means the exact retail sequence is still missing one step above the
builder.

The same dual-slot caller shows that before the `0x14194D5F0` call it does:

- `rcx = *(0x143E08C88)`
- `rdx = &0x143E08C80`
- `r8d = 1`
- `call [vtable + 0x78]`

Only after that preflight does it call `0x14194D5F0`.

So the next remaining narrow seam is no longer the source entry itself, but the
upstream shared-context bringup call through:

- `*(0x143E08C88)->virtual + 0x78`

### Runtime Result: The `0x143E08C88` Shared-Context Owner Is Null In Campaign

`slot1_probe18` reached a non-crashing readout:

- `manager = 0x7FF68C6E3900`
- `context = 0x7FF68DB08C80`
- `owner = 0`
- `context+8  = 0`
- `context+18 = 0`

and exited early because the upstream owner pointer itself is absent.

That is a useful correction:

- the previous blocker was not “wrong preflight slot” anymore
- the whole `0x143E08C88 -> +0x78 -> 0x143E08C80` seam is simply inactive in
  the current campaign state

The next candidate is therefore the sibling upper context family already used
by nearby branches:

- `0x143E0EF00`
- `0x143E0EF08`

### Structural Correction: `0x143E08C88` And `0x143E0EF08` Are Embedded `+0x8` Owner Fields

The recent runtime/static pass makes one thing explicit:

- `0x143E08C80` and `0x143E0EF00` are concrete global context objects
- `0x143E08C88` and `0x143E0EF08` are just their embedded `+0x8` pointer fields

Evidence:

- the early constructor at `0x14007C104` initializes the object at `0x143E0EF00`
  and zeroes `0x143E0EF08`
- `0x140ACBE10` is a concrete constructor for one of these `0x800`-backed
  context families and ends with `mov [rcx+0x8], 0`
- runtime `probe19` simply confirmed that both objects currently have a null
  `+0x8` field in campaign

So the active question is no longer “who writes those two globals”, but:

- which method populates the context object's `+0x8` owner pointer
- and in which subsystem state that owner becomes live

### Structural Follow-Up: `0x143E0EF18` Is `ctxB + 0x18`, Not A Standalone Global

The latest static/live pass tightens the second shared-context family further:

- `0x143E0EF00` is the real context object
- `0x143E0EF08` is its embedded owner pointer at `ctxB + 0x8`
- `0x143E0EF18` is its embedded current-selected pointer at `ctxB + 0x18`

Concrete evidence:

- `0x14194CFB0` works directly on `ctxB + 0x18` and falls back to the
  one-entry path only when that field is null
- `0x14194D5F0` does the same split:
  - if `ctxB + 0x18 == 0`, it builds the fallback entry from `ctxB + 0x8`
    into `ctxB + 0x1D00`
  - otherwise it enumerates the active `+0x800` / `0x1C0`-stride entry family
- live campaign probe still shows both fields null:
  - `ctxB + 0x8  == 0`
  - `ctxB + 0x18 == 0`

### New Active Lifecycle Anchor: `0x140B1E400`

`0x140B1E400` is now the strongest ctxB-side synchronization point.

What it actually does:

- scans candidate objects from the `0x146B442C0/0x146B442E8` bands
- computes the current selected object as:
  - `ctxB + 0x18` if `ctxB + 0xC8 != 0`
  - otherwise null
- if the candidate differs from the current selected object, it does **not**
  immediately commit `ctxB + 0x18`
- instead it stages a pending publish into:
  - `0x143E10E90 = candidate_ptr`
  - `0x143E0F618..0x143E0F638 = staged vector block`
  - `0x143E10EC8 = 0`
  - then calls `0x140BF1FF0(1, 0)`

So `0x140B1E400` is a pending-selection publisher, not the final writer of
`ctxB + 0x18`.

### Important Negative Result: Still No Non-Null Campaign Bind For `ctxB + 0x8`

The constructor / reset chain is now clear:

- `0x140ACBE10` zeroes `ctx + 0x8`
- `0x14007C104` / `0x140B1DF80` install ctxB's vtable and zero `ctxB + 0x8`
- `0x140B1E0A0` tears down through `ctxB + 0x8 -> owner->virtual +0x50`,
  then clears `ctxB + 0x8` again

What is still missing:

- a non-null writer for `ctxB + 0x8`
- a final commit path from pending `0x143E10E90` into `ctxB + 0x18`

This matters because the campaign runtime result is now stronger:

- the net/shared context family is not merely "idle"
- its owner bind (`ctxB + 0x8`) and active selection (`ctxB + 0x18`) are both
  absent in the current campaign state
- so blind runtime probing above `0x14194D5F0` is no longer the right move

### New Commit Handoff: Pending `0x143E10E90` Goes Through A Global Service, Not A Local Store

The next static pass closes the missing transition one level further:

- the larger ctxB state machine around `0x140B1E890..0x140B1FA61` is keyed by
  `0x143E10E84`
- the branch at `0x140B1ECBC` is the first confirmed **pending -> commit
  handoff** for ctxB

What this branch actually does:

- loads `r8 = 0x143E10E90` (the pending candidate)
- if the pointer is non-null, calls a global service:
  - `rcx = *(0x143880318)`
  - `rdx = &0x143E0EF00` (`ctxB`)
  - `r8  = pending candidate`
  - `call [rcx->vtable + 0x48]`
- immediately clears `0x143E10E90 = 0`
- then either:
  - exits on negative status, or
  - publishes the follow-up ctxB packet through `0x142204E50`

This is the strongest current result on the commit side:

- `0x140B1E400` is still only the publisher/stager
- but the staged candidate is no longer a dead end
- it is handed to a **global service commit path** at `0x140B1ECBC`

Important correction:

- the nearby `+0x18` stores inside the `0x140BF26A0/0x140BF26E0` helper band are
  scratch/helper-object initialization, not the final ctxB current-selected
  store
- the real unresolved step is now narrower:
  - what `*(0x143880318)->virtual + 0x48(&ctxB, pending)` does internally
  - and whether it writes `ctxB + 0x18` directly or delegates once more

### Service Layer Pinned Further: `0x143880318` Is A Mode-Dependent Global Selection Service

The next pass tightens the global service behind the pending-commit handoff:

- startup block `0x1409FA94E..0x1409FAA23` installs a mode-dependent service
  object into the global pair:
  - variant A: `0x141973750`, stored to both `0x143880300` and `0x143880318`
  - variant B: `0x1419796F0`, stored to `0x143880320` and then to
    `0x143880318`
- immediately after install it runs a common bind path:
  - `0x1409EB500(service, 0, upper_context)`
  - then `service->virtual + 0x8`

- teardown block `0x1409FDD70..0x1409FDDEE` does the inverse:
  - `service->virtual + 0x18`
  - then `service->virtual + 0x0`
  - then clears both `0x143880318` and `0x143880300`

This service is now confirmed to sit above both shared-context families:

- startup calls `service->virtual + 0x20` on:
  - `0x143E08C80`
  - `0x143E0AC40`
  - and one additional context object passed in `rsi`
- the ctxB pending branch `0x140B1ECBC` calls `service->virtual + 0x48`
  with:
  - `rdx = &0x143E0EF00`
  - `r8  = 0x143E10E90`

So the missing piece is no longer “is there any global manager at all”.
There is. The remaining unknown is specifically the implementation of this
service family's `virtual + 0x48` commit.

### New ctxB Object Surface: No Direct `ctxB + 0x18` Writer In Visible Object Slots

The next static pass closed a large part of the concrete `ctxB` object family.

First, the object itself is now pinned:

- `0x14007C104` initializes the concrete object at `0x143E0EF00`
- it calls `0x140ACBE10`
- then zeroes `ctxB + 0x8`
- then installs vtable `0x1429EF890`

Important constructor detail:

- `0x140ACBE10` is only the base constructor
- it seeds the `+0x800` entry family and clears `ctxB + 0x8`
- it does **not** bind the missing owner

The visible `ctxB` vtable slots are now typed further:

- `0x14194E0D0`
  - wildcard dispatcher over the `ctxB` entry family
- `0x14194E0E0`
  - resolves the desired current entry id and re-enters the `virtual +0x08`
    path
- `0x14194DE00`
  - dispatches the requested state across the 12-entry `0x1C0`-stride table
- `0x14194DEE0`
  - larger rebuild/select pass over the same entry table
- `0x140B20760`
  - just marks a global dirty byte
- `0x140B20770`
  - resets local runtime flags and tails into the refresh path
- `0x14194C550`
  - emits a selection-present event packet when `ctxB + 0x718` is live
- `0x14194C5F0`
  - emits another selection-present packet when current state exists
- `0x14194C690`
  - does the same when `ctx->+0x18` is live
- `0x14194C730`
  - if `ctxB + 0x8` owner exists, calls `owner->virtual +0x50(ctxB)`,
    then re-enters `ctxB->virtual +0x60`
- `0x140B1FAF0`
  - consumes current-selection state and entry counters, but does not assign
    current selection

The useful negative result is now stronger:

- no direct absolute store to `ctxB + 0x18` was found
- no visible `ctxB` vtable slot above performs that store either

So the live `ctxB + 0x18` transition is now most likely one of:

1. indirect through `ctxB + 0x8 -> owner->virtual +0x50`
2. indirect inside the global selection service `virtual +0x48`

### New Narrowing: `ctxB + 0x8` Is Still The Missing Bind Surface

Two nearby helpers tighten the local side further:

- `0x140B1E100`
  - small pending-selection publisher
  - writes the staged candidate into `0x143E10E90`
  - mirrors the vector block into `0x143E0F618..+0x38`
  - does not commit `ctxB + 0x18`
- `0x140B1E1A0`
  - upper permit gate that consults both shared-context families

And the reset/unbind path is now fully concrete:

- `0x140B1E0A0`
  - resets active entries through `0x14194CD20`
  - if `ctxB + 0x8` owner exists, calls `owner->virtual +0x50(ctxB)`
  - then re-enters `ctxB->virtual +0x60`
  - then explicitly clears `ctxB + 0x8`

This means the blocker is no longer vague:

- `ctxB + 0x8` definitely participates in the live bind/refresh contract
- but the first non-null writer for `ctxB + 0x8` is still missing

### New Commit-Side Narrowing: Service `virtual +0x48` Does Queue/Packet Work, Not A Plain Direct Store

Both mode-dependent implementations of `*(0x143880318)->virtual +0x48` are now
decoded from retail disassembly:

- variant A `0x141975870`
- variant B `0x14197AE50`

Common result:

- neither implementation performs an obvious direct global store to
  `ctxB + 0x18`
- both look like queue/packet/entry-service workers
- both consume the pending candidate and prepare service-side state

Variant B is pinned hardest:

- scans a `0x9570`-stride service entry family under `service + 0x4B8`
- resets per-entry state
- calls `service->virtual +0x10`
- builds a `0xC0` packet from `candidate + 0x1ED0/+0x1ED4` and global state
- writes `candidate + 0x18 = r8`
- clears `candidate + 0xB8/+0xBC`
- loops until `candidate + 0xB8` changes

Variant A is similar in shape:

- scans four `0x8D68`-stride bands under `service + 0x50`
- does not directly assign `ctxB + 0x18`

So the remaining unresolved edge is now very narrow:

- either the true `ctxB + 0x18` writer is hidden behind the owner callback
  `ctxB + 0x8 -> virtual +0x50`
- or it is performed deeper inside the service/entry family after the
  visible `virtual +0x48` worker returns

### New Bind-Side Result: The First Real Writer To `ctx + 0x8` Is Service `virtual +0x20`

The next static pass finally pinned a real writer for the embedded owner field
used by both shared-context families.

Variant A service:

- vtable `0x142B1DA18`
- `virtual +0x20 = 0x1419739E0`
- `virtual +0x28 = 0x141973A90`
- `virtual +0x48 = 0x141975870`

Variant B service:

- vtable `0x142B1E908`
- `virtual +0x20 = 0x141979A60`
- `virtual +0x48 = 0x14197AE50`

The key part is the same in both `virtual +0x20` variants:

- find or allocate a free service entry
- store the context pointer into the service entry array
- then write:
  - `context + 0x8 = service`

Concrete stores:

- variant A:
  - `0x141973A38: [service + slot*0x8D68 + 0x50] = ctx`
  - `0x141973A3D: [ctx + 0x8] = service`
- variant B:
  - `0x141979ABD: [service + slot*0x9570 + 0x4B8] = ctx`
  - `0x141979AC5: [ctx + 0x8] = service`

So the first real non-null writer to the embedded owner field is no longer
unknown. It is the service family's `virtual +0x20` bind.

### Important Negative Result: Startup Bind Still Does Not Touch `ctxB`

The same pass also narrowed the campaign blocker further.

Startup around `0x1409FAA43` calls `service->virtual +0x20` only for:

- `0x143E08C80`
- `0x143E0AC40`
- `rsi` (the third context object passed through that stage)

It does **not** call `service->virtual +0x20` for:

- `0x143E0EF00` (`ctxB`)

I also checked the retail disassembly for any window that combines:

- `0x143880318`
- `&ctxB = 0x143E0EF00`
- and `service->virtual +0x20`

Current result:

- no such `ctxB` bind call-site is visible in the retail `.text` scan

This is now the strongest explanation for the campaign state:

- `ctxB + 0x8` stays null
- because the real bind writer is known,
- but `ctxB` is not currently observed going through that bind surface

So the next narrow step is no longer “find any writer to `ctxB + 0x8`”.
It is:

- find the first retail path that feeds `ctxB` into `service->virtual +0x20`,
  or
- prove that campaign instead uses the `ctxB->virtual +0x28` /
  `service->virtual +0x48` family without ever performing the standard bind

### Correction: `0x140B1EE70` Uses Fixed `ctxB` Token `0xA`, Not `0x140B204E0`

Direct `ctxB` vtable decode from `0x140B1DF80 -> 0x1429EF890` corrected the
earlier slot mapping.

Relevant `ctxB` slots are now concrete:

- `+0x28 = 0x140B1DF30`
- `+0x38 = 0x140B204A0`
- `+0x60 = 0x140B207A0 -> 0x14194C550`

That means `0x140B1EE70` is still the live `ctxB -> global-service` request
path, but its first local call is not the heavy worker at `0x140B204E0`.

It actually does:

- `ctxB->virtual +0x28`
- returns fixed token `0xA`
- then forwards that token into
  `*(0x143880318)->virtual +0x28(service, &ctxB, 0xA, 1)`

So the `r8d` handed to the service is currently a fixed local selection token,
not a derived world-state hash.

`0x140B204E0` remains a neighboring heavy request/payload builder in the same
family, but it is no longer tied to `ctxB->virtual +0x28`.

A retail image scan also found no direct `rel32` callers for `0x14194BE80`, so
that isolated `mov [ctx+0x8], rdx` helper is not yet a public bind seam by
itself.

### Service `virtual +0x48`: Variant B Is A Real Pending -> Commit Worker

The next important narrowing is on `service->virtual +0x48`.

Variant A `0x141975870` still looks mostly like queue/packet work:

- finds the bound entry for `ctx`
- clears `ctx + 0x18`
- packages `ctx + 0x1ED0/+0x1ED4` plus the incoming pending pointer
- publishes into the service-local payload store at `service + 0x24600`

Variant B `0x14197AE50` is much more direct and is now the strongest live
commit candidate:

- finds the bound service entry for `ctx`
- resets the entry-local object at `entry + 0x8D28`
- calls `service->virtual +0x10`
- builds a `0xC0` payload from:
  - `ctx + 0x1ED0`
  - `ctx + 0x1ED4`
  - the incoming pending pointer passed as `r8`
- writes the incoming pending pointer to:
  - `ctx + 0x18`
- then spins through repeated `service->virtual +0x10` calls until
  `ctx + 0xB8` flips
- if the wait resolves with `ctx + 0x18 == 0`, it stores `-1` in `ctx + 0xBC`
- finally sets `ctx + 0xB8 = 1`

This is the first concrete path that does perform a visible store to
`ctxB + 0x18`, but only inside variant B's `service->virtual +0x48` worker.

### Updated Narrowest Unknown

The blocker shifted again:

- the plain startup `service->virtual +0x20(&ctxB)` bind is still not observed
- `service->virtual +0x48` is still a real pending -> commit worker, but it
  only matters if variant B is actually forced
- the remaining unknown is back to the local owner contract around `ctxB+0x8`

So the next narrow questions are:

- what writes `ctxB + 0x8` in campaign, and
- whether campaign ever forces variant B at all outside the debug/test gate.

### Correction: `0x1430641DC` Is `Net Init Error Testing`, And Startup Keeps Variant A By Default

The startup logic around `0x1409FA94E` is now concrete enough to matter.

It does not choose the service variant arbitrarily, and `0x1430641DC` is not a
normal game-mode enum. It is the selector behind the `Net Init Error Testing`
setting.

1. constructs variant A
   - `0x141973750`
   - stores it to `0x143880300` and `0x143880318`
2. copies the upper packet into the service header through `0x1409EB500`
3. runs `service->virtual +0x8`
4. keeps variant A if:
   - `service->virtual +0x8` succeeded, and
   - `0x1430641DC != 0x14`
5. only if init fails, or `0x1430641DC == 0x14`, does it construct variant B
   - `0x1419796F0`
   - stores it to `0x143880320` and then overwrites `0x143880318`

`0x14` maps to `MatchmakingServer` in that test-enum string table, so variant B
is now best read as a forced fallback/debug branch, not the default campaign
path.

So the unresolved runtime question is now narrower than before:

- does campaign ever leave variant A at all,
- or is variant B only reachable through failing init / explicit test forcing.

### Exact Vtable Result: The Live `service->+0x28` Slot Is Now Fully Pinned

The `.rdata` vtables are now decoded directly, so the `service->+0x28`
discussion is no longer based on nearby helper guesses.

Exact slot maps:

Variant A `0x142B1DA18`:

- `+0x20 = 0x1419739E0`
- `+0x28 = 0x141974E10`
- `+0x48 = 0x141975870`

Variant B `0x142B1E908`:

- `+0x20 = 0x141979A60`
- `+0x28 = 0x14197ABC0`
- `+0x48 = 0x14197AE50`

That means the active campaign call at `0x140B1EE70` is now exact:

- if service A is installed, it reaches `0x141974E10`
- if service B is installed, it reaches `0x14197ABC0`

Both exact `+0x28` workers accept the real call shape:

- `rcx = service`
- `rdx = &ctxB`
- `r8d = token_from_ctxB_slot_+0x28`
- `r9b = 1`

Variant A `0x141974E10`:

- resets the ctx entry
- stores the token in `ctx + 0x208`
- seeds `ctx + 0x1ECC = 0x100`
- builds a larger `0x128` payload into `service + 0x24600`

Variant B `0x14197ABC0`:

- resets the ctx entry
- stores the token in `ctx + 0x208`
- clears entry-local state at `entry + 0x8D68`
- seeds a request through `0x141920B00`
- then drives local follow-up through `ctx->virtual +0x48` and `0x14194CA10`

So the old compatibility guess is gone. The open question is now purely which
variant is installed at `0x143880318` in campaign when the live `ctxB` path
runs.

### Post-Bind Consumer Band: `0x140B21DE0 / 0x140B21E70 / 0x140B21F10`

The next static pass pinned a useful consumer band above the still-missing
owner bind.

- `0x140B21E70` and `0x140B21F10` both require:
  - `ctxB + 0x18 != 0`
  - `ctxB + 0x8 != 0`
- both then call:
  - `0x14194D5F0(&ctxB, out_list, 0)`

That makes them immediate post-bind users of the `ctxB + 0x8` contract.

`0x140B21E70` goes further:

- enumerates the built source list
- resolves each entry through `0x14194E510(&ctxB, entry)`
- and returns success once a resolved object has byte `+0x42 != 0`

`0x140B21F10` is a simpler gate:

- if `ctxB + 0x18` or `ctxB + 0x8` is missing, it returns `1`
- if `0x14194D5F0` yields at least one source entry, it returns `0`

This does not reveal the first non-null writer for `ctxB + 0x8`, but it does
pin the next high-value seam:

- once `ctxB + 0x8` is bound, campaign immediately feeds that owner contract
  into `0x14194D5F0`
- so the next useful static direction is not broader service probing, but the
  local owner-bind -> source-list consumer band around
  `0x14194D5F0 / 0x14194E510`.

### Owner Interface Contract Around `ctxB + 0x8` Is Wider Than Just Teardown

The next pass tightened the bound-owner contract itself.

Previously only teardown-style slots were pinned:

- `owner->virtual +0x50(&ctxB)`
- `ctxB->virtual +0x60()`

Now several additional live consumer sites are anchored:

- `0x140AEAAFC`
  - if `ctxB + 0x8` exists, calls `owner->virtual +0x58(&ctxB, 1)`
  - then immediately rebuilds the source list through `0x14194D5F0`
- `0x140835A95`
  - calls `owner->virtual +0x70(&ctxB, source_id_from_entry+0xE8)`
- `0x14083E5AE`
  - same `owner->virtual +0x70(&ctxB, source_id)` pattern
- `0x1409F9259`
- `0x140BFB700`
- `0x140C05A54`
- `0x140C07DEB`
- `0x140C0AA55`
- `0x140C0C1F5`
- `0x140C0C2F5`
- `0x140C0C3E6`
  - all load `ctxB + 0x8` and call `owner->virtual +0x58(...)`
- `0x140C11697`
  - loads `ctxB + 0x8` and calls `owner->virtual +0x78(...)`

This matters because it means the missing bind is not feeding a trivial owner.
Once `ctxB + 0x8` becomes non-null, campaign immediately drives a richer owner
interface:

- `+0x50`
- `+0x58`
- `+0x70`
- `+0x78`

So the remaining writer search is now framed more tightly:

- not just "who stores any pointer into `ctxB + 0x8`"
- but "who binds the concrete owner that implements this `+0x50/+0x58/+0x70/+0x78` contract for `ctxB`"

### New Fallback Contract: `0x14194D5F0` Clones The Owner Header Directly

The next static pass tightened the single-entry fallback path enough to matter.

If `ctxB + 0x18 == 0`, `0x14194D5F0` does **not** call back into the owner to
build the fallback source. It directly clones a fixed header from the bound
owner pointer in `ctxB + 0x8`:

- copies `owner + 0x8 .. +0x27` into `ctxB + 0x1D00 .. +0x1D1F`
- copies dword `owner + 0x28` into `ctxB + 0x1D20`
- emits that `ctxB + 0x1D00` block as the single fallback source entry

That lines up exactly with startup helper `0x1409EB500`, which seeds the live
global selection service object `0x143880318` with:

- `service + 0x8 .. +0x27 <- upper_packet + 0x0 .. +0x1F`
- `service + 0x28 <- upper_packet + 0x20`

This is the strongest local evidence so far that the intended fallback owner
for `ctxB + 0x8` can be the **service object itself**, not a separate
service-entry pointer.

So the bind-side question is narrower again:

- not merely "who writes any owner into `ctxB + 0x8`"
- but "where does campaign actually perform the missing
  `service->virtual +0x20(&ctxB)` bind that would make the service header
  visible through `ctxB + 0x8`"

### Local ctxB Wrappers Above The Owner Contract

The same pass also pinned several tiny local wrappers on top of `ctxB + 0x8`:

- `0x14194DB30`
  - `owner->virtual +0x58(ctxB, bool)` or return `2`
- `0x14194DB70`
  - `owner->virtual +0x60(ctxB, bool)` or return `2`
- `0x14194DB90`
  - `owner->virtual +0x98(ctxB)`
- `0x14194DBB0`
  - `owner->virtual +0xA0(ctxB)`
- `0x14194DBD0`
  - `owner->virtual +0xA8(ctxB, token)`
- `0x14194DBF0`
  - stores token in `ctxB + 0x200`
  - then dispatches `owner->virtual +0x78(ctxB, token)`

This widens the proven local owner-facing surface above the earlier
`+0x50/+0x58/+0x70/+0x78` consumers, but still does not reveal the first
campaign bind into `ctxB + 0x8`.

### Important Correction: The Dual-Slot `AddOrUpdate` Caller Still Sources From `ctxA`, Not `ctxB`

The next pass on the upper caller around `0x140A392EC / 0x140A3936A` changes an
important assumption.

That caller does inspect `ctxB`:

- builds `ctxB` source lists through `0x14194D5F0(&ctxB, out, 0)`
- compares `ctxB` and `ctxA` source counts
- uses that comparison in upper gating/selection

But when it actually creates tracker entries through
`0x140A90550(NetGameManager::AddOrUpdateNetPlayer)`, the real call shape is
still:

- `rcx = 0x143DD70C0` manager
- `rdx = &0x143E08C80` (`ctxA`)
- `r8  = source_entry_from_ctxA_list`
- `r9d = slot`

So in this strongest known dual-slot manager caller:

- `ctxB` is a gating / availability / selection input
- `ctxA` remains the real source context for `AddOrUpdate`

This matters because it means the missing `ctxB + 0x8` bind is still important
for the shared-context selection layer, but it is **not** currently the direct
manager-side source seam for built-in slot bringup.

### Correction: Startup Does Bind `ctxB` Through `service->virtual +0x20`

The latest retail pass invalidated the earlier "startup skips ctxB" reading.

The exact startup sequence inside the `0x1409F9B20` function is now pinned:

- `0x1409FA6DE`
  - `rsi = 0x143E0EF00` (`ctxB`)
- `0x1409FAA43 .. 0x1409FAA54`
  - `*(0x143880318)->virtual +0x20(service, &0x143E08C80)`
- `0x1409FAA57 .. 0x1409FAA68`
  - `*(0x143880318)->virtual +0x20(service, &0x143E0AC40)`
- `0x1409FAA6B .. 0x1409FAA78`
  - `*(0x143880318)->virtual +0x20(service, rsi)`
  - and at this point `rsi == &ctxB`

So startup really does bind all three global shared-context objects:

- `0x143E08C80`
- `0x143E0AC40`
- `0x143E0EF00` (`ctxB`)

I also re-scanned the retail `.text` by walking `.pdata` and looking for the
global-service `call [rax+0x20]` shape. Current result:

- exactly these three startup bind sites are visible for `0x143880318`
- no additional later `service->virtual +0x20(&ctxB)` call-site showed up
- no raw-store equivalent of `ctxB+0x8 = service` showed up in the same
  service/ctxB function family either

This changes the blocker materially:

- the binary does contain a real bind path for `ctxB`
- the remaining problem is now lifecycle/state:
  explain why live campaign can still end up with `ctxB+0x8 == 0`
  despite the startup bind

The strongest already-known clear/unbind points remain:

- `0x1409FDD34`
- `0x140B1E0E8`

### New ctxB Vtable Narrowing: `ctxB->virtual +0x20` Is Only A Getter

Direct `.rdata` decode from `0x140B1DF80 -> 0x1429EF890` pinned the concrete
ctxB slots further:

- `+0x20 = 0x1407DF190`
  - returns `ctxB+0x18` only if byte `ctxB+0xC8 != 0`
  - otherwise returns `0`
- `+0x38 = 0x140B204A0`
  - toggles bit 0 in `ctxB+0x200`
  - loads owner from `ctxB+0x8`
  - tail-calls `owner->virtual +0x78(ctxB, token)`
- `+0x40 = 0x140B204D0`
  - returns the inverted low bit of `ctxB+0x200`

Practical consequence:

- upper callers that only do `ctxB->virtual +0x20` are not bind paths
- they are consumers of already-existing current-selection state
- the startup `service->virtual +0x20(&ctxB)` bind is now the first confirmed
  owner-writer, while the ctxB object methods sit downstream of it

### New Lifecycle Narrowing: Later Clear Paths Now Have Concrete Upper Callers

After the startup-bind correction, the next question became:
who later clears the owner back out of `ctxB+0x8` in live campaign state.

Direct call-target scan now pins the main clear path much tighter:

- `0x1409FD360`
  - direct callers:
    - `0x14014EAB0`
    - `0x1409EE7D0`
    - `0x1409EE9B0`
    - `0x1409F78B0`
    - `0x1409F7C60`
    - `0x1409FF21E`
    - `0x141CE32E9`
- `0x140B1E0A0`
  - direct retail `call` xrefs: none found

This is useful because it moves the live-owner-null problem out of the
abstract "maybe there is no bind" bucket.

The current strongest reading is now:

- startup does bind `ctxB`
- later upper lifecycle/state transitions can still drive the
  `0x1409FD360` teardown family
- that teardown family is a better next target than more speculative
  probing around the original bind site

### New Lifecycle Correction: `0x1409FD360` Is A Broad Front-End Net Shutdown Path

The next static pass tightened the direct callers above `0x1409FD360` enough
to classify that family better.

What is now concrete:

- `0x1409FD360` is broader than a local `ctxB`/service helper:
  - it resets the shared-context entry bands,
  - clears `0x143E0EF08` and `0x143E08C88`,
  - tears down `0x143880318` through `virtual +0x18` then `virtual +0x0`,
  - and clears the global service aliases.
- `0x1409EE7D0` is an upper token handler:
  - hashes stage strings including `added-to-stage` and `popped`
  - can drive `0x1409FD360` from that front-end transition band
- `0x1409F78B0` is another upper token path:
  - hashes `update`
  - works through `0x14383E560->virtual +0x30/+0x38`
  - on its restart branch calls `0x1409FD360`, then continues into
    `0x1409FBA60`
- `0x1409F7C60` is the sibling upper token path:
  - checks hashes for `update` and `popped`
  - works through `0x14383E560->virtual +0x40`
  - can also call `0x1409FD360`, then `0x1409FBA60`
- `0x1409EE9B0` now looks explicitly multiplayer/front-end:
  - its surrounding tokens include `ss-fmenu-multi-main`
  - `PAGE_MULTIPLAYER_CONNECTING`
  - `part-module/part-multiplayer`
- `0x141CE32E9` is another upper state gate:
  - only reaches `0x1409FD360` on the `edx == 4` branch
  - and only when byte `[0x143FE1080 + 0xB3]` is set

One more useful negative result:

- the direct surface of `0x1409FBA60` does **not** show a direct jump back
  into:
  - `0x1409FA94E`
  - `0x1409EB500`
  - `0x143880318`
  - or the concrete bind workers `0x1419739E0 / 0x141979A60`

So the strongest current lifecycle reading became narrower, but not fully
one-way yet:

- the live `ctxB+0x8 == 0` state is still more plausibly inherited from an
  earlier front-end/net shutdown family,
- but the restart side is **not** absent:
  the remaining question is now whether the later re-entry callback actually
  fires and reaches the bind.

### New Re-Entry Narrowing: `0x1409FBA60` Schedules `0x1409F9B20` Indirectly

The next pass tightened the startup side enough to correct one more thing.

Direct xref search for `0x1409F9B20` showed:

- no direct `call 0x1409F9B20` sites
- but one address-taken site inside `0x1409FBA60`:
  - `0x1409FD068: lea rax, 0x1409F9B20`

The surrounding code in `0x1409FBA60` is now concrete:

- builds a local callback record with:
  - `callback = 0x1409F9B20`
  - zeroed trailing fields
- passes that record through:
  - `0x1415B43B0`
  - `0x1415B4870`
  - `0x1415B47C0`

That changes the re-entry model materially:

- `0x1409FBA60` still does **not** directly jump to the bind block
  `0x1409FA94E`
- but it **does** schedule the outer startup callback `0x1409F9B20`
- and `0x1409FA94E` is now better read as an inner bringup block inside that
  outer callback, not as an independent public seam

So the blocker moved again:

- not "is there any restart path back to the bind"
- but "under what runtime conditions does the scheduled `0x1409F9B20`
  callback actually execute and rebind `ctxB` after `0x1409FD360`"

### New Delivery Narrowing: The `0x1409F9B20` Restart Callback Is Deferred Through A Work Queue

One more static pass tightened the callback path beyond "address taken".

The `0x1409FBA60` restart side is now concrete:

- `0x1409FD068`
  - builds a local record with `callback = 0x1409F9B20`
- `0x1415B43B0`
  - allocates a work batch from global scheduler state `0x1437E9700`
- `0x1415B4870`
  - copies one or more `0x80`-byte records into that batch
- `0x1415B47C0`
  - finalizes the batch
  - and does **not** invoke the callback inline
- `0x1415B4560`
  - pushes the finalized batch into a per-lane queue
  - the queue is `0x2200`-stride per lane
  - and has a visible `0x400` entry cap
  - from there it can signal a downstream scheduler/worker path

This is the strongest restart-side result so far, because it cuts off another
false simplification.

The live model is no longer:

- shutdown happened
- some direct function later called startup again

It is now:

- shutdown happened
- `0x1409FBA60` packaged `0x1409F9B20` as deferred work
- that work was submitted into the scheduler rooted at `0x1437E9700`
- the remaining unknown is which consumer drains that lane queue, and under
  what conditions it actually executes the queued startup callback in the
  campaign runtime

### DECISIVE: The Deferred Queue Is `ndjob`, And The Scheduler Is `NdiNetInit` (multiplayer-gated)

New tooling: `scripts/disasm_va.py` disassembles any VA directly from retail
`tll.exe` with capstone (rip-relative resolved), bypassing the corruption-prone
`ida/tll.i64`. `scripts/scan_drain.py` scans the 218 functions referencing the
scheduler global for the queue drain side. These replace headless IDA disasm
for the scheduler band, which the packed DB had left undefined (db bytes).

Two identities are now confirmed by decoded assert strings, not inference:

1. The deferred work queue rooted at `0x1437E9700` is the Naughty Dog **ndjob**
   fiber job system (`shared/src/corelib/job/job.cpp`; strings `ndjob::Yield`,
   `This job (%s) is not allowed to sleep`). The drain consumer is therefore
   the generic ndjob worker pool, which always runs — it is NOT a special
   per-feature consumer and NOT the blocker.

2. The restart worker `0x1409FBA60` is **`NdiNetInit`** from
   `game/net/flow.cpp`. Decoded strings inside it:
   - `NdiNetInit`, `!g_ndiStartNetInit`
   - `Trying to initialize net while it's active. This is very wrong.`
   - `PAGE_MULTIPLAYER_CONNECTING`, `popup_mp_connecting`, `pMainPageStack`

Exact enqueue mechanics (decoded, `0x1415B4560`):
- global `0x1437E9700` holds a pointer to ndjob scheduler state
- lane array at `state + 0x4FC0`, stride `0x2200` per lane, depth cap `0x400`
- a task-group pointer (`r15`) is priority-inserted into `lane + 0xC8`, count at
  `lane + 0`; lane id = `[group + 0x4C]`; affinity mask = `[group + 0x38]`
- on insert it wakes workers via import `0x1425D6668`

Exact record build in `NdiNetInit` (`0x1409FD068`):
- `[rbp+0xC0] = 0x1409F9B20` (callback = field 0 of the 0x80-byte record)
- trailing fields zeroed; priority field `[rsp+0x20] = 3`
- `0x1415B43B0(handle,0)` alloc -> `0x1415B4870(batch,&record,handle)` copy 0x80
  -> `0x1415B47C0(batch)` finalize -> tail into enqueue `0x1415B4560`
- a second record at `[rbp+0x58]` schedules callback `0x1409F8320`

Consequence (closes the P0 drain question, reframes the blocker):

- The queue drain is generic ndjob and always fires; "the consumer never runs"
  is the wrong model.
- The thing that is multiplayer-gated is the **scheduling** side: the only
  address-taken site of the ctxB-rebinding callback `0x1409F9B20` is inside
  `NdiNetInit`, behind `PAGE_MULTIPLAYER_CONNECTING`.
- So the live `ctxB+0x8 == 0` in single-player campaign is best explained as:
  net is initialized once at boot (ctxB bound), the front-end/stage teardown
  family `0x1409FD360` later clears it, and only `NdiNetInit` (entered through
  the multiplayer connecting flow) ever re-schedules the rebind. Campaign never
  runs `NdiNetInit`, so ctxB's owner is never re-bound.

Strategic redirect:
- Chasing "make the queued `0x1409F9B20` job fire in campaign" effectively means
  booting the multiplayer net stack — wrong layer for local two-PC co-op.
- The earlier finding stands and is now the better lever: the strongest
  dual-slot manager caller already sources `AddOrUpdateNetPlayer (0x140A90550)`
  from **ctxA** (`0x143E08C80`), using ctxB only as a gating/selection input.
- Therefore the second-player bringup path should target ctxA / the net-player
  manager directly, instead of resurrecting the multiplayer-gated ctxB owner
  bind.

### Verification + Pivot Evidence: NdiNetInit Is Front-End-Gated, AddOrUpdateNetPlayer Sources Via ctxA With A Local-Player Flag

New tool `scripts/find_callers.py` does a byte-level scan of `.text`
(opcode E8/E9 rel32 + `lea r64,[rip+disp]`), alignment-independent, because the
packed IDB misses most direct CALL xrefs (IDA reported only `.pdata` refs for
these). Verified against known data (it reproduces the 0x1409FD360 caller set
and the 0x1409FD068 lea).

Reframe verification — who actually calls the net-restart:
- `NdiNetInit (0x1409FBA60)` direct callers: exactly three, all inside the
  front-end `update`-token handlers:
  - `0x1409F7AC2` (in `0x1409F78B0`)
  - `0x1409F7E5F` (in `0x1409F7C60`)
  - `0x1409FF205`
- These are front-end stage/menu transition handlers; combined with the
  `PAGE_MULTIPLAYER_CONNECTING` strings inside NdiNetInit, the ctxB owner rebind
  is confirmed to ride the multiplayer connect flow, not normal campaign play.

Pivot evidence — `AddOrUpdateNetPlayer (0x140A90550)`, full caller set (12) and
internals:
- callers cluster in the NetGameManager band (`0x140A392EC`, `0x140A3936A`,
  `0x140A394C8`, `0x140A3A02C/06E`, `0x140A4DFFA/E035`, `0x140A58287/494`,
  `0x140AD69B5`, `0x140ADF307`, `0x140C08656`).
- manager `rcx` holds an array of up to 10 player slots, stride `0x43D0`,
  scanned from `manager + 0x59C` (match key `[entry+0xE8]` vs slot, plus slot
  index `r9`).
- on create/update it resolves the source entry through **ctxA**:
  - `0x140A9075E: lea rcx, [0x143E08C80]` (ctxA)
  - `0x140A90765: call 0x14194E510(&ctxA, source_entry)`  -> resolved object
  - then `0x140A90826: call 0x14194E6E0(source, slot, resolved, 1)`
- slot population from source entry `r14`:
  - `[slot+0x1DC] = [r14+0xE8]` (source/player id)
  - `[slot+0x7B8] = [r14+0xE4]`, `[slot+0x7BC] = [r14+0xF0]`
  - `[slot+0x7C0] = ymm [r14]` (32-byte block), `[slot+0x7E0] = [r14+0x20]`
  - `[slot+0x1E0] = r9d` (player index)
- 5th arg (byte `[rsp+0x4F0]`, `r12b`) is an **is-local-player** flag:
  - when set, `[slot+0x1DC]` is overwritten from a local-id counter byte at
    `0x14304172A`, which is then incremented
  - `[slot+0x1E8] = r12b` records local-vs-remote on the slot

Why this matters for the north star:
- AddOrUpdateNetPlayer binds players via **ctxA**, exactly the context startup
  binds and that does not depend on the multiplayer-gated ctxB owner.
- It already has a local-player branch (the is-local flag + local-id counter at
  `0x14304172A`).
- So the concrete second-player seam is:
  `AddOrUpdateNetPlayer(manager=0x143DD70C0, rdx=?, r8=source_entry, r9d=slot,
  is_local=1)` with a minimal source entry carrying `+0xE8/+0xE4/+0xF0/+0x20`
  and the leading 32-byte block, resolvable through ctxA.
- Open follow-ups (P0b): (1) decode `0x14194E510` to pin the minimal valid
  source-entry shape; (2) find the existing caller that already passes
  is_local=1 (split-screen / second-controller path) and copy its source-entry
  construction; (3) identify the promotion that gives that slot real
  player-like ownership vs buddy/NPC.

#### Exact AddOrUpdateNetPlayer call ABI (confirmed at 0x140A58287)

```
rcx       = 0x143DD70C0   ; NetGameManager
rdx       = 0x143E08C80   ; ctxA  (2nd arg is literally ctxA)
r8        = source_entry  ; rdi
r9d       = slot_index    ; e.g. [rbx+0x1c]
[rsp+0x20]= is_local byte ; 0 = remote here
-> rax    = slot pointer  ; caller then inits [rax+0x220]=-1, [rax+0x455]=0, ...
```

is_local across callers: `0x140A58287` -> 0; `0x140A392EC` -> r15b (computed);
`0x140A3A02C`/`0x140A3A06E` -> r14b (computed). So a local-player branch already
exists in the dual-slot manager callers; finding the one that passes is_local=1
gives a ready-made second-local-player construction to mirror.

### P0b Progress: The Networked Second-Player Path Is The Multiplayer Net Subsystem

Decoded the ctxA source resolver `0x14194E510(ctx, key)`:
- ctxA holds a 12-entry named table at `ctx+0x800`, stride `0x1C0`; entry name at
  `+0xE0`, typed value (tag 0x70) at `+0xFC`; global fallback at `ctx+0x1ED4`.
- AddOrUpdateNetPlayer's `source_entry` is matched/resolved against this table,
  i.e. a second player needs a corresponding ctxA entry.

Decoded the remote add path `sub_140A58190` (contains the is_local=0 caller
0x140A58287):
- selects by global byte `0x143E02054`:
  - ==0: scans the ctxA entry table (base `0x143E09568 = ctxA+0x8E8`, stride
    0x1C0, 12 entries) matching id `[entry] == [desc+0x18]`; sets
    `source_entry = matched ctxA entry`, then
    `AddOrUpdateNetPlayer(manager, ctxA, entry, slot, is_local=0)`.
    Post-init writes remote markers `[slot+0x88D/0x88E/0x88F] = (entry id !=
    desc id)`.
  - !=0: builds a name via 0x14014DB00 (string "late-join") and uses the
    late-join variant `0x140A90A00`.
- `0x140A90A00` (late-join AddOrUpdate variant) callers: 0x140A4DE0D,
  0x140A4E829, 0x140A581FD, 0x140A583F5 (all in the net band).

Identity confirmed: this whole path is the **multiplayer NetGameManager** (names
`NetGameManager`, `NetCharacter-`, `late-join`). So both candidate seams for a
real second player are parts of the multiplayer net subsystem:
- ctxB owner bind  -> gated by NdiNetInit (multiplayer connect flow)
- ctxA net-player  -> NetGameManager (multiplayer session player tracker)

### Strategic fork (needs a decision)

Two-PC co-op is inherently networked, and the engine's "real second player"
machinery is the multiplayer net subsystem, which single-player campaign does
not run. The decisive open question that picks the path:

**Does single-player campaign register the local player (Chloe) through
NetGameManager (0x143DD70C0) at all (an is_local=1 registration that runs
offline)?**
- If YES: the manager is live in campaign; adding Nadine as a second entry
  (mirroring the is_local path + a ctxA entry) is the clean route.
- If NO: the manager is dormant in campaign, and a real second player means
  either standing up multiplayer net infra in campaign (heavy, fights mode
  gating) OR elevating the existing campaign companion (Nadine buddy) to a
  player-controlled entity with independent camera + a thin PC2 input/camera
  transport (different track than buddy-control polish).

### DECISIVE RUNTIME RESULT: NetGameManager Is Dormant In Single-Player Campaign

Probe `scanner/src/netplayer_probe.cpp` (built
`scanner/build/tll_netplayer_probe.exe`) read NetGameManager (0x143DD70C0) live
during confirmed single-player campaign gameplay (as Chloe):

- module base 0x7FF79F5E0000; manager 0x7FF7A33B70C0.
- manager object IS constructed: `manager+0 = 0x7FF7A1FC3900` (vtable), ~6.4K
  non-zero bytes, a constructed sub-object vtable at `slot[0]+0x3C0`.
- but ALL 10 player slots (stride 0x43D0) are empty: id@0x1DC, idx@0x1E0,
  local@0x1E8, id@0x59C, obj@0x3C8, payload — all zero.
- RESULT: 0/10 occupied -> the local player (Chloe) is NOT registered through
  NetGameManager in campaign.

Conclusion (closes the P0b decisive question):
- NetGameManager / the ctxA net-player path is multiplayer-only and dormant in
  single-player campaign; the +0x3C0 vtables are constructed-but-unassigned
  per-slot sub-objects, not live players.
- Together with ctxB being NdiNetInit-gated, BOTH "real net player" seams belong
  to the multiplayer subsystem that campaign never runs.
- Therefore a second player in campaign cannot come from re-binding ctxB or
  populating NetGameManager without effectively booting multiplayer net infra.
- The viable route is to ELEVATE the existing live campaign companion (Nadine)
  into a player-controlled entity (independent input + camera, host-authority),
  then add a thin PC2 input/camera transport — distinct from buddy-control
  polish. This becomes the new primary track.

### MAJOR: The Engine Has Native Split-Screen / Local-2-Player Infrastructure

String + disasm sweep of retail tll.exe shows the second-view and local-second-
player machinery is NATIVE, not something the mod must build:

Camera — two managers exist:
- `GetCameraManager` (0x141C06420) takes an index and asserts `index <= 1`
  (file camera*.cpp line 0x1AD). Manager handle table at `0x14697AD50`
  (stride 0x10 handle/id pairs). So `CameraManager(0)` and `CameraManager(1)`
  are first-class — a real second camera path.
- `CameraManager::*` methods, `Hack separate split screen mode camera`,
  `Switch on split screen mode`.

Split-screen rendering — native:
- `!g_renderOptions.IsHorizontalSplitscreenMode()`
- `Cs_MergeSplitScreenBufferPost` (split-screen buffer merge compute shader)
- `Foreground LOD split-screen tweak`, `Enable Split MP HUD`, `Fake Splitscreen`,
  `Split Screen Debug`, `CONTROLLER_TYPE_SPLITINHALF`.

Local-2-player — native:
- `local-player-0`, `local-player-1`, `logicalPlayerID < g_maxLocalPlayers`,
  `i < m_numPlayers`, `num-players`, `spot-player-count`
- `controller-player-1`, `controller-player-2`,
  `net-is-second-player-available?`, `net-login-split-screen`,
  `net-logout-split-screen`, `AssignTeams: ... are splitscreen players`.

Implication / architecture reframe (removes the "kludge" concern):
- The two project fundamentals (second viewport + second local player) are
  present in the engine, used by the MP modes, not by campaign.
- Goal becomes: enable the engine's own split-screen / local-2-player path
  INSIDE campaign, with Nadine as local-player-1.
- That makes it a genuine local co-op build; Steam Remote Play Together (built
  for native local co-op) then delivers the two-PC, own-screen experience with
  no custom video stream and no input-routing hacks.
- Open caveat: this infra is wired through the MP/net layer; need to prove it can
  be activated in campaign without booting a full net session (render-level
  splitscreen flag + CameraManager(1) + g_maxLocalPlayers may be drivable
  independently). Next step = runtime activation probe.

### DECISIVE RUNTIME RESULT: CameraManager(1) Is Live In Campaign

Runtime read in a live campaign session (`scanner/build/tll_read.exe`):

`0x14697AD50` camera-manager table:
- slot0 handle/id = `(0x10E0200160, 0x19)`
- slot1 handle/id = `(0x10E0200188, 0x1A)`

Both referenced objects were then read directly as raw process addresses:
- `0x10E0200160`
- `0x10E0200188`

They are not null placeholders. They are live paired objects with cross-links:
- slot0 `+0x10 -> 0x10E0200188`
- slot1 `+0x18 -> 0x10E0200160`
- both also hold distinct downstream object pointers at `+0x28/+0x30/+0x38`

This materially strengthens the architecture reframe:
- `CameraManager(1)` is not just present in retail code; it is already
  constructed and alive during single-player campaign gameplay.
- Therefore the second-view foundation exists in campaign runtime right now.
- The remaining activation problem is narrower: not "invent a second camera",
  but "bind/use the already-live secondary camera path as local-player-1's
  gameplay view".

### Narrowed Static Activation Track: g_maxLocalPlayers / logicalPlayerID checks

The assert string `logicalPlayerID < g_maxLocalPlayers` at `0x142841F48` has at
least four code sites taking its address:
- `0x14245FB87`
- `0x14246907F`
- `0x142490ABC`
- `0x142499070`

Three useful decoded windows show the same pattern:
- extract a compact player-id field (`sar ... 8`, `and ... 3`)
- compare against a global dword via `cmp reg, dword ptr [rip+...]`
- assert via the `g_maxLocalPlayers` string if out of range
- then branch on higher mode bits like `0x10000 / 0x20000`

Concrete compare sites now pinned:
- `0x142469076: cmp r14d, dword ptr [rip+0x1366293]`
- `0x142490AB4: cmp ebp,  dword ptr [rip+0x133E856]`
- `0x142499068: cmp esi,  dword ptr [rip+0x13362A2]`

These are strong candidates for the runtime local-player limit / activation
layer. Next step is to resolve those rip-relative globals and classify the three
surrounding functions, then test whether bumping the limit and the splitscreen
render flag wakes a usable second local-player path in campaign.

### BREAKTHROUGH: `net-login-split-screen` Boots Local-Player-1 In Campaign

A real in-process diagnostic was finally executed on the game thread through the
live `NadineBuddy` update hook (`tll_teleport_hook.dll`, hotkey F7). The hook:
- set `g_maxLocalPlayers` low dword to `2` (global `0x1437CF310`),
- forced the second-player availability byte `0x143071038 = 1`,
- called `net-login-split-screen` handler `0x140C02220`,
- queried `net-is-second-player-available?` before/after,
- then called `net-logout-split-screen` handler `0x140C02420`.

Observed live log in confirmed campaign gameplay:

- before:
  - `avail_byte=0`
  - `login_state=0`
  - `max_local_players=2`
  - `query=0`
- after login:
  - `avail_byte=1`
  - `login_state=3`
  - `max_local_players=2`
  - `query=1`
  - `cam0=0x10E0232880`
  - `cam1=0x10E0236A00`
- after logout:
  - `avail_byte=1`
  - `login_state=3`
  - `query=1`

This is the decisive activation result:
- `net-login-split-screen` DOES successfully boot the second-local-player path
  in single-player campaign once the two simple gates are satisfied:
  - local-player limit = 2
  - second-player availability byte = 1
- The engine's own split-screen/local-2-player machinery is therefore NOT hard-
  blocked behind a full multiplayer net session.
- The returned camera objects (`cam0`, `cam1`) differ from the earlier handle
  table objects, which strongly suggests the login path resolves/activates the
  usable gameplay camera layer after boot.
- `net-logout-split-screen` did not immediately clear the state in this test;
  either logout is asynchronous/deferred, or the query reflects a latched local
  activation state. That is secondary now.

New primary question (much narrower than before):
- after the local-player-1 path is live, how do we bind **Nadine** to it instead
  of whatever stock local-player-1 target/slot the engine expects?
- and can we switch the active second local player/camera to campaign Nadine
  without relying on dormant multiplayer player-tracker paths?

This is the first runtime proof that the project's preferred architecture is
real: activate the engine's own local-2-player/split-screen path in campaign,
then attach Nadine to local-player-1.

### Runtime Local-Player Target Snapshot Before Split Login

New in-process F6 diagnostic on the live `NadineBuddy` hook logged the local
player resolver state during ordinary campaign gameplay, before another
split-screen login attempt:

- `local-player-0 = 0x10E1060F80`
  - vtable RVA `0x2A4BFE8`
  - inline name `Player`
- `local-player-1 = 0x0`
- `nadine-buddy = 0x10E1304880`
  - vtable RVA `0x2949638`
  - inline name `nadine-buddy`

This is a very strong simplification of the binding problem:
- campaign does NOT already keep a hidden stock actor in `local-player-1`
- `local-player-1` is simply null before split-screen login
- so the next question is not "which existing actor do we have to evict from
  local-player-1", but "what object appears there after `net-login-split-screen`,
  and can we make that object resolve to Nadine?"

### BREAKTHROUGH 2: `local-player-1` Can Be Resolved To `NadineBuddy`

A follow-up in-process experiment hooked the local-player resolver vfunc
(`0x143FE1070->virtual +0xA8`) so that requests for index `1` return the live
`NadineBuddy` object when present.

Observed live log after `F7` (split login) then `F6` (target snapshot):

- `resolve_local_player(1): original=0x0 patched=0x10E12FCB80`
- `local-player-0=0x10E1060900`
  - vtable RVA `0x2A4BFE8`
  - inline name `Player`
- `local-player-1=0x10E12FCB80`
  - vtable RVA `0x2949638`
  - inline name `nadine-buddy`
- `nadine-buddy=0x10E12FCB80`
  - same object as local-player-1

This proves a second decisive point:
- the engine's native local-player-1 path does NOT require a stock `Player`
  class specifically at resolver time;
- a live campaign `NadineBuddy` object can stand in as the resolved
  `local-player-1` target, at least far enough for the runtime lookup layer to
  accept and publish it.

So the campaign co-op path is now materially narrowed to:
1. raise native split-screen/local-player machinery in campaign (`F7` path);
2. bind resolver/player-1 lookup to `NadineBuddy`;
3. verify whether second camera / control / HUD actually consume that binding,
   or whether a deeper compatibility layer still expects a `Player`-class actor.

This is far beyond the earlier hypothesis stage. We now have runtime proof that:
- campaign split-screen/local-player boot works,
- and `NadineBuddy` can occupy the `local-player-1` resolver slot.

### Runtime Owner-Mode Probe: `NadineBuddy + 0x1748 = 0` Produces Follower Behavior

Live owner graph snapshot established that for the current `NadineBuddy`:
- `owner + 0x1748 = 1`
- `owner + 0x1894 = 0`
- `owner + 0x1895 = 0`

Given `0x141DE73D0`, that means the mode resolver effectively falls back to the
cached mode at `+0x1748` directly. A targeted runtime patch then changed:

- `0x10E1305FC8` (`NadineBuddy + 0x1748`) from `1` -> `0`

Verified by immediate read-back.

Observed in gameplay: Nadine simply follows Chloe / stays in buddy-follow
behavior, i.e. no direct-control effect.

Conclusion:
- mode `0` is the follower/buddy-authored path, not a player-like control mode.
- This is a clean narrowing of the owner graph: `+0x1748` is a real behavior
  selector, and mode `0` is now characterized.
- Next highest-value runtime mode probe is `+0x1748 = 2`.

### Camera Layer Narrowing: Second Camera Needs A Higher Retained-Camera Transition/Bus, Not Just Pair Injection

Further camera-side runtime/static work sharpened the second-view problem.

What is now established:

- The earlier global retained/live pair at `0x143D76C10/18` is **not** the right
  seam for `CameraManager(1)` specifically. The higher focus handlers
  `0x140746460` and `0x1407465E0` both call `GetCameraManager(0)` and therefore
  primarily drive the primary camera/focus path.

- The real second-camera-local cached target seam is inside the live object
  returned by `GetCameraManager(1)` (e.g. `cam1=0x10E0236A00` during the probe):
  - `cam1 + 0x2A8`
  - `cam1 + 0x2B0`

- `CameraManager(1)`-specific handlers:
  - `0x140746150` = sync retained binding by mode for manager 1
  - `0x140746340` = retained state transition handler for manager 1
  Both validate `manager1+0x2A8/+0x2B0` against `ptr->(+8)->+0xBC`, mirror mode
  bits, and then drive the same lower transition path as manager 0.

- Lower second-camera state transition helpers:
  - `0x141C074E0` = direct attach retained binding
  - `0x141C07640` = direct detach retained binding
  - `0x141C07730` = toggle attach/detach based on whether `+0x2A8/+0x2B0` are
    currently valid

- Runtime probes already tried:
  1. global pair injection (`0x143D76C10/18`) with Nadine pair -> no visible effect
  2. manager1-local pair injection (`cam1+0x2A8/+0x2B0`) -> no visible effect
  3. direct manager1 attach (`0x141C074E0`) after injecting Nadine pair -> no visible effect
  4. manager1 toggle path -> no visible effect

- Fresh live Nadine identity fields for the camera seam:
  - `Nadine + 0xA8 = 0x10E020FAE8`
  - `Nadine + 0xBC = 0x000006BD` (or nearby session-specific variants like 0x6C0/0x6C1)

Practical conclusion:
- second camera ownership is deeper than the cached target pair and direct attach helpers;
- the next real layer is the retained-camera command/message bus above them, not
  more raw pair patching.

Most promising next bus/commit seams:
- `0x1413BA140` / `0x1413B9EA0` retained-player camera message handlers
- `0x1414538E0` retained-camera state record builder
- `0x141634F20` / `0x1416350A0` retained-camera work-item / payload queue helpers
- `0x1416D1330` retained-camera command context initializer

This means the camera problem is now well framed:
`manager1 pair + attach` is not enough; need the higher retained-camera bus to
commit/activate a stable second-camera owner/focus state for Nadine.

### Retained-Camera Bus Narrowing: The Real Next Layer Is Above Manager1 Pair/Attach

Deeper reverse on the retained-camera bus clarified why direct manager1 attach was
still insufficient.

Established lower layer:
- `0x141C074E0` = direct attach retained binding for a camera manager
  - sets `manager + 0x128 = 1`
  - updates global camera-state blocks through `0x141837530`
  - if `manager + 0x2A8/+0x2B0` form a valid live pair, accepts them and marks
    slot activity
- `0x141C07640` = inverse/detach path
- `0x141C07730` = toggle attach/detach based on whether `+0x2A8/+0x2B0` are valid

Runtime probes already tried and now considered closed enough:
- manager1-local pair injection into live `cam1 + 0x2A8/+0x2B0`
- direct attach `0x141C074E0`
- toggle `0x141C07730`
All produced no stable visible second-camera ownership.

This pushed the camera investigation upward to the retained-camera bus.

Key upper-layer constraints:
- `0x140746150` / `0x140746340` are true CameraManager(1) retained transition
  handlers; they mirror mode bits, validate `+0x2A8/+0x2B0`, then drive the
  lower attach/detach helpers.
- `0x140746460` / `0x1407465E0` are higher retained-camera owner/focus handlers,
  but both currently query `GetCameraManager(0)` and primarily drive the primary
  camera/focus path, not manager1 directly.
- `0x141C0DFD0` (`CommitCameraTargetDescriptor`) and `0x141C04CE0`
  (`ApplyCameraTargetState`) are the concrete target-state commit path, but they
  still need a correct higher-level descriptor/command source.

Most important new upper-bus finding:
- `0x1414538E0` (`BuildRetainedCameraStateRecord`) is not a trivial payload copy.
  It branches on multiple hashed command/opcode ids and can produce materially
  different retained-camera records.
- One especially relevant branch (`hash 0xCBD12A27`) uses a source pair from
  `rsi`, validates it, resolves a runtime object, computes a transform from that
  runtime object, and finally dispatches `0x14145BAD0(..., edx=4)` with the
  built transform payload.
- This is the first strong sign that the real second-camera ownership path is a
  message/record-driven bus operation rather than just "put pair in +0x2A8 and
  attach".

Practical next focus:
- identify the opcode/hash meanings for the `0x1414538E0` branches,
- especially `0xCBD12A27`, `0xDD1AA255`, `0xC300E067`, `0xDC3D0961`,
- then trace how those records are queued through `0x141634F20/0x1416350A0/...`
  into the command bus and which one corresponds to stable camera-owner/focus
  activation for a second view.

### Correction: The `0x1414538E0 / 0x14145BAD0` Family Is Rail/Vehicle-Specific, Not A Generic Second-Camera Ownership Bus

Further retail disassembly plus exact engine-hash recovery from the table at
`0x142BEA360` materially narrows the retained-camera branch that previously
looked like a generic second-camera owner/focus bus.

Exact branch hashes now matched:

- `0x6FE024E2` -> `npc-started-vehicle-jump`
- `0x80D9115E` -> `get-speed`
- `0x89BC72B5` -> `on-animation-done`
- `0x9ADE823E` -> `slow-to-stop`
- `0xC132C051` -> `on-animation-started`
- `0xC300E067` -> `rail-scrub`
- `0xDC3D0961` -> `get-spline-id`
- `0xEB5AB718` -> `get-surface-pat`
- `0xF515BD98` -> `transfer-to-spline`

This reclassifies `0x1414538E0` sharply:

- the surrounding helpers are not generic retained-camera verbs;
- they are tightly anchored on rail/vehicle state:
  - `0x141444010` hashes and dispatches `get-vehicle-class`
  - `0x141444C50` hashes and dispatches `get-rail-controller`
  - `0x14145BAD0` hashes and dispatches `go-ragdoll`
- the `0xCBD12A27` branch still remains unnamed, but it now clearly sits in the
  same rail/vehicle family:
  - validates a retained/live pair from `rsi + 0x8/+0x10`
  - resolves the corresponding live object through `0x141444C50`
  - extracts a transform from `object + 0x100`
  - then calls `0x14145BAD0(..., edx=4)` with that derived payload

Concrete meaning of the correction:

- `0x1414538E0` is not the generic second-camera ownership activation seam we
  were hoping for;
- it is a retained rail/vehicle camera-state/transition builder, likely used
  when camera ownership has to follow rail-controller / vehicle / spline state.
- `0x14145BAD0` is the corresponding retained rail-controller state-apply path,
  not a general second-view owner commit.

Practical consequence:

- do **not** spend the next cycle trying to force manager1 ownership through
  `0x1414538E0/0x14145BAD0`;
- they remain useful as a supporting retained-camera family for vehicle/rail
  scenarios,
- but the generic `Nadine -> second camera` problem now needs a different,
  more campaign-native camera ownership seam.

### Split-Screen Boot Is Real, But The Stock Player-2 Source Still Routes Through `ctxB` / `NetPlayerData`, Not Nadine

The next pass tightened what the split/local-player activation path actually
does in campaign.

`net-login-split-screen` (`0x140C02220`) is now better characterized:

- its string gate is `*net-allow-sub-sign-in*`
- it still requires the runtime availability byte `0x143071038`
- it promotes the split login state at `0x14307103C` to `3`
- it flips the render-side split flag `0x143FE0268 = 1`

Its immediate helper band is also less opaque now:

- `0x140B22C00`
  - copies a `0x24`-byte sub-player descriptor block
  - source is either the live global block at `0x143071008..+0x28` or a
    fallback/default block when split state is not active yet
- `0x140B22D00`
  - is anchored by the assert strings:
    - `NetPlayerData::Init`
    - `m_isSlave == isSlave`
  - so split login explicitly configures a `NetPlayerData` object as a
    slave/sub-player-side record
- `0x140B222B0`
  - seeds split-side `NetPlayerData`/profile blocks from:
    - `*net-starting-gold*`
    - `*net-starting-xp*`
    - `*net-starting-mission-0*`
    - `*net-starting-mission-1*`
    - `*net-starting-mission-2*`

The decisive new correction is one layer higher:

- after split login, the large refresh path `0x140ACC140` still pulls its
  stock player-2 content through the old `ctxB` band
- it calls `0x14194CFB0(&ctxB)` to count active source entries
- and if more than one source is available, it calls `0x140B21DE0`
  before continuing the refresh

That means:

- campaign **can** boot the engine's native split/local-player state
- but the stock player-2 source behind that state is still tied to the
  `ctxB` / owner-selection family, not to a campaign-native Nadine owner path

`net-logout-split-screen` (`0x140C02420`) sharpens the same picture from the
other side:

- it funnels into `0x140B9DE90`
- that helper reinitializes the same split-side `NetPlayerData` blocks,
  clears `0x14307103C` and `0x143071038`,
  and conditionally re-runs `0x140ACC140`

Practical consequence:

- split/local-player activation remains a **real and useful** supporting seam
- but it is **not** yet a self-sufficient foundation for Nadine's stable second
  view
- the real next camera-side options are now narrower:
  1. override the stock `ctxB`-fed player-2 source that `0x140ACC140`
     refreshes from, or
  2. bypass that stock feed and attach a campaign-native Nadine camera owner
     path directly

### Further Narrowing: Split Login Boots Summary State, But Stock `local-player-1` Actor Resolution Still Stays Null

The next static/runtime pass sharpened the exact boundary between the useful
split/local-player seam and the still-missing actor/camera ownership layer.

`0x140ACC140` is now narrower than "generic split refresh":

- it does **not** call `GetCameraManager(1)` or any direct camera-owner handoff
  path
- after `0x14194CFB0(&ctxB)` and optional `0x140B21DE0`, it just publishes the
  chosen source-summary pointer back through:
  - `0x14194E3B0(&ctxA, &chosen_source)`
  - `0x14194E3B0(&ctxB, &chosen_source)`
- so the function refreshes source/session summary state; it is not where a
  live second actor or camera owner is born

The chosen source path is also more concrete now:

- `0x140B21DE0`
  - if `ctxB+0x18` is valid, returns the current-selected ctxB source pointer
  - otherwise falls back to the split sub-player descriptor block rooted at
    `0x143071008`
- `0x140B22C00`
  - copies only a `0x24`-byte split sub-player descriptor
  - this is a descriptor/state feed, not a live actor pointer
- `0x14194E510`
  - resolves only **kind `0x70`** payloads from those source entries
  - returns either:
    - `ctx+0x1ED4` when fallback kind `ctx+0x1ED0 == 0x70`, or
    - `entry+0xFC` when an active 0x1C0-stride ctx entry has kind `0x70`
- `0x14194E3B0`
  - hands that selected source summary through `ctx+0x8->virtual +0x80(..., 0x70)`
  - then refreshes the concrete `0x70` payload behind either
    `ctx+0x1D00+0xFC` or the matching active entry's `+0xFC`

That means the stock split path is still operating on a source-descriptor /
summary layer, not directly on `NadineBuddy` or a campaign-native second camera
owner.

The runtime side tightened the same picture:

- the hook log repeatedly shows
  `resolve_local_player(1): original=0x0 patched=0x...`
  while split login is already active (`login_state=3`, `cam1 != 0`)
- so even after `net-login-split-screen` has successfully raised split state
  and `CameraManager(1)`, the stock campaign resolver path can still return
  null for `local-player-1`
- `NadineBuddy` appears there only because the hook on
  `0x143FE1070->virtual +0xA8` patches the return value

So the current state is now much cleaner:

- split/local-player boot is real
- `CameraManager(1)` is real
- but stock campaign actor resolution for local-player-1 is still separate from
  that summary/descriptor path and still unresolved

This sharply narrows the next frontier:

1. identify the stock `local-player-1` resolver-owner contract behind
   `0x143FE1070->virtual +0xA8`, and
2. separately identify a campaign-native camera owner/follow path for Nadine,
   instead of assuming the split summary helpers already solve actor ownership

### Resolver-State Narrowing: `0x143FE1070` Is A Startup-Copied Local-Player Routing Block

One last static pass also narrowed the global behind the local-player hook.

`0x143FE1070` is not just a raw "current local-player pointer":

- startup/init code at `0x140154A59` copies a template block into it
- a later config/update path at `0x1415EF17F` copies another `0x120`-byte
  block over the same global
- so this is a real local-player routing/resolver state object, not a single
  actor pointer

The first nontrivial consumer also makes that concrete:

- `0x14128396E`
  - indexes `0x143FE1070 + 0x78 + local_index*8`
  - and calls the resulting object's `virtual +0x30(token)`

That means the current local-player seam is already split into layers:

- `0x143FE1070` = routing/resolver state block
- `virtual +0xA8` = later actor-resolution step inside that block
- the current camera/ownership blocker is therefore inside the resolver-owner
  contract above `+0xA8`, not inside the split summary helpers below

### New Class Split: `ctxA` And `ctxB` Are Separate Startup-Built Context Classes

The next pass pinned the two campaign-visible source/selection globals as real
sibling classes rather than one fuzzy "shared context" blob.

Early init is now concrete:

- `0x140072A50`
  - calls `0x140ACBE10` on `0x143E08C80`
  - installs vtable `0x1429E61F0`
- `0x14007C100`
  - calls `0x140ACBE10` on `0x143E0EF00`
  - zeroes the embedded owner field at `ctxB + 0x8`
  - installs vtable `0x1429EF890`
  - seeds `0x143E10E88`

`0x14194AF00` is the real hard reset for these context objects:

- clears the 12 `0x1C0`-stride active-entry bands
- clears `ctx+0x18`, `ctx+0x8`, `ctx+0x200`, `ctx+0x20C`
- resets `ctx+0x1EC0`, `ctx+0x1ECC`, `ctx+0x1F54..+0x1F5C`
- seeds the default bytes around `+0xA1/+0xB0..+0xF0`, `+0x7C0`, `+0x748`

This also forced one correction:

- `0x140B1E050` is **not** an owner teardown helper by itself
- it hard-resets global `ctxB` through `0x14194AF00`
- then clears the staged globals around `0x143E10E80/0x143E10E90`

So `ctxA` / `ctxB` are now a real, typed pair:

- `ctxA = 0x143E08C80`, vtable `0x1429E61F0`
- `ctxB = 0x143E0EF00`, vtable `0x1429EF890`

### New `ctxB` Surface: Concrete Vtable Slots And Owner Wrappers

The `ctxB` vtable is now concrete enough to stop treating it as an opaque
selection helper.

Pinned slots:

- `+0x28 = 0x140B1DF30`
  - returns fixed token `0xA`
- `+0x30 = 0x140B1DF20`
  - returns `1`
- `+0x38 = 0x140B204A0`
  - toggles bit `ctxB+0x200` and dispatches owner `+0x78`
- `+0x40 = 0x140B204D0`
  - returns inverted low-bit state of `ctxB+0x200`
- `+0x48 = 0x140B20710`
  - clears `ctxB+0x1FD0`
  - under global gate `0x1437E6F39`, sets `ctxB+0x200=1` and calls
    `owner->virtual +0x78(ctxB, 1)`
  - clears `ctxB+0x10`
- `+0x50 = 0x140B20760`
  - only marks dirty byte `0x1437E7513`
- `+0x58 = 0x140B20770`
  - resets small ctx/global runtime flags and tails into `0x140ACC140`
- `+0x60 = 0x140B207A0 -> 0x14194C550`
- `+0x80 = 0x140B20240`
  - if `ctxB+0x18` is live, sets `ctxB+0x1F80=1`
  - always sets `0x1437E7513=1`
- `+0x88 = 0x140B20260`
  - packet/publish worker that sets `ctxB+0x10=1` and packages a large payload
    through `0x141C9ED50`
- `+0x98 = 0x140B20490`
  - returns `0`
- `+0xA8 = 0x140B204E0`
  - heavy token-keyed packet/publish worker through `0x141C9ED50`

The owner wrappers above it are also now concrete and operate on the embedded
owner field directly:

- `0x14194DB30` -> `ctxB+0x8 -> owner->virtual +0x58(ctxB, bool)`
- `0x14194DB70` -> `ctxB+0x8 -> owner->virtual +0x60(ctxB, bool)`
- `0x14194DB90` -> `ctxB+0x8 -> owner->virtual +0x98(ctxB)`
- `0x14194DBD0` -> `ctxB+0x8 -> owner->virtual +0xA8(ctxB, token)`
- `0x14194DD80` -> `ctxB+0x8 -> owner->virtual +0x88(ctxB, bool)`

That matters because the current bind question is now narrower:

- the active `ctxB` object already knows exactly which owner interface it wants
- the unresolved part is the live producer of `ctxB+0x8`, not the shape of the
  consumer side

### New Narrowing: `ctxB` Is Stock Player-2 Source Selection, Not The Actual Local-Player Resolver

The next static pass also clarified the dividing line between the split/session
summary path and the real local-player resolver path.

Useful local helpers:

- `0x14194C980`
  - returns the current-selected source entry:
    - fallback `ctxB+0x1D00` when `ctxB+0x18 == 0`
    - otherwise the matching active `0x1C0` entry by `ctxB+0x1EC4`
- `0x14194C870`
  - returns active entry `index` from `ctx+0x800 + index*0x1C0`
  - or null if that slot is inactive

And the wider front-end/state users now line up with that reading:

- `0x1409ECB50`
  - refreshes `ctxA`
  - consults `ctxB+0x18`, pending state, and selection counters
  - publishes follow-up records
- `0x140AC0CE0` and `0x140AC1780`
  - refresh `ctxA`
  - then consult the bound `ctxB` owner through `+0x50/+0x60`
  - then continue into packet/session state publication

So the current role split is now sharp:

- `ctxB` = stock player-2 source-selection / summary controller
- `0x143FE1070->virtual +0xA8` = separate local-player actor resolver path

That leaves the actual frontier much cleaner:

1. identify the resolver-owner contract above `0x143FE1070->virtual +0xA8`
2. identify the campaign-native Nadine camera owner/follow path
3. treat `ctxB` as a supporting stock player-2 source family, not as the final
   actor/camera ownership layer

### Camera-Layer Correction: `0x143FE1070` Feeds The Second-Camera Attach Path Directly

The next pass forced one important correction:

- `0x143FE1070` is **not** a monolithic local-player actor object.
- It is a startup/config-copied `0x120`-byte bundle whose first qword is used as
  a live owner pointer, while the rest of the block includes generic registered
  descriptor/formatter state.

Static startup/config refresh now lines up like this:

- startup copy:
  - `0x1401546E7 .. 0x140154ABC`
- later refresh from config payload:
  - `0x1415EF130`
- both then register the same descriptor blocks through:
  - `0x141830F50`

The generic descriptor side is now concrete enough to separate it from the
actor resolver:

- registered blocks around `0x143684668 .. 0x143684898` and `0x14317C3C8`
- formatter/helper family:
  - `0x141830C70` boolean formatter
  - `0x141830CA0` integer formatter
  - `0x141830CB0/0x141830CC0/0x141830CD0/0x141830CE0`
  - `0x141831150`
  - `0x1418311D0`

So the earlier broad reading

- "`0x143FE1070` is the local-player resolver object and the `+0x78` family are
  its resolver subobjects"

is too coarse.

The sharper reading is:

- `0x143FE1070` = startup/config bundle
- `*(qword*)0x143FE1070` = live owner pointer
- that owner pointer supplies `virtual +0xA8(local_index)` for actor
  resolution

The decisive proof is now camera-side:

- `0x141C0DFD0`
  - reads `rcx = qword ptr [0x143FE1070]`
  - then calls `owner->virtual +0xA8(screen_index)`

That means the Nadine `resolve_local_player(1)` hook sits directly in the
second-camera attach pipeline, not just in a higher summary/query layer.

### Second-Camera Manual Cluster: Exact Registration Map

The registration block around `0x14074D220` is now concrete:

- `0x140745CD0` <- `Manual Camera 1`
- `0x140746260` <- `Joypad 1 Controls Game`
- `0x140745D70` <- `Manual Orthographic Sun Camera`
- `0x140746150` <- `Manual Camera 2`
- `0x140746340` <- `Joypad 2 Controls Game`
- `0x140745C80` <- `Swap Joypad Screens`
- `0x1436732CA` <- `Bind Manual Cam To Player Parent`
- `0x1436732C9` <- `Bind Manual Cam To Player`

Current file defaults in retail image:

- `0x1436732CA = 1`
- `0x1436732C9 low byte = 0`

Sibling runtime bytes that participate in the same cluster:

- `0x1436732CB`
- `0x1436732CC`
- vector/scalar payload region:
  - `0x1436732D0`
  - `0x1436732E0`

### `CameraManager(1)` Attach/Detach Path Is Now Concrete

`0x141C074E0` is the actual attach side:

- sets `camera+0x128 = 1`
- toggles two lane/state bands under:
  - `0x143FE1088 + 0xDB0 + (screen_index + 4) * 0xBF0`
  - `0x143FE1088 + 0xDB0 + (screen_index + 2) * 0xBF0`
- if `camera+0x2A8/+0x2B0` does **not** already resolve to a live retained
  object:
  - builds fallback descriptor `0x14373A820`
  - pushes it through `0x141C0DFD0`
  - caches returned `(ptr,id)` back into `camera+0x2A8/+0x2B0`

`0x141C07640` is the detach side:

- sets `camera+0x128 = 0`
- flips the same `0x143FE1088` lane/state bands the opposite way
- if cached retained target is still valid, calls:
  - `target->virtual +0x208(..., 2)`
- then clears per-screen active byte under `0x14383DE48`
- and clears runtime byte `0x1436732CB`

`0x141C07420` is the smaller lane-state helper:

- sets `camera+0x128 = bool`
- only toggles the two `0x143FE1088 + 0xDB0 + idx*0xBF0` bands

### `0x141C04CE0` Accepts Actor Override, Not Just Stock Player

The builder below the resolver path is now clearer:

- `0x141C0DFD0`
  - resolves actor through `(*0x143FE1070)->virtual +0xA8`
  - packages descriptor + small control payload
  - calls `0x141C04CE0`

And `0x141C04CE0` itself:

- accepts an optional resolved actor override
- falls back to descriptor-owned object only when override is null
- later copies:
  - `[actor + 0xA8]`
  - `[actor + 0xBC]`
- into camera-state payload
- calls actor virtual `+0x10`, then potentially returned object `+0x20`
- shows no obvious static `Player`-only class gate

So on the static path, a resolved `NadineBuddy` can flow through this builder.

### Manual-Camera Follow Layer: Concrete Campaign-Side Sinks

The manual-camera cluster above the attach helper is now split more cleanly.

`0x140746150` (`Manual Camera 2`):

- for mode/state `4`, toggles `CameraManager(1)` between:
  - `0x141C07640` detach
  - `0x141C074E0` attach
- returns whether a valid `(ptr,id)` is now cached at `camera+0x2A8/+0x2B0`

`0x140746340` (`Joypad 2 Controls Game`):

- mirrors/toggles bit `0x10` between paired objects
- if cached retained target exists:
  - drives `0x141C07420`
- otherwise:
  - clears `camera+0x128`
  - toggles the same `0x143FE1088` lane/state bands

`0x140746500` / `0x140746640` establish a more useful sink than the old
split-summary path:

- both call `0x141C0DFD0`
- both cache the returned `(ptr,id)` into:
  - `0x143D76C10`
  - `0x143D76C18`
- `0x140746640` uses fallback descriptor:
  - `0x143053770`
- `0x140746500` uses caller-provided descriptor and writes the same cache

This cache is now the practical manual-camera actor seam:

- not `ctxB`
- not the generic descriptor sub-blocks under `0x143FE1070`
- but the resolved actor pair cached at `0x143D76C10/18`

`0x141C07E20` is the follow-state writer sitting immediately under that layer:

- called from:
  - `0x140745B2D`
  - `0x14074610C`
  - `0x141767DA5`
  - `0x141DA7CFE`
- resolves the currently cached actor under `camera+0x100`
- validates it through `0x141F53500`
- writes:
  - transform block to `actor+0x3A0/+0x3B0`
  - follow vectors/scalars to `actor+0x1D0..+0x21C`

But the next pass forced one more correction:

- `0x141C07E20` itself now looks **manual/debug-heavy**, not like the final
  campaign gameplay camera path.
- two of its extra callers are now clearly or strongly debug-flavored:
  - `0x141767DA5`
    - sits under `teleportplayer`
  - `0x141DA7CFE`
    - follows a selected-object / top-camera flavored loop
- the nearby registration cluster around `0x14074A580` also confirms camera
  debug tooling:
  - `Stick Camera`
  - `Draw Stick Camera Debug`
  - `Draw Stick Pan Camera Debug`
  - `Focus Top Camera on Selected Object`

So the manual-camera layer remains useful as a resolver proof, but it should
not be treated as the final campaign-native second-camera ownership path.

### Stronger Gameplay Camera Anchor: The Existing Manager-0 Retained-Camera Family

The more useful non-debug camera result now sits back in the already-known
retained-player family.

`0x140EDE5E0` is now concrete:

- if retained state is absent:
  - calls `0x1410F3350`
  - then `GetCameraManager(0)`
  - then `0x141C07640`
- if retained state is present:
  - calls `0x1410F3DD0`
  - then `GetCameraManager(0)`
  - then `0x141C074E0`
- both branches finish by resolving slot `0` through `0x1410F0F60`

This makes the manager-0 path sharper:

- `0x1410F3350`
  - fallback spawn/refresh path in the upper retained-player family
- `0x1410F3DD0`
  - `RefreshPrimaryRetainedPlayerCamera`
- `0x141C078D0`
  - heavy retained-camera update path
- `0x141C074E0 / 0x141C07640`
  - attach/detach bridge for the active retained player

That means the current strongest gameplay-facing question is no longer
"what else can the manual camera debug tools do?"

It is now:

- whether there is a **manager-1 / local-player-1 analog** to this mature
  manager-0 retained-camera lifecycle,
- or whether Nadine camera ownership will require adapting the manager-0 family
  rather than leaning on the debug/manual camera stack.

So the remaining camera blocker is now narrower than before, but slightly
different:

1. which exact owner pointer gets installed at `*(qword*)0x143FE1070` at
   runtime,
2. whether a manager-1/local-player-1 analog to
   `0x1410F3DD0 -> 0x141C078D0 -> 0x141C074E0`
   already exists,
3. if not, where the manager-0 retained-camera lifecycle can be reused or
   adapted for Nadine.

### Camera Route Family Is Now Clearer Than The Old `GetCameraManager` Reading

One important correction is now strong enough to treat as canonical:

- `0x141C06420` is **not** a raw `CameraManager(index)` getter.
- it takes a screen/route index `0..1`,
- reads the corresponding `(ptr,id)` pair from the two-entry table at
  `0x14697AD50`,
- validates the live object through `ptr->+8 / entity+0xBC`,
- marks `entity+0x72`,
- and returns the resolved **screen-route entity**.

So the live second-camera seam is now better described as a
`screen-route entity` family, not as a direct `CameraManager(index)` family.

The startup builder for that table is now concrete too:

- `0x141C04F80`
  - loops exactly two entries,
  - hashes the `CameraManager` request token,
  - builds selector-backed packets via `0x1417C32A0`,
  - wraps/materializes them through `0x1417C3790`,
  - and stores the resulting `(ptr,id)` pairs into `0x14697AD50`.

There is also a direct table consumer below it:

- `0x141C051F0`
  - iterates the same two-entry table,
  - resolves each live route entity,
  - notifies the global observer at `0x1438929B8` through `virtual +0x8`,
  - then calls route-entity `virtual +0x48`.

So the startup route-table itself is already live and consumed, not dead setup.

### Route Entity vs Retained Slot: The Useful Split

The current camera frontier is cleaner if we separate two indices:

- retained-player slot paths continue to key off `+0x144` in their own owner
  families:
  - `0x14104EC00`
  - `0x14104F080`
  - `0x141051500`
  - `0x1410F0D10`
  - `0x1410F0F60`
  - `0x141C077B0`
- screen-route/camera ownership paths key off `+0x124` in their own owner
  families:
  - `0x141C0DFD0`
  - `0x141C0F57B`
  - `0x140746260`
  - `0x140745CD0`

That means the previous "find manager1 clone" framing was too coarse.
What we actually need is the owner family that carries the **screen-route
index at `+0x124`**, not another pass through generic retained-slot helpers.

### `0x141C0D4B0` And `0x141C0DFD0` Are Route-Entity Workers

`0x141C0D4B0` now reads as a route-entity child selector:

- scans the route entity child list at `route+0x100`
- validates live children
- filters on child mode `+0x1B0`
- and returns the preferred camera child

This is a stronger read than the old "CameraManager::GetCurrentCamera(bool)"
guess.

`0x141C0DFD0` also got stronger:

- it is not just a manual-camera one-off
- it has a broad direct-call fanout across gameplay code
- it locks `route+0x2C0`
- resolves the actor through:
  - `(*(qword*)0x143FE1070)->virtual +0xA8(route+0x124)`
- commits the descriptor through:
  - `route+0x2F0`
  - `0x141C04CE0`
- then notifies the external observer at `0x1438929B8` through `virtual +0x30`

So `0x141C0DFD0` is now the strongest generic campaign-facing camera-route
worker on this layer, not just a debug/manual attach stub.

### The `0x143FE1070` Bundle Still Matters, But As A Resolver Owner

The startup/config bundle reading is now stronger too:

- `0x140154A59`
  - bulk-copies a prepared block into `0x143FE1070`
- `0x1415EF17F`
  - does the same from another config/startup path
- `0x14128396E`
  - uses subobjects under `0x143FE1070 + 0x78 + idx*8`
  - and dispatches through their `virtual +0x30`

So `0x143FE1070` still does **not** look like a direct actor pointer.
It looks like a startup-copied resolver-owner bundle with per-index subobjects,
and `0x141C0DFD0` depends on that bundle's `virtual +0xA8` contract.

### Manual Camera Paths Are Now Easier To Place

The explicit handlers around the old manual seam now fit the route-entity model:

- `0x140745CD0`
  - `Manual Camera 1`
  - toggles attach/detach on screen-route entity `0`
- `0x140746260`
  - `Joypad 1 Controls Game`
  - uses screen-route entity `0`
  - drives `0x141C07420` or lane-state bands under `0x143FE1088`
- `0x140746460`
  - uses the cached actor pair at `0x143D76C10/18`
  - otherwise rebuilds it through `0x141C0DFD0`
- `0x1407465E0`
  - sibling path with fallback descriptor `0x143053770`
- internal block `0x141C075DC`
  - writes the returned `(ptr,id)` into `route+0x2A8/+0x2B0`
  - and sets a per-screen activity byte at `0x14383DE48[route+0x124]`

This means the manual-camera family is no longer our best *final* gameplay
camera seam, but it does show a stable, writable route-entity contract:

- actor override cache at `0x143D76C10/18`
- attached target pair at `route+0x2A8/+0x2B0`
- activity byte keyed by `route+0x124`

### Updated Camera Frontier

So the next camera step is now narrower and more concrete:

1. type the owner class/family that carries `route+0x124`
2. type the `0x143FE1070` resolver-owner contract above `virtual +0xA8`
3. find the campaign caller that can drive `0x141C0DFD0` / `0x141C075DC`
   with a Nadine-compatible override, without leaning on the manual/debug
   toggle path

### First Non-Manual Gameplay Caller Above `0x141C0DFD0`

That third blocker is now narrower too.

The cluster around:

- `0x14073C9E3`
- `0x14073CB4D`
- `0x14073CBF6`
- `0x14073CCBC`

is a real non-manual caller family for `0x141C0DFD0`.

What it does:

- repeatedly calls `0x141C0D4B0`
  - gets the current preferred camera child from the route entity
  - reads `child+0x1A8`
- compares that result against multiple descriptor/sentinel objects:
  - `0x1430542E0`
  - `0x143054560`
- builds compact descriptor records on the stack
- pushes them through `0x141C0DFD0`

The surrounding body also:

- maintains a priority/score-like ladder over a repeated local array
  (`+0x4C/+0x68`, `+0x94/+0xB0`, `+0xDC/+0xF8`, `+0x124/+0x140`, ...)
- emits debug text under strings that clearly include `Camera` and
  `Priority`

So this is a much better frontier than the old manual-camera toggle handlers:

- `0x141C0DFD0` now has an explicit gameplay-facing caller cluster,
- that cluster already chooses between camera descriptors/sentinels,
- and it already uses the same route-entity child selection via
  `0x141C0D4B0`.

That does **not** yet prove Nadine camera ownership is solved.
But it does mean the next question is no longer
"is there any non-debug caller at all?"

It is now:

- what exact owner/state machine contains the `0x14073C9E3..0x14073CCBC`
  camera-priority loop,
- and whether Nadine-compatible route descriptors can be introduced there
  cleanly.

### Camera Overlay Family Behind The Gameplay Route Loop

The `0x14073C9E3..0x14073CCBC` cluster is no longer just a loose caller band.
It sits inside a real per-screen overlay updater:

- `0x14073C8A0`
  - `UpdatePerScreenCameraOverlayPriorityStateAndCommitRouteDescriptors`
  - keyed by `route+0x124`
  - uses a zero-initialized dynamic state bank at `0x143D763E0` with stride
    `0x2B0`
  - keeps per-screen cached/selected overlay state in repeated lanes around:
    - `+0x184`
    - `+0x18C/+0x1D4`
    - `+0x1D4/+0x21C`
    - `+0x21C/+0x264`
  - and commits route-descriptor overrides through `0x141C0DFD0`

This updater is driven by concrete camera-control sentinels, not anonymous
objects:

- `0x143054040` = `kCameraStrafe`
- `0x1430542E0` = `kCameraSwim`
- `0x143054560` = `kCameraZoom`
- exclusion/special-case controls already confirmed nearby:
  - `0x143053820` = `kCameraCheat`
  - `0x1430539E0` = `kCameraShield`
  - `0x143053BB0` = `kCameraMelee`
  - `0x143053BE0` = `kCameraMeleeOverride`

The companion builder under the same family is now clear too:

- `0x14073CDD0`
  - `BuildPerScreenCameraOverlayFollowStateFromRouteDescriptor`
  - copies/merges a `0x50`-byte overlay descriptor state
  - reads route index from `r8+0x124`
  - hash-matches `CameraControlZoom` and `CameraControlScreenShot`
  - consumes a static per-screen tuning bank at `0x1430732F8` with stride
    `0xC0`
  - and outputs the resolved follow-state used by the gameplay route path

That static tuning bank is now a useful anchor in its own right. For each
screen it provides byte/float tuning fields around:

- `+0x00/+0x01/+0x02` enable/mode bytes
- `+0x04/+0x05` gate bytes
- `+0x08/+0x0C/+0x10/+0x14` scalar thresholds

So the current second-view frontier is stronger than before:

- `0x141C0DFD0` is not the whole story;
- the real gameplay layer above it is a per-screen camera-overlay state machine
  keyed by `route+0x124`;
- and that state machine already reasons in stock campaign camera-control
  descriptors like strafe, swim, zoom, melee, shield, contextual-look, and
  vehicle.

RTTI now gives a strong owner-side anchor for this layer:

- `0x142C99C00` = `CameraManagerCustom`
- `0x142C99C50` = `ICameraManagerCustom`
- `0x142C99BB0` = `CameraControlStick`

This does **not** yet type the route entity itself.
But it does prove that the gameplay overlay loop we are sitting in is a real
`CameraManagerCustom` / `CameraControlStick` camera-control layer, not an
anonymous helper band.

### Special Camera Normalization Gates

Two sibling methods in the same family are now concrete enough to matter:

- `0x14073C620`
  - `NormalizeSpecialCameraControlDescriptorToContextualLook`
  - remaps special camera descriptors through `0x141C04E90` using
    `kCameraContextualLook`
  - the proven descriptor set includes:
    - `kCameraEdge`
    - `kCameraCover`
    - `kCameraKnockedDown`
    - `kCameraEvade`
    - `kCameraMonkeyBars`
    - `kCameraBalance`
    - `kCameraOffBalance`
    - `kCameraFreeRope`
    - `kCameraShield`
    - `kCameraFollow`
    - `kCameraVehicle`
  - plus a special-case for `kCameraStrafe` when byte `+0x120` is clear

- `0x14073C760`
  - `ShouldAcceptCameraOverlayCandidateForRouteEntity`
  - filters overlay candidates against the current preferred route child
  - rejects melee/melee-override sentinels in the current route conditions
  - combines the `0x14073BF80` and `0x14073C0E0` filter bands before
    accepting the candidate

This matters because the camera blocker is now tighter:

- the non-manual route caller cluster is already real;
- the descriptors it works with are now typed;
- the next unresolved layer is no longer "what does the overlay loop do?"
- it is "where does the route-owner/overlay-owner get allocated and bound, and
  can a Nadine-compatible descriptor path be inserted there without fighting
  the stock strafe/swim/zoom/melee gates?"

### CameraControlStick Subclass Identities Around The Overlay Layer

The `CameraManagerCustom / CameraControlStick` family is now concrete enough
to stop treating the nearby init wrappers as anonymous:

- `0x141F521B0`
  - `ConstructCameraControlStickBaseAndAssignUniqueScreenId`
  - primary in-place constructor for the `0x320` `CameraControlStick` base
  - installs the vtable at `0x142BB1D98`
  - seeds the default descriptor/follow state through:
    - `+0x1A8..+0x240`
    - `+0x260..+0x2F0`
    - `+0x300..+0x314`
  - assigns the unique per-control screen id in `+0x124` through globals:
    - `0x146DB1628`
    - `0x146DB1630`

- `0x141F52660`
  - `InitCameraControlStickBaseForScreenAndConfig`
  - remains the shared init phase on top of that base constructor
  - reads screen index from `config+0xA0`
  - seeds `kCameraNone` at `+0x1A8`
  - pulls per-screen defaults from `0x1466BFFB0`

The surrounding subclass identities are no longer speculative:

- `0x142983288`
  - vtable for `CameraControlCinemaFollow`
  - slot `0` = `0x14070C6E0`
  - slot `11` = `0x14070C750`

- `0x14070C620`
  - `AllocOrInitCameraControlCinemaFollow3E0`
  - alloc-or-init wrapper for a `0x3E0` `CameraControlCinemaFollow`
  - optionally allocates `0x3E0`
  - runs `ConstructCameraControlStickBaseAndAssignUniqueScreenId`
  - installs `0x142983288`
  - clears subclass state around:
    - `+0x350`
    - `+0x3A8`
    - `+0x3B0`
    - `+0x3B8`

- `0x14070C6E0`
  - `DestroyCameraControlCinemaFollowAndFree3E0`
  - freeing destructor for that `0x3E0` object
  - restores `0x142BB1D98`
  - clears `+0x124`
  - runs shared cleanup `0x1416A3800`

- `0x14070C750`
  - `InitCameraControlCinemaFollowSubclass`
  - calls `InitCameraControlStickBaseForScreenAndConfig`
  - then seeds follow-specific state:
    - `+0x398`
    - `+0x39C`
    - `+0x3A4`
    - `+0x3A8`
  - this now aligns with nearby strings:
    - `CameraControlCinemaFollow`
    - `camera-cinema-follow.cpp`
    - `CameraControlCinemaFollow::Init`

- `0x142981FE8`
  - vtable block for `CameraControlAnimatedPhotographMode`
  - slot `11` = `0x141C13A80`
  - this block sits directly after the strings:
    - `CameraControlAnimatedPhotographMode`
    - `kCameraAnimatedPhotographMode`

- `0x141C13A80`
  - `InitCameraControlAnimatedPhotographModeSubclass`
  - copies `config+0x80` into `+0x398`
  - then tail-runs `InitCameraControlStickBaseForScreenAndConfig`

- `0x142986048..`
  - camera-control block anchored by the strings:
    - `CameraControlFixedLookAround`
    - `*camera-fixed-look-around-settings*`
    - `kCameraFixedLookAround`
  - the hit at `0x1429860A0` is slot `11` inside that vtable family

- `0x141F6C1A0`
  - `InitCameraControlFixedLookAroundSubclass`
  - copies `config+0x80` into `+0x368`
  - then tail-runs `InitCameraControlStickBaseForScreenAndConfig`

- `0x14298B5A0`
  - vtable for `CameraControlSpider`
  - slot `0` = `0x140750DB0`
  - slot `11` = `0x140750E70`
  - confirmed by the adjacent strings:
    - `CameraControlSpider::UpdateLocation`
    - `CameraControlSpider::StickToGroundFalling`

- `0x140750D20`
  - `AllocOrInitCameraControlSpider520`
  - alloc-or-init wrapper for a `0x520` `CameraControlSpider`
  - runs `ConstructCameraControlStickBaseAndAssignUniqueScreenId`
  - installs `0x14298B5A0`
  - initializes the embedded band at `+0x3A0` through `0x141D9EF50`
  - clears subclass state around:
    - `+0x380`
    - `+0x388`
    - `+0x4B0..+0x4F0`

- `0x140750E70`
  - `InitCameraControlSpiderSubclass`
  - calls `InitCameraControlStickBaseForScreenAndConfig`
  - builds an 8-block CRC over static template `0x142BEA360`
  - publishes it through `0x141E776A0`
  - stores resulting handle at `+0x390`
  - seeds subclass scalars at:
    - `+0x3B0`
    - `+0x480`

This matters because the camera frontier is now tied to real gameplay
`CameraControl*` subclasses, not only to anonymous route/entity helpers.
The next useful bind search is therefore:

- who allocates/binds these `CameraControlStick`-derived objects into the
  per-screen `CameraManagerCustom` lifecycle,
- and which of those subclasses is responsible for the stock `screen 1`
  gameplay camera when split/local-player state is alive.

### RTTI Split Inside The Camera Overlay Owner Family

The owner-side camera family is now sharper because the adjacent vtable blocks
finally separate cleanly under MSVC RTTI:

- `0x142987DB8`
  - `vfptr[-1] = 0x142C99C00`
  - complete-object RTTI name:
    - `.?AVCameraManagerCustom@@`

- `0x142987E00`
  - `vfptr[-1] = 0x142C99BB0`
  - complete-object RTTI name:
    - `.?AVCameraControlStick@@`

- `0x14298E820`
  - `vfptr[-1] = 0x142C9A3F0`
  - complete-object RTTI name:
    - `.?AVCameraControlStrafe@@`

That means the current overlay/camera owner frontier is not one flat class.
It is at least a three-way family split:

- a `CameraManagerCustom`-typed bundle rooted at `0x143053850`
- a `CameraControlStick`-typed large overlay subclass using vfptr
  `0x142987E00`
- and a larger `CameraControlStrafe`-typed constructor base at `0x14298E820`

Concrete new pieces:

- `0x14073C460`
  - initializes the global bundle at `0x143053850`
  - writes vfptr `0x142987DB8`
  - clears adjacent cached pointers/flags/scalars through `0x1430538B8`

- `0x14073C520`
  - reset/teardown companion for that same global bundle
  - re-seeds `0x143053850` with vfptr `0x142987DB8`
  - frees and clears the neighboring cached heap pointers

- `0x140772E20`
  - shared in-place constructor for a `0x1FA0` large camera-control object
  - calls `ConstructCameraControlStickBaseAndAssignUniqueScreenId`
  - then installs vfptr `0x14298E820`
  - by RTTI, this is a `CameraControlStrafe`-typed constructor base
  - seeds extended state across:
    - `+0x330..+0x5A0`
    - `+0x1CA0..+0x1EF0`
  - initializes an embedded band at `+0x7C0` through `0x140773180`

- `0x14073B6E0`
  - alloc-or-init wrapper for a `0x1FA0` overlay/route-family object
  - allocates `0x1FA0` when needed
  - runs `0x140772E20`
  - then overwrites vfptr `0x142987E00`

So the current strong reading is:

- `0x140772E20` gives us the large shared `CameraControlStrafe`-typed base
- `0x14073B6E0` turns that into a more specialized large
  `CameraControlStick`-typed overlay object
- `0x143053850` separately carries a `CameraManagerCustom`-typed global bundle

The relationship between the `CameraManagerCustom` global bundle and the live
`0x1FA0` `CameraControlStick`-typed overlay object is still unresolved.
But this is already enough to tighten the next bind search:

- not "what class even owns this camera loop?"
- but "where does the live `0x1FA0` overlay object created by
  `0x14073B6E0` get attached to the per-screen route/camera lifecycle, and how
  does the `CameraManagerCustom` bundle at `0x143053850` participate in that
  handoff?"

### CameraManagerCustom Vtable Contract And Real Route-Owner Path

The `CameraManagerCustom` bundle at `0x143053850` is not a flat no-op.
Its vtable at `0x142987DB8` now splits cleanly like this:

- `+0x00 -> 0x14073B4E0`
  - immediate `ret 0`
- `+0x08 -> 0x14073B4F0`
  - immediate `ret 0`
- `+0x10 -> 0x14073C8A0`
  - per-screen overlay priority updater
- `+0x18 -> 0x14073CDD0`
  - per-screen overlay follow-state builder
- `+0x20 -> 0x14073C400`
  - raw `0x50` descriptor/follow-state copier with low flag-bit merge
- `+0x28 -> 0x14073B650`
  - tiny predicate over descriptor-state dword `+0xC == 2`
- `+0x30 -> 0x14073C620`
  - special-camera normalization into `kCameraContextualLook`
- `+0x38 -> 0x14073C760`
  - candidate gate against the current preferred route child

This matters because the top two published observer callbacks are no-op, but
the later slots are live and participate in the route update loop.

The route-local update contract is now much tighter:

- `0x141C04C50`
  - hard-resets a route descriptor block at `route+0x2F0`
  - clears:
    - descriptor pointer at `+0x10`
    - descriptor-state kind at `+0x18`
    - actor override payload at `+0x20..+0x58`
    - attached pair at `+0x60`
    - active helper pointer at `+0x70`
    - mode bytes at `+0x78/+0x7A`

- `0x141C04CE0`
  - writes a fresh descriptor block into `route+0x2F0`
  - copies:
    - descriptor pointer / kind into `+0x10/+0x18`
    - actor override payload into `+0x20..+0x58`
    - active/fallback actor pair into `+0xD0`
    - helper object from the descriptor source into `+0x70`

- `0x141C077B0`
  - resolves screen-route pose seed by screen index
  - path:
    - `ResolveScreenRouteEntityByIndex(screen)`
    - `route->+0xB0`
    - gate bytes `+0x55/+0x56`
    - bitset gate through `0x14697AE34`
  - outputs either:
    - active pose payload from `route->+0xB0+0x60`
    - or a zero/identity default

- `0x141C078D0`
  - no longer reads like a retained-only helper
  - it is a real screen-route entity camera-state updater:
    - walks child-control list at `route+0x100`
    - writes child follow-state into:
      - `child+0x1D0..+0x21C`
      - optional actor override into `child+0x3A0..+0x468`
    - chooses a preferred child / fallback descriptor
    - then calls `0x141C06F20`

- `0x141C06F20`
  - core route-child insertion/recompute worker
  - inserts or refreshes a child entry under the route
  - carries forward best available follow-state from the current child list
  - caches active descriptor/follow payload into route-local state such as:
    - `route+0x158..+0x198`
    - `route+0x1D0..+0x21C`
    - `route+0x528..+0x5A0`
  - final commit goes through `0x141C06F20 -> 0x141C078D0` caller chain, not
    through the old manual-only helpers

- `0x141C0E9F0`
  - walks the child-control list at `route+0x520`
  - probes each child owner through `child->+0x70->virtual +0x8/+0x10`
  - then uses `CameraManagerCustom->virtual +0x38`
  - first accepted child gets cached into route state

This is the real correction:

- `0x1438929B8` is still only a published bundle pointer,
- but the bundle is not useless;
- its live policy methods are `+0x10/+0x18/+0x20/+0x28/+0x30/+0x38`,
- and the real camera-owner lifecycle sits in the route entity around:
  - `0x141C04C50`
  - `0x141C04CE0`
  - `0x141C077B0`
  - `0x141C078D0`
  - `0x141C06F20`
  - `0x141C0E9F0`

This also sharpens the old manager0 retained path:

- `0x1410F3E00`
  - marks the retained slot-0 live actor active
  - builds a pose seed through `0x141C077B0`
  - resolves screen-route entity `0`
  - then directly calls `0x141C078D0`

So the mature camera route is now better read as:

- `retained actor / gameplay caller`
- `ResolveScreenRouteEntityByIndex(screen)`
- `ResolveScreenRoutePoseSeedOrDefault`
- `UpdateScreenRouteEntityCameraStateFromActorTransform`
- route-local child selection / descriptor commit

The next useful question is no longer "does second camera exist?".
It is:

- which gameplay caller can feed `screen 1` into this same route lifecycle with
  a Nadine-compatible actor override,
- or where `route+0x100/+0x520` children get built for screen `1` in campaign.

One more boundary is now concrete:

- every direct `0x141C078D0` caller checked so far still resolves
  `ResolveScreenRouteEntityByIndex(0)` first:
  - `0x1410F3E32`
  - `0x1402171FD`
  - `0x14029EEE2`
  - `0x1402CEE79`
  - `0x141F65846`
  - and sibling gameplay paths

- `screen 1` still appears directly in:
  - manual-camera cluster around `0x140746192 / 0x140746389`
  - descriptor/attach workers around `0x141C0DFD0`

So the second-screen problem is tighter than "find another camera object":

- stock direct route-update path currently looks primary-screen-only,
- second screen seems to rely on either:
  - a separate `screen 1` route updater not yet pinned,
  - or a descriptor-only attach/override path above `0x141C0DFD0`.

### `screen 1` Is Not Manual-Only: Generic Route Push And A Stock Two-Screen Loop

The old boundary around `screen 1` tightened again.

`0x14073E330` is now pinned as a generic per-screen route push:

- it does **not** hardcode screen `0`
- it takes the explicit screen index in `ecx`
- then:
  - calls `ResolveScreenRouteEntityByIndex(ecx)` via `0x141C06420`
  - forwards the descriptor/follow pair into `0x141C0DFD0`

That already means second-screen route work is not limited to the manual-camera
helpers.

The stronger result is above it:

- `0x1413CB6F0..0x1413CB80C`
  - is a real two-screen loop
  - iterates `ebx = 0 .. 1`
  - resolves both route entities through `0x141C06420`
  - checks the preferred child through `0x141C0D4B0`
  - requires `child+0x1A8 == 0x1430535E0`
  - then pushes a zeroed descriptor through `0x14073E330`

That sentinel matches the already established `kCameraContextualLook` family:

- `0x14073C620`
  - normalizes special camera controls into `kCameraContextualLook`
- `0x1430535E0`
  - is now the strongest current `kCameraContextualLook`-side sentinel in the
    stock two-screen loop

So the boundary changed:

- `screen 1` is **not** only a manual/debug attach path
- there is at least one stock gameplay-facing two-screen route push
- but it currently sits at the **contextual-look / descriptor** layer, not yet
  at a proven Nadine follow-owner layer

This is the new practical frontier:

- prove which higher owner/state machine contains the
  `0x1413CB6F0..0x1413CB80C` two-screen loop
- decide whether that loop can be fed a Nadine-compatible descriptor/owner
- keep the separate question of a stable Nadine follow-owner path open

### The Stock Two-Screen Descriptor Loops Sit In `CameraControlFollow`

The owner-side type question got much narrower.

The string anchor at `0x1429865A0` is:

- `CameraControlFollow`

and it now shows up in both proven stock two-screen loops:

- `0x1413CB6F0..0x1413CB80C`
- `0x1413CF013..0x1413CF14A`

Both loops:

- iterate `ebx = 0 .. 1`
- hash/compare against route child token `+0x1A8`
- then resolve the route by explicit screen index
- and push through:
  - `0x14073E330`
  - `0x141C0DFD0`

This matters because it clarifies the screen-1 split:

- a stock **two-screen gameplay path does exist**
- but it currently appears in the `CameraControlFollow` /
  descriptor-commit layer
- not in a symmetric direct `screen 1 -> 0x141C078D0` route updater

So the current camera picture is now:

- `0x141C078D0`
  - still the heavier direct route-entity follow-state updater
  - checked gameplay callers are still predominantly screen-0-facing

- `0x14073E330 -> 0x141C0DFD0`
  - generic per-screen descriptor push
  - now proven to be used by stock two-screen loops under
    `CameraControlFollow`

The next useful question is no longer "does stock screen 1 exist?".
It is:

- whether `CameraControlFollow`-side two-screen descriptor routing is enough to
  hold a stable Nadine view,
- or whether Nadine still needs a separate follow-owner path under the heavier
  `0x141C078D0` route-state updater.

### `screen 1` Descriptor Routing Already Feeds A Real Mode-Based Route-Child Updater

The next layer below the two-screen descriptor loops is no longer opaque.

- `0x141C0E590`
  - selects the first active route child in `route+0x520`
  - requires:
    - `child+0x78 != 0`
    - `child+0x18 == mode_id`
  - stages that child through `0x141C0E500`
  - then forwards the descriptor into `0x141C0ECC0`

- `0x141C0ECC0`
  - is the heavy route-state recompute worker below that seam
  - it:
    - locks `route+0x2C0`
    - pulls preferred child through `0x141C0D4B0`
    - pulls accepted child through `0x141C0E9F0`
    - compares those against route-local override state such as:
      - `route+0x2F0`
      - `route+0x300`
      - `route+0x368/+0x378`
    - then rebuilds the effective route follow / descriptor payload

This changes the screen-1 question again:

- stock `screen 1` descriptor routing is not just a shallow `0x141C0DFD0`
  write
- it already feeds a real mode-based route-child update stack
- so the next blocker is narrower:
  - whether Nadine can be introduced at the descriptor/child-selection layer
    already used by `CameraControlFollow`
  - or whether she still needs a separate heavy follow-owner path under
    `0x141C078D0`

### The Camera Frontier Has Moved Up To A Mounted `player+0x36C0` Animated-Camera Owner

The next useful step above the route-child layer is no longer hypothetical.

`0x140238500` now reads as a mounted per-screen camera-mode owner worker:

- `owner+0x84`
  - explicit screen index
- `owner+0x88`
  - current mode id
- resolves the route through `0x141C06420`
- dispatches an owner virtual `+0x10` on mode change
- then builds an `apReference-extra` keyed descriptor/state record around the
  current animation contribution id

This is already above plain descriptor commit and above the raw route-child
recompute helpers.

`0x140238800` is the heavier sibling of the same owner family:

- rooted at `player+0x36C0`
- reads current/secondary actor pairs from:
  - `+0x70/+0x78`
  - `+0x60/+0x68`
- uses the strings:
  - `player-animated`
  - `animated-camera-control`
  - `CameraAnimationDirector`
  - `apReference-extra`
- feeds the resulting descriptor into `0x141C0E590`
- and in both the normal and fallback paths still ends up rebuilding route-child
  state and materializing gameplay camera objects under `0x143FE1088`

The strongest gameplay caller above this owner is no longer the isolated
`0x1410EA7C0` tail. The broader live caller block around `0x1410E9D80` now
looks like:

- choose / hash player animation tokens such as:
  - `player-roll`
  - `player-air-punch`
- build a small local state record
- call the mounted owner at `player+0x36C0 -> 0x140238800`
- then continue through the same surrounding mounted Player bands:
  - `+0x1E70`
  - `+0x1DE0`
  - `+0x1DF8`
  - `+0x1D90`
  - `+0x1DC0`
  - `+0x1DF0`

That is the cleanest camera result so far:

- the second-screen stock path is not stopping at generic route helpers
- it already rises into a real mounted Player owner
- and that owner is animation / gameplay-state aware, not just a debug camera
  shim

So the frontier shifts again:

- not back to `ctxB`
- not back to generic split refresh
- not even primarily to `0x141C0DFD0`

The practical camera question is now:

- whether this mounted animated-camera owner at `player+0x36C0` is reusable for
  a `NadineBuddy`-compatible route/actor pair
- or whether Nadine needs a parallel owner that obeys the same
  `0x140238500 / 0x140238800 -> 0x141C0E590` contract

### `player+0x36C0` Now Has A Real Class Identity: `PlayerCameraAnimationDirector`

The mounted owner above the second-screen route stack is no longer anonymous.

`0x140C30290` is the in-place constructor for the object stored at
`player+0x36C0`, and RTTI on its installed vtable `0x142A04C38` resolves to:

- `PlayerCameraAnimationDirector`

That immediately tightens several earlier guesses:

- `player+0x36C0` is a formal top-level Player field, not an ad hoc helper
  pointer
- the early top-level Player zero-init band clears it at `0x1410C97A6`
- the later Player init path writes the real object pointer at `0x1410DDEE3`
- relocation/fixup code at `0x140F649FB` also treats `+0x36C0` as a real
  pointer-bearing mounted subobject

The constructor shape is now concrete:

- seeds actor-pair fields:
  - `+0x60/+0x68`
  - `+0x70/+0x78`
- seeds mode/state fields:
  - `+0x80/+0x84/+0x88`
- seeds an embedded contribution band at:
  - `+0x90..+0xD8`

That lines up directly with the live workers already pinned above it:

- `0x140238500`
  - mode switch worker over `+0x84/+0x88`
- `0x140238800`
  - heavy animated-camera update over the actor pairs at
    `+0x60/+0x68/+0x70/+0x78`

The initialization side also got sharper:

- `0x1410DDEE3`
  - stores the constructed object into `player+0x36C0`
  - then continues through `player+0x3D98`, `player+0x4220`, and a larger
    Player setup band
- this is not a late optional camera attachment; it belongs to the real Player
  bringup

One more useful detail from the owner contract:

- `PlayerCameraAnimationDirector::virtual +0x10` (`0x140C35960`)
  - already uses the actor pair at `+0x70/+0x78`
  - hashes an authored camera token (`FreeRope`)
  - consults `player+0x108/+0x10C` against the shared lookup table at
    `0x14386FD00`
  - and copies the resolved player record into `player+0x110`

So this layer is not just “camera with some strings around it”.
It is already tied back into the same hidden Player identity/record machinery
we had pinned earlier.

That narrows the camera frontier again:

- we no longer need to ask what class owns the stable second-screen route stack
- we now need to ask whether `PlayerCameraAnimationDirector` can be fed a
  Nadine-compatible actor/identity pair through:
  - `+0x60/+0x68`
  - `+0x70/+0x78`
- or whether Nadine needs a sibling owner that implements the same virtual
  contract

The virtual surface is also already meaningful enough to guide the next pass:

- `virtual +0x10` (`0x140C35960`)
  - derives the current animated camera contribution id from the stable pair at
    `+0x70/+0x78`
  - and uses the hidden player identity path (`+0x108/+0x10C -> 0x14386FD00 ->
    +0x110`) while doing it

- `virtual +0x28` (`0x140C30760`)
  - queries contribution timing/type data from the same stable pair
  - and still special-cases the `FreeRope` authored token on that path

- `virtual +0x30` (`0x140C30930`)
  - returns another timing/type flavor over the stable pair
  - again by walking the live actor path rather than a detached camera cache

- `virtual +0x40` (`0x140C35D00`)
  - exposes a compact value derived from the contribution counter at `+0xE4`

So the owner contract is no longer vague:

- this class consumes a real actor pair
- turns it into contribution ids / timing / type
- and then feeds the route-child camera stack above it

That makes the next question very concrete:

- who writes the transient/stable pair fields
  - `+0x60/+0x68`
  - `+0x70/+0x78`
- and can those writers be redirected from Chloe to `NadineBuddy`

Two small predicates made that contract sharper:

- `0x140237760`
  - returns true when `PlayerCameraAnimationDirector` already has:
    - a valid pending actor pair at `+0x60/+0x68`
    - or a resolved contribution id at `+0x80 != 0xFF`
- `0x140239100`
  - is the plain inline validator for a `(ptr,id)` actor pair field
  - confirmed live use:
    - `lea rcx, [director + 0x60]`
    - `call 0x140239100`

That matters because `+0x36C0` is now proven to sit directly on the stock
descriptor push path, not only on a deeper camera-owner layer:

- `0x141004E08`
  - calls `DirectorHasPendingActorPairOrResolvedContribution`
  - if false, falls back to `player+0x1F60 -> virtual +0x310`
  - then still builds a descriptor and pushes it through:
    - `0x14073E330`
    - `0x141C0DFD0`

There are also multiple live writers/consumers around the same state:

- `0x140DD2E66`
  - validates `director+0x60`
  - and if that fails while `director+0x80 == 0xFF`, calls the director's
    `virtual +0x0` and then writes `director+0x88`

- `0x141261179`
  - reaches `player+0x36C0`
  - can write:
    - `director+0x88`
    - `director+0xF0`

- `0x1412666C2`
  - forces `director+0x80 = 0x64` on one authored branch

- `0x1412682A4`
  - reaches `player+0x36C0`
  - and calls the director's `virtual +0x0`

So the current camera frontier is no longer abstract at all:

- `PlayerCameraAnimationDirector` is already inside the stock descriptor-routing
  path for gameplay camera state
- and the concrete remaining problem is to find the writers that seed or swap:
  - `+0x60/+0x68`
  - `+0x70/+0x78`

That is the narrowest current route to a stable Nadine second view.

### `player+0xA8/+0xBC` And `player+0x100` Are Separate Upper-Layer Tracks

One ambiguity is now closed.

The canonical Player pair at:

- `player + 0xA8/+0xBC`

is not the same thing as the hidden authored-descriptor carrier rooted at:

- `player + 0x100`

The separation is visible directly in `Player::Init`:

- `0x1410DCC76`
  - copies `player + 0xA8/+0xBC`
  - straight into `player + 0x3BE0`

while the later optional retained/input packet branch:

- `0x1410DE392..0x1410DE3CC`

does something else entirely:

1. takes `incoming_packet + 0x70`
2. resolves it through:
   - `0x141D23180`
   - `0x141BB3210`
   - `0x1402B13C0`
3. builds a compact carrier through:
   - `0x1416858E0`
4. refreshes the embedded authored-descriptor object at:
   - `player + 0x100`
   through:
   - `0x1416862B0`

So that late retained branch updates the hidden descriptor track, not the
canonical Player pair used by `PlayerCameraAnimationDirector`.

The helper family is now concrete enough to stop conflating the two:

- `0x1416858E0`
  - builds a compact descriptor carrier from a runtime object exposing
    `+0x50/+0x54`
- `0x1416862B0`
  - consumes that carrier
  - validates the keyed entry against `0x14386FD00`
  - and rebuilds the embedded authored-descriptor state under `player+0x100`

At the same time, `PlayerCameraAnimationDirector` is still seeded from the
canonical Player pair path:

- `0x140C30290`
  - copies `owner + 0xA8/+0xBC` into director `+0x70/+0x78`

That means the camera frontier narrows again:

- the next concrete task is no longer "type `player+0x100` more"
- it is to find the real writers/seeders for the canonical Player live pair at
  `+0xA8/+0xBC`, because that pair feeds the stable `PlayerCameraAnimationDirector`
  actor path while the late retained branch does not

### The Canonical `+0xA8/+0xBC` Pair Is Generic Runtime-Object Identity Materialized By `0x1417C3790`

The canonical pair is no longer floating.

The lowest shared runtime-object ctor is now pinned at:

- `0x1416A3330`

and it gives the base identity shape directly:

- `object+0xA8 = 0`
- `object+0xBC = 0x1416A38A0()`

So the ctor only seeds:

- a null identity/owner handle at `+0xA8`
- a fresh object id at `+0xBC`

It does **not** attach the final live handle used later by `Player`.

The generic materializer `0x1417C3790` now closes the missing half.

In the concrete materialization block around `0x1417C3CD1..0x1417C3D1C` it:

- writes packet source/owner into:
  - `object+0xA0`
- links the intrusive node back to the object through:
  - `node+0x8 = object`
- stores that node pointer into:
  - `object+0xA8 = node`
- recomputes the runtime id through:
  - `0x14161FC60`
  - then writes the result into:
    - `object+0xBC`

So the canonical `+0xA8/+0xBC` pair later consumed by:

- `PlayerCameraAnimationDirector`
- retained-player slot tables
- upper gameplay/runtime builders

is not a special `Player`-only field pair.

It is generic runtime-object identity:

- `+0xA8` = intrusive materialization node / owner handle
- `+0xBC` = materialized runtime object id

That changes the camera/control frontier again:

- the old question "which Player ctor writes `+0xA8/+0xBC`?" is closed
- the next real question is:
  - which runtime-materialization path produces the `Player` object whose
    generic identity later seeds `PlayerCameraAnimationDirector`,
  - or whether it is cleaner to bypass that and redirect the director through
    explicit writers of `+0x60/+0x68` or `+0x70/+0x78`

### One Live `PlayerCameraAnimationDirector` Path Already Comes Back Through Retained Slot `0`

The retained-slot bridge is now explicit in one live camera/gameplay caller.

In the block around `0x141290F96..0x141290FAE`:

- it calls:
  - `0x1410F0D10(0)`
- does it twice on the success path
- then uses:
  - `returned_entity + 0x36C0`
  - `-> 0x140237760`

So at least one live `PlayerCameraAnimationDirector` predicate path already
reads the actor through:

- retained player slot table
- `GetRetainedPlayerEntityBySlot(0)`
- then `entity+0x36C0`

That is a useful closure:

- retained slot identity seeded by the generic runtime-object/materializer
  family is not isolated bookkeeping
- it is already upstream of live director/camera logic

So the next fork is concrete:

- either find a second retained/local-player entity path that can resolve to
  `NadineBuddy` or a Nadine-compatible entity,
- or bypass that layer and feed `PlayerCameraAnimationDirector` directly
  through its pair fields/writers

### `0x140C34AC0` Is A Secondary-Pair Contribution Worker, Not A Pair Seeder

The only direct caller currently confirmed for `0x140C34AC0` is:

- `0x141261301`

Its actual contract is narrower than the older wording implied.

`0x140C34AC0`:

- first validates the secondary actor pair:
  - `director+0x70`
  - `director+0x78`
- if that pair resolves, marks the live actor through byte `+0x72`
- then compares contribution age against:
  - `director+0xF0`
- and updates only the contribution/ring state:
  - `director+0xD0`
  - `director+0xD8`
  - `director+0xE0`
  - `director+0xE4`

What it does **not** do:

- it does not seed `director+0x60/+0x68`
- it does not seed `director+0x70/+0x78`

So this worker now clearly belongs downstream of pair ownership rather than at
the point where the animated-camera target is chosen.

### The Known `0x141290F96..0x141290FAE` Bridge Is Slot `0` Twice, Not A Slot `0/1` Pair

The concrete disassembly around `0x141290F96..0x141290FAE` now matters more
than the older shorthand summary.

Current exact block:

- `xor ecx, ecx`
- `call 0x1410F0D10`
- `test rax, rax`
- if live:
  - `xor ecx, ecx`
  - `call 0x1410F0D10`
  - `mov rcx, [rax+0x36C0]`
  - `call 0x140237760`

So this already-proven live bridge is:

- retained slot `0`
- then retained slot `0` again
- then `entity+0x36C0`
- then `DirectorHasPendingActorPairOrResolvedContribution`

This does **not** currently prove a symmetric slot-`1` live bridge into the
director path. That remains an open question and should not be inferred from
this block.

### `0x141261140` Is The First Confirmed Animation-Driven Update Path Above `player+0x36C0`

The only currently confirmed direct caller of
`AdvancePlayerCameraAnimationDirectorContributionState` is:

- `0x141261301`

That call sits inside the larger block beginning at `0x141261140`, which now
reads as a real animation-driven update path over `player+0x36C0`, not a
generic camera helper.

Confirmed behavior in this block:

- loads:
  - `player+0x36C0`
- writes:
  - `director+0x88`
  - optional contribution age into `director+0xF0`
- gates on contribution/control bits under:
  - `r13+0x50`
- if the filtered path is accepted:
  - calls `0x140C34AC0`
- otherwise:
  - falls back to `director->virtual +0x0`

This matters because it narrows the next search:

- the primary pair writers are still not here,
- but `PlayerCameraAnimationDirector` is now confirmed to sit under a live
  animation-contribution update path rather than only under route plumbing or
  retained-slot predicates.

### `0x140C349D0` Is The First Director Method That Touches Both Pair Families At Once

`0x140C349D0` is a more useful `PlayerCameraAnimationDirector` method than the
earlier tiny predicates because it spans both pair families in one place.

Observed behavior:

- validates the primary pair:
  - `+0x60/+0x68`
- when that pair resolves:
  - increments `director+0xE4`
  - and under one global byte forces `director+0x80 = 0xFF`
- then hard-resets contribution ring state:
  - `director+0xD0 = 0`
  - `director+0xE0 = -1`
- then validates the secondary pair:
  - `+0x70/+0x78`
- when that pair resolves:
  - marks the live actor through byte `+0x72`
  - refreshes `director+0xF0` from the actor's `+0xE0 -> +0x10` time source

This is the first method on the director that proves:

- the two pair families are not redundant;
- the primary pair `+0x60/+0x68` participates in contribution invalidation /
  epoch advance;
- the secondary pair `+0x70/+0x78` participates in stable live contribution
  timing.

It still does **not** seed either pair family. So the real writer search stays
upstream of this method.

### The Director Constructor Already Seeds The Secondary Pair `+0x70/+0x78`

The constructor `0x140C30290` is no longer just generic setup for
`PlayerCameraAnimationDirector`.

It now reads concretely as:

- zeroes the primary pair:
  - `+0x60/+0x68`
- if incoming `rdx` is non-null:
  - reads:
    - `rdx+0xA8`
    - `rdx+0xBC`
  - copies that 16-byte pair into:
    - `director+0x70/+0x78`
- otherwise zeroes that secondary pair too
- then initializes:
  - `+0x80/+0x84/+0x88`
  - contribution band `+0x90..+0xD8`
  - `+0xE0 = -1`
  - `+0xF0 = 0`

This is the first concrete write site for a director pair family:

- the stable/secondary pair `+0x70/+0x78` is seeded directly in the ctor
  from the incoming **Player** object's canonical runtime identity
  `(+0xA8/+0xBC)`;
- the pending/primary pair `+0x60/+0x68` is *not* seeded there and remains a
  separate upstream writer problem.

That narrows the search materially:

- one branch now goes upward through the exact call shape:
  - `0x1410DDED3: mov rdx, rsi`
  - `0x1410DDED9: call 0x140C30290`
  so the ctor receives the top-level `Player` itself as the source object;
- the other goes upward through explicit writers of `+0x60/+0x68`.

Concrete meaning:

- `PlayerCameraAnimationDirector` does not need a separate helper object to
  learn its stable camera subject;
- the stock path simply copies the Player's own canonical live pair into
  `director+0x70/+0x78` during mounted-subobject construction.

### `0x140238800` Is The First Confirmed Direct Writer Of `director+0x60/+0x68`, But It Only Consumes And Clears The Pair

The earlier writer search for the transient/pending pair is now split more
cleanly.

`0x140238800` does directly write `PlayerCameraAnimationDirector +0x60/+0x68`,
but not in the way the older hypothesis implied.

What the function does on its live path:

- resolves the stable pair first:
  - `director+0x70/+0x78`
- validates the transient pair:
  - `director+0x60/+0x68`
- uses both while building the animated-camera descriptor and route-child state
  through:
  - `0x141C0E590`
- and then writes a zeroed inline pair back into:
  - `director+0x60/+0x68`

Concrete reset writes are now confirmed twice inside this worker:

- `0x140238C25..0x140238C35`
  - zeroes stack `(ptr,id)`
  - copies it into `director+0x60/+0x68`
- `0x140238FDF..0x140238FEF`
  - does the same on the fallback tail

So `0x140238800` is not the upstream producer of a non-zero pending pair.
It is the first confirmed **consume-and-clear** point for that pair.

That changes the real frontier:

- the writer question is no longer "who touches `+0x60/+0x68` at all?"
- it is now:
  - who seeds a **non-zero** pending pair before `0x140238800` consumes it,
  - or whether the stock path expects that pair only as a short-lived one-shot
    handoff from some higher animation/camera owner

One neighboring helper also sharpened:

- `0x140237E60`
  - resolves the current screen route from `owner+0x84`
  - refreshes the hidden descriptor track through `0x1416860A0`
  - marks `owner+0x8C = 1`
  - builds a `player-animated` fallback descriptor

That helper sits exactly on the reset branches around `0x140238800`, so it now
belongs to the same "pending pair was not reusable, rebuild fallback camera
state instead" lifecycle.

### The Broad Caller Around `0x1410E9EBA` Still Does Not Seed A Non-Zero Director Pending Pair Before `0x140238800`

The strongest gameplay caller above `PlayerCameraAnimationDirector` was worth
checking directly because it was the obvious place for a one-shot pair seed.

The widened block around `0x1410E9D80..0x1410EA0D2` does **not** populate
`player+0x36C0` before calling:

- `0x1410E9EB0`
  - `mov rcx, [player + 0x36C0]`
- `0x1410E9EBA`
  - `call 0x140238800`

What happens before that call:

- token/hash work around:
  - `player-roll`
  - a second authored token at `0x142A4B2A0`
- hidden-descriptor work through:
  - `player+0x100`
  - `0x14014E240`
  - `0x1405D96B0`

What happens after that call is the more interesting correction:

- the same caller loops over validated `(ptr,id)` candidates
- and passes each one into:
  - `0x1403D4070(player, &pair)`

But `0x1403D4070` still does not come back to `player+0x36C0`.
Its live work is on:

- `player+0x100`
- hidden descriptor refresh / record publication
- actor-side geometry / authored-camera calculations

So this upper caller gives a useful negative result:

- the non-zero pending pair for `PlayerCameraAnimationDirector +0x60/+0x68`
  is not being written inline immediately before `0x140238800` here
- and the post-call candidate loop through `0x1403D4070` still feeds the
  hidden descriptor path rather than the director directly

That narrows the frontier again:

- the pending-pair seeder is likely elsewhere in the mounted camera/animation
  owner family, or arrives through a virtual path not visible in flat callers

### The Formal `PlayerCameraAnimationDirector` Vtable Surface Is Now Typed Enough To Rule Out Pair Setters

The live vtable at `0x142A04C38` now reads concretely as:

- `+0x00 -> 0x140C349D0`
- `+0x08 -> 0x140C303E0`
- `+0x10 -> 0x140C35960`
- `+0x18 -> 0x140C30410`
- `+0x20 -> 0x140C30420`
- `+0x28 -> 0x140C30760`
- `+0x30 -> 0x140C30930`
- `+0x38 -> 0x140C35CF0`
- `+0x40 -> 0x140C35D00`

The important correction is not the exact table itself, but what it rules out.

The newly closed slots behave like this:

- `0x140C303E0`
  - only advances the contribution-ring base at `+0xD8` when an existing entry
    pointer falls within a supplied bounds window
- `0x140C35CF0`
  - immediate no-op return
- `0x140C30760`
  - reads the stable pair `+0x70/+0x78`
  - derives timed output / mode-class result
  - does not write either pair family
- `0x140C30930`
  - same shape: stable-pair reader, derived timing/mode output, no pair writes
- `0x140C30410`
  - just returns helper code pointer `0x140C30250`
- `0x140C30420`
  - just returns helper code pointer `0x140C2FAC0`

So the director's formal virtual surface is now strong enough to close another
loop:

- there is still no evidence that any exposed `PlayerCameraAnimationDirector`
  slot materializes a non-zero pending pair into `+0x60/+0x68`
- the stock pending-pair seeder is therefore more likely to be an external
  upstream helper in the same mounted camera/animation family than a hidden
  setter inside the director's own vtable contract

### The Control Frontier Above `MoveTo` Is Now Sharper: Shared Callers Feed Full Snapshots Into `owner+0x15B8`

The older `MoveTo` work was already known to be too low-level for a playable
Nadine. The more useful update here is that the shared locomotion/controller
seams above it are now clearer.

Two helpers on the `owner+0x15B8` controller have now been typed more tightly:

- `0x141E0ED10`
  - compares incoming mode against `controller+0x34`
  - maps labels such as:
    - `None`
    - `NavPoly`
    - `NavLedge`
  - checks whether the owner object is currently active through its bitset
  - publishes the active-state transition through `0x141DE05D0("SetActive", ...)`
  - stores the new mode back into `+0x34`
- `0x141E0EDF0`
  - consumes a `0x30`-byte snapshot block
  - copies it into `controller+0x40/+0x50/+0x60`
  - clears `+0x70/+0x78`
  - then runs mode-specific follow-up helpers to rebuild auxiliary state when
    the current mode needs it

The strongest new caller on this layer is the shared path around `0x141E18B03`:

- it pulls a transform/state snapshot from:
  - `source + 0x230`
  - `source + 0x240`
  - `source + 0x250`
- resolves the locomotion/controller object through:
  - `(source_owner + 0x1D0) + 0x15B8`
- then calls, in order:
  - `0x141E0ED10(controller, owner, mode)`
  - `0x141E0EDF0(controller, snapshot)`

That matters because it is materially closer to a real "play as Nadine" ingress
than the older nav-goal backend:

- it is not just `MoveTo`
- it is not just `NpcMoveTo`
- it is an owner-side mode switch plus full snapshot apply into the shared
  locomotion/controller subobject

So the direct-control frontier is now cleaner too:

- for a first playable Nadine demo, the better control-side target is no longer
  the raw `MoveTo` issue path
- it is the broader `owner+0x15B8` mode/snapshot layer above it

### `0x141DE9E60` Still Looks Like The Better Real Control/Application Bridge Than Raw `MoveTo`

The older reading of `0x141DE9E60` already had it seeding owner runtime state
from an incoming state record. The new correction is that this path still looks
closer to a first "play as Nadine" bridge than the lower `MoveTo` backend.

The useful shape is now concrete:

- `rcx = owner`
- `rdx = incoming state record`
- `r8d = mode`
- `r9b = optional flag`

Early effects:

- logs/labels the incoming record
- validates it against `0x14386FD00` via `record+0x8/+0xC`
- pushes the resolved descriptor (or null) into `owner->virtual +0x1E8`
- copies:
  - `record+0x30 -> owner+0x130`
  - `record+0x40 -> owner+0x140`
- calls `owner->virtual +0x208`
- then drops into the shared `owner+0x15B8` layer through:
  - `0x141E0ED10`
  - `0x141E0EDF0`

The more important downstream part remains:

- it rebuilds broad owner runtime mirrors
- reuses the canonical owner pair at `+0xA8/+0xBC`
- and later re-dispatches a large synthesized actor-side block through the
  resolved live actor's `virtual +0xD0`

That makes the control-side frontier more specific:

- if the first demo target is "switch to Nadine and actually play",
  the strongest current control/application seam is no longer:
  - `MoveTo`
  - `NpcMoveTo`
- it is:
  - `incoming state record`
  - `0x141DE9E60(owner, record, mode, flag)`
  - shared `owner+0x15B8` mode/snapshot apply

So the next useful question is not "how do we push another nav goal into Nadine",
but:

- what live source path normally materializes the incoming state record passed
  into `0x141DE9E60`,
- and whether that source can be redirected from Chloe's active control stream
  into `NadineBuddy`.

### `0x141DE9E60` Now Looks Embedded In a Materialized `BehaviorScavenge` Dispatch Family

The direct `.text` caller search above `0x141DE9E60` stayed mostly cold, but
the function itself now has a stronger indirect shape.

Three repeated `.rdata` blocks include the same contiguous function family:

- `0x142930060`
- `0x142B83B08`
- `0x142BD9910`

Each block contains, in the same order:

- `0x1403CF9F0`
- `0x141DE71C0`
- `0x1403CFA00`
- `0x141DE9E60`
- `0x141DEE3E0`
- `0x141DEE560`
- trailing helper entries

Those are not ordinary flat callsites. They look like repeated indirect dispatch
tables for closely related materialized-state variants.

That matters because the current-state apply bridge is no longer best treated as
"one standalone helper with two rare callers". It now looks embedded in a
formal materialized-state behavior interface.

### `BehaviorScavenge` Registration Publishes the Relevant Indirect Tables

The first concrete registration seam on this family is now pinned too.

- string `0x142940348` = `BehaviorScavenge`
- `0x1404B8180` writes:
  - `0x143048978 -> 0x142940348`
  - `0x143048970 -> 0x142952020`

So at least one live materialized-state behavior family is explicitly
registered as `BehaviorScavenge`, with an indirect dispatch table published in
globals instead of direct flat calls.

The nearby table roots now read as:

- `0x142951FC0`
- `0x142952020`

Both include the same small helper band and behavior-side workers, including:

- `0x1404B7F00`
- `0x1404B80C0`

That is the strongest new reason to stop expecting a simple direct caller above
`0x141DE9E60`: the live ingress is very likely behavior-table driven.

### `0x1404B7F00` Is a Live Indirect Installer For a `0x570` Materialized State Variant

One of the newly pinned behavior-side workers is materially more useful than the
older `0x48/0x540/0x9B0` replacement helpers.

`0x1404B7F00`:

- starts from `owner+0x15B8`;
- builds a nav/snapshot block through `0x141DF2D60`;
- optionally specializes the source from an actor-like object when
  `source+0x60 == 0xAC710201`;
- detaches the current `owner+0x1F80` object through `0x14048F180`;
- allocates `0x570`;
- constructs it through `0x140473590`;
- reattaches it through `0x14048F260`.

This is not the old blind `MoveTo` backend. It is a real materialized-state
install path that already consumes locomotion snapshot/state from the same broad
owner layer we care about for playable Nadine.

### `0x140473590` Constructs The `0x570` BehaviorScavenge Variant

The ctor path is now concrete enough to pin:

- `0x140473590` initializes a `0x570` object;
- installs vtable `0x14293F7D0`;
- seeds a local `+0x40` subobject through `0x140473240`;
- initializes behavior-side fields at:
  - `+0x538`
  - `+0x540`
  - `+0x558`
  - `+0x560`
- then passes the caller-supplied snapshot/source payload into the subobject
  through `0x140473020`.

So the control frontier sharpened again:

- `0x141DE9E60` is one useful record-apply bridge;
- but a broader current-state path already exists through:
  - `BehaviorScavenge` registration
  - indirect materialized-state tables
  - `0x1404B7F00`
  - `0x140473590`
  - `owner+0x1F80`

That looks closer to a real "switch current playable state" surface than the
old flat caller hunt around `0x141DE9E60` alone.

### The Published `BehaviorScavenge` Root Is Metadata, Not A Clean Flat Dispatch Table

One earlier reading was too aggressive.

`0x1404B8180` still publishes:

- `0x143048978 -> "BehaviorScavenge"`
- `0x143048970 -> 0x142952020`

But the target region around `0x142951CC0..0x142952020` is not a simple qword
array of direct worker pointers.

Raw retail bytes show a mixed metadata blob:

- a small pointer-heavy head around `0x142951CC0`
- then controller/debug strings such as:
  - `Dodge`
  - `Panic`
  - `ClimbController`
  - `MeleeActionController`
  - `CoverController`
  - `EvadeController`
  - `LookAtController`
  - `AimController`
  - `WeaponController`
  - `CombatController`
  - `TraverseController`
  - `CinematicController`
  - `ShieldController`
  - `SearchController`
  - `ReviveController`
  - `InvestigateController`

So the useful correction is:

- the `BehaviorScavenge` registration is real;
- but `0x142952020` is better treated as a behavior/controller metadata root,
  not as a neat direct dispatch table to walk entry-by-entry.

That pushes the practical control frontier away from metadata-root hunting and
toward the live state-preparation branches that actually converge into owner
state install/apply.

### `0x9B0` And `0x70` Variants Are Now Named: `BehaviorShootMoving` And `BehaviorTurnToFace`

The sibling materialized-state ctors are no longer anonymous.

- `0x140480230`
  - seeds a large `0x9B0` variant;
  - hashes the label string `BehaviorShootMoving`;
  - copies caller payload into the large behavior object and tail state.
- `0x14048A260`
  - seeds a small `0x70` variant;
  - hashes the label string `BehaviorTurnToFace`;
  - copies the compact turn-to-face payload into `+0x40..+0x64`.

That matters because the `+0x1F80` installation surface is now clearly a
multi-behavior family with named movement/combat-side variants, not one opaque
"generic state blob".

### `0x1404BA9C0` Is A Stronger Direct-Control Branch Than The Metadata Roots

The branch around `0x1404BA9C0` is now a better practical seam for playable
Nadine work than continued metadata-root hunting.

What tightened:

- it carries the debug string
  `-- NavCharacter::SetStrafeMode: %s @ %s`;
- it uses `Normal / Always / Never` mode strings through `0x1403CF8B0`;
- for accepted branches it prepares staged state blocks in globals:
  - `0x143048E80`
  - `0x143048EA0`
  - `0x143048EC0`
  - `0x143048ED4`
- then it calls `owner->virtual +0xC48` with that prepared state;
- and later converges into `0x141DE4D20`.

This is the important control-side correction:

- the useful live seam is no longer just
  `BehaviorScavenge registration -> maybe some indirect worker`;
- it is now
  `NavCharacter::SetStrafeMode-style branch -> owner virtual +0xC48 -> prepared state sink`.

That is much closer to "switch Nadine into a Chloe-like controllable state"
than another pass over behavior metadata.

### `0x141DE4D20 -> 0x141E15030` Is The Shared Prepared-State Sink

The low-level convergence point is now concrete.

`0x141DE4D20`:

- copies a compact prepared record into stack-local owner state;
- writes `owner+0x1748` from `record+0x18`;
- routes through `owner+0x15C0`;
- then calls `0x141E15030`.

`0x141E15030` is the heavy shared sink:

- validates the owner actor pair through `0x14386FD00`;
- updates `owner+0x110/+0x120` from the validated actor state;
- compares the incoming state record against current nested-control state;
- rebuilds control deltas and thresholds;
- emits `NavMesh...`-tagged diagnostics;
- commits accepted state into owner-side nested-control storage.

Direct caller scan confirms this is not niche:

- `0x141DE4D20` has many gameplay callers
  (`0x1404707C5`, `0x140474089`, `0x1404BB24B`, ...)
- `0x141E15030` is called from multiple wrappers in the same family
  (`0x141DE4E1A`, `0x141DE4F52`, `0x141DE52A7`, `0x141DE53CB`).

So the current strongest control-side path for the first demo now reads:

- high-level `NavCharacter::SetStrafeMode`-style branch
- `owner->virtual +0xC48`
- `0x141DE4D20`
- `0x141E15030`

rather than:

- old raw `MoveTo`
- or abstract `BehaviorScavenge` metadata roots by themselves.

### `0x141DE4F90` Is Stronger Than A Generic Apply Helper: It Already Bridges Control, Selection, And Snapshot Push

The older summary of `0x141DE4F90` was still too neutral.

The fuller body now reads as a real owner-side takeover seam:

- pulls a scalar/mode snapshot from `owner+0x15B8` through `0x141E0D530`;
- normalizes it through `0x14190F9F0`;
- logs the chosen record label through `0x1403CF8B0`;
- inspects current nested-control selection through `owner+0x15C0+0x2F8`;
- if a competing live record is present, deactivates it through
  `owner+0x15C8 -> virtual +0x10 -> virtual +0xC0`;
- resolves the accepted runtime object for `record->+0x60`;
- applies the new one through `virtual +0xE0` with packed owner transforms;
- pushes the record transform through `owner->virtual +0xC48`;
- refreshes `owner+0x15C0` through `0x141E17530`;
- and finally re-applies the accepted snapshot into `owner+0x15B8` through
  `0x141E0EDF0`.

That is much closer to a real "switch active controllable state" seam than a
generic record sink. It already joins:

- control scalar/source state,
- nested-control selection,
- current-state object install,
- and locomotion snapshot apply.

### `0x141DE5190` Is A Higher-Level Prepared-State Builder Above The Shared Sink

There is also a more specific wrapper above `0x141E15030`.

`0x141DE5190`:

- copies the incoming compact record;
- injects two extra scalar inputs from `xmm2/xmm3`;
- forces prepared record kind `4`;
- writes `owner+0x1748` from the source record;
- then routes the result into `0x141E15030`.

Direct callers now pinned:

- `0x140474051`
- `0x1404BC0EC`

This matters because the takeover frontier is no longer a single flat
prepared-state sink. We now have:

- `0x141DE4D20` for direct prepared transform/state blocks
- `0x141DE5190` for the scalar-augmented prepared-state variant
- `0x141DE4F90` for the heavier current-state-object + nested-control path

For the first Nadine demo, `0x141DE4F90` and `0x141DE5190` now look more
valuable than another round of raw `MoveTo` or metadata-root hunting.

### The `0x1403F5B60` Family Is A Prepared NPC Action Layer, Not The Best First Direct-Control Seam

The older reading of `0x1403F5B60` as a generic locomotion initializer is now
too broad.

What tightened:

- the surrounding `0x140545000..0x14056B000` callers repeatedly resolve source
  objects through `0x141E31150 / 0x141E31230 / 0x141E31310`;
- they then write command kind into `owner+0x24F4` and fill the pending action
  block at `owner+0x2470..+0x24DC`;
- several of those callers sit directly under script/NPC command strings such
  as:
  - `npc-set-allow-lose-target-out-of-zone`
  - `npc-in-melee-with-player?`
  - nearby `npc-turn-to` / `npc-stop-aiming` string families.

So this whole family is better read as a prepared NPC action/behavior layer.
It is still useful, but it should not be confused with the first clean seam
for "play as Nadine like Chloe".

### `0x14058B900` Is The Strongest Live Non-Script Layer Above `owner+0x15B8`

The strongest new control-side result is that there is already a live,
non-script branch above the shared controller at `owner+0x15B8`.

`0x14058B900` now reads as a large combat/action-state handler that:

- resolves live actor/runtime identity;
- filters a state-id set;
- forces low-cardinality nested state through `0x141DE5A10` when
  `owner+0x24C8 < 3`;
- commits packaged state through:
  - `0x141E0EDF0`
  - `0x141E0ED10`
- uses actor-graph/state helpers through:
  - `0x1418B7060`
  - `0x14189E5A0`
  - `0x14189CC80`
- and routes follow-up work through nested virtuals `+0x178 / +0x170 / +0xB0`.

Two live inner branches now stand out:

#### `0x14058BA40`

- writes a full snapshot into `owner+0x15B8` through `0x141E0EDF0`;
- switches controller mode `1` through `0x141E0ED10`;
- then resolves actor-graph selection data through `owner+0x1E0` and
  `0x1418B7060`.

#### `0x14058BC70`

- writes a blended snapshot into `owner+0x15B8`;
- switches controller mode `2`;
- then finalizes the nested-control side through `0x141DEF910`.

This is the most important control-side correction from this pass:

- the `0x1403F5B60` command family looks more like prepared NPC actions;
- the `0x14058B900 -> 0x14058BA40 / 0x14058BC70` family looks like a live
  runtime action layer that already drives the same shared controller used by
  the stronger Nadine ingress around `0x141DE9E60`.

### The Immediate Control Frontier Is Now Narrower

The next useful step is no longer "search more command writers".

It is:

1. climb above `0x14058B900`;
2. identify which live owner/state family decides when the mode-1 and mode-2
   branches fire;
3. check whether that decision can be redirected toward `NadineBuddy` for the
   first playable switch demo.

### The Control Bridge `0x141DE9E60` Is Reached Through A Shared Character Virtual NadineBuddy Already Has (slot +0xC48)

Climbed above `0x141DE9E60` (the stronger "play as Nadine" control/application
bridge). It has exactly two direct callers:

- `0x140416B9C` inside `InstallOrApplyCurrentStateRecord` (`0x140416B20`)
- `0x1404872D8` inside `sub_140486EB0`

Neither caller has further direct `.text` callers (both are dispatched as
virtuals). Pointer scan of `.rdata` shows their vtable membership:

- `0x140416B20` (InstallOrApplyCurrentStateRecord) appears in ~12 character-class
  vtables at the same slot, i.e. it is a SHARED character virtual, not a
  one-off. Confirmed slots:
  - NadineBuddy vtable (`0x142949638`) + `0xC48` -> `0x140416B20`
  - This is exactly the slot earlier used for the "native teleport" effect on
    NadineBuddy. So that slot is really the install/apply-current-state-record
    virtual, and the visible teleport was a side effect of applying a state
    record.
- `0x140486EB0` appears in exactly ONE vtable (slot ptr at `0x142941490`); it is
  a class-specific aim/transform-basis virtual that builds an orientation basis
  from runtime objects (validates retained slot via `0x14386FD00`, quaternion
  math over record `+0xA0/+0xC0/+0xF0`) and then seeds owner state via
  `0x141DE9E60`.
- `0x141DE9E60` itself is referenced from 3 vtables
  (`0x1429300A0`, `0x142B83B48`, `0x142BD9950`).

Why this matters for the elevation goal:
- the control-apply bridge into `owner+0x15B8` (`0x141DE9E60` -> `0x141E0ED10` /
  `0x141E0EDF0`) is reached through a SHARED character virtual that NadineBuddy
  already implements at slot `+0xC48`;
- so NadineBuddy is not missing the ingress virtual; the open question stays
  "what live owner/state family decides to call slot +0xC48 / `sub_140486EB0`
  with a real current-state record, and can that decision be driven for Nadine".

Next concrete probe:
- decode the upper owner update that invokes character virtual `+0xC48` and the
  aim virtual `sub_140486EB0` per frame, and find the gate that selects when a
  real (non-null) current-state record is applied vs cleared.

### SYNTHESIS: Camera Route (screen 1) And Control/Local-Player Both Converge On The Same Resolver `0x143FE1070->+0xA8(index)`, And index 1 Is Empty

Cross-checking the camera-route track and the control/local-player track shows
they bottom out at the SAME resolver, which unifies the whole second-player
problem:

- camera route side (per the camera lifecycle anchors):
  - `0x141C04F80` builds the two-entry route table `0x14697AD50`
  - `0x141C06420` resolves a live route entity from that table by index
  - `0x141C0DFD0` resolves the route's actor through
    `(*(qword*)0x143FE1070)->virtual +0xA8(route+0x124)` where `route+0x124`
    is the screen index (0/1)
  - `0x141C075DC` then writes the attached target pair into `route+0x2A8/+0x2B0`

- control / local-player side (earlier):
  - `local-player-0/1` resolve through `(*(qword*)0x143FE1070)->virtual +0xA8(index)`
  - this is the CharacterManager-style `GetCharacter(index)` family
    (`0x141D63C50` = `CharacterManager::GetCharacter`, asserts
    `index < m_maxCharacterCount`)
  - runtime: `local-player-0 = Player`, `local-player-1 = 0`

So both the second camera (screen index 1) and the second local player resolve
their actor from `0x143FE1070->+0xA8(1)`, and that returns null in campaign
because CharacterManager has no character at index 1.

This is the unifying blocker:
- it is NOT two separate problems (camera vs control);
- both need `0x143FE1070->+0xA8(1)` to return a valid character;
- and the control-apply ingress already exists on NadineBuddy (shared virtual
  slot `+0xC48` -> `0x141DE9E60`).

Why the earlier resolver-hook test (returning NadineBuddy for index 1) did not
visibly work is now better framed: returning the raw `NadineBuddy` object is not
the same as registering a valid CharacterManager character entry with a matching
`+0xBC` id and `+8` backing object that the route/control validators accept
(`ptr->(+8)->+0xBC == id`).

Next concrete probe (unifies both tracks):
- decode `0x143FE1070`'s object + its `+0xA8` vfunc target and the
  CharacterManager character array layout;
- determine the minimal valid character entry shape (`+8` object, `+0xBC` id)
  that `GetCharacter(1)` must return so BOTH the screen-1 camera route and the
  local-player-1 control path accept it;
- then test registering NadineBuddy as character index 1 rather than hooking the
  resolver to return the bare object.

### CharacterManager Layout + Why The Resolver Hook Was Insufficient (observers not notified)

`sub_141D63640` is `CharacterManager::RemoveCharacter` (file
`character-manager.cpp`, asserts use the `CharacterManager` / `m_character`
strings). Decoded layout:

- character array base = `manager + 0`
- array entry = 16 bytes: `{ character_ptr (+0), id (+8) }`
- count field = `manager + 0xC`
- character validity check used everywhere:
  `entry.ptr->(+8)->+0xBC == entry.id`
- Remove scans for the matching entry, compacts the array down, decrements
  `manager+0xC`, then:
  - iterates an observer/listener list at `manager+0x10 .. +0x18`
    (linked nodes; `node->vtable +8(node, character)`),
  - and calls `character->virtual +0x810` -> `0x141FEBE20`.

`CharacterManager::GetCharacter` (`0x141D63C50`) is the matching index accessor
(`index << 4 + base`, validate, return `entry.ptr`). The resolver
`0x143FE1070->virtual +0xA8(index)` used by both the screen camera route and the
local-player path resolves characters from this same manager.

Critical correction to the earlier resolver-hook experiment:
- returning `NadineBuddy` directly from the `+0xA8` resolver did NOT run the real
  registration, so the CharacterManager observer list (`manager+0x10`) was never
  notified and `character->+0x810` was never called;
- that is the most likely reason nothing visibly changed: camera/control
  observers only react to a real Add/Remove, not to a hooked getter return.

So the correct lever is the real `CharacterManager::AddCharacter` (inverse of
`0x141D63640`): write `{NadineBuddy-character, id}` into the array, increment
`manager+0xC`, and let it notify the observer list. That is the single unifying
action that should wake BOTH the screen-1 camera route and the local-player-1
control path.

Next probe:
- find `AddCharacter` (sibling in the `0x141D63xxx` cluster that writes
  array[count] and increments `manager+0xC` while notifying `manager+0x10`),
- and determine the exact character object/id shape it expects so NadineBuddy
  (or a thin character wrapper over her) qualifies as `entry.ptr` with
  `entry.ptr->(+8)->+0xBC == id`.

### KEY: `CharacterManager::AddCharacter (0x141D63420)` Is The Single Unifying Registration For A Second Character

`sub_141D63420` is `CharacterManager::AddCharacter(manager, character)`:
- `rcx = manager` (the object behind `*(0x143FE1070)`)
- `rdx = character`

Behavior:
- capacity = `manager + 8`, count = `manager + 0xC`; asserts count < capacity
- dedupe-scans existing entries [0..count) against the incoming character
- builds the new 16-byte array entry from the character's canonical identity pair:
  - `entry.ptr = character + 0xA8`
  - `entry.id  = character + 0xBC`
- writes it into `array[count]` (array base = `manager + 0`), then `count++`
- notifies the observer list at `manager + 0x10 .. +0x18`:
  `node->vtable[0](node, character)` for every registered observer

This is the decisive lever, because:
- the entry uses exactly `character+0xA8 / character+0xBC`, the canonical
  runtime-identity pair that NadineBuddy already has
  (live: `Nadine+0xA8 = 0x10E020FAE8`, `Nadine+0xBC ~= 0x6BD`);
- so NadineBuddy already qualifies as a valid `character` argument with no
  wrapper needed for registration;
- AddCharacter performs the REAL registration and notifies observers, which is
  exactly what the earlier resolver-hook skipped.

Concrete next experiment (replaces the resolver hook):
- resolve `manager = *(uintptr_t*)(module + 0x3FE1070)`
- call `AddCharacter(manager, NadineBuddy)` on the game thread
- expect: `GetCharacter(1)` now returns the Nadine entry, count becomes 2, and
  observers (screen-1 camera route + local-player-1 control) get notified
- watch whether the screen-1 camera route and local-player-1 control path wake
  up using Nadine as the real character at index 1

`sub_141D63640` = `CharacterManager::RemoveCharacter` (inverse: compacts array,
decrements `manager+0xC`, notifies observers via `node->vtable+8`, calls
`character->virtual +0x810`). Use it to cleanly unregister for test toggling.

### CORRECTION: Nadine Is Already A Registered CharacterManager Character — The Resolver Has A Separate "active local player" Filter

The AddCharacter lead was followed to a concrete result that corrects the earlier
convergence claim.

The real `CharacterManager` instance is `*(0x143898630)` (NOT `*(0x143FE1070)`;
that latter global is a separate local-player resolver owner). AddCharacter
callers (`0x140402904`, `0x1410D34B7`, ...) load `this` from `*(0x143898630)`.

Live CharacterManager (`*(0x143898630)`):
- array base = manager+0, capacity (manager+8) = 0x20, count (manager+0xC) = 2
- entry0: `{ptr=0x10E0201128, id=0x69E}` -> Player (object 0x10E1060900)
- entry1: `{ptr=0x10E020FAE8, id=0x6BF}` -> NadineBuddy (object 0x10E1304200);
  note `ptr` equals `Nadine+0xA8`, `id` equals `Nadine+0xBC`.

So Nadine IS ALREADY character index 1 in CharacterManager. AddCharacter is not
the blocker, and registering her again would just hit the dedupe path.

Why `local-player-1` still resolves to 0 (corrected model):
- the local-player resolver `(*(0x143FE1070))->virtual +0xA8(index)` returns the
  character, and the local-player lookup then passes it through `0x141E98C10`,
  which returns the object only if a bit is set:
  - `id = [0x146AA11B4]` (a current view/screen/context selector)
  - `bitset = character->[+0x30]`
  - returns character only if `bit[id]` is set in that bitset, else 0
- Nadine exists as a character, but her `+0x30` "active local player / view" bit
  for the current selector is not set, so `local-player-1` resolves to 0.

Corrected frontier (replaces the AddCharacter plan):
- the second-player lever is NOT character registration (already done);
- it is the "active local player / view membership" gate: make Nadine's `+0x30`
  bitset include the bit indexed by `[0x146AA11B4]` (or whatever per-screen
  selector the screen-1 route/control uses), so the resolver + `0x141E98C10`
  filter return her as local-player-1 / the screen-1 route actor.

Next probe:
- decode `0x141E98C10` filter inputs precisely (what `[0x146AA11B4]` is, and the
  `character+0x30` bitset structure / which bit = "local player N" or "visible on
  screen N");
- find who sets that bit for Player (index 0) and whether the split-screen login
  path is supposed to set it for index 1.

### Accuracy Note: The `0x141E98C10` Bit-Filter Reconstruction Is Inconclusive

Runtime check of the candidate filter for the local-player resolver:
- selector `[0x146AA11B4] = 0x1A2` (word index 6, bit 34)
- Player entry.ptr+0x30 bitset = 0x10E023B880; word[6] = 0; bit34 = 0
- Nadine entry.ptr+0x30 bitset = 0x10E1258C80; word[6] = 0; bit34 = 0

Both Player and Nadine read bit34 = 0, so `0x141E98C10` with this selector is NOT
the gate that distinguishes "Player resolves as local-player-0" from "Nadine does
not resolve as local-player-1". Either the local-player resolver `+0xA8` does not
funnel through this exact filter/selector, or the bitset base/word math here is
not the one used at the live call site.

What IS solid from this pass:
- `CharacterManager` (`*(0x143898630)`) already holds Player (id 0x69E) AND
  Nadine (id 0x6BF) as characters 0 and 1 — character registration is NOT the
  blocker.
- The local-player resolver owner `*(0x143FE1070)` is a SEPARATE object from
  CharacterManager; do not conflate them.

Corrected next probe (more careful):
- decode the actual local-player resolver vfunc `(*(0x143FE1070))->+0xA8` body
  (not assume it is CharacterManager::GetCharacter), and capture exactly how it
  decides player 0 vs player 1;
- find who registers a local player into THAT resolver (vs CharacterManager),
  and whether the split-screen login path is supposed to populate index 1 there.

### Local-Player Resolver Struct `0x143FE1070` (0x120 bytes): Player-1 id Slot Is Unset, Distinct id Space

`0x143FE1070` is NOT a pointer; it is an embedded 0x120-byte resolver struct.
`sub_1400C63C0` zero-inits it (memset 0x120 via 0x1424ECF80) and sets two dword
"id" fields to -1:
- `+0xE8` (`0x143FE1158`)
- `+0x104` (`0x143FE1174`)
(the 0x1C delta between them = per-local-player slot stride; two slots.)

Live runtime dump (campaign) of `0x143FE1070`:
- `+0xE0 = 2`, `+0xE4 = 2`  (look like local-player counts)
- `+0xE8 = 0x5ACA`          (player-0 id slot: SET)
- `+0x104 = 0xFFFFFFFF`     (player-1 id slot: STILL UNSET / -1)
- `+0x00 = 0x1103294000`    (a sub-object/manager pointer; its vtable+0xA8 read
  external/garbage this session, so treat with care)

So the corrected, verified blocker shape:
- character registration is done (CharacterManager already has Player + Nadine);
- the local-player RESOLVER has two id slots, and the player-1 id slot (+0x104)
  is never populated in campaign (stays -1);
- the resolver id space is distinct from CharacterManager ids
  (player-0 resolver id 0x5ACA != CharacterManager Player id 0x69E), so the
  player-1 slot cannot be naively set to a CharacterManager id.

Writer search:
- only `sub_1400C63C0` (init) references `+0xE8/+0x104` via direct rip-relative
  stores;
- runtime population of `+0xE8` must come from an INDEXED write
  (`[base + idx*0x1C + off]`), and the resolver base is taken via `lea` in only
  that one init site, so the registrar is not locatable by static rip-xref.

Recommended next step (dynamic, higher yield than more static/poke):
- attach x64dbg, set a hardware write breakpoint on `resolver+0xE8`
  (`module+0x3FE1158`) during a normal campaign load;
- capture the function that writes player-0's id there — that is the local-player
  registrar whose index-1 path is the real second-player lever;
- only then decide whether to drive it for Nadine.

### Correction: The `0x1406B3D70` Family Is `Patrol@ScriptSkill`, Not A Clean First Direct-Control Path

The newer climb above `owner+0x15B8` needed one more correction.

The qword hit at `0x14297B260` is a real vtable slot for:
- `0x1406B3D70`
- `0x1406B3E00`
- `0x1406B3FA0`
- `0x1406B4260`
- `0x1406B4530`

The complete object locator at `0x142C95C38` resolves this vtable block to:
- `.?AVPatrol@ScriptSkill@@`

So:
- `0x1406B3D70`
- `0x1406B3640`
- `0x1406B3940`
- the branch rooted at `0x1406B40D0`

are not the best "play as Nadine" seam after all. They are a patrol/script-skill
control layer for AI behavior.

This does NOT make them useless, but it does mean:
- they are too high in the AI stack;
- they should not be treated as the primary direct-control ingress for the first
  playable Nadine demo.

The practical correction is:
- keep `owner+0x15B8` itself as the important shared controller layer;
- stop treating the `Patrol@ScriptSkill` router as the main takeover point.

### Correction: The Second Direct Caller Above `0x141DE9E60` Is `BehaviorTeleport`

The single vtable block containing `0x140486EB0` sits at `0x142941490`.
Its complete object locator at `0x142C84510` resolves to:
- `.?AVBehaviorTeleport@@`

So `0x140486EB0` is not a generic player-control producer. It is
`BehaviorTeleport`-specific logic that:
- validates a retained/runtime pair;
- builds an orientation/aim basis from record fields around `+0xA0/+0xC0/+0xF0`;
- then calls `0x141DE9E60`.

This tightens the earlier picture:
- one direct path into `0x141DE9E60` is the shared character virtual
  `0x140416B20` that NadineBuddy already has at vtable slot `+0xC48`;
- the other direct path is AI behavior logic (`BehaviorTeleport`), not raw
  gameplay input.

So the strongest reusable ingress for a first playable Nadine demo remains:
- the shared state-record apply virtual `0x140416B20`,
- plus the lower shared controller methods on `owner+0x15B8`,
  especially `0x141E0ED10` / `0x141E0EDF0`.

### The Clean Control Frontier Is Now The Shared `owner+0x15B8` Layer, Not Patrol/Teleport

After the two corrections above, the cleanest control-facing layer is:
- `0x141DE9E60`
- `0x141E0ED10`
- `0x141E0EDF0`
- and the broader shared caller region around `0x141E18B03`

What is already clear there:
- snapshots are materialized from `source+0x230/+0x240/+0x250`;
- the controller is resolved from `(source_owner+0x1D0)+0x15B8`;
- the path then pushes:
  - mode via `0x141E0ED10`
  - snapshot via `0x141E0EDF0`

That is a better frontier for "switch to Nadine and play as her" than the
Patrol/Teleport families above it.

### Concrete Shared Snapshot/Publish Block: `0x141E18A70..0x141E18BA3`

The earlier anchor "around `0x141E18B03`" can now be tightened into a full
working block:
- `0x141E18A70 .. 0x141E18BA3`

What this block concretely does:
- optional setup through:
  - `0x141CCAE20`
  - `0x141CCABA0`
  - `0x141E14890(rcx=rdi, edx=4, r8=rdi+0x260)`
- marks `rdi+0x468 |= 1`
- copies the live snapshot from:
  - `rsi+0x230`
  - `rsi+0x240`
  - `rsi+0x250`
- resolves the shared controller through:
  - `rdx = *(rdi+0x1D0)`
  - `controller = *(rdx+0x15B8)`
- applies:
  - mode via `0x141E0ED10`
  - snapshot via `0x141E0EDF0`
- then continues owner-side publication:
  - `rdi+0x240 = (*( *(rdi+0x1D0)+0xE0 )+0x10)`
  - `0x141E16CF0(rdi)`
  - `0x14189AF80(rdi, ..., ..., 0)`
  - tail jump into `0x14189AF30`

This is materially stronger than the earlier "some caller near `0x141E18B03`"
reading. It is now a concrete shared snapshot-to-controller-and-publish block.

That matters because the first playable Nadine demo does NOT need the whole AI
patrol stack if this block can be redirected or reproduced with:
- a Nadine-owned `rdi+0x1D0`
- a valid source snapshot at `rsi+0x230/+0x240/+0x250`

### Sibling Publish Block: `0x141E18D00`

The neighboring block at `0x141E18D00` is a useful contrast:

- it does **not** pull the shared snapshot from `source+0x230/+0x240/+0x250`;
- instead it works through owner-local state at:
  - `+0x258`
  - `+0x2F8`
  - `+0x300`
- it may call `0x141E16CF0(owner)`;
- then it runs `0x141E14890(owner, ..., owner+0x260)`;
- and finally publishes through the **same** storage pair:
  - `0x14189AF80`
  - `0x14189AF30`

So the shared storage/publication tail is now clear:
- one branch (`0x141E18A70`) publishes after feeding `owner+0x15B8` from a
  source snapshot;
- the sibling branch (`0x141E18D00`) publishes from owner-local state instead.

That makes the remaining open question even narrower:
- where exactly does the source object that owns `+0x230/+0x240/+0x250` come
  from before `0x141E18A70`.

### Small Descriptor-Node Builders Used By The Same Publish Tail

Three tiny sibling allocators are now pinned:
- `0x141E1D110`
- `0x141E1D5C0`
- `0x141E1F890`

All three:
- allocate a 0x20-byte helper object;
- store the owner back-pointer at `+0x10`;
- clear `+0x18`;
- install different vtables.

This matters because `0x141E18A70` passes one of these node types
(`0x141E1F890`) into `0x14189AF80`, while `0x141E18D00` uses a sibling node
type (`0x141E1D110`).

So the publication tail is now structured enough that the unresolved part is no
longer "what happens after the snapshot is applied". The unresolved part is
strictly the producer of the incoming source snapshot.

### `owner+0x2F8` Now Reads As A Live `ActionPack` Handle, Not An Anonymous Snapshot Source

The next pass tightened the producer side enough to stop treating the
`owner+0x2F8` source as an unknown blob.

Two live paths now clearly write `owner+0x2F8 = source+0x64` and then continue
through the same validation/install machinery:

1. a `ProcessCommand_MoveTo` family path around `0x141E16500`
2. a `SwitchActionPack / ResumeMoving` family path around `0x141E17600`

String anchors on those paths are now concrete:
- `ProcessCommandMoveTo: Already There`
- `ProcessCommand_MoveTo: %s Controller Exit -> EnterImmediately`
- `SwitchActionPack: NavAnimController::Reset()`
- `ResumeMoving: Setting new action pack:`
- `ResumeMoving: Setting AP to pathfind TAP:`
- `ResumeMoving: Setting AP to goal AP:`

That is the decisive semantic shift:
- the shared source behind `0x141E18A70` is not just "some object with
  +0x230/+0x240/+0x250";
- it is a live `ActionPack`-side runtime object carrying:
  - id at `+0x64`
  - kind tag at `+0x60`
  - snapshot payload at `+0x230/+0x240/+0x250`

### Shared `ActionPack` Validation / Release / Enter Helpers

The surrounding helper family is now much sharper:

- `0x141E18350`
  - resolves `owner+0x2F8`
  - validates the live source through source virtual `+0xB0`
  - stores `source+0x60` into `owner+0x304`
  - runs the special `0xAC710201` gate through `0x141E181D0`
  - then enters the reserve/prepare path
  - sets `owner+0x300 = 1` on success

- `0x141E18450`
  - releases the live source resolved from `owner+0x2F8` through source
    virtual `+0xD0`
  - clears the five pending handle slots at `owner+0x2E4..+0x2F4`
  - resets `owner+0x300` back to `0`

- `0x141E18700`
  - takes a validated source object
  - records `owner+0x300 = 2`
  - copies the shared snapshot from `source+0x230/+0x240/+0x250` into
    `owner+0x260..+0x280`
  - and then drops into the same publication tail used by the shared snapshot
    block

- `0x141E189C0`
  - refreshes `owner+0x238` from the controller time source at
    `owner+0x1D0+0xE0+0x10`

- `0x141E181D0`
  - is now firmly the special "first tap window" gate for source kind
    `0xAC710201`

### The Control Frontier Is Narrower Now

The control question is no longer:
- what kind of object lives behind `owner+0x2F8`

That part is now good enough:
- it is an `ActionPack`-side runtime object

The open question is narrower:
- which Chloe-side path chooses and fills that live `ActionPack` source
- and whether that same source-selection path can be redirected onto
  `NadineBuddy` for the first demo where control switches from Chloe to Nadine.

### `owner+0xE90` And `owner+0x258` Are Backup `ActionPack` Handles Feeding `owner+0x2F8`

The next pass tightened the owner-local side enough to stop treating
`owner+0x258` and `owner+0xE90` as random neighbor fields.

The sibling publish block `0x141E18D00` now reads concretely:
- it first tries to resolve `owner+0x258`
- for a broad mode range it prefers `owner+0xE90` if that handle is live
- otherwise it falls back to `owner+0x258`
- and then copies the chosen handle into `owner+0x2F8`

So the current source handle is not seeded out of nowhere:
- `owner+0x2F8` is rebuilt from backup `ActionPack` handles already cached on
  the owner

Two producer-side facts are now concrete:

- around `0x141E15DE0`
  - the code resolves `owner+0xF0`
  - stores the resulting source id into `owner+0x258`
  - copies the source snapshot into `owner+0x260..+0x280`
  - and also updates nearby owner-side state such as `+0x2E0`, `+0x440`,
    `+0xEB8..+0xECC`

- around `0x141E1A900`
  - when the selected object passes a local gate, its id at `+0xA10` is copied
    into `owner+0xE90`
  - then owner flags `+0x470/+0x471/+0x468` and staging state around
    `+0x290/+0xEE0` are updated

This is the strongest practical narrowing so far for the first playable Nadine
demo:
- we no longer need to guess where `owner+0x2F8` comes from;
- it can be rebuilt from `owner+0xE90` and `owner+0x258`;
- the next useful question is which Chloe-side path populates those two backup
  `ActionPack` handles in normal play, and whether the same writes can be
  redirected onto `NadineBuddy`.

### The Two Backup `ActionPack` Feeds Already Have Clear Gameplay Meaning

One more pass made the two backup feeds much less abstract.

#### `0x141E15A00` = `StopAndStand`

The branch around `0x141E15A00` is now pinned by strings:
- `ProcessCommand_StopAndStand: NavAnimController::StopLocomoting()`
- `ProcessCommand_StopAndStand: status CommandSucceeded (already stopped)`

What it does:
- builds a local snapshot through `0x141E179D0`
- publishes it through `0x14189AF80 / 0x14189AF30`
- then releases the current `ActionPack` through `0x141E18450`
- and clears `owner+0x2F8 = -1`

So this branch is a stop/stand owner command over the same `ActionPack`
machinery, not a separate control system.

#### `0x141E1C820` = `Resume`

The branch around `0x141E1C820` is now pinned by strings:
- `NavStateMachine::Resume(void)`
- `Resuming navigation with invalid/non-moving handoff (and no pending cmd), forcing nav anim controller to stop`
- `Resuming navigation with a valid moving handoff, going to blind moving`

What it does:
- scans resumable entries
- builds a handoff snapshot through `0x141E179D0`
- chooses between moving and non-moving resume cases
- and publishes the result through the same owner-side storage tail

This matters because it shows the current control frontier is already inside a
real gameplay state machine:
- stop
- resume
- current `ActionPack`
- handoff snapshot

That is closer to "switch to Nadine and play" than the older low-level
`MoveTo` picture.

### The Next Useful Question Is Now Very Tight

The next control-side question is no longer:
- what object is stored in `owner+0x2F8`
- or what `owner+0xE90` / `owner+0x258` mean

Those are now good enough.

The narrow question now is:
- in ordinary Chloe gameplay, who chooses between the `StopAndStand`,
  `Resume`, and other `ActionPack` sources,
- and which function actually writes `owner+0xF0`, `owner+0xE90`, and
  `owner+0x258` before the snapshot/publish block runs.

### `owner+0x258` Sits Under The Real Moving-State Updater

One more pass finally tied `owner+0x258` to a concrete owner state machine.

`0x141E15C80` is now strong enough to read as the heavy moving-state updater:
- it branches on the descriptor at `owner+0x80`
- it validates current moving inputs
- it resolves the moving source from `owner+0xF0`
- it stores that source id into `owner+0x258`
- it copies the source snapshot into `owner+0x260..+0x280`
- and it updates nearby moving-state bands like:
  - `+0x2E0`
  - `+0x440`
  - `+0xEB8..+0xECC`

The string anchors on this path are now concrete:
- `Processing command MoveToActionPack (but action pack is blocked by '%s') FAILED`
- `Processing command MoveAlongSpline (but spline has disappeared!) FAILED`
- `Processing command MoveToActionPack (but action pack has disappeared!) FAILED`

So `owner+0x258` is no longer just a spare slot. It is the moving-state backup
`ActionPack` handle.

### There Is Also A Higher Dispatcher Over `owner+0x80`

`0x141E1A040` is now the useful owner-side dispatcher above that moving-state
logic:
- it reads the current mode from `owner+0x80`
- for one mode it goes into `0x141E158B0`
- for several others it goes into `0x141E15C80`
- then it copies the resolved descriptor fields from `owner+0x80` into
  `owner+0x120..+0x1B0`
- refreshes owner-side time fields like `+0x228/+0x218`
- and marks publication state through `+0x468`

This matters because it gives the first clear nontrivial owner-side chooser
above the raw `ActionPack` helpers.

### Where `owner+0xE90` Fits

`owner+0xE90` still comes from a different branch:
- the path around `0x141E1A900`
- that path writes `owner+0xE90 = selected_object+0xA10`
- and then updates:
  - `+0x470`
  - `+0x471`
  - `+0x468`
  - `+0x290`
  - `+0xEE0`

That means the owner currently has at least two distinct backup feeds into the
same final `ActionPack` selection:
- moving-state feed via `owner+0xF0 -> owner+0x258`
- selected-object feed via `owner+0xE90`

This is a stronger frontier for the first playable Nadine demo than another
pass on low-level movement commands, because it is finally sitting on the same
owner-side chooser that already drives Chloe's live movement states.

### `owner+0x80` Now Has A Usable Mode Map

The reset/log path at `0x141E17240` now gives a practical mode map for the
state descriptor stored at `owner+0x80`:

- `1` -> `StopAndStand`
- `2` -> `MoveToLocation`
- `3` -> `MoveToActionPack`
- `4` -> `MoveAlongSpline`

This is important because `0x141E1A040` already dispatches on that same field.
So the state chooser above Chloe's movement is no longer opaque.

### Two More Neighbor Paths Around The Same Command-State Layer

Two neighboring functions are now useful enough to keep together with the
moving-state updater:

- `0x141E158B0`
  - exit/cleanup path for the current command state under `owner+0x80`
  - refreshes publication state
  - inspects the current action-pack through the controller
  - then routes command-specific cleanup and release

- `0x141E16920`
  - exit path for the reserved-action-pack state (`owner+0x300 == 2`)
  - publishes current or fallback descriptor
  - refreshes `owner+0x238`
  - releases the current action-pack through `0x141E18450`
  - advances `owner+0x208` into the post-exit phase

This means the owner-side command layer is now readable as a small state
machine instead of a bag of unrelated helpers:
- current command kind in `owner+0x80`
- current phase in `owner+0x208`
- current action-pack kind in `owner+0x304`
- current/reserved status in `owner+0x300`
- moving backup action-pack in `owner+0x258`
- selected-object backup action-pack in `owner+0xE90`

### There Is A Shared Prepared Movement Setter Above The Command-State Layer

`0x141E14890` is now the strongest common setter above those branches:

- it accepts:
  - `rcx = owner`
  - `edx = mode id`
  - `r8  = prepared descriptor`
- it validates actor context through `owner+0x1D0` and `0x14386FD00`
- it stores the mode in `owner+0x444`
- it copies the prepared descriptor into:
  - `owner+0x290`
  - `owner+0x2A0`
  - `owner+0x2B0`
- it derives owner-side follow vectors into `owner+0x2D0`

This does **not** write `owner+0x80` directly, but it is already a central
live ingress for movement-state changes.

Its gameplay callers now cover several ordinary paths:

- `0x141DE4A0A` with `edx = 4`
- `0x141E1818A` with `edx = 4`
- `0x141E18AF7` with `edx = 4`
- `0x141E18D1A` with `edx = 4`
- `0x141E18E2A` with `edx = 0` or `4`
- `0x141E1B159` with `edx = 0,1,2,3,4`

So the owner-side movement machinery is no longer just:
- raw `MoveTo`
- raw `owner+0x80`

There is now a common prepared-state setter sitting in between.

### The Large Chooser Above It Is `0x141E1BA20`

`0x141E1BA20` is now the strongest ordinary-Chloe path above that setter.

What it does:
- reads queued entries and current owner/action-pack state
- unpacks candidate records through `0x141E06C00`
- compares them against the current prepared descriptor in `owner+0x290`
- then feeds `0x141E14890` with concrete mode ids

This is where the practical mode feed is now visible:
- `edx = 1`
- `edx = 2`
- `edx = 3`
- `edx = 4`
- plus `edx = 0` on the fallback branch

That means the nearest useful frontier has shifted again:
- not to the low NPC command writers
- not only to the `owner+0x80` dispatcher
- but to the large live chooser that already converts ordinary Chloe state into
  the prepared descriptor setter `0x141E14890`.

### `owner+0x80` Is Starting To Look More Like A Mirror Than The Primary Ingress

The direct caller map around `0x141E14890` makes this sharper.

There are now many ordinary gameplay callers that bypass `owner+0x80` and feed
the shared prepared setter directly:

- `0x141DE4A0A` with mode `4`
- `0x141E17051` with mode `4`
- `0x141E1818A` with mode `4`
- `0x141E18D1A` with mode `4`
- `0x141E18E2A` with mode `0` or `4`
- `0x141E1CDA0` with mode `2`
- `0x141E1CDB9` with mode `0`
- `0x141E1E595` with mode `2`
- `0x141E1E74A` with mode `3`
- `0x141E1E795` with mode `0`
- `0x141E1E8CD` with mode `2`
- `0x141E1F1E9` with mode `1`
- `0x141E1F26D` with mode `0`
- `0x141E1F3AA` with mode `2`
- `0x141E1F46E` with mode `1`
- `0x141E1F5AF` with mode `0`
- `0x141E202B9` with mode `4`

And those callers feed it from several concrete descriptor sources:

- `owner+0x260`
- `owner+0x290`
- stack-built temporary descriptors

So the practical picture has changed:

- `owner+0x80` is still a real command-state description
- but it no longer looks like the cleanest upstream interception point
- the stronger live center now looks like:
  - `0x141E14890`
  - its mode field `owner+0x444`
  - its prepared descriptor at `owner+0x290/+0x2A0/+0x2B0`

That is a better frontier for the first playable-switch demo than treating
`owner+0x80` as the only place where Chloe's live movement state is chosen.

### `0x141E1A000` Is The Main Owner-Side Movement Update Loop

One more pass finally showed that the broad owner update loop above all of this
is already visible.

`0x141E1A000` is now the strongest ordinary-Chloe owner update loop above both
`0x141E1BA20` and the older `owner+0x80` dispatcher.

What it does:
- first runs `0x141E1BA20`
- then checks controller state through `owner+0x1D0`
- if needed, dispatches `owner+0x80` into:
  - `0x141E15C80`
  - or `0x141E158B0`
- mirrors the resolved descriptor into `owner+0x120..+0x1B0`
- refreshes owner time at `+0x228/+0x218`
- updates flags under `+0x468/+0x472`
- and then handles several follow-up cases:
  - disappeared traversal action-pack
  - reserved action-pack cleanup
  - further moving-state follow-up through `0x141E1AA20`

So the practical center shifted again:
- `0x141E14890` is still the shared setter
- but the stronger ordinary update loop above it is now
  `0x141E1A000`

### `0x141E1AA20` Is The Pathfind-Result Follow-Up Inside That Loop

The next pass made the inner follow-up at `0x141E1AA20` much clearer.

It is pinned by strings:
- `ProcessPathFindResults`
- `Our ActionPack turned invalid, releasing`
- `We are action pack usage state reserved, but we have no valid AP pointer, releasing`
- `Releasing old AP`
- `for new AP`

What it does:
- works from the current prepared mode in `owner+0x444`
- resolves the current, reserved, and backup action-pack handles:
  - `owner+0x2F8`
  - `owner+0xE90`
  - `owner+0x258`
- compares them against path results
- releases invalid or disappeared action-packs
- builds compact descriptors through `0x141E06C00`
- then feeds replacement modes into `0x141E14890`:
  - `0`
  - `1`
  - `2`
  - `4`

This matters because `0x141E1A000` is no longer just a wrapper over
`0x141E1BA20`.

It has its own real second-stage movement/path chooser in `0x141E1AA20`.

### `0x141E1E4F0` Is The Ordinary `UpdateStateMoving` Family

The large family around `0x141E1E4F0` is now pinned by strings:
- `UpdateStateMoving: can arrive`
- `UpdateStateMoving: LocoCtrl HasArrivedAtGoal = TRUE`
- `UpdateStateMoving: status CommandSucceeded (goal reached)`
- `UpdateStateMoving: NavAnimController::StopNavigating()`
- waypoint-change logging

What it does:
- uses the current prepared mode from `owner+0x444`
- validates the current moving action-pack state
- can feed `0x141E14890` with:
  - mode `2`
  - mode `3`
  - mode `0`
- checks arrival and waypoint changes
- then routes into success/failure handling for the moving command

This is important because it looks like a normal moving-state updater for Chloe,
not an edge-case helper.

### `0x141E1F300` Is A Narrower `PostRootLocatorUpdate` Family

The sibling family around `0x141E1F300` is now pinned by strings:
- `PostRootLocatorUpdate: resolve entry succeeded but requires me to be stopped - stopping`
- `PostRootLocatorUpdate: resolved entry - Repath to new entry point (%0.2fm away)`

What it does:
- builds temporary descriptors from:
  - `owner+0x360`
  - `owner+0x370`
  - `owner+0x380`
- feeds `0x141E14890` with:
  - mode `2`
  - mode `1`
  - mode `0`
- then does publication and post-root follow-up checks

So this path is live and useful, but it looks narrower than the main
`UpdateStateMoving` family.

### Two Neighbor Paths Are Real, But Not The Main Ingress

`0x141E18120` is pinned by:
- `Abandoning interrupted state '%s'`

What it does:
- resets the helper at `owner+0x1D8`
- can release the current action-pack
- rebuilds a fresh descriptor through `owner+0x1D0 -> virtual +0x1C8`
- feeds mode `4` into `0x141E14890`
- republishes through `0x141DE0690`

This is a cleanup/rebuild path, not the cleanest primary ingress for live
movement.

`0x141E20280` is pinned by:
- `UpdateStatePlayingEnterAnimation: status CommandSucceeded (entered action pack)`
- `UpdateStatePlayingEnterAnimation: ActionPack vanished, exiting`
- `UpdateStatePlayingEnterAnimation: status CommandFailed (action pack disappeared)`

What it does:
- if the current action-pack is still live, feeds mode `4` into `0x141E14890`
- otherwise falls into failure/exit handling and republishes

So it is useful for classification, but also not the strongest generic movement
entry.

### Current Best Control Frontier

The strongest ordinary-Chloe control picture is now:
- shared setter:
  - `0x141E14890`
- large chooser:
  - `0x141E1BA20`
- broad owner update loop:
  - `0x141E1A000`
- ordinary moving-state family:
  - `0x141E1E4F0`

And the weaker neighboring families now look like:
- `0x141E1F300` = post-root / interrupted-state update
- `0x141E18120` = abandon interrupted state
- `0x141E20280` = enter-animation update

That means the next useful step is no longer another pass on every direct
caller of `0x141E14890`.

The tighter question now is:
- who calls the broad owner update loop `0x141E1A000` in ordinary Chloe
  gameplay,
- and whether that same loop can be redirected onto `NadineBuddy`.

### `0x141E1A000` Is An Internal Label Inside Real `NavStateMachine::Update(bool)`

One more pass corrected the boundary.

`0x141E1A000` is not the real function start.

The real function start is:
- `0x141E19F90`

And it is now pinned by the literal string:
- `void __cdecl NavStateMachine::Update(bool)`

The block at `0x141E1A000` is just the central internal label inside that
larger function.

This matters because the call search now finally found the first real outer
edge:
- `0x141DE8CB1 -> 0x141E19F90`

### The First Real Outer Caller Sits Inside `NavCharacter::Update`

The caller block around `0x141DE8B80..0x141DE8E60` is anchored by:
- `void __cdecl NavCharacter::Update(void)`
- source path `...\\gameplay\\ai\\agent\\nav-character.cpp`

The important part is:
- it checks `owner+0x15B8`
- reads a bit from `owner+0x15A8`
- then calls:
  - `0x141E19F90`
  - with `rcx = owner+0x15C0`
  - and `dl` derived from that owner-side flag

So the strongest current control stack is now no longer abstract:

- outer gameplay update:
  - `NavCharacter::Update` block around `0x141DE8B80..0x141DE8E60`
- state-machine update:
  - `0x141E19F90 = NavStateMachine::Update(bool)`
- inner update labels:
  - `0x141E1A000`
  - `0x141E1AA20`
  - `0x141E1BA20`
  - `0x141E1E4F0`

That is the best control frontier so far for the first Nadine demo.

### BREAKTHROUGH (resolver line): Campaign `GetLocalPlayer(index)` Reads The Pair Table `0x143EEBE80` — Slot 1 Empty Is The Direct Blocker

Closed the resolver line that was left hanging (resolver vtable `+0xA8`).

`*(0x143FE1070)` sub-object vtable `+0xA8` = `0x140298D70` (GetLocalPlayer-style):
- gate byte `0x143E07473` (split/coop-active). If NOT active (ordinary campaign),
  it tail-calls `0x1410F0F60(index)`.
- if split/coop active, it instead scans the net-player slot array
  `0x143DD7488` (= NetGameManager `0x143DD70C0 + 0x3C8`), stride `0x43D0`, which
  is dormant/empty in campaign (already proven).

`0x1410F0F60(index)` is the campaign resolver:
- if split gate `0x143E07473` (and `0x143E02054`) not set -> branch `0x1410F0F90`:
  - reads a 16-byte pair from `0x143EEBE80 + index*0x10` = `{ptr(+0), id(+8)}`
  - validates `ptr->(+8)->+0xBC == id`
  - returns `ptr` (and marks `ptr+0x72=1`) or 0
- if split active -> calls `0x140A90E80(NetGameManager, index)` instead (net path)

So the REAL campaign local-player slot table is `0x143EEBE80` (16-byte
`{ptr,id}` pairs, index 0/1). `local-player-1 = 0` because
`0x143EEBE80 + 0x10` (slot 1) is unpopulated in campaign.

This supersedes the earlier `0x143FE1070 +0xE8/+0x104` id-slot model and the
CharacterManager model as the blocker: those are different registries; the value
that BOTH the campaign control resolver and (per the camera convergence) the
screen route consume is this `0x143EEBE80` pair table.

Direct lever (to test):
- write Nadine's pair into slot 1:
  - `0x143EEBE80 + 0x10 + 0 = Nadine+0xA8` (ptr, e.g. 0x10E020FAE8)
  - `0x143EEBE80 + 0x10 + 8 = Nadine+0xBC` (id, e.g. 0x6BF)
- then `GetLocalPlayer(1)` returns Nadine in campaign without touching the
  net/split path.
- runtime-verify slots 0/1 first (slot 0 should already hold Chloe's pair).

Caveat to verify: confirm `0x143EEBE80` is the table the SCREEN-1 camera route
also reads (camera route used `route+0x124` index through the same
`0x143FE1070->+0xA8`); if so, populating slot 1 is the single unifying lever for
both control and camera. If the camera route uses the split-active branch, it may
need the split gate too.

### CONVERGENCE: Reader (this line) + Writer (Codex line) Both Center On `g_RetainedPlayerSlotTable 0x143EEBE80`

The two parallel investigations meet exactly here:

- READER: `0x1410F0F60 ResolveRetainedPlayerHandleBySlot` (campaign branch) reads
  `0x143EEBE80 + index*0x10` {ptr,id}, validated by `ptr->(+8)->+0xBC==id`. The
  top resolver vfunc `0x140298D70` funnels campaign into this reader.
- WRITER: `0x1410F14C0 CommitResolvedPlayerSpawn` is the central retained
  player-spawn commit path; it builds a ProcessSpawnInfo packet (vtable
  0x14291CF98) tagged `Player` / `*player-health-settings*`, materializes a
  runtime object via `0x1417C3790`, harvests its `+0xA8/+0xBC` retained-live
  pair, and stores it into `0x143EEBE80`.

So `g_RetainedPlayerSlotTable 0x143EEBE80` is the single shared registry behind
the local-player resolution (and, per the earlier camera convergence, the
screen-route actor too). Slot 0 = Chloe; slot 1 unpopulated in campaign.

Two candidate levers, increasing fidelity:
1. DIRECT (fast test): write Nadine's pair into slot 1 at runtime
   - `0x143EEBE80 + 0x10 + 0 = Nadine+0xA8` (ptr)
   - `0x143EEBE80 + 0x10 + 8 = Nadine+0xBC` (id)
   then check whether GetLocalPlayer(1) / screen-1 route / control wake up.
2. PROPER: drive `0x1410F14C0 CommitResolvedPlayerSpawn` for a second player so
   the slot is populated through the real spawn path.

Recommended order: do the DIRECT slot-1 pair write first (cheap, reversible);
if consumers need full spawn init, escalate to CommitResolvedPlayerSpawn.

### Runtime Result: Raw Slot-1 Pair Write Crashes — Confirms Slot Table Is A Live Gate, Needs Full Spawn Init

DIRECT test executed: wrote Nadine's pair into `g_RetainedPlayerSlotTable` slot 1
- `0x143EEBE90 (slot1.ptr) = 0x10E020FAE8` (Nadine+0xA8)
- `0x143EEBE98 (slot1.id)  = 0x6C0`        (Nadine+0xBC)
Verified table: slot0 {0x10E0201128, 0x69F}=Chloe, slot1 {0x10E020FAE8, 0x6C0}=Nadine.

Result: the game CRASHED shortly after.

Interpretation:
- consumers DO read slot 1 (the write was not inert) — confirms `0x143EEBE80` is
  a real live gate for the second local player;
- but a raw {ptr,id} pair is insufficient: downstream consumers expect a fully
  initialized second-player runtime (the state that `CommitResolvedPlayerSpawn`
  builds: ProcessSpawnInfo packet, materialized runtime object, associated
  player/camera/control substructures), not just a validated handle pair;
- so the correct lever is to drive the real writer
  `0x1410F14C0 CommitResolvedPlayerSpawn` for a second player, OR to understand
  exactly which extra per-slot substructures the consumers dereference and
  ensure they exist for Nadine before publishing the pair.

Decision: pause runtime poking; switch to STATIC analysis of
`0x1410F14C0 CommitResolvedPlayerSpawn` to learn the full slot-1 init contract.

### CommitResolvedPlayerSpawn Contract: The Slot Pair Is Harvested From A Materialized Player-Runtime Object (`0x1417C3790`)

Static decode of `0x1410F14C0 CommitResolvedPlayerSpawn` end-to-end:

Args (Win64): `rcx, edx, r8, r9, [stack...]`. Internally:
- `r12 = r8`  -> the SLOT INDEX used for the table store
- `rdi = rcx`, `r15 = r9`, `edx` spilled to `[rsp+0x30]`

Build sequence before the store:
- `0x1417C32A0 InitializeSelectorBackedProcessSpawnPacket` (ProcessSpawnInfo,
  vtable `0x14291CF98`, tagged with strings `Player` `0x1428D8960`,
  `*player-...*` `0x14292B6A8/0x14292B6C8`)
- `0x1417651D0` record resolve/create (second packet layer + fallback)
- `0x1417C3790` MATERIALIZES the runtime player object (returns `obj` in rax)

Store into the slot table (`0x1410F17A4..0x1410F17E9`):
- if `obj != 0`: `pair = { obj+0xA8, obj+0xBC }` else `{0,0}`
- `*(0x143EEBE80 + index*0x10) = pair`  (index = r12)
- also mirrors the pair into the caller out-struct at `rdi+0/+8`

Conclusion on the crash:
- the slot pair format I wrote manually ({Nadine+0xA8, Nadine+0xBC}) is exactly
  the format the reader validates, so the crash was NOT a malformed pair;
- the crash is because slot 1 had no MATERIALIZED player-runtime object
  (`0x1417C3790` output) and its associated per-player substructures that
  consumers dereference. Raw pair publish skips all of that.

So the real second-player init contract = run the full
`CommitResolvedPlayerSpawn` build (packet -> record -> materialize 0x1417C3790)
for index 1, not a raw slot poke.

Next static step:
- decode `0x1417C3790` (what player-runtime object it materializes and from what
  spawn-info), and the 4 callers of CommitResolvedPlayerSpawn
  (`0x1410F1A2B/0x1410F2123/0x1410F2DE1/0x1410F3541`) to see how the slot index
  (r8) and spawn-info are chosen — i.e. how slot 0 (Chloe) is normally spawned
  and what a slot-1 invocation would require.

### Full Second-Player Spawn Pipeline Mapped (parameterized by player index)

The retained-player slot table is populated by a real, index-parameterized
spawn pipeline:

`ScriptSpawnPlayer 0x140F73910` (TaskManager::SpawnPlayer; reads/validates slot
table `0x143EEBE80`, builds spawn descriptor, passes player index in `edx=esi`)
  -> `SpawnPlayerFromRetainedDescriptor 0x1410F1910` (or `SpawnPlayerFallback
     0x1410F3350`); builds Player-facing packet (`*player-starting-weapons*`,
     `TrySpawnPlayerAtContinue`)
    -> `CommitResolvedPlayerSpawn 0x1410F14C0` (slot index = arg r8 -> r12)
      -> `ResolveOrMaterializeRuntimeStateObjectFromSelectorPacket 0x1417C3790`
         (materializes the player-runtime object)
      -> store `{obj+0xA8, obj+0xBC}` into `0x143EEBE80 + index*0x10`

Key implication:
- the pipeline is INDEX-PARAMETERIZED (player/slot index flows
  ScriptSpawnPlayer.esi -> ... -> CommitResolvedPlayerSpawn.r8 -> table index),
  so the engine structurally supports spawning a player into slot 1;
- the earlier raw slot-1 pair write crashed precisely because it skipped this
  whole pipeline (no materialized runtime object / packet / per-player state);
- therefore the correct second-player lever is to drive this spawn pipeline with
  index=1 and a valid spawn descriptor, NOT to poke the slot table.

Upper entry callers of the spawn path also include the retained-player camera
message band (`0x1413C076A` in RunSecondaryRetainedPlayerSpawnAndNotifyOuterOwner240,
`0x1413BAC98`), tying spawn to the retained/camera owner family.

Next static step:
- decode how `ScriptSpawnPlayer` derives the player index `esi` and who calls
  ScriptSpawnPlayer (script command / TaskManager entry), to find the point
  where index=1 (second player) could be requested with a valid descriptor;
- and what minimal spawn descriptor a second player needs (weapons/look/transform)
  so CommitResolvedPlayerSpawn(index=1) materializes Nadine safely.

### GRAND SYNTHESIS: Spawn + Local-Player + Camera All Hang Off The Two-Screen Route Table `0x14697AD50`; Split-Screen Route 1 Is The Root Gate

Decoding how the spawn pipeline chooses its slot index closed the loop across
every line of this investigation.

`ScriptSpawnPlayer 0x140F73910` (script command) hardcodes the primary path
index 0 (`xor esi,esi`); `RunSecondaryRetainedPlayerSpawn 0x1413C04A0` also
passes edx=0. The actual per-spawn slot index is NOT a plain player number — it
is derived inside `SpawnPlayerFromRetainedDescriptor` via
`0x141C077B0(out, index)`.

`0x141C077B0` (GetCameraRouteEntityByIndex; assert `index <= 1`, camera.cpp:0x19D):
- reads the two-entry CAMERA ROUTE TABLE `0x14697AD50 + index*0x10` {ptr,id}
- validates the route entity (`ptr->(+8)->+0xBC==id`, bytes `+0x55/+0x56`, and a
  bit test via selector `0x14697AE34`)
- returns the pair/payload from `route_entity + 0x60`

So the slot index used by the spawn pipeline IS the camera screen-route index.

This unifies everything found so far:
- camera route table `0x14697AD50` (route 0/1; = CameraManager(0)/(1))
- local-player slot table `0x143EEBE80` (slot 0/1; written by
  CommitResolvedPlayerSpawn, read by ResolveRetainedPlayerHandleBySlot)
- spawn pipeline (ScriptSpawnPlayer -> SpawnPlayerFromRetainedDescriptor ->
  CommitResolvedPlayerSpawn) keyed by route index
- the control resolver `(*(0x143FE1070))->+0xA8` -> campaign branch reads the
  same slot table

All of them are index-parameterized over the SAME two-screen route, and route 1
is inactive in ordinary campaign. That is why:
- local-player-1 = 0,
- screen-1 camera produced only artifacts/blink,
- raw slot-1 pair write crashed (route 1 / its entity were never stood up).

Root gate (single): the two-screen route / split-screen activation. Route entity
1 in `0x14697AD50` must be a valid, stood-up route (the split-screen path we
already exercised via `net-login-split-screen 0x140C02220`, CameraManager(1)
live, `IsHorizontalSplitscreenMode`, etc.). Once route 1 is a valid route entity,
the spawn pipeline can target index 1 and populate slot 1 properly, and the
local-player + camera consumers resolve Nadine without a raw poke.

Reframed plan (converged):
- the second player is NOT primarily a CharacterManager / slot-table / spawn
  problem in isolation; those are all consumers of route index 1;
- the primary lever is standing up a VALID screen-route entity 1 in
  `0x14697AD50` (split-screen route), then letting the existing spawn pipeline
  fill slot 1 for that route;
- next: decode what makes a route entity in `0x14697AD50` "valid" (the
  `+0x55/+0x56` bytes and the `0x14697AE34` selector bit), and which function
  constructs/stands up route entity 1 during split-screen — that constructor is
  the true second-player entry point.

### CORRECTION: Both Route Entities (0 AND 1) Are Built At CameraManager Init — Blocker Is Route-1 "active" Gate, Not A Missing Entity

`0x141C04F70` is `CameraManager::Init` (strings `CameraManager`,
`CameraManager::Co...`, `A Device...`). It builds BOTH route entities in a loop
`ebx = 0..1` (`cmp ebx,2; jb`):
- per entry: hash "CameraManager", init packet `0x1417C32A0`, materialize via
  `0x1417C3790`, harvest `{obj+0xA8, obj+0xBC}`, store into
  `0x14697AD50 + ebx*0x10`.

So the two-entry route table `0x14697AD50` is FULLY populated at init — route
entity 1 already exists. This matches the earlier runtime read where BOTH slots
were live: slot0 {0x10E0200160, 0x19}, slot1 {0x10E0200188, 0x1A}.

This corrects the prior "route 1 not stood up" claim:
- route entity 1 EXISTS;
- what differs is the per-route ACTIVE/VALID gate that `0x141C077B0`
  (ResolveScreenRoutePoseSeedOrDefault) and the local-player slot path require:
  - `route->(+8)->+0xBC == id` (identity ok by construction)
  - `route->+0xB0` child present, with bytes `route_child+0x55 != 0` and
    `+0x56 != 0`
  - a bit test via selector `0x14697AE34` in the child's bitset
  When those gates are not set for route 1, the pose-seed/active path falls back
  to default and the spawn/local-player slot 1 stays unfed.

So the real second-player gate is narrower and more precise:
- not "create route 1" (already created),
- not "register a character" (Nadine already a CharacterManager char),
- but "mark route 1 ACTIVE": populate `route1->+0xB0` child + set its
  `+0x55/+0x56` bytes and the `0x14697AE34` selector bit, which is what
  split-screen activation is supposed to do for the second screen.

Next step:
- runtime-inspect route1 entity `0x10E0200188`: its `+0xB0` child, `+0x55/+0x56`
  bytes, and compare to route0 `0x10E0200160` (which IS active);
- find the function that sets route_child `+0x55/+0x56` and the `0x14697AE34`
  selector bit (the route-activation path), and whether split-screen login drives
  it.

### CORRECTION 2: Route 0 vs Route 1 Gate Bytes Are IDENTICAL — "Route-1 inactive" Hypothesis Not Supported; Blocker Is The Unfilled Slot, Filled Only By A Route-1 Spawn

Runtime diff of the two route entities in `0x14697AD50`:
- route0: entry {0x10E0200160, 0x19} -> route obj 0x10E0232880 -> +0xB0 child
  0x10E0236970; child+0x50 qword = 0x7F01010000000006 (so +0x55=1, +0x56=1)
- route1: entry {0x10E0200188, 0x1A} -> route obj 0x10E0236A00 -> +0xB0 child
  0x10E023AAF0; child+0x50 qword = 0x7F01010000000006 (so +0x55=1, +0x56=1)

The route-1 child gate bytes are IDENTICAL to route-0. So route 1 is not
"inactive" by those bytes — both route entities look structurally valid/active.
(The hand-decoded selector-bit `0x14697AE34` check was inconclusive: both
children's +8 pointed at the same static global, indicating my manual layout walk
for that last check is unreliable; do not trust it.)

Honest corrected model:
- route entity 1 exists AND looks as valid as route 0 at the route-table level;
- the concrete missing thing remains the LOCAL-PLAYER SLOT `0x143EEBE80[1]`
  (still empty) which is ONLY written by the spawn pipeline
  `CommitResolvedPlayerSpawn` when a spawn runs for route index 1;
- in campaign no spawn runs for route index 1 because the only spawn entry
  (`ScriptSpawnPlayer`) hardcodes route index 0.

So the true lever is: invoke the spawn pipeline for ROUTE INDEX 1 with a valid
descriptor. The cleanest known seam:
- `SpawnPlayerFromRetainedDescriptor 0x1410F1910` takes the route index as an
  arg (passed as edx by ScriptSpawnPlayer = 0); calling it with index=1 and a
  Chloe-like descriptor would drive CommitResolvedPlayerSpawn for slot 1 and
  materialize a real runtime object (avoiding the earlier raw-pair crash).

Caveat: needs the correct descriptor/context args (a1,a3,a4,a6) that
ScriptSpawnPlayer builds; next static step is to capture exactly what
ScriptSpawnPlayer passes into 0x1410F1910 so the same shape can be reissued with
index=1.

### Exact Call Shape: ScriptSpawnPlayer -> SpawnPlayerFromRetainedDescriptor (0x140F73C34)

Captured the dispatch:
- `rcx = [rsp+0x30]` : local spawn DESCRIPTOR block (built just above by a 0x70+
  byte vmovups copy loop from a source struct, plus per-field fixups; ProcessSpawnInfo-like)
- `rdx = esi`        : ROUTE INDEX (= 0 in the primary path; xor esi,esi earlier)
- `r8  = r14`        : spawner/player context object (selected at func start via
  `r14 = [rdx+rcx*8]` from a spawner table)
- `[rsp+0x28] = rbp-0x80` : secondary out/context pointer
- `[rsp+0x20] = dword`    : flag set earlier

So `SpawnPlayerFromRetainedDescriptor(rcx=descriptor, rdx=routeIndex, r8=context,
stack: outptr, flag)`. The route index is literally arg2 (`rdx`).

Concrete second-player experiment plan (in-process, captures real args; avoids
the earlier raw-pair crash by going through the real spawn path):
1. hook `SpawnPlayerFromRetainedDescriptor 0x1410F1910`; on the normal Chloe
   spawn, log rcx (descriptor), rdx (index, expect 0), r8 (context),
   [rsp+0x28], [rsp+0x20].
2. snapshot/copy that descriptor + context.
3. re-issue `SpawnPlayerFromRetainedDescriptor(copy_descriptor, 1, context, ...)`
   to drive a real spawn into route/slot index 1 (materializes a runtime object,
   fills 0x143EEBE80[1] via CommitResolvedPlayerSpawn).
4. observe: GetLocalPlayer(1) non-null, screen-1 camera + local-player-1 control
   wake, ideally no crash since the runtime object is properly materialized.

This is the cleanest path to a real second player and is the recommended next
build/runtime step.

### Spawn-Arg Capture Succeeded (HW breakpoint, no code patch)

Injected the spawn-capture hook (DR0 hardware breakpoint on
`SpawnPlayerFromRetainedDescriptor 0x1410F1910` via VEH, armed on all threads;
no code modification). Triggered a location load. Captured a real Chloe spawn:

`SPAWN captured: desc=0x672C6E0 idx=0 ctx=0x11484F49B8 a4=0x672C6F0 flag=0x0 out=0x672C6F0`

- idx=0 confirms primary route index 0 (Chloe), as predicted (ScriptSpawnPlayer
  hardcodes 0).
- descriptor content (0x200 bytes from rcx) copied into the DLL buffer; ctx
  (r8), a4 (r9), flag ([rsp+0x28]=0), out ([rsp+0x30]) saved.
- desc/out are low/stack-ish addresses (0x672C6E0/0x672C6F0), confirming the
  descriptor is a caller-local block — copying it at capture time was required.

Caveat observed: black screen during/after the location load while the HW
breakpoint stayed armed. Likely the persistent breakpoint + VEH overhead (or
all-thread DR0) slows spawns during load. Mitigation for next build: disarm the
breakpoint immediately after the first capture (clear DR7 on all threads) so it
does not keep firing.

Next: add reissue — call
`SpawnPlayerFromRetainedDescriptor(g_spawn_desc, 1, g_spawn_ctx, out, flag)` to
drive a real spawn into route/slot index 1, and disarm the breakpoint after
capture to avoid the load-time stall.

### Fix: HW Breakpoint Hang Root Cause = Missing Resume Flag; Added Auto-Disarm + Reissue

The black-screen/hang after the location load was caused by the exec hardware
breakpoint re-trapping the SAME instruction in an infinite loop: on
EXCEPTION_CONTINUE_EXECUTION the Resume Flag (EFlags bit 0x10000) must be set,
otherwise the breakpoint immediately re-fires. Fixed:
- VEH now sets `ContextRecord->EFlags |= 0x10000` before continue.
- After the first capture it also disarms on the current thread
  (`Dr0=0; Dr7 &= ~1`) so it stops firing during load.
- Added `reissue_spawn_for_slot1()` (hotkey F12): calls
  `SpawnPlayerFromRetainedDescriptor(g_spawn_desc, 1, g_spawn_ctx, g_spawn_a4,
  g_spawn_flag, g_spawn_out)` to drive a real spawn into route/slot index 1.

Test sequence: inject -> trigger one checkpoint/respawn (capture, no hang now)
-> press F12 (reissue with idx=1) -> check GetLocalPlayer(1)/slot table
0x143EEBE80[1], screen-1 camera, control; watch for crash.
Risk note: g_spawn_desc is a copied stack block; if the descriptor embeds
pointers to other caller-local stack objects, reissue may use stale pointers.

### MAJOR BREAKTHROUGH: Reissuing The Spawn Pipeline With index=1 Creates A REAL Second Playable Local Player In Campaign

Executed the planned experiment and it WORKED:
- captured a real Chloe spawn (`SPAWN captured idx=0 ctx=0x11484F49B8 ...`)
- pressed F12 -> `reissue spawn idx=1` via
  `SpawnPlayerFromRetainedDescriptor(g_spawn_desc, 1, g_spawn_ctx, g_spawn_a4,
   g_spawn_flag, g_spawn_out)`
- `reissue spawn idx=1 returned` (no crash)

In-game result (user observed): Chloe "cloned" — a second Chloe appeared; one
stays in place, the other is PLAYER-CONTROLLED. That is a genuine second local
player (route/slot index 1) running in single-player campaign.

This validates the entire converged model:
- second player = drive the existing spawn pipeline for route/slot index 1
  (NOT raw slot pokes, NOT multiplayer net, NOT CharacterManager re-registration);
- `SpawnPlayerFromRetainedDescriptor(desc, 1, ctx, ...)` materializes a real
  player runtime object and fills `0x143EEBE80[1]`, and the engine's native
  local-2-player path then makes it controllable.

Significance: this is the first time a real, controllable second player exists in
campaign. The remaining work is now well-shaped and incremental:
1. make the second player's look/character = Nadine instead of a Chloe clone
   (swap the descriptor's character/look fields, or post-spawn retarget);
2. give the second player independent camera (screen-1 camera route should now
   have a valid slot-1 actor to follow);
3. route a separate input source to the second player (PC2);
4. stability: the spawn descriptor is a copied stack block — verify robustness,
   ideally build a clean descriptor instead of reusing the captured copy.

Repro: inject hook -> trigger one checkpoint/respawn (auto-captures, no hang) ->
press F12 -> second controllable player appears.

### Next Experiment Plan: Combine Spawn(idx=1) + Split-Screen Activation For Player-2 Camera

Now that a valid slot-1 actor exists (spawn idx=1 works), retry split-screen
camera — earlier F7 only produced artifacts because slot 1 had no actor.

Combined runtime test (no code change; hotkeys already present):
1. inject hook; trigger one checkpoint/respawn (auto-capture, no hang).
2. press F12 -> spawn second player into route/slot 1 (controllable clone).
3. press F7 -> split-screen login/activation (net-login-split-screen +
   one-shot split render mode) — now with a real slot-1 actor for screen route 1.
4. observe: does a proper second viewport render following player 2 (vs the
   earlier blink/artifacts)?

If the second view follows player 2, that is the "own screen" half for two-PC
(then Steam Remote Play Together / transport handles delivery). If it still
artifacts, decode how screen-route 1 selects its follow actor now that slot 1 is
populated, and which extra render/route bind is missing.

### Confirmed: Player 2 (slot 1) Is A Full `Player` Class, Slot Table Stably Populated

After spawn idx=1, runtime read of the slot table and player objects:
- slot table `0x143EEBE80`: slot0 {0x10E02129A0, 0x1117}, slot1 {0x10E02102B8,
  0x1310} — BOTH populated (slot1 stays filled; spawn idx=1 is durable).
- slot0 -> player obj 0x10E1065680, vtable RVA 0x2A4BFE8, name "Player"
- slot1 -> player obj 0x10E1406700, vtable RVA 0x2A4BFE8, name "Player"

So player 2 is a real full `Player`-class actor (full player moveset/control),
not a NadineBuddy/NPC. Functionally this is the stronger base for co-op than
elevating the buddy. The "Chloe clone" look is just the look/character on a real
Player; making it Nadine is a look/character-def swap, secondary to gameplay.

Split viewport status: F7 (split login + render mode=2) after spawn did NOT
produce a visible second viewport (Nadine merely entered buddy-follow on the
mode change); cam0/cam1 route entities unchanged. So a real second on-screen view
is still a separate task from having a second playable Player.

Updated remaining tracks (priority for two-PC):
1. Second on-screen viewport for player 2 (split render / second camera actually
   rendering) — the real "own screen" half; current blocker.
2. Independent input to player 2 (PC2).
3. (cosmetic) make player 2 look = Nadine.

### Variant A Phase 1 MVP Implemented: Player-State Replication Over UDP

Per the approved plan (variant A, host-authoritative two-instance co-op), Phase 1
"players only" state replication is implemented in the hook:

- `coop_protocol.h`: added `StatePacket` (pos[4], quat[4], velocity[3],
  anim_state, seq/ts) + `kStatePort=31338`, magic "COPS".
- `teleport_hook.cpp`:
  - `retained_player_object(index)`: resolves the live `Player` from
    `g_RetainedPlayerSlotTable 0x143EEBE80` slot index (entry.ptr -> +8).
  - `start_state_net()`: UDP recv socket bound to 31338 + send socket; peer IP
    read from `reports/coop_peer.txt` (default 127.0.0.1); recv thread stores
    latest `StatePacket`.
  - `send_local_player_state()`: reads LOCAL player (slot 0) world transform
    `+0x240` + quat `+0x140`, sends to peer.
  - `apply_peer_state_to_puppet()`: writes peer pos/quat onto the REMOTE puppet
    (slot 1 Player, the idx=1 spawn) at `+0x240/+0x130/+0x140` (direct-write MVP).
  - `net_tick()` runs each game-thread `hooked_update`; `start_state_net()` runs
    at install.

Test setup (two instances):
1. Put the peer's LAN IP in `reports/coop_peer.txt` on each machine (or
   127.0.0.1 for loopback if two instances on one box are possible).
2. Both load the SAME checkpoint; inject the DLL into both.
3. On each, run the capture (checkpoint/respawn) + F12 reissue to spawn the
   remote puppet (slot 1).
4. Move on one machine; the other machine's puppet (slot 1 Player) should mirror
   position/orientation.

Expected first issues to iterate on:
- direct-write transform may be overwritten by the puppet's own sim -> switch
  apply to native teleport vfunc (slot 0xC48) or disable puppet AI/sim;
- jitter -> add interpolation;
- two instances on one PC may be blocked by Steam (use two machines / LAN).

### Variant A Phase 1: Position Replication WORKS (loopback self-test)

Single-PC loopback test (peer=127.0.0.1, +2m offset): injected hook, spawned
puppet (F12), moved Chloe. Result: the puppet mirrors Chloe's position/heading
in real time (slides alongside, +2m). So the netcode pipeline
(send local transform -> UDP -> recv -> apply to puppet) is VALID end-to-end.

Known gap (expected): the puppet has NO locomotion animation — it "slides/gets
dragged" because we hard-write its transform each tick; the engine never plays
run/roll/etc. on it. Position fidelity is there; animation fidelity is not.

This is exactly the Phase 1 success criterion (see each other move) and cleanly
exposes the next increment.

### Phase 2 direction: animation fidelity on the puppet

Two viable approaches:
1. Drive the puppet via MOVE INTENT / locomotion instead of teleport: feed a
   target/velocity so the engine plays the proper run/turn animations itself
   (reuse the NpcMoveTo/locomotion path already proven for the buddy, or feed
   the player locomotion controller at +0x15B8). Smoothest, engine-authentic
   anims, but the puppet is a Player not an NPC, so the control-feed must match
   Player locomotion.
2. Replicate a coarse anim/state id + velocity in StatePacket and set the
   puppet's anim/locomotion state from it; keep transform authoritative for
   position. More explicit, needs the anim-state field mapping.

Recommended: approach 1 (move-intent driven) — let the engine animate from
motion, only correct position drift periodically. Velocity is already in
StatePacket (currently unused) to support this.

### Input Layer Mapped: Campaign Uses A Single-Active-Pad Front-End (2026-06-16)

Goal of this pass: find how to give the 2nd player (slot-1 puppet) movement
input, after confirming a virtual XInput controller (idx=1) works but its stick
drives the SINGLE existing player, not a 2nd one.

XInput hooks (teleport_hook.cpp) established:
- `XInputGetState` IAT slot RVA `0x25D6D60` (ord 2), `XInputGetCapabilities`
  `0x25D6D68` (ord 4), `XInputSetState` `0x25D6D58` (ord 3). Hooking GetState +
  GetCapabilities for idx=1 makes a virtual pad the engine accepts.
- Pad poll function `0x141A9A790(manager, inputStateOut)`; thin per-pad wrapper
  `0x141A99F40`. Each pad object: index@+0x0, connected@+0x4, caps@+0xC,
  `XINPUT_STATE`@+0x20 (so sThumbLX@+0x28, sThumbLY@+0x2A).
- Stick normalizer `0x141A99FD0(pad, dst)`: reads pad+0x28/+0x2A (int16),
  /32768, clamps, writes normalized LX/LY to `dst+0x10/+0x14` (RX/RY follow).

Input-manager object (vtable base `0x142B37E50`, ctor at `0x14179C495`, size
`0x70`, heap-allocated via `0x1415A0E90(0x70)`):
- +0x0 vtable; +0x8..+0x20 = FOUR pad-object pointers (controller slots 0..3);
- +0x28 = ACTIVE pad pointer; +0x58 = round-robin scan counter (cycles 0..3).
- Poll logic: every 4th frame (counter==0) it polls GetCapabilities for all 4
  slots (idx 0..3 observed; idx0=connected, idx1/2/3=1167), and only calls
  GetState for connected ones; then scans connected pads and the FIRST one with
  input activity (`0x14179C170` = "any axis non-zero" predicate) becomes the
  active pad (+0x28), feeding THE one player.

CONCLUSION: the campaign input front-end is **single-player / single-active-pad**
("any controller controls the game"). It does NOT route pad-index -> player-
index. That is why a virtual idx=1 pad just becomes the active pad and moves
Chloe (player 0). A true 2nd controllable player needs the engine's separate
2-local-player input routing, which campaign does not invoke (split-login set
max_local_players=2 + spawning the slot-1 Player did NOT make idx=1 feed a 2nd
player). Net: input injection is proven to work; the remaining blocker is the
2-player input ROUTING, a separate uninvoked system.

### Input Dispatch Pipeline Mapped (2026-06-16) — toward redirecting control

Goal: redirect player control from Chloe to our spawned Player clone (camera
already follows it via the retained-slot swap). Traced the input pipeline:

- Stick normalizer `0x141A99FD0(pad, dst)` writes the normalized left stick to
  `dst+0x10/+0x14`. `dst` = a MODULE GLOBAL input-state at RVA `0x66E4220`
  (.data), captured at runtime (exec bp on the normalizer, hotkey numpad0).
- Refs to that global (found via scripts/find_global_refs.py = per-.pdata-
  function decode; linear sweep is unreliable): functions `0x14179C775`,
  `0x14179E310`, `0x14179E6C0`.
- `0x14179E310` is the INPUT DISPATCH loop: r14 = `*(0x143FE1088)`, r15 = input-
  state global. It iterates the consumer array `0x146B2C620` (count
  `0x146B1B55C`); for each consumer: `rcx=consumer; rdx=input-state;
  call [*(consumer)+0x30]`. The FIRST consumer whose +0x30 returns true claims
  focus (stored at `0x146B2C618`) and the loop breaks. So input is FOCUS-based:
  input-state -> first consumer that handles it -> drives the focused player.
- => "control" is not a direct player pointer; it's a focus consumer object
  (vtable+0x30 = try-handle-input) that holds/drives the controlled player.
  Current focused consumer = `*(0x146B2C618)`.

NEXT to redirect control to the clone: identify the gameplay consumer (RTTI of
`*(0x146B2C618)`) and the player reference inside it, then repoint that to the
clone; OR change which consumer/player has focus. Reverse tooling added:
scripts/find_global_refs.py (per-function rip-rel ref finder),
scripts/rtti_name_by_vtable.py. Component map of the Player object: +0xb0
PlayerSnapshot (Nadine = NpcSnapshot), +0x340 CharacterAttackHandler, +0x4a8
HealthSystem, +0x1b0 EffectControl, +0x330 RegionControl, +0x350
FoliageSfxController, +0xa0 ProcessHeap.

### Input chain deeper still (2026-06-16) — control ref not yet reached

Read-bp on the input-state stick global (g_module+0x66E4230, hotkey numpad0)
caught readers: rva 0x1A9A054 (input module, self) and rva 0x183B571. The latter
is inside a large virtual `0x14183B4DA` (size 0x1067, no direct callers = vtable
method): it reads the input-state global (r15) and writes a SENSITIVITY-SCALED
copy (xmm6 = +/-scale) element-by-element into another buffer (rsi). So input is
processed/copied through more stages before reaching the gameplay player control;
the read-bp keeps catching intermediate copy/scale stages, not the final
apply-to-player. The focus-consumer global 0x146B2C618 is null in gameplay (it's
the UI/overlay input stack, not gameplay control). The gameplay player-control's
player reference has NOT been reached after: input manager -> active pad ->
normalizer 0x141A99FD0 -> input-state global 0x66E4220 -> scale-copy virtual
0x14183B4DA -> (more). Net: redirecting control to the clone is a multi-layer
chain not yet cracked; the campaign input/control is deeply staged.

### BREAKTHROUGH: Fresh IDA DB + per-player input slots (2026-06-16)

Rebuilt a clean IDA database from the real exe: ida/tll_fresh.exe.i64 (the old
ida/tll.i64 is corrupt — Hex-Rays returned JUMPOUT stubs; the fresh one
decompiles cleanly). Tooling: scripts/ida_dump_pseudo_env.py (TLL_IDA_TARGETS/
TLL_IDA_OUTPUT), run on ida/tll_fresh.exe.i64.

Hex-Rays of the input dispatch sub_14179E310 (and sibling sub_14179E6C0(mgr))
reveals the MULTI-PLAYER input distribution:
- After the UI focus loop (consumer array qword_146B2C620, count 146B1B55C,
  vtable+0x30 try-handle, focus stored 146B2C618), there is a SECOND loop over
  6 slots (v5 = 0..5):
    for slot in 0..5:
      idx = *(slotstruct+0x18)
      inputblock = &qword_1466E4220[166 * idx]      // 6 input-state blocks, 166 qwords each
      (*manager+0x20)(manager, *manager, inputblock, slotstruct, slot)
  manager = *(0x143FE1088); slot structs at manager+0xE88 + slot*0xBF0 (same
  0xBF0 stride as the per-screen camera bands under 0x143FE1088).
- So per-player input EXISTS (6 slots). Slot 0 = active player (Chloe); slots
  1-5 idle in campaign. The "single active pad" front-end feeds slot 0's block.
- => path to a real player-2: activate slot 1 (bind a character + feed its input
  block), the engine's own native local-multiplayer distribution. Connects to
  g_maxLocalPlayers / split-login / our virtual XInput idx=1.
NEXT: decompile the per-slot apply (manager vtable+0x20) to see how a slot binds
its player, to activate slot 1.

### FrameState per-slot map + control-binding still buried (2026-06-16)

Fresh DB (ida/tll.i64, labeled) decompilation results:
- The per-player input manager *(0x143FE1088) is class **FrameState** (vtable RVA
  0x28FB968). Per-slot input apply = FrameState vtable+0x20 = **0x140289150**
  (FrameState_PerSlotInputApply). It processes per-slot camera heading (cos/sin)
  into the slot's input block and calls Input_ScaleCopyVirtual.
- Per-slot ENABLE gates: byte FrameState+0x70B8+slot and struct
  FrameState+0x6FC8+0x28*slot (set for slot 0, 0 for 1-5). Slot struct passed to
  the apply = FrameState+0xDB0+0xBF0*slot (the per-screen camera bands).
- Runtime scan of FrameState slot structs (FrameState+0xDB0+0xBF0*{0,1}) for the
  controlled player: NO hits for the player object, its retained handle
  (chloe+0xA8), its id (chloe+0xBC), or the retained-table slot0 handle. So the
  FrameState slots do NOT reference the controlled player — they are pure
  per-viewport camera/input state.
- IDA xrefs to the input-block global 0x1466E4220: only the input system
  (sub_14179C740, sub_14179D1E0, Input_FocusDispatch, Input_PerPlayerDispatch).
  The player reads its block via a passed/cached pointer, not the global.

CONCLUSION: the control->player binding is a buried passed-pointer/handle/index
chain; static xrefs + runtime memory scans did NOT pin it after many layers.
RECOMMENDED next: LIVE debug (x64dbg) — HW breakpoint on the controlled player's
velocity write (chloe+0x260) or move field, then read the CALL STACK to see the
control->player chain and the controlled-player reference directly.

### Velocity-write call stack = animation/IK, not control (2026-06-16)

DLL velwriter HW-bp on Chloe+0x260 now also logs the call stack (return RVAs in
.text). The chain (0x1bb857d kinematics -> ... -> 0x1406CD220 etc.) decompiles to
LegIk processing (LegIkModeFreeze/LegIkModeMoving). So the velocity write happens
during the ANIMATION/IK pass; its stack does NOT reach the input/control. Confirms
velocity is an animation-time output; the control->player binding is upstream and
not reachable from here. Combined with all prior negative results (FrameState
slots hold no player ref; input-block global only referenced by input system;
CharacterManager 0x143898630 is a 32-entry scene list, not the player resolver;
retained-slot swap moves camera but not control), the control->player binding
remains a buried passed-pointer chain. Reaching it efficiently needs live
call-stack inspection at the PLAYER's input-read / move-intent write (x64dbg HW
bp + stack), not the velocity/animation path.

### Move-intent is NOT a player field; it's the per-slot input block (2026-06-16)

velprobe extended to scan PlayerSnapshot (*(player+0xb0)): it holds only velocity
copies (PS+0x120 == player+0x260 velocity; PS+0x130 == horizontal "desired"
velocity, Y=0), magnitude ~run speed, direction = movement. No stick-magnitude
(~1) intent anywhere in the player / controller / PlayerSnapshot. => the player
does NOT cache the raw move-intent; it reads its per-slot input block
(0x1466E4220 + 166*localIndex) transiently each frame.

CONSOLIDATED control model (fully mapped):
- A player reads input from block[localIndex]. Active player = index 0 ->
  block[0], fed by the single-active-pad front-end. block[1..5] are never fed
  (no 2nd pad/player in campaign).
- FrameState (*(0x143FE1088), 6 slots) processes the blocks; per-slot ENABLE
  gates byte FrameState+0x70B8+slot and struct FrameState+0x6FC8+0x28*slot are
  set only for slot 0.

CONCRETE PATH to a controllable player-2 (the dormant native local-2-player):
  (1) set FrameState slot-1 gates; (2) feed input block[1] = 0x1466E4220+166
      (our virtual XInput idx=1, or write it directly); (3) bind the slot-1
      spawned Player to localIndex 1 so it reads block[1].
This replaces the earlier dead ends (CharacterManager swap, FrameState player-ref
scan, velocity/animation stack). Now tractable with ida/tll.i64 (fresh, labeled).

### BREAKTHROUGH: FrameState slot-1 gate activates a moving 2nd player (2026-06-16)

Setting the FrameState slot-1 enable byte (*(FrameState+0x70B8+1)=1, FrameState =
*(0x143FE1088)) ACTIVATED slot-1 input processing: the spawned slot-1 Player
(F12 clone) came alive and MOVES on its own (runs in a circle) without any input
from us. This is the first time the 2nd player is an ACTIVE, MOVING slot-1
player driven by the native per-slot path (not a teleported/NPC puppet).
The circling = slot-1's input block (0x1466E4220 + 1*0x530) holds uninitialized/
garbage state (spinning camera heading); we are not yet feeding it clean move
input. Hotkey: numpad + toggles g_drive_slot1 (sets the gate each tick + writes
arrow keys to block[1]+0x10/+0x14).
NEXT: find the correct MOVE field in the input block (probe block[0]=0x1466E4220
while Chloe moves for the stick-magnitude vector), then feed block[1] there to
steer the clone cleanly; and initialize slot-1 camera so it stops spinning.

### Slot-1 clone control: spin branch is a demo; sub_1402893D0 isn't Chloe's path (2026-06-16)

After the slot-1 gate breakthrough (clone moves), tried to steer it cleanly:
- The MOVING branch (FrameState+0x70B8+slot gate) computes heading = cos/sin of
  angle = (float)(*(int64)(FrameState+0xE0)) * const — FrameState+0xE0 is a
  GLOBAL frame counter (freezing it = BLACK SCREEN). So that branch is a default/
  demo rotating heading, not real per-player input. The clone circles because of
  this global angle.
- The other branch sub_1402893D0 (FrameState+0x6FC8+0x28*slot gate) IS a real-
  input copier (struct+0x14/+0x18 heading, +0x1C mag, +0x10 flags -> block), but:
  (a) feeding the struct at mgr+0x6FF0 produced block heading=0 (wrong address —
  decompiler showed `(int)_RBP+...` truncation; real struct addr differs), and
  (b) an exec-bp on sub_1402893D0 (0x2893D0) did NOT fire while Chloe moves =>
  Chloe's main movement input does NOT go through sub_1402893D0.
NET: slot-1 activation moves the clone (real milestone), but clean directional
control needs Chloe's ACTUAL move-input application function (not sub_1402893D0,
not the demo spin). Next: breakpoint where Chloe's per-frame move-heading is
computed from her stick (capture that function + its struct), then replicate for
slot 1. Hotkeys: numpad+ = slot-1 spin gate (clone circles), numpad0 = exec-bp
capture on sub_1402893D0.

### Runtime follow-up: input block writers and look-field probe (2026-06-19)

Current run produced two useful concrete results.

1. PlayerSnapshot look field:
   - `player+0xB0` points to a PlayerSnapshot-like object.
   - `PlayerSnapshot+0x140` holds a packed pair; the upper dword at
     `PlayerSnapshot+0x144` is the character/base hash.
   - slot 0 and slot 1 both originally held `chloe-base` (`0xB70B7EAF`) there.
   - Runtime patching `PlayerSnapshot+0x144` to `nadine-base` (`0x8A419C8E`)
     for both slots survived and did not crash:
     `snapshot+140=0x8a419c8e7fbbee02`.
   - Visual effect is still not proven. The Windows screenshot captured the
     desktop/terminal, not the game window. Treat this as a durable metadata
     patch, not yet a confirmed model swap.
   - `tll_slot_dump.exe` now prints this field and supports
     `--set-look <slot|all> <chloe|nadine|hex>` so future sessions do not need
     hardcoded live addresses.

2. Spawn descriptor look patch:
   - `reissue_spawn_for_slot1()` now hashes `chloe-base`, `coop-chloe`, and
     `nadine-base` and scans the captured 0x200-byte spawn descriptor before
     spawning slot 1.
   - Live result: `slot1 spawn look patch summary: patched=0 already_nadine=0`.
   - So the currently copied descriptor block does NOT contain the direct
     Chloe/Nadine look hashes. Player 2's visible Chloe clone is sourced either
     from adjacent context (`ctx/a4/out`), a pointed-to subobject, or a later
     materialized PlayerSnapshot/character definition.

Input-block writer breakpoints also resolved the writer side:

- `InputManager_NormalizeStick 0x141A99FD0(pad, dst)` writes the live normalized
  left stick to `dst+0x10/+0x14`; runtime hits:
  - `0x141A9A054` / `0x141A9A059` for `block[0]+0x10/+0x14`.
- Its helper `0x141A99CE0` writes the first camera-adjusted pair:
  - `0x141A99E58` / `0x141A99E5C` for `block[0]+0/+4`.
- Runtime examples while Chloe moved:
  - `block0[+0]=1.37688e-08`, `block0[+4]=-0.567903`
  - `block0[+10]=0`, `block0[+14]=-0.671407`

This confirms the concrete input-block fields. The current slot-1 experiment
still fails because the manual call to the per-slot apply function wipes the
prepared slot-1 block:

- before manual slot-1 apply: `[1.37688e-08,-0.567903,0,-0.671407]`
- after manual slot-1 apply:  `[0,0,0,0]`

Next implementation step: stop manually calling the per-slot apply function from
the hook, feed/copy the prepared block[1] only, and let the engine's own update
sample it. If movement still does not respond, the missing piece is the
local-index binding for the spawned slot-1 Player, not the stick values.

Follow-up static/runtime correction:

- The per-slot dispatch loop at `0x14179E310` always iterates all 6 FrameState
  slots. For each slot it chooses the input block through:
  `inputBlock = 0x1466E4220 + 0x530 * *(uint8_t*)(slotStruct+0x18)`.
- The relevant `slotStruct` is the large per-slot state at:
  `FrameState+0xE88 + slot*0xBF0`, not the small `FrameState+0x6FC8+0x28*slot`
  branch-control structure.
- Runtime read on the current run confirmed:
  - slot 0 large struct at `FrameState+0xE88`: byte `+0x18 == 0`
  - slot 1 large struct at `FrameState+0xE88+0xBF0`: byte `+0x18 == 1`
- Therefore slot 1 already selects `block[1]` correctly. The latest blocker was
  self-inflicted: the hook manually called `FrameState_PerSlotInputApply` with a
  mismatched argument shape, and that call zeroed the prepared slot-1 block.

Source change now in `teleport_hook.cpp` (requires rebuild + reinject before
runtime verification):

- `apply_slot1_perslot_on_game_thread()` is disabled by default behind
  `g_manual_slot1_apply` (toggle Numpad2 only for diagnostics).
- `drive_slot1_tick()` keeps `slot1Struct+0x18 = 1`, keeps the spin/demo gate
  off, and fills `block[1]`.
- After `reissue_spawn_for_slot1()` returns, the hook now enables `g_drive_slot1`
  for 4 seconds and feeds forward input into `block[1]`.

Next runtime test:

0. rebuild/reinject the DLL with the latest source change;
1. load a gameplay save;
2. wait for auto spawn capture + auto slot-1 spawn;
3. observe whether the clone moves forward cleanly for 4 seconds.

If yes, the clean per-slot input path is proven. If no, the next missing piece is
not the stick values or slot input index; it is the downstream binding between
the spawned slot-1 `Player` and the per-slot control consumer.

### Runtime confirm: look-hash patch + clean block[1] feed both insufficient (2026-06-19, Opus)

Tested Codex's pending changes live:
1. Clean block[1] feed (no manual per-slot apply): the slot-1 clone STILL does
   NOT move. So feeding the input block alone is not enough — the engine's own
   per-slot apply (real-input branch, branch1 gate=1) overwrites block[1] from
   the branch1 struct (FrameState+0x6FC8+0x28), which is empty (mirrored from
   slot0's unused branch0) => block[1] zeroed => clone stands. The spin path
   moved the clone only because it WROTE the block AND called
   Input_ScaleCopyVirtual to propagate to the player.
2. `tll_slot_dump.exe --set-look slot1 nadine`: snapshot+0x144 changed
   0xb70b7eaf(chloe-base) -> 0x8a419c8e(nadine-base) on the slot-1 Player, no
   crash, BUT the clone is VISUALLY STILL CHLOE. Confirmed: the look hash is
   load-time metadata; patching it post-spawn does not reload the model/skeleton.
   A Nadine model needs the correct character def at SPAWN time (descriptor does
   not carry the hash; it comes from ctx/a4/out or a pointed-to subobject).

STATE: two core pieces of "playable Nadine" remain blocked — (a) clean
directional control of the slot-1 Player (binding spawned-Player <-> per-slot
control consumer / feeding the real-input branch struct correctly), and (b) the
Nadine model (load-time). Slot-1 clone exists and can be made to move in a circle
(spin gate) but not cleanly controlled, and looks like Chloe.

### Slot-1 per-slot input structs are NOT cleanly isolated (2026-06-19, Opus)

Tried driving the slot-1 clone via the per-slot real-input branch struct
(branch1 = FrameState+0x6FC8+0x28*1), built clean (zeroed + gate byte + our
heading/mag, no branch0 mirror, no direct block feed; let the engine's
sub_1402893D0 propagate). RESULT: the CLONE stands, but writing branch1
drives CHLOE (the active player is "carried forward" by our auto-forward feed).
- Codex's branch0->branch1 mirror previously made the clone run/jump/open-map =
  it dragged Chloe's live control/button state into slot 1.
- So the +0x6FC8 per-slot "branch" structs do NOT map cleanly to slot index in
  the way assumed: writing branch1 affects the ACTIVE player, not the slot-1
  clone. The structures bleed into Chloe -> instability (chaos, Chloe death,
  Chloe auto-walk).
NET: the slot-1 clone can be made to MOVE (spin-gate demo) but cannot be cleanly
& independently controlled via these per-slot structs without destabilizing the
active player. Clean isolation of slot-1 input from slot-0 (Chloe) is the
unsolved crux. The clone also lacks the Nadine model (load-time streaming) and
collision. Recommend reassessing the clone route vs alternatives at this point.

### Spawn-time look-source probe: descriptor/nearby ctx do not carry Nadine base hash (2026-06-19, Codex)

Added bounded runtime instrumentation in `teleport_hook.cpp` at
`SpawnPlayerFromRetainedDescriptor 0x1410F1910`. On the first real Chloe spawn it
now scans:
- captured 0x200-byte descriptor copy;
- live descriptor;
- ctx/a4/out;
- the current stack slice;
- first-level pointers from those regions.

Live run after rebuild/reinject:

```
SPAWN captured: desc=0x511c570 idx=0 ctx=0x11484f49b8 a4=0x511c580 flag=0x0 out=0x511c580
SPAWN look-source probe hashes: chloe-base=0xb70b7eaf coop-chloe=0x9ffcfd6c nadine-base=0x8a419c8e ...
SPAWN look-source probe done: hits=16
```

Important result:
- no `chloe-base`, `coop-chloe`, or `nadine-base` hit in the descriptor, live
  descriptor, ctx, a4, out, or stack window;
- only generic `player` hash hits and text hits containing `nadine`;
- raw memory dump of those `nadine` text pointers showed level/script names
  like `ganesh-...-nadine...`, not a character/model selector.

Narrowed process scan:
- around ctx `0x11484f0000..0x1148502000`: no `chloe-base`, no `nadine-base`;
  only `player` hits;
- around live player heap `0x10e0000000..0x10e2000000`:
  - `chloe-base` appears only in player snapshot objects
    (`slot0 snapshot+0x144`, `slot1 snapshot+0x144`, plus one sibling snapshot);
  - `nadine-base` not present in that range.

Current implication:
- the small spawn descriptor and immediate ctx/a4/out arguments still do not
  carry the final player look/base id;
- post-spawn `PlayerSnapshot+0x144` is confirmed as metadata, but the visible
  model was already selected earlier;
- next useful frontier is the builder/materializer that creates or fills
  `player+0xB0` / `PlayerSnapshot`, especially the upstream path that writes
  `snapshot+0x140/0x144` during `CommitResolvedPlayerSpawn ->
  0x1417C3790` or the surrounding player construction path.

Follow-up live test:
- set slot0 `PlayerSnapshot+0x144` to `nadine-base` before manually reissuing
  `SpawnPlayerFromRetainedDescriptor(..., idx=1, ...)`;
- `F12` did call the reissue path and returned:
  `slot1 spawn look patch summary: patched=0 already_nadine=0`,
  `reissue spawn idx=1 returned`;
- immediately after that `tll.exe` was no longer running, so the process likely
  crashed after the reissue;
- no post-reissue slot dump was possible.

Conclusion: "patch slot0 snapshot before reissuing slot1 spawn" is not a clean
or proven route. Treat it as unsafe unless a future run adds tighter guards and
captures the crash site. The still-clean route is to find the actual
PlayerSnapshot / visual asset source during materialization rather than patching
a live snapshot ad hoc.

### PlayerSnapshot is downstream; visible character source is `player+0x190` (2026-06-19, Codex)

Static follow-up narrowed the model/look source past `PlayerSnapshot`:

- `PlayerSnapshot` type registration:
  - vtable/type area: `0x142A3CFA0`
  - registration thunk: `0x1400A9190`
  - factory stored at `0x143ED31D0+0x68 = 0x143ED3238`
  - concrete creator: `0x140FDD4D0`
  - constructor: `0x140FDD610`
- `0x140FDD610` only initializes an empty `PlayerSnapshot`; it zeroes
  `snapshot+0x140/+0x144`. It does not choose Chloe/Nadine.
- During player materialization, `0x1417C3790` writes `player+0xB0` from
  `Player::vtable+0x1A8`; that slot is the `PlayerSnapshot` factory path above.
- The first `PlayerSnapshot` virtual method `0x141BB3EC0` fills
  `snapshot+0x144` by calling `Player::vtable+0x300`.
- `Player::vtable+0x300 = 0x141BB8BC0`; it reads `player+0x190`, resolves that
  id through the upper actor/state registry, and returns the resolved hash to
  the caller.

Therefore `PlayerSnapshot+0x144` is downstream metadata. The earlier visible
character/model selection must be controlled by the Player upper visual state,
especially:

```
0x141BB4280 Player upper draw-control lifecycle
  0x141BB4A9A: rbx = r12+0x98 pair, or fallback from Player+0x238
  0x141BB4AFB: Player+0x738(pair)
  0x141BB4B03: player+0x170 = pair[0]
  0x141BB4B17: player+0x190 = pair[1]   <-- current spawn-as-Nadine seam
  0x141BB4B48: resolve player+0x190 through 0x1417CD080
  0x141BB4BC0: Player+0x6F0(resolved record)
  0x141BB4BC6: player+0x1A8 = result
```

Runtime hook added in `teleport_hook.cpp`:

- logs live NadineBuddy `+0x170/+0x190` once it appears;
- resolves NadineBuddy `+0x190` through `0x1417CD080`;
- around reissued slot-1 spawn, arms one-shot current-thread breakpoints at:
  - `0x141BB4B01` before `player+0x170 = pair[0]`
  - `0x141BB4B14` before `player+0x190 = pair[1]`
- if the Nadine visual pair is valid, skips the original source reads and feeds
  Nadine's `+0x170/+0x190` pair into the two following Player writes.

First runtime check with only `player+0x190` patched was partial: the user saw
only hair left on the clone. That is useful signal, not a dead end: mixing
Chloe's `+0x170` with Nadine's `+0x190` can produce a broken partial visual, so
the spawn-time override must patch the pair atomically.

Second runtime check after rebuilding/reinjecting paired override:

```
Nadine visual identity candidate: object=... +170=0xd27c074f +190=0xed25c708 resolved(+190)->0x5e0f6b9f valid=1
visual seed bp armed ... look=...4b01 state=...4b14 nadine_look=0xd27c074f nadine_state=0xed25c708 valid=1
VISUAL look seed hit ... src(+0/+4)=0x3f334972/0x6c2b2396 nadine=0xd27c074f/0xed25c708 active=1 valid=1 PATCHED
VISUAL state seed hit ... src(+0/+4)=0x3f334972/0x6c2b2396 nadine=0xd27c074f/0xed25c708 active=1 valid=1 PATCHED
reissue spawn idx=1 returned
```

So the pair override is proven to execute during slot-1 creation.

Visual result reported in-game: still only floating hair, no full Nadine body.
External slot dump confirmed that the second Player really did receive Nadine's
two patched visual ids:

```
slot1 object=0x10e13c7180
  +170 = 0xd27c074f   (Nadine look id)
  +190 = 0xed25c708   (Nadine state id)
```

But nearby Player identity/model fields were still mixed:

```
  +174 = 0xcde4b742   (same "player" hash seen on Chloe)
  +180 = 0xde7baff5   (unchanged from player path)
  +1A8 = 0x11191f2f00 (graph/object produced from the mixed state)
  snapshot+144 = 0xc67a06f1, not nadine-base
```

Static follow-up explains why this can happen:

```
0x141BB4A49: mov eax, [player+0xD8]
0x141BB4A50: player+0x174 = eax
...
0x141BB4B03: player+0x170 = pair[0]
0x141BB4B17: player+0x190 = pair[1]
0x141BB4BC0: player->virtual +0x6F0(resolved +0x190 record)
0x141BB4BC6: player+0x1A8 = result
0x141BB4BEE: player->virtual +0x700(...)
```

Conclusion: `+0x170/+0x190` are necessary but not sufficient for a full character
swap. They are enough to pull Nadine hair, but the body/rig/clothing source still
comes from the surrounding Player identity/visual graph path, especially
`+0x174/+0x180/+0x1A8` and the `+0x6F0/+0x700` virtuals. Do not repeat the
paired `+0x170/+0x190` test as if it could produce full Nadine by itself.

Next useful test is to capture/compare the complete source packet consumed by
`0x141BB4280` and the outputs from `+0x6F0/+0x700`, then identify which field
selects the body/rig record in addition to the hair/look record.

### Visual swap: body mesh is streamed at spawn, NOT swappable post-spawn (2026-06-19, Opus)

Continued Codex's visual seam. Findings:
- player+0x170 = a RESOLVED look id, NOT a selector. Feeding hash("nadine")
  (0xc7e2539b, the selector) -> clone is plain Chloe (no effect). Feeding
  NadineBuddy's resolved +0x170 (0xd27c074f, Codex) -> Nadine HAIR only.
  So +0x170 swaps an attachable look item (hair), not the body.
- Player vtable+0x740 (0x141BCD340) = ResolvePlayerAmbientOccluder... = AMBIENT
  OCCLUDER (lighting) selection + draw-control mesh refresh, writes player+0x180
  (occluder id), refreshes draw-control at player+0x198. It is lighting, NOT the
  body mesh source.
- Identified hashes (CRC32 poly 0x04C11DB7 init 0): player+0x174 = hash("player")
  (class id, same on Chloe+clone), player+0x170 source pair[0] = hash("chloe")
  = 0x3f334972, clone snapshot+0x144 after patch = hash("nadine-dlc-base").
CONCLUSION: the BODY/RIG is the streamed character mesh, loaded at spawn/
materialize from the (Chloe) retained descriptor. Only attachable look items
(hair) are swappable via the Player draw-control ids. A full Nadine body requires
spawning from a Nadine character def, but campaign has no Nadine-as-Player
descriptor. So the clone's body is fundamentally Chloe; the visual swap is
blocked at the body-mesh streaming layer (confirmed by 2 agents: hair-only).

### BREAKTHROUGH: clean per-slot input injection into the slot-1 clone (2026-06-19, Opus)

Decompiled the per-slot apply (0x140289150) + real-input branch (0x1402893D0).
Two distinct slot gates in FrameState (*0x143FE1088):
- FrameState+0x70B8+slot  = DEMO-SPIN enable. Heading = global frame counter
  [FrameState+0xE0] -> cos/sin -> constant rotation = the "runs in a circle".
  (I had been setting this -> that WAS the circle.) DO NOT use for control.
- FrameState+0x6FC8+0x28*slot = per-slot SCRIPTED-INPUT injector (clean). Byte[0]
  = enable real-input branch. sub_1402893D0 maps this struct into slot's input
  block (0x1466E4220+0x530*slot):
    struct+0x14 -> block LEFT stick X (move)   (block+0x00 / +0x10)
    struct+0x18 -> block LEFT stick Y (move)   (block+0x04 / +0x14)
    struct+0x1C -> block+0x08 / +0x18 = RIGHT stick X (camera/turn)
    struct+0x20 -> block+0x0C / +0x1C = RIGHT stick Y (camera/turn)
    struct+0x10 (u32) -> block buttons qword (block+1304/+1288/+1296)
    struct+0x0C = duration(float), struct+0x24 = elapsed accumulator
RESULT (live log): feeding struct with leftstick=(0,1), flags=0x500 ->
block1 read back as sx=0 sy=1 btn=0x500 => the engine CONSUMED our input for
slot 1. The clone comes alive from OUR struct, NOT from Chloe. Two bugs fixed:
  (1) "stands" = struct+0x10 flags were 0; the spin path forces block-buttons
      |=0x500 ("input active"); the real branch writes them verbatim, so 0 =
      locomotion ignores a valid stick. Set 0x500.
  (2) "circle" = I put magnitude into struct+0x1C, which is the RIGHT stick
      (camera) -> constant turn. Zero the right stick; left stick carries the
      move magnitude. -> clone should walk STRAIGHT.
This is the clean slot-1 control surface (no Chloe bleed): we own only
FrameState+0x6FC8+0x28 (slot 1) and the engine's own per-slot apply does the rest.

### Input format NAILED; clone's sole blocker = per-slot camera basis (2026-06-20)

Drove Chloe (slot 0, HAS camera) via the per-slot scripted-input struct with the
right-stick zeroed -> Chloe walks STRAIGHT. So clean per-slot input injection is
fully proven on a real camera'd player. Field semantics of the slot input block
(0x1466E4220+0x530*slot), written by sub_1402893D0 from struct+0x14/18/1C/20:
  block+0x00/+0x04 = LEFT stick = camera-RELATIVE move vector
  block+0x08/+0x0c = RIGHT stick = camera YAW RATE (incremental; not a static basis)
  block buttons (block+1304) = struct+0x10 flags; 0x500 = "input active"
The earlier "circle" (on BOTH the clone AND Chloe) = nonzero right-stick spinning
the camera every frame while moving camera-relative. Zeroing it -> straight.
=> The clone (slot 1) STANDS with right-stick 0 because slot-1's camera basis is
NULL: a camera-relative stick through a null camera yields a zero world vector.
CONCLUSION: control plumbing is solved. The ONLY remaining blocker for a
natively-controllable clone is giving slot 1 a CAMERA BASIS (yaw). Camera follows
the retained-slot table; input slot-0 is bound to Chloe via a cached ref (the two
are decoupled). Next: test swap(cam->clone)+drive-slot1, and find where the
locomotion reads the per-local-player camera yaw so we can seed slot 1.

### Appearance/body swap — exhaustive RE (2026-06-20)

Goal: fully-playable Nadine = Chloe mechanics + Nadine body, replacing NPC Nadine.
The clone is already a full Player (control/abilities); the ONLY blocker is the
BODY/appearance. Findings:
- The co-op LOOK system exists: SelectRetainedCoopPlayerLookHash (0x140B580C0)
  returns a character look hash by index: 0=coop-drake, 1=coop-chloe,
  2=coop-sullivan (NO nadine). It's a registered callback (sub_14007D3A0) and
  returns 0 unless in a "coop game" (dword_143E07470 HIBYTE + byte_143E03568) -
  DORMANT in campaign.
- CommitResolvedPlayerSpawn (0x1410F14C0) is the central spawn; it's called ONLY
  by SpawnPlayerFromRetainedDescriptor (0x1410F1910, our clone path) and
  SpawnPlayerFallback (0x1410F3350). So all player spawns go through the retained
  descriptor; the co-op variant passes a co-op descriptor (look=drake/etc.).
- Nadine look asset names exist: nadine-base(0x8A419C8E), nadine-default(0xBA17B98B),
  nadine-dlc-base(0xC67A06F1) - but these are NPC art, not a coop player skin.
- TEST (LOOKSWAP): scanned the clone's captured spawn args (desc 0x200, ctx 0x800,
  a4 0x800) for chloe-base/coop-chloe/hash("chloe")/chloe-default and replaced with
  coop-drake(0x9ba01002). RESULT: patched=0 - NONE of the Chloe look hashes are in
  the spawn args. So the body look is NOT a patchable hash in our spawn; it's a
  CHARACTER-RECORD reference resolved internally (the captured Chloe descriptor
  references Chloe's record).
CONCLUSION: body-swap to Drake/Nadine requires either (a) activating/replicating
the dormant co-op spawn (which builds a non-Chloe descriptor) or (b) injecting a
look-hash override into the spawn packet's base field (packet+0x20, read by
Player::Init; currently 0=default Chloe) by hooking InitializeSelectorBacked
ProcessSpawnPacket (0x1417C32A0) gated to our clone spawn - SPECULATIVE, untested.
The mechanically-playable clone keeps Chloe's body (+ Nadine hair via +0x170).

### Look-injection (packet base) test — CONFIRMED DEAD (2026-06-20)
Armed a DR2 breakpoint at InitializeSelectorBackedProcessSpawnPacket (0x1417C32A0)
entry during the clone reissue spawn; when rdx(selector)==hash("Player")=0xa5a63e9b,
overrode r9 (arg4 -> packet+0x20 "base") from 0 to coop-drake (0x9ba01002).
RESULT: interception fired and overrode r9, BUT the body stayed CHLOE. So
packet+0x20 (base) is NOT the character/body selector - overriding it has no visual
effect. The Player PROCESS CLASS itself = Chloe (hardcoded character art); the
selector +0x8 = "Player" resolves to that. There is no "Player-Nadine" class. The
co-op skins (drake/chloe/sullivan) override the body via a DIFFERENT field in the
(dormant) co-op spawn path - not packet+0x20.
FINAL WALL (appearance): a Player's BODY in campaign = Chloe, bound by the Player
process class / character record, NOT overridable via any spawn field we can reach.
Body-swap to Nadine would require reconstructing/activating the co-op character
spawn (which builds a non-Chloe character) AND a Nadine co-op skin (which isn't in
the campaign coop look set). This is multi-session deep work with no guarantee.
The clone remains a FULL playable Player mechanically, but with Chloe's body.

### Enemy damage/death relay tests — direct AiHitController calls are unsafe (2026-06-26)

Goal: find a host-side primitive for "client hit enemy -> host applies damage".

Runtime results:
- Enemy `HealthSystem` confirmed at `enemy+0x4A8 -> HealthSystem`, with
  current HP `int32 @ +0x3C` and max HP `int32 @ +0x44`.
- In the tested encounter, all listed `npc-*` enemies can show `HP=0` while
  still being live `Pirate` objects (`vtable RVA 0x294B9D0`) and not ragdolls.
  So HP=0 alone is NOT the death transition.
- Writing HP to 0 did not produce a kill/ragdoll.
- Capturing real `AiHitController::TakeHit` calls works; owner is normally
  controller+0x8 -> enemy, e.g. `npc-sv-shotgun-*`.
- Calling `AiHitController::Die(false)` with a copied real HitDescription returns
  without crashing but only knocks the enemy down; the enemy remains live
  (`vtable RVA 0x294B9D0`, not `ProcessRagdoll`).
- Re-calling native `TakeHit` with a copied/amplified HitDescription CRASHES.
- Calling `AiHitController::Die(true)` with the captured controller/template
  CRASHES.

Conclusion:
- Directly replaying enemy damage/death through `AiHitController` is CLOSED as a
  safe path for now.
- For enemy sync, use safer surfaces:
  - host observes native deaths caused by normal local gameplay and replicates
    dead/remove state to the peer;
  - client copy can be hidden/sunk when host says dead;
  - client->host damage needs a different native entry point found statically or
    by call-stack tracing, not manual `TakeHit`/`Die` replay.

### Enemy damage static reverse — HealthSystem and NpcArmor layer (2026-06-26)

Goal: find the safe native entry point for "client hit enemy -> host applies
damage" without replaying low-level `AiHitController::TakeHit`/`Die`.

Static artifacts:
- `reports/pseudo_healthsystem.txt`
- `reports/pseudo_healthsystem_core.txt`
- `reports/pseudo_damage_cluster.txt`
- `reports/pseudo_damage_support.txt`
- `reports/pseudo_damage_plus88_candidates.txt`
- `reports/xrefs_damage_strings.txt`
- `reports/xrefs_damage_npc_strings.txt`
- `reports/pseudo_npc_damage_kill.txt`
- `reports/pseudo_npc_damage_callers.txt`

HealthSystem vtable:
- Base `0x142BBE128`.
- Important slots:
  - `+0x68` -> `0x141FCADD0`
  - `+0x70` -> `0x141FCA8C0`
  - `+0x80` -> `0x141FCA910`
  - `+0x88` -> `0x141FCA920`
  - `+0xB8` -> `0x141FCAFA0`
  - `+0x1B8` -> `0x141FCA840`
  - `+0x1C0` -> `0x141FCAE40`
- `0x141FCA920` only writes `health+0x30`.
- `0x141FCA910` only writes `health+0x34`.
- `0x141FCA8C0` only writes max HP at `health+0x44`.
- `0x141FCADD0` is the proper current-HP setter path:
  - calls `+0xB8` (`0x141FCAFA0`) to compute current/effective HP;
  - stores old HP into `health+0x3C`;
  - updates timestamp/history fields;
  - calls `+0x1C0` (`0x141FCAE40`) with the new HP.
- `0x141FCAE40` only writes `health+0x38` and the history ring under
  `health+0x58`; it does not notify `AiHitController` and does not perform the
  death transition.
- Static conclusion matches runtime: even the proper HealthSystem setter is not
  enough to kill an enemy. Death conversion is higher, in the Npc/AttackInfo/
  armor damage layer.

Script health setter:
- `0x14055AAE0` is a script-exposed health setter.
- It resolves an actor, checks `actor+0x4A8 -> HealthSystem`, and calls
  HealthSystem virtuals `+0x88`, `+0x80`, `+0x70`, `+0x68`.
- It logs strings like `'%s' is already dead` and `'%s' has no health system`.
- This is useful evidence for HealthSystem layout, but not a safe kill/damage
  primitive because it still goes through the non-death HealthSystem setter path.

NpcArmor/AttackInfo layer:
- The important damage string is:
  `bool __cdecl NpcArmor::OnHealthDamage(class Npc *,const struct AttackInfo *,float,float)`
  at string address `0x142930A50`.
- Exact function found:
  - `0x1403D7DF0` = `NpcArmor::OnHealthDamage(...)`.
- Direct callers:
  - call from `0x14040AA70` inside function `0x140409F70`;
  - call from `0x14040B404` inside function `0x14040B250`.
- `0x1403D7DF0` appears to be an armor/body-part damage processor, not the
  top-level "apply damage to this NPC" API. It receives an already built
  `AttackInfo`, checks hit/body-piece state, detachment definitions, mesh/prop
  damage, and can call lower helpers such as `sub_1403D9F50`, `sub_1403DA520`,
  `sub_1403DAAB0`, and `sub_141BBF130`.
- `0x140409F70` is a much more promising upper damage handler:
  - `a1` is the NPC/character;
  - `a2` is the `AttackInfo` pointer;
  - it reads HealthSystem from `a1[149]` (`a1+0x4A8`);
  - it calls virtual `+0xB8` and `+0xF8`-like HealthSystem paths;
  - it calls the NpcArmor object at `a1[1064]` (`a1+0x2140`);
  - it invokes `NpcArmor::OnHealthDamage(a1[1064], a1, AttackInfo, ...)`;
  - it contains knocked-down/death-state decision logic after the armor call.
- `0x14040B250` is a smaller related handler that also calls
  `NpcArmor::OnHealthDamage(*(_QWORD *)(a1+0x2140), a1, AttackInfo, ...)`, but
  it is less complete than `0x140409F70`.

Important non-candidates:
- `AiHitController::TakeHit` (`0x1405001E0`) and `Die` (`0x140500C20`) are low
  reaction/death-animation paths and are unsafe to replay manually.
- `0x140504FF0`, `0x1405048C0`, `0x140508840`, `0x140508C20`, and
  `0x140502AB0` are reaction selection/application/update helpers, not the safe
  high-level damage entry.
- A broad scan for virtual `+0x88` calls produced many false positives. In
  particular, `0x140510C90`/`0x140510D60` call a global service at
  `qword_143FDF520+0x88/+0x90`, not `AiHitController::TakeHit`.
- `0x1403E7310` is `NpcManager::SetupVisionChecks`, not a kill command despite
  being near NPC-manager strings.

Current static conclusion:
- Safe damage is most likely not "set HP" and not "call AiHitController".
- The next promising target is the real caller chain above `0x140409F70`: find
  who builds/fills `AttackInfo`, calls `0x140409F70`, and what minimal
  `AttackInfo` fields must be valid for bullets/melee/explosions.
- Next static commands:
  - inspect direct callers of `0x140409F70` and `0x14040B250`;
  - inspect vtable slot(s) that dispatch to these functions;
  - identify `AttackInfo` fields around offsets already seen in
    `0x140409F70`: `+0x08`, `+0x10`, `+0x14`, `+0x1C`, `+0x20`, `+0x28`,
    `+0x30`, `+0x40`, `+0x8B0`, `+0x8B4`;
  - only after that design a safe host-side damage relay test.

### Enemy damage static reverse — native Attack event publisher found (2026-06-27)

The previous next-step list is now completed.

New artifacts:
- `reports/pseudo_attack_event_entry.txt`
- `reports/pseudo_attack_event_sites.txt`
- `reports/pseudo_attack_string_refs.txt`
- `reports/pseudo_attack_publish_callers.txt`
- `reports/pseudo_attackinfo_ctors.txt`
- `reports/pseudo_attackinfo_base.txt`

Full native chain:

1. `0x14027A270(AttackInfo*, target)` builds and publishes the authored
   `"Attack"` event (`event id 0x0539335E`) to the target.
2. `Npc::EventHandler` at `0x140410C50` recognizes `0x0539335E` and calls
   `0x14040E7C0(target, event)`.
3. `0x14040E7C0`:
   - requires at least one event argument (`event+0x128`, `int16`);
   - extracts `AttackInfo*` from the boxed value at `event+0x28` through
     `0x141831310`;
   - calls target virtual slot `+0x1170`;
   - performs the surrounding hitmarker, last-hit, knockdown, kill-combo,
     killer/victim notification, and multiplayer notification logic.
4. Character virtual slot `+0x1170` is `0x140409F70` for the common campaign
   character families.
5. `0x140409F70` calls `NpcArmor::OnHealthDamage` (`0x1403D7DF0`) and runs the
   HealthSystem/knockdown/death decision path.

`0x140409F70` vtable membership:
- common slot offset `+0x1170`, index 558;
- present in `Npc`, `Alcazar`, `Buddy`, `Cutter`, `Drake`, `Elena`, `Human`,
  `NadineBuddy`, `Nadine`, `Pirate`, `NetBuddy`, `Rafe`, `Samuel`, and
  `Sullivan`.

`0x14027A270` is confirmed as the high-level publisher, not an inferred helper:
- explosion path passes a complete explosion AttackInfo;
- carry-object contact path passes a complete AttackInfo;
- melee paths pass `CloseCombatAttackInfo`;
- projectile/network paths pass `ProjectileAttackInfo`;
- generic paths pass `NdAttackInfo`/`AttackInfo`;
- all use the same `(attackInfo, target)` call shape.

AttackInfo class layout and clone contract:
- `NdAttackInfo`:
  - vtable `0x1428F91B0`;
  - small polymorphic base size `0x18`;
  - deleting destructor `0x140279140`;
  - virtual clone at `+0x8` -> `0x1402790F0`.
- `AttackInfo`:
  - vtable `0x1428F9198`;
  - size `0x860`;
  - constructor `0x14027A160`;
  - copy constructor `0x140279590`;
  - deleting destructor `0x140279FB0`;
  - virtual clone at `+0x8` -> `0x140279540`.
- `CloseCombatAttackInfo`:
  - vtable `0x1429C4970`;
  - size `0x880`;
  - constructor `0x1408F8990`;
  - heap clone `0x1408F89E0`;
  - in-place copy `0x1408F8B60`;
  - deleting destructor `0x1408F8B20`.
- `ProjectileAttackInfo`:
  - vtable `0x142935C50`;
  - size `0x8C0`;
  - constructor `0x1403FB2B0`;
  - heap clone `0x1403FB340`;
  - in-place copy `0x1403FB520`;
  - deleting destructor `0x1403FB4E0`.

Critical correction to the current DLL prototype:
- The old prototype copied only `0x100` bytes from `AttackInfo`.
- That is structurally invalid: the common object is `0x860`, close combat is
  `0x880`, and projectile is `0x8C0`.
- Important fields used by the damage path exist around `+0x7C0..+0x858` and
  derived fields extend through `+0x878` / `+0x8B4`.
- The old direct call
  `0x140409F70(enemy, partialCopy, savedA3)` must not be tested.
- It has been disabled in `scanner/src/teleport_hook.cpp`; the damage hook is
  also disabled by default.

Safe relay design supported by the static evidence:
1. While a real AttackInfo is alive, clone it through its own virtual `+0x8`.
   This preserves the correct derived type and full object size.
2. Retain the heap clone only while its referenced process handles remain valid.
3. Apply it through `0x14027A270(clone, target)`, not through
   `0x140409F70`/`AiHitController`.
4. Destroy the clone through its virtual `+0x0` deleting destructor.

Remaining static gate before enabling a live test:
- decide whether the relay should clone a projectile/melee template or construct
  a fresh generic `AttackInfo` with `0x14027A160`;
- validate which embedded handles may become stale between capture and replay;
- enable the prepared test only for an explicit controlled run.

The first and third items from the previous gate are now complete:
- `0x140279540` allocates exactly `0x860` bytes and calls the complete
  `AttackInfo` copy constructor `0x140279590`.
- Native call sites pass stack-allocated AttackInfo subclasses to
  `0x14027A270` and destroy/reset them immediately after it returns. Therefore
  the publisher consumes the pointer synchronously; a relay clone can be
  destroyed immediately after the call.
- `scanner/src/teleport_hook.cpp` now contains the full polymorphic
  clone/publish/delete prototype:
  - validates known AttackInfo vtables;
  - clones through virtual `+0x8`;
  - publishes through `0x14027A270`;
  - deletes through virtual `+0x0`;
  - rejects captured templates older than 15 seconds.
- Both `g_install_npc_damage_hook` and
  `g_enable_attackinfo_replay_test` remain `false`, so no runtime behavior has
  been enabled.
- Release builds of both `tll_teleport_hook.dll` and
  `tll_teleport_hook_v2.dll` succeeded after this change.

### AttackInfo field accessors and delayed-replay hazards (2026-06-27)

Artifact:
- `reports/pseudo_attack_event_accessors.txt`

The authored script accessors provide direct field evidence:
- `AttackInfo+0x08/+0x10` = source/attacker process-handle pair.
  - The engine resolves it through `*(handle.ptr+0x8)` and validates the
    generation against the target object's `+0xBC`.
  - `event-attack-source?` uses this pair through `sub_1401ABD00` /
    `sub_140177F30`.
- `AttackInfo+0x14` = float attack magnitude exposed by the no-argument attack
  value accessor.
- `AttackInfo+0x18` = weapon/type symbol used by `event-attack-weapon?`.
- `AttackInfo+0x1C` = attack type used by `event-attack-type?`; valid script
  values are `0..5`.
- `AttackInfo+0x24` = damage-point count.
- Damage points begin at `AttackInfo+0x30`, stride `0x50`.
- Per damage point:
  - `point+0x40` = float damage value;
  - `point+0x30/+0x34` = hit-object handle pair;
  - the script getter resolves that pair through `qword_14386FD00`;
  - other point fields include authored impact position/direction and joint/
    object metadata.
- Derived projectile fields used by the script view remain at
  `AttackInfo+0x8B0/+0x8B4`.

Implications:
- A full clone is memory-safe, but a delayed clone is not automatically
  semantically safe.
- `AttackInfo+0x08/+0x10` may refer to an attacker that has despawned or changed
  generation. For co-op relay this pair should be rewritten from the current
  host-side representation of the remote player.
- Each damage point can refer to the original target's hit object/joint. Reusing
  it on another enemy can select the wrong armor/body part or leave a stale
  handle.
- A production relay should either:
  - rebuild the damage-point list for the current target; or
  - construct a fresh generic AttackInfo with `0x14027A160` and fill only
    validated source, damage, attack type, impact, and target fields.
- The prepared clone-based prototype remains appropriate only for a short,
  controlled feasibility test on a compatible live enemy. Its 15-second expiry
  limits but does not eliminate stale-handle risk.

### Fresh AttackInfo construction and armor calculation (2026-06-27)

Artifacts:
- `reports/pseudo_damagepoint_builders.txt`
- `reports/pseudo_damageinfo_build_core.txt`
- `reports/ida_names_npcarmor.txt`
- `reports/pseudo_npcarmor_damage_builders.txt`
- `reports/disasm_npcarmor_compute_damage.txt`
- `reports/pseudo_damagepoint_armor_map.txt`
- `reports/disasm_damagepoint_armor_map.txt`

Recovered `DamageInfo` layout:
- `AttackInfo+0x20` / `DamageInfo+0x00` = total float damage.
- `AttackInfo+0x24` / `DamageInfo+0x04` = damage-point count.
- First point starts at `AttackInfo+0x30`; stride is `0x50`.
- Point vectors occupy `point+0x00`, `+0x10`, and `+0x20`.
- `point+0x30/+0x34` is the hit-object registry handle.
- `point+0x38` is additional collision/object metadata.
- `point+0x40` is the per-point float damage written by the projectile collision
  builder.
- `0x1402791C0` initializes the complete `0x7A0`-byte `DamageInfo`.
- `0x1414F1420` copies a complete `DamageInfo`.

`NpcArmor` vtable `0x1429348C0` resolves the damage stages:
- `+0x10 -> 0x1402942A0` = ordinary `GameArmor::ComputeDamage`.
- `+0x18 -> 0x140294610` = `GameArmor::ComputeExplosionDamage`.
- `+0x20 -> 0x141F38A90` = apply armor-damage results.

The ordinary calculation requires positive total damage and a non-zero point
count before distributing damage over points. Each point is mapped to an armor
piece through `0x140294C30`. A point that does not map to armor is not rejected:
its share remains in the final health damage. Therefore a fresh generic attack
can safely start with one zero-initialized point and no stale hit-object handle.
`0x140294C30` explicitly returns `-1` when the point handle is zero, invalid, or
not a damageable collision object; it does not dereference a null registry entry.

`scanner/src/teleport_hook.cpp` now contains a disabled fresh-object probe:
- construct base `AttackInfo` through `0x14027A160`;
- copy Nadine's live process handle from `+0xA8/+0xBC`;
- set total damage, first-point damage, and count to one;
- set the first impact position from the selected enemy's world position;
- publish through `0x14027A270`;
- run the virtual destructor in-place without freeing the stack storage.

`g_enable_fresh_attackinfo_test` remains `false`; no live behavior is enabled.
Both release DLL targets build successfully with this probe.

### Fresh AttackInfo live validation (2026-06-27)

The controlled `VK_DECIMAL` test succeeded in campaign gameplay:
- a newly constructed base `AttackInfo` published through `0x14027A270`
  actually removed health from a live enemy;
- Nadine's current process handle was accepted as the attack source;
- the publisher returned and the stack object was destroyed without a crash;
- no captured attack template or stale hit-object handle was required.

The first test did not hit the nearest enemy. This was not engine target
selection: the probe selected the first live enemy in `CharacterManager` order.
The implementation now computes 3D distance from Nadine, rejects dead/zero-HP
enemies, and selects the minimum. It also logs target name, distance, and HP
before/after publication. This nearest-target correction is built but still needs
one in-game confirmation.

One physical key press produced repeated publications because the old handler
relied on the unreliable `GetAsyncKeyState` low bit and keyboard repeat. The
handler now uses an explicit released-to-pressed transition. The live-test switch
has been returned to its default disabled state.

### Two-instance enemy death relay validation (reported 2026-06-27)

A prior controlled run with two game instances reached an important end-to-end
result: killing an enemy on the client was relayed to the host and the enemy death
was synchronized back to the client. The client then crashed. This proves that
client damage capture, network delivery, authoritative host application, and at
least the beginning of enemy-death state delivery all executed.

Code inspection identified a concrete unsafe client action: a dead host snapshot
made the client write the still-live enemy's Y coordinate to `-10000` every 16 ms.
Thus the visible disappearance was not a native death transition. Repeatedly moving
a live physics/script actor outside the world is the leading crash cause. The fix
is to apply one fresh native attack locally while suppressing relay recursion, then
stop all transform writes for that dead actor. This correction still requires a
two-instance confirmation.

### Two-instance focus crash isolated (2026-06-28)

Process-tagged logging and a breakpoint-free exception observer identified the
client crash exactly:
- host process remained alive;
- client process raised `0xC0000005` at module RVA `0x3F5D47`;
- this is `RequestDisableAi` (`0x3F5D30`) plus `0x17`;
- the exception followed client enemy-snapshot application, not damage/death
  publication.

The client called `RequestDisableAi` on every matched enemy before applying the
host transform. At least one enemy can be present in `CharacterManager` while its
AI controller is not ready for that request. Generic pointer/readability checks
cannot prove the internal controller is initialized. Enemy synchronization no
longer disables enemy AI. Health and transform application remain enabled; local
AI may temporarily fight transform correction until a safer controller-state test
is recovered. The separate NadineBuddy manual-control call is retained because it
succeeded and targets the known fully initialized buddy object.

Follow-up correction: the separate NadineBuddy call is not safe during scene
construction either. In the next clean run, role B remained alive while role A
terminated immediately after `NadineBuddy AI disabled for manual control`; no
damage or enemy-snapshot action preceded it. Runtime `RequestDisableAi`/
`RequestEnableAi` calls are now disabled for the stable co-op build. Network avatar
movement continues through native `MoveTo` with AI left enabled.

The subsequent no-crash run exposed two over-broad guards that suppressed
synchronization rather than improving safety:
- the retained Player object address legitimately changes several times, so a
  two-second pointer-identity cooldown could remain permanently blocked;
- returning from the entire network tick when the process was not foreground also
  stopped outgoing player-state packets, leaving the active copy with stale data.

The pointer-identity cooldown has been removed. Background copies may continue the
read-only state/enemy send path, while world mutation (enemy apply, damage apply,
Nadine movement) remains foreground-only and packets expire after 1.5 seconds.

### Combat divergence and delayed host termination (2026-06-28)

The next two-instance combat run showed that client-to-host damage relay remained
functional: enemies killed by the client disappeared on the host. The apparent
desynchronization had a local cause. Role B forwarded each hit and then returned
from the hooked damage handler without invoking the game's original handler, so
the same hit was applied only on role A.

The host terminated after several minutes of combat without reaching the exception
observer. Role A did not need the NPC damage vtable hook: its own hits are already
native, while relayed client hits are consumed by the network tick and published as
fresh `AttackInfo` objects. The hook is now installed only after role assignment and
only in role B, reducing the host combat path to unmodified game code.

Role B now relays a hit and then invokes its native damage handler as well. This lets
both worlds execute their own ordinary hit and death transitions. Client ragdolls are
excluded from subsequent host transform/health writes, avoiding the earlier unsafe
combination of a completed local death with continuously pinned physics state. The
optional forced client death publisher remains disabled. This change requires a new
two-instance combat confirmation.

### AI pause preconditions and background synchronization (2026-06-28)

The next run no longer crashed, but player and enemy worlds visibly diverged. Packet
delivery continued. The client applied only 10 of 13 host enemy records because three
local enemies had already completed their native death transition and were correctly
excluded as ragdolls. Actor names in the tested encounter are unique; name matching is
not the cause of this particular divergence.

Static decompilation of `RequestDisableAi` (`0x1403F5D30`) explains the earlier exact
crash at `+0x17`: the function immediately dereferences `npc+0x1FC0` and writes byte
`+0x368`, then walks the binding rooted at `npc+0x1F88`, with no null checks. Scene
construction can expose a registered `Pirate` before those subobjects exist. A guarded
predicate now mirrors the complete native access chain, including the nested registry,
before allowing the call.

The guarded enemy pause worked without the previous access violation, but exposed a
semantic problem: paused client enemies cannot react to the local Nadine, and the
current packet does not contain host attack/animation state to replace that behavior.
Client enemy pausing is therefore off by default until those states are recovered.
NadineBuddy pausing also remains disabled after a host fatal exit.

A separate one-PC test defect was also identified: all received world mutations were
restricted to the foreground process. Switching windows therefore stopped remote
player movement, client enemy application, and host consumption of relayed damage in
the background copy. These operations already execute synchronously from the game's
update hook and remain protected by `world_stable()`; the foreground restriction has
been removed from the production synchronization paths and retained only for manual
diagnostic probes.

The same failed run exposed an independent packet-validity bug before the host
terminated. Role B sent its first slot-0 position while the Player still contained
the engine sentinel `FLT_MAX`; role A logged target `(3.40282e+38, 3.40282e+38)`,
computed an infinite distance, and could feed NaN direction components into native
`MoveTo`. Player packets are now rejected at send, receive, and application unless
all three coordinates are finite and within a broad world bound. Enemy snapshots
use the same validation. This is the strongest direct explanation for the fatal
host exit and requires a clean rerun.

The following live report also confirmed why a host kill remained live on the client:
the dead flag arrived, but `g_enable_client_death_publish` was still deliberately off.
It is now enabled. A host dead flag publishes one fresh lethal `AttackInfo` on the
matching live client enemy under relay-recursion suppression. The unsafe
`RequestEnableAi` call formerly placed before that publication has been removed.
Locally completed ragdolls remain excluded from transform writes.

### Static crash audit: lethal AttackInfo closed, npc-die recovered (2026-06-28)

The next client terminated immediately after a host-side kill, before the post-call
enemy-death log line. No ordinary exception reached the observer. Static comparison
with native attack builders found that combat uses `ProjectileAttackInfo` or
`CloseCombatAttackInfo` for complete kills. The fresh base object is proven only for
non-lethal health damage; base `AttackInfo` is now prohibited from crossing zero HP.

Script `npc-kill` (`0x14055DF80 -> 0x1416A5C20`) recursively destroys the process
tree and bypasses gameplay death, so it was rejected. Script `npc-die`
(`0x140541EF0`) provides the proper forced-death sequence without an `AttackInfo`:
1. check Pirate virtual `+0x860` for an existing death;
2. read total maximum health through HealthSystem virtual `+0xC0`;
3. apply it through Pirate `+0xD00` / HealthSystem `+0x20`;
4. resolve AiHitController through `npc+0x15C8`, virtual `+0x78`;
5. zero `HitDescription[0x70]`, set `+0x10=(0,0,1,0)`, `+0x48=-1`, and
   `+0x64=maxHealth`;
6. call AiHitController virtual `+0xB8` (`0x140500C20`) with `true`.

The implementation now mirrors this sequence with exact vtable/readability checks.
Both host and client route potentially lethal replicated damage through it; base
AttackInfo remains only for non-lethal damage.

The MoveTo path was audited independently. Its native script wrapper validates every
navigation frame through `0x141DEF6C0`; the mod previously skipped this check and
refreshed commands every 140 ms. It now validates the NPC clock, controller, radius,
target, and native navigation frame, and uses the previously stable 250 ms interval.

### Priority reset: complete players before enemies (2026-06-28)

The project order is now strict: first reproduce both players completely between
the two game instances; only then resume enemy synchronization. Enemy snapshots,
damage relay, and mirrored death are preserved but disabled by default.

The existing `StatePacket` is only a positional prototype. It contains transform,
velocity, and the low 32 bits of the slot-0 input button field; `anim_state` is not
an engine animation state. Full player synchronization requires two channels:
- frequent snapshots for continuous movement, orientation, aim, weapon and health;
- reliable ordered events for fire, reload, weapon changes, melee, traversal,
  interactions, damage, death and respawn.

The first blocking dependency is a full remote actor in each process. The current
`NadineBuddy + MoveTo` proxy cannot execute the full Player action graph, while a
second retained `Player` has the required mechanics. Its interaction with local
aim/fire must be isolated from slot-1 input and camera activation before choosing
the remote-actor implementation.

### Correction: the passive-Player aim test also ran camera 1 (2026-06-28)

The prior conclusion that retained slot-1 occupancy alone disables local aim/fire
was not cleanly isolated. In the old "passive clone" build, `aux_input_thread`
automatically set `g_drive_screen1_cam=true` whenever slot 1 existed. The game-thread
hook then called `UpdatePlayerAnimatedCameraControlOwnerAndRouteState` at
`0x140238800` for the slot-1 owner every frame. Logs from the same run contain the
repeating `screen1cam` calls.

Static auditing found no direct weapon prohibition in the eight functions that
reference retained slot 1 explicitly. `0x1404CA550` enumerates both players for
`AiClimbController::IsBlocked`, not for weapon gating. A real weapon-state branch at
`0x141436E70` does change behavior under the multiplayer/split mode byte in
`dword_143E07470`, confirming that split activation can affect weapon logic, but it
does not prove slot-1 occupancy alone is the cause.

The automatic camera-1 call is now removed. The next valid runtime matrix is:
1. slot-1 Player present, split login off, slot-1 input off, camera-1 update off;
2. enable only slot-1 input;
3. return to clean state, then enable only camera-1 update.

### Player snapshot protocol v2 (2026-06-28)

`StatePacket` version 2 replaces the misleading `anim_state` placeholder with the
engine-resolved slot-0 input fields: left stick, right stick, complete 64-bit button
mask, and pressed/released edges. Position, quaternion, and velocity remain. This is
the continuous source stream needed for remote movement, camera, aiming and action
recovery; reliable one-shot action delivery remains a separate required channel.

### Clean passive Player test: aim works; joypad command pool crashes (2026-06-28)

The corrected runtime test created retained slot 1 while all three unrelated paths
were off: split login stayed 0, `max_local_players` stayed 1, slot-1 input stayed
off, and the automatic camera-1 owner update had been removed. Chloe's normal aiming
worked. This disproves the previous claim that retained slot-1 occupancy alone gates
local aim/fire.

The process then raised two access violations at RVA `0x183A090`, inside
`0x141839FB0`. The function manages `disable-joypad-%i-commands-code/script` records.
At the faulting instruction it writes through a record popped from a fixed free pool:
- per-player joypad manager = `g_LocalPlayerInputManager + 0xDB0 + 0xBF0*index`;
- free-record pointer array around manager `+0x4F0`;
- free count at `+0x538`;
- active-record array pointer at `+0x598` (count at active `+0x20`).

The follow-up run showed that both manager 0 and manager 1 were initialized with
eight valid records. The crash instead used the `joypad-2` manager. Two threads
failed almost simultaneously; `free_count` had underflowed to `-1/-2`, and the
record register contained the poison pattern `0xAEB3A273AEB3A273`. Thus this is a
concurrent fixed-array mutation caused by two Player jobs sharing a manager whose
native code assumes single-Player serialization, not a missing slot-1 constructor.

The mod now detours only `0x141839FB0` through an exclusive SRW lock, then executes
the original function unchanged. This preserves all local input commands while
serializing the shared fixed-array mutation. Collision-filter mutation remains
removed from the clean test.

### Joypad serialization validated; remote Player path enabled (2026-06-29)

The serialized build survived a fresh campaign load with a retained slot-1 Player
for more than two minutes. The `joypad-2` manager remained valid (`free_count=6`,
`active_count=1`) and no exception was raised. This closes the shared command-pool
race as the cause of the previous delayed crashes.

The two-instance path now creates slot 1 only after receiving a fresh peer packet.
Role A gives the remote Player Nadine's appearance; role B keeps the remote Player
as Chloe while its local slot-0 Player uses Nadine's appearance. Peer transform and
velocity application no longer stops when the process loses foreground focus. The
old `NadineBuddy + MoveTo` network avatar is disabled while this full Player path is
selected, preventing two competing remote avatars.

### One-PC camera coupling is shared XInput, not network state (2026-06-29)

The current player packet carries resolved look-stick values but does not apply them
to either camera. Both local cameras nevertheless rotated together during the
two-instance loopback test because both processes poll XInput user 0 and therefore
receive the same physical controller state even while one window is in the
background. For loopback testing only, the XInput hook now returns a connected but
neutral controller state to the background process. Non-loopback two-PC play is
unchanged; each machine keeps its own local camera and controller.

### Remote fire delay: button state was transmitted but never consumed (2026-06-29)

The remote retained Player previously received only transform and velocity. Although
`StatePacket` v2 already carried the complete slot-0 button mask and its edges, no
code copied those fields into slot 1. Any observed remote shot was therefore stale
native state, not synchronized firing. The first action layer now writes fresh peer
buttons into the proven native slot-1 scripted-input structure
`FrameState+0x6FC8+0x28`; it sets the required `0x500` active bits and clears the
structure immediately when peer data becomes stale. Movement and right-stick axes
remain zero in this test so weapon action delivery is isolated from the unresolved
slot-1 camera basis.

### False remote-player death: cloned HealthSystem starts at zero (2026-06-29)

Live memory on role A showed local slot 0 at `100/100`, while the freshly spawned
remote slot-1 Player had a valid HealthSystem at `Player+0x4A8` but current health
`+0x38=0` and maximum `+0x44=100`. The passive clone did not evaluate the death
transition; activating its weapon/input graph exposed the zero health and produced
the false "second player died" state while the real peer remained alive.

`StatePacket` v3 now carries current and maximum player health. Before slot-1 input
is enabled, the receiver seeds the clone's HealthSystem from the fresh peer packet
and maintains it thereafter. Enemy damage hooks remain disabled during player work.

The first v3 build accidentally reused the enemy probe's `+0x3C` current-health
offset. Static `HealthSystemGetCurrentHealth` and live memory both show that the
Player HealthSystem reads current HP from `+0x38`; local `+0x38=100`, while `+0x3C`
is zero. Sending `+0x3C` therefore marked both remote players dead and enabled the
grey failure screen. The Player protocol now uses `+0x38`.

### Alt-tab false death: preserve health across suspended peer (2026-06-29)

The remaining false death was deterministic only after switching windows. A
background TLL instance pauses its game loop and stops sending snapshots; after
the 1500 ms freshness limit, the foreground copy stopped applying all peer state,
including health. Slot-1 actions are still cleared on stale data, but the last
confirmed health is now retained until fresh packets resume. Health is also seeded
synchronously immediately after `SpawnPlayerFromRetainedDescriptor` returns, before
network input is enabled, and restored after the clone update.

Static correction: `0x1410DE890` / Player virtual `+0x850` lazily resolves
`player-collision-settings*`; it is not an upper Player rebuild or
`player+0x3828` publisher. The IDA label was corrected.

### Player death path recovered; stale transform causes local replica death (2026-06-29)

Preserving only health did not prevent the alt-tab death. Static reverse recovered
the actual path:
- Player virtual `+0xBE8` = `0x140D79C00`;
- `+0xBF0` = `0x140D79950`;
- `+0xBF8` = `0x140D78EC0`;
- `+0xC00` = `0x140D792D0`.

All four are native death-entry variants that publish `player-died`; two also drive
the HealthSystem transition. Shared cleanup `0x14042B400` subsequently publishes
`player-friend-died` when character `+0x57C bit 1` identifies a friend. Therefore
the friend event is a consequence, not the trigger.

The stale-packet path released the clone from transform enforcement while its local
physics/update continued, allowing it to author a fall/death independently of the
real peer. The replica now freezes at the last confirmed transform during peer
suspension. All four death entries are guarded for retained slot 1: they are logged
and suppressed while the last peer snapshot reports HP > 0; local slot 0 remains
untouched. Real remote death still requires an explicit synchronized lifecycle
event and is not considered complete.

Runtime validation succeeded: repeated switching between the two instances no
longer killed either remote Player. Both processes remained responsive. No
`REMOTE-DEATH blocked` line was emitted, so none of the four guarded death entries
was reached during the successful run. This isolates the effective fix to holding
the last confirmed transform (and health) while peer packets are paused; the death
entry guards remain a safety boundary for other local-only hazards.

### Native player animation and fire frontier (2026-06-29)

Runtime confirmed the campaign fire core at `PlayerFireController::Fire
0x140E2C730`: `controller+0x108` is the owning Player and R8 is the active weapon.
The multiplayer `NetEventEmitProjectileBullet` send wrapper `0x140A46D70` remained
silent and is not the campaign source.

The attempted raw pose chain `Player+0x1A8 -> +0x10 -> owner` was disproven.
A hardware write breakpoint identified `0x1418BBBB1` in `0x1418BB6D0`; static
analysis shows its 32-byte entries contain effect ids, pointers and flags, not
joint transforms. Cross-process pose transport was removed.

The viable native route is `NetEventCharacterMove::FillAnimCmdBitStream
0x140BC4E60`, which serializes up to 4096 bits of complete animation commands.
Each Player embeds `NetAnimCmdGenerator` at `Player+0x1B30`; its receive path uses
`AddSnapshot 0x140A01E50`, `ProcessSnapshot 0x140A02890`, and
`UpdateFlagsAndWeapons 0x140A07160`. The next test must first generate, parse and
apply one local Player animation snapshot to the clone within a single process;
only then should that self-contained payload be transported between copies.
Detailed checkpoint: `reports/player_animation_weapon_frontier.md`.

### Correct per-slot Joypad base and rejected false activation fields (2026-06-30)

**STATIC:** `Input_PerPlayerDispatch_MgrArg 0x14179E6C0` uses two related addresses
that had been conflated:

- `FrameState+0xE88 + slot*0xBF0` is the dispatch loop cursor;
- `FrameState+0xDB0 + slot*0xBF0` is the real `Joypad` object passed as argument 4
  to `FrameState_PerSlotInputApply 0x140289150` and then as destination to
  `Input_ScaleCopyVirtual 0x14183B410`;
- the block selector read at cursor `+0x18` is therefore `Joypad+0xF0`.

Evidence is independent and convergent: the dispatch instructions explicitly use
`lea r9,[rbx-0xD8]`; the `Joypad` constructors/resetters
`0x141836640/0x1418369C0/0x141836BB0` initialize an object through `+0xBE8`; and the
previously confirmed disabled-command pool is rooted at
`FrameState+0xDB0+0xBF0*index`.

**REJECTED:** the claimed circular-motion differences at
`slotstruct+0x08/+0x10/+0x54/+0x5C`. The diagnostic started at the loop cursor and
was shifted `+0xD8` past the real object, so those offsets and the derived
right-stick-as-heading theory are invalid.

**STATIC:** the circular-motion gate writes `dword_143ECEAA4` bytes `+1` and `+3`.
The debug-menu builder `0x140306DE0` binds these exact addresses to `Infinite Ammo`
and `Infinite Grenades`. They are unrelated cheats, not locomotion activation.
Normal replicated input must not set both the circular-motion gate and the scripted
input gate: doing so invokes the same state processor twice with two different
sources.

**CONFIRMED:** a corrected runtime capture of the complete slot-1 `Joypad` after
each mutually exclusive branch produced `JOYPAD-DIFF total=0` in both processes.
There is no persistent per-branch activation field in that `0xBF0` object. This does
not exclude transient values consumed and cleared within one frame.

**REJECTED:** mass hardware execution breakpoint at the converged
`Input_ScaleCopyVirtual` exit `0x14183C9C6`. After arming 63/67 threads, both game
processes terminated without a capture hit. The diagnostic was removed. The safe
replacement reads the previous frame's eight `block[1]` floats while the circular
branch runs; it does not suspend or alter any game thread.

### Slot-1 input values closed; missing-CameraManager claim rejected (2026-07-01)

**CONFIRMED:** the safe `block[1]` capture isolated the resolved values of the two
mutually exclusive branches. The moving circular branch left
`[cos,sin,0.5,-0.72]`; the scripted branch left `[sx,sy,0.5,0]`, with the same
`0x500` button field. Reproducing the circular branch's right-stick values in the
scripted branch did not move the remote Player. Therefore neither the persistent
`Joypad` object nor these eight resolved values contain the missing activation.

**REJECTED:** `CameraManager(1) is NULL in campaign`. This contradicts the earlier
live read of both entries in `0x14697AD50`: route entities 0 and 1 were both present,
resolvable and structurally paired. `0x141C06420` resolves a screen-route entity;
its existence must not be conflated with a usable active camera child/pose basis.

**STATIC:** concrete Player movement modes are camera-route dependent:

- exact source signature in `0x140D12B30` identifies
  `CoverMoveUpdate::UpdatePostAnim(Player&)`; it reads `Player+0x4010` and passes
  that screen index to `ResolveScreenRoutePoseSeedOrDefault 0x141C077B0`;
- the gun-movement family `0x140C9C630` reads the same `Player+0x4010`, resolves
  the route through `0x141C06420`, then selects its preferred camera child through
  `0x141C0D4B0`;
- this agrees with the already confirmed role of `Player+0x4010` as the per-player
  screen index and the two-entry route table.

This narrows the blocker to the usable route-child/pose basis or its movement
consumer. It does not yet prove which field yields zero movement, so no runtime
patch is justified. Next: find the shared conversion/consumer used by ordinary
movement, resolve its virtual calls and trace its fallback path.

### Player movement uses a separate Joypad index at Player+0x39B8 (2026-07-01)

**STATIC:** the previous camera-only model was incomplete. The real movement layer
is the MSVC RTTI class `StickMoveInterface`, with vtable `0x142A30740` and main
update `PlayerMoveStick_MainUpdate 0x140F03500`. Independent evidence converges:

- `0x14024D7E0` returns the embedded input-command state at `Player+0x39A0`;
- `0x1402B1890` returns `state+0x18`, therefore `Player+0x39B8`;
- `PlayerMoveStick_MainUpdate` multiplies that value by `0xBF0` and selects
  `FrameState+0xDB0+0xBF0*index`, while separately reading `Player+0x4010` for
  screen/camera routing;
- command query `0x141E39D50` independently repeats the same
  `state+0x18 -> *0xBF0 -> FrameState+0xDB0` selection before calling the Joypad
  command lookup `0x141837CD0`;
- the Player constructor family `0x140B2DF90` zeroes `Player+0x39B8`; the direct
  member-reference audit found no later direct non-zero write.

This identifies `Player+0x39B8` as the player-to-Joypad binding, distinct from the
screen index. It is a stronger explanation for the remote clone's missing movement,
animation and actions than a missing camera object.

Competing hypotheses and falsifiable prediction before runtime:

1. **Primary:** reissuing the spawn with route index 1 sets `Player+0x4010=1` but
   leaves constructor-default `Player+0x39B8=0`; the clone therefore reads Joypad 0.
2. The spawn pipeline or an indirect setter later writes `Player+0x39B8=1`; then the
   binding hypothesis is rejected and the next target is the command-state contents.
3. Both indexes remain 0; then the reissued route index was not committed into the
   Player object and the spawn-index model must be corrected.

The read-only post-spawn diagnostic logs both fields for retained slots 0 and 1 as
`PLAYER-INPUT-BIND`. No field is modified by this test.

### Retained slot is not the Joypad slot: clone reads Joypad 3 (2026-07-01)

**CONFIRMED:** the read-only diagnostic rejected all three numeric predictions
above. In both game copies the values were stable and identical:

- retained slot 0: `Player+0x39B8=2`, `Player+0x4010=0`;
- retained slot 1: `Player+0x39B8=3`, `Player+0x4010=1`.

Thus the clone already has a distinct input binding, but the current injector
targets the wrong destination. It writes the scripted-input structure and gate for
dispatch slot 1, while the clone's movement and command readers select Joypad 3.
The distinction is supported independently by:

- `Input_PerPlayerDispatch_MgrArg 0x14179E6C0`, whose loop variable `0..5` selects
  the destination Joypad object and is passed as argument 5 to the per-slot apply;
- `FrameState_PerSlotInputApply 0x140289150`, which indexes both gates by that same
  argument and has a special native condition for exactly slots 2 and 3;
- `PlayerMoveStick_MainUpdate` and `PlayerInputCommandState_IsCommandActive`, which
  read the destination Joypad by `Player+0x39B8` and therefore select 2/3, not the
  retained-table indexes 0/1;
- live values from two independent processes, proving that retained index 1 maps
  to Joypad index 3 in the tested campaign state.

The earlier `Joypad` comparison at `FrameState+0xDB0+0xBF0*{0,1}` therefore
compared two objects not consumed by either active Player. Its `JOYPAD-DIFF total=0`
result remains true for those objects but cannot diagnose the clone.

Competing hypotheses before the corrective runtime test:

1. **Primary:** feed destination Joypad 3 while keeping its source-block selector
   at 1. The clone should execute native movement/actions and produce locomotion
   animation, while Chloe remains on source block 0 through Joypad 2.
2. If Joypad 3 receives the copied values but the clone remains inert, a later
   ownership/activity gate exists between Joypad 3 and `StickMoveInterface`.
3. If Chloe also reacts, source block 1 is not isolated and its selector/lifetime
   must be recovered before further action injection.

Falsifiable prediction: after targeting the clone's actual `Player+0x39B8`, the
log must show destination 3/source 1 and the clone must animate from its fed
movement. It must not move Chloe. A destination read-back without clone animation
rejects the primary hypothesis and points to a downstream Player activity gate.

### Joypad 3 feed reaches its source block, but Player movement state stays downstream (2026-07-01)

**REJECTED:** targeting dispatch destination Joypad 3 is not sufficient to animate
the clone. Both processes logged `destination=3 source=1`; source block 1 contained
the transmitted axes and `0x500` activity bits, while the local Player remained on
Joypad 2. Nevertheless the remote replica did not locomote. With the existing
`g_clone_freerun=true` policy it stayed still until its distance exceeded four
metres and then snapped to the peer position. The visible teleport is therefore
the explicit `kSnapDistSq=4m` fallback, not a low network rate; player state is
already sent at approximately 60 Hz.

**STATIC:** `PlayerMoveStick_MainUpdate 0x140F03500` does not consume raw Joypad
axes directly. It reads the embedded `PlayerInputCommandState`:

- `0x141E39C80(state, out, 0)` copies the 16-byte movement vector at `state+0x90`
  (`Player+0x3A30`);
- `0x141E39D10(state, 0)` reads movement magnitude at `state+0x7C`
  (`Player+0x3A1C`);
- sole updater `0x141E39FB0` calls two virtual providers on the embedded state,
  derives direction/magnitude, and publishes the fields above;
- the updater has one caller, Player virtual slot 16 (`vtable+0x80`) at
  `0x140C9F8B0`. The existing synchronization hook is instead virtual slot 87
  (`vtable+0x2B8`) at `0x141BB8320`, whose complete body only updates transform
  history and never calls slot 16.

Correction to the previous section: `Player+0x39B8` does select the per-player
Joypad for command queries, but the locomotion vector itself is consumed from the
embedded command state after the separate slot-16 update. The call to
`0x14183A1C0` with command `"move"` adds a temporary command-disable entry; it is
not an analog-axis reader.

Competing hypotheses for the next read-only test:

1. Player slot 16 is not scheduled for retained slot 1. It will log calls for the
   local Player only.
2. Slot 16 is called for both Players, but the clone's before/after movement fields
   remain zero; one of the embedded state's virtual providers rejects Joypad 3.
3. Slot 16 publishes non-zero clone movement, so the remaining blocker is later in
   `StickMoveInterface` or locomotion ownership.

The diagnostic hook must call the original slot-16 method unchanged and only log
`Player+0x39B8`, `+0x3A1C`, and `+0x3A30` before/after. No forced update is allowed
until this scheduling/provider distinction is observed.

### Player slot 16 runs for clone; raw move provider remains zero (2026-07-01)

**CONFIRMED:** Player virtual slot 16 runs continuously for retained slots 0 and 1
in both processes. Hypothesis 1 above is rejected. During local movement:

- local Player/Joypad 2 published non-zero `raw`, magnitude 1 and a changing
  camera-relative move vector;
- remote Player/Joypad 3 continued to publish `raw=(0,0)`, magnitude 0 and a zero
  move vector;
- at the same time the remote feed log showed non-zero axes in source block 1.

The failure is therefore inside the native Joypad-to-Player command resolution,
not Player scheduling and not the later `StickMoveInterface` consumer.

**STATIC:** the exact raw-move path is:

1. `Player_UpdateInputCommandState 0x140C9F8B0` calls
   `PlayerInputCommandState_Update 0x141E39FB0`.
2. The embedded object is RTTI class `PlayerJoypad`, vtable `0x142A2C388`.
3. Common helper `0x141E38B40` selects the Joypad through state `+0x18`, hashes
   command `"move"`, checks it through `0x14183A3D0`, resolves its descriptor via
   `0x14183ABF0`, and calls `0x14183D1F0`.
4. `0x14183D1F0` reads processed Joypad pairs `+0x48/+0x50`, applies threshold
   `+0x58/+0x5C`, and writes state `+0x28` (`Player+0x39C8`).

The remaining alternatives are now exhaustive:

1. `Joypad+0xBE0` is set for Joypad 3, causing `Input_ScaleCopyVirtual` to reset it.
2. Command `"move"` is disabled for Joypad 3 by `0x14183A3D0`.
3. Joypad 3 lacks a command descriptor from `0x14183ABF0`.
4. Descriptor exists and is enabled, but processed axes `+0x48/+0x50` remain zero;
   then the fault is the earlier source-to-Joypad conversion itself.

The next pass-through diagnostic records exactly these four conditions for both
Players. It must not modify the flags, command tables or axes.

### Joypad 3 is cleared by the global keyboard-input branch (2026-07-01)

**STATIC:** all four earlier alternatives are now separated. Runtime showed
`Joypad3+0xBE0=0`, command `"move"` enabled, a non-null descriptor, and non-zero
source block 1, while processed pairs `Joypad3+0x48/+0x50` remained zero.
Static analysis of `Input_ScaleCopyVirtual 0x14183B410` explains the exact loss:

- `GameInfo` vtable `0x1429040B8`, virtual `+0x90 = 0x14029AB50`, returns true
  when `GameInfo+0x499C` is non-zero (unless Steam overlay state from virtual
  `+0x98` is active);
- when that result is true, `Input_ScaleCopyVirtual` skips the direct source-axis
  copy and clears the processed pair unless named keyboard-stick commands supplied
  a replacement;
- destination indexes 0 and 2 alone enter the `kb-left-stick-*` and
  `kb-right-stick-*` command branch (`test a3, 0xFFFFFFFD`); destination 3 cannot;
- the six native Joypads are initialized in pairs by `0x14179D780`: source
  selectors alternate 0/1, while Joypads 2 and 3 both carry class value 0. Thus
  Joypad 3 is not misconstructed; it is the second member of the normal gameplay
  pair but has no keyboard-command replacement.

Competing hypotheses for the narrow runtime test:

1. The global keyboard branch is the sole reason Joypad 3 loses source block 1.
2. Direct analog copy runs after selecting it, but later swap/scaling logic still
   clears the pair.
3. Joypad 3 becomes non-zero but `PlayerJoypad` rejects it later, so locomotion
   remains zero.

Falsifiable prediction: force virtual `+0x90` to return false only on the thread
and call depth processing destination 3. Source block 1 must then appear at
`Joypad3+0x48`, then at `Player+0x39C8/+0x3A1C`, and the remote Player must play
native locomotion without changing Chloe/Joypad 2. A non-zero Joypad pair with a
zero Player pair rejects hypothesis 1 in favour of hypothesis 3; a still-zero
Joypad pair supports hypothesis 2.

**REJECTED TEST MECHANISM:** the first implementation used a C++ `thread_local`
flag around the detoured `Input_ScaleCopyVirtual`. Source block 1 became non-zero,
but `Joypad3+0x48` remained zero. Exact disassembly shows this can happen only when
the saved `GameInfo+0x90` result at stack `var_3530` is still true. External live
reads proved that the active GameInfo object uses vtable `0x1429040B8` and its
`+0x90` entry did point into the injected DLL, so the table patch itself was live;
the dynamic TLS flag did not survive as intended on the game's existing worker
thread. This run does not distinguish the three hypotheses above. The replacement
uses an atomic current-thread id plus an explicit override-hit counter.

**REJECTED TEST MECHANISM 2:** the first atomic-thread-id run proved that the
patched virtual function was reached (`modeOverrides` increased), but the counter
then froze while non-zero source axes continued arriving and the processed pair
stayed zero. Concurrent destination-3 calls can interleave their
`exchange -> original -> restore` windows and leave another thread's id installed.
This is a test-harness race, consistent with the already confirmed concurrent
two-Player input work. Destination-3 `Input_ScaleCopyVirtual` calls are now
serialized around the override window; other destinations remain untouched.

**STATIC:** the earlier counter freeze is caused by an even earlier window-focus
gate, not by the analog-mode override:

- live background memory was `byte_1437EA6BF=1`, `byte_1437EA62C=0`, and
  `byte_1437EC6C3=0`, while the Joypad diagnostic independently showed
  `Joypad3+0xBE0=0`;
- `0x141596D60` writes `byte_1437EA62C = (hWnd == GetForegroundWindow())`;
- window procedure `0x141598080` sets it on `WM_SETFOCUS` (`7`) and clears it on
  `WM_KILLFOCUS` (`8`);
- `Input_PerPlayerDispatch 0x14179E6C0` and
  `Input_ScaleCopyVirtual 0x14183B410` both require it before conversion.

This matches the one-PC observation exactly: the foreground instance authors
movement, but the replica that must consume it is in the background and resets its
Joypad before conversion. The next test serializes all calls to
`Input_ScaleCopyVirtual`; only destination 3 temporarily sees the focus byte as 1,
and the original value is restored before any local destination runs.

Falsifiable prediction: with a non-zero source in a background copy, the override
counter must continue increasing, `Joypad3+0x48` and `Player+0x39C8/+0x3A1C` must
become non-zero, and native remote locomotion must play. Joypad 2 must remain
unchanged. If Joypad 3 is still zero despite an increasing counter, the remaining
candidate is post-copy clearing inside `Input_ScaleCopyVirtual`.

### Background Joypad 3 reaches native Player movement (2026-07-01)

**CONFIRMED:** the serialized focus/mode override matched the prediction in both
directions. In the background process, while the saved focus byte remained zero:

- the override counter continued increasing;
- non-zero source block 1 axes appeared unchanged at `Joypad3+0x48/+0x4C`;
- Player slot 16 then published the same non-zero raw pair at `Player+0x39C8`,
  magnitude `1` at `Player+0x3A1C`, and a non-zero world movement vector at
  `Player+0x3A30`;
- the local Player remained bound to Joypad 2 with zero remote axes.

This confirms that the window-focus byte `byte_1437EA62C` and the keyboard-mode
branch in `Input_ScaleCopyVirtual 0x14183B410` were the complete blockers between
the received source block and native remote locomotion. No downstream Player
activity gate remains for ordinary movement.

The remaining visible position error is a separate synchronization problem. The
current network drive does not transmit the local Player's native movement result;
it synthesizes a stick from the positional error and therefore mixes control with
position correction. The next static task is to identify the minimal stable
`PlayerInputCommandState` result to transmit and the exact post-update point at
which to apply it to the remote Player, leaving positional reconciliation as a
separate bounded correction.

### Native movement replication boundary (2026-07-01)

**STATIC:** the minimal ordinary-movement result is now bounded to three fields in
the embedded `PlayerInputCommandState`:

- raw move pair at state `+0x28` (`Player+0x39C8`);
- magnitude index 0 at state `+0x7C` (`Player+0x3A1C`);
- 16-byte world move vector index 0 at state `+0x90` (`Player+0x3A30`).

Independent evidence:

1. `PlayerInputCommandState_DeriveMoveVectors 0x141E397C0` is the sole derivation
   helper. It reads the raw pairs, computes both magnitudes at `+0x7C/+0x80`,
   resolves the corresponding runtime Player through resolver vfunc `+0xA8`, and
   writes world vectors at `+0x90/+0xA0`.
2. `PlayerInputCommandState_GetMoveMagnitude 0x141E39D10` and
   `PlayerInputCommandState_GetMoveVector 0x141E39C80` expose exactly those arrays;
   index 0 maps to `+0x7C` and `+0x90`.
3. `PlayerMoveStick_MainUpdate 0x140F03500` reads both index-0 results repeatedly
   for ordinary movement and traversal decisions.
4. Player vtable slot 16 calls `PlayerInputCommandState_Update` at `0x140C9FE4F`
   near its return. Runtime already confirms the later movement pass consumes the
   values published by this call in the same frame, so a pass-through hook can
   replace the remote result after the native update without skipping scheduling.

Competing implementations:

1. **Primary:** transmit the local raw pair plus native world vector/magnitude;
   feed the raw pair through Joypad 3 for command activity, then replace the remote
   index-0 result after Player slot 16. This preserves native Player movement while
   removing screen-1 camera rotation from the replicated direction.
2. Raw-pair-only replication lets the remote screen-1 basis reinterpret direction;
   prior live logs already showed its world vector differs from the source intent.
3. Position-error chasing produces movement but is a correction controller, not
   input replication; it causes circling/overshoot and cannot preserve starts,
   stops or authored traversal intent.

Falsifiable prediction for packet version 4: while the peer moves, the remote
slot-16 log must show `peerMove=1`, and its post-hook `Player+0x3A1C/+0x3A30` must
match the sender's transmitted magnitude/world vector even when the receiving
window is in the background. The clone must animate and move in the same world
direction without circling toward the peer position. If fields match but direction
still differs, a later consumer transforms the vector again; if `peerMove=0`, the
packet version/freshness/validation path is at fault. No position write is part of
this test.

**CONFIRMED:** packet version 4 matched the prediction in both processes. The
remote slot-16 hook reported `peerMove=1`, copied raw pairs and world vectors
matched the sender across cardinal and diagonal movement, both background and
foreground copies remained alive, and visual confirmation showed correct animated
movement without circling.

**REJECTED:** pure free-run as the final position policy. With identical native
movement replicated, ordinary floating-point, collision and scheduling differences
still accumulated about two metres of positional error. This is expected for two
independent physics simulations and proves that input replication alone is not a
position authority mechanism.

**STATIC:** position reconciliation should use a bounded addition to the already
confirmed native world movement result, not a per-frame transform write:

- `PlayerMoveStick_MainUpdate` consumes the world vector/magnitude after slot 16,
  so a small error-directed addition remains inside native locomotion/collision;
- peer position is already received at approximately 60 Hz and validated at every
  network boundary;
- prior per-frame transform pin/lerp was runtime-rejected because it cancelled or
  visibly damaged locomotion animation;
- current free-run proves the opposite extreme preserves animation but permits
  unbounded sub-snap drift.

Competing policies: hard/soft transform writes are rejected for ordinary drift;
no correction is rejected by the observed two-metre error; the primary policy is
a horizontal deadband plus capped additive movement correction, retaining a direct
snap only for large authored teleports.

Falsifiable prediction: with both players idle after moving, an error above the
deadband must make the remote replica walk natively toward the peer, then stop
inside the deadband without sliding or teleporting. During movement the correction
may bend the replica's path slightly but must preserve animation and converge. A
large scripted displacement must still use the existing snap fallback.

**CONFIRMED (position, partial):** visual testing showed materially smaller
positional error with native-vector correction. The remaining defect is idle
rotation, not failure to converge.

**STATIC:** the idle rotation has two independent causes visible in the current
implementation:

- `apply_puppet_transform_post` resolves the authoritative quaternion at
  `Player+0x140`, but the `g_clone_freerun` branch returns before the quaternion
  copy. Thus free-run synchronizes neither idle facing nor movement-facing drift.
- near the positional deadband, logs show correction magnitudes around
  `0.001..0.012`; these keep the native move command active while their direction
  changes around the target, so an otherwise idle replica repeatedly turns.

The `Player+0x140` quaternion remains the strongest facing boundary: it is already
sent in every state packet, the native move-command builder copies the same field
as actor facing, and the earlier transform-authority run visually held the clone in
the peer's direction. Position writes caused the animation regression; a validated
quaternion-only write did not.

Competing fixes: disabling all correction would reintroduce positional drift;
deriving facing from the correction vector would preserve the circle; applying the
peer quaternion independently of position mode and rejecting sub-threshold
correction commands addresses both observed causes.

Falsifiable prediction: while both players stand still, the remote quaternion must
remain equal to the peer quaternion and no correction command below the minimum
strength may be emitted. The clone must stop rotating. During real movement it must
still face as the peer does; position convergence must remain active for meaningful
errors.

### Native button-command replication boundary (2026-07-01)

**STATIC:** all ordinary Player action commands can be driven through the normalized
Joypad button masks rather than by mirroring animation state or individually
overriding command-query results.

Independent evidence:

1. `Input_ScaleCopyVirtual 0x14183B410` reads the source block's 64-bit current
   mask at `source+0x518`, stores the resulting Joypad current mask at `+0xE0`,
   and derives pressed/released masks at `+0xD0/+0xD8` from the previous mask.
2. The digital command resolver `0x141838C90` reads exactly Joypad `+0xD0`,
   `+0xD8`, and `+0xE0` according to the binding descriptor type, and also uses
   per-button timestamps for hold windows.
3. The wider keyboard/button resolver `0x141838740` performs the same
   held/pressed/released distinction over four 64-bit mask groups and timestamp
   tables. `0x141838050` combines these resolvers for every named command.
4. Campaign Player consumers query authored command hashes through
   `PlayerInputCommandState_IsCommandActive 0x141E39D50`: `jump`, `crouch`,
   `melee-triangle`, `drop`, `pull-out-rope`, `horse-mount`, and `taunt` are all
   visible in `PlayerMoveStick_MainUpdate 0x140F03500`; slot 16 also queries
   fire, reload, melee and object-throw commands.

Competing implementations:

1. **Primary:** transmit local Joypad 2 `+0xE0` and feed it into remote source
   `+0x518` before native Joypad 3 conversion. This lets the engine derive edges,
   hold durations and command results using the remote Player's normal graph.
2. Mirroring each named command result is incomplete: command queries are
   state-dependent and the resolver also applies timing, dependency and disable
   rules that a Boolean map would discard.
3. Direct animation-state or pose replication remains rejected by the malformed
   multiplayer art-group data in campaign and the disproven `Player+0x1A8` path.

Falsifiable prediction: pressing jump, crouch or melee must make the local Joypad
2 current mask appear in the state packet and in remote `source+0x518`; native
conversion must then produce Joypad 3 pressed/current masks and the corresponding
remote animation. If the masks match but the command remains inactive, Joypad 3
uses a different binding table. If the source matches but Joypad 3 does not, the
post-source conversion still contains a destination-specific gate.

### Reject correction-only locomotion while the peer is idle (2026-07-01)

**CONFIRMED:** the asymmetric idle dragging seen in the player-2 process is caused
by applying position correction as locomotion while the authoritative peer has no
movement input.

- PID 9212 repeatedly received peer movement magnitude `0` but generated position
  corrections up to approximately `(-0.36, -0.17)`; Joypad 3 therefore received
  synthetic input and published a non-zero movement magnitude.
- In the same update, the post-slot-16 hook copied the peer's zero raw pair to
  `Player+0x39C8` while retaining the non-zero corrected world vector at
  `Player+0x3A30`.
- The opposite process (PID 31668) had correction `(0,0)` and did not exhibit the
  defect. Button masks were only the known synthetic `0x500`, excluding the new
  action replication as the cause.

This combination asks the animation graph to remain idle while the movement
consumer translates the body. It explains both the apparent backward drag and the
eventual visible reconciliation. The rejected policy is correction-only native
movement while `peer.move_magnitude <= 0.01`.

**STATIC FIX:** bounded correction now runs only while the peer has real movement.
When the peer is idle, sub-four-metre drift is left stationary and will converge
during the next real movement; the existing direct snap remains only for authored
or severe displacement above four metres.

Competing explanations rejected by the logs: a role-specific button mapping would
produce different command masks, but both sides had only `0x0/0x500`; a hard-snap
loop would require errors above four metres, while the recorded defect occurred
with the capped `0.04..0.40` correction vector.

Falsifiable prediction: with both local players idle, neither remote Player may
receive a non-zero correction or movement magnitude, so no idle body drag occurs.
While the peer moves, bounded correction remains active and position error should
still shrink. Only an error above four metres may cause a direct snap.

**CONFIRMED (partial):** idle correction became `(0,0)` in both processes and the
user confirmed that the backward idle drag was materially improved. A small final
position offset remains after movement stops, as predicted by leaving sub-four-
metre idle drift stationary.

### Longitudinal lag cannot converge through a normalized move vector (2026-07-01)

**STATIC:** direction-only correction cannot close all position error while the
peer is already moving at full input magnitude.

Independent evidence:

1. Runtime logs show repeated peer magnitude `1` with correction vectors reaching
   the configured `0.4` cap, especially in PID 21284; a visible position offset
   nevertheless remains.
2. The post-slot-16 hook adds correction to the world movement vector and then
   explicitly normalizes every result above magnitude `1` back to `1`.
3. Therefore a replica lagging behind along the peer's movement direction cannot
   gain speed: both continue at the same capped magnitude. When the peer stops,
   the confirmed idle-drag fix correctly disables further locomotion correction,
   preserving the residual offset.
4. `apply_puppet_transform_post` already owns the validated post-update world and
   local position boundary. The rejected implementation moved 35% of the entire
   error every update; its visible animation damage does not establish that a
   small absolute cap is unsafe.

Competing policies:

1. Increasing the direction correction is ineffective for longitudinal lag due
   to normalization and can bend movement more aggressively.
2. Re-enabling correction-only locomotion at idle is runtime-rejected because it
   translates an idle animation.
3. **Primary:** while real peer movement is active, add a post-update horizontal
   nudge capped at `0.015` world units per update, with a `0.12` deadband. Keep it
   disabled at idle and retain the existing four-metre snap for severe divergence.

Falsifiable prediction: during sustained movement, position error must converge
instead of remaining at the `0.4` direction-correction cap. The replica must keep
its native movement animation, with no visible teleport per update. On release,
the nudge must immediately stop and the idle body must remain stationary. Visible
micro-teleports reject the `0.015` cap; unchanged longitudinal lag means world/local
position is overwritten after this post-update point.

### Process-global keyboard commands leak into remote Joypad 3 (2026-07-02)

**CONFIRMED:** a local jump can trigger the remote replica in the same process
without reaching the other game instance. This is local keyboard-command leakage,
not network replication.

Independent evidence:

1. During the false remote jump, Joypad 3 source/current/pressed masks remained
   zero in both process logs. The new per-Joypad network button path therefore did
   not author the action.
2. `Joypad_EvaluateKeyboardButtonBinding 0x141838740` ignores Player ownership and
   reads held/pressed/released masks directly from process-global
   `LocalPlayerInputManager` groups plus their shared timestamps.
3. Both `Joypad_EvaluateCommandDescriptor 0x141838050` and
   `Joypad_GetCommandValue 0x1418384D0` evaluate a descriptor's per-Joypad gamepad
   binding (`0x141838C90`) and process-global keyboard binding (`0x141838740`)
   independently. Either one can accept the command.
4. The observed scope matches exactly: the replica in the active process jumps,
   while the authoritative player in the peer process does not.

Competing explanations rejected: a transmitted jump would appear in remote Joypad
3 masks and in the peer process; transform/position correction cannot initiate the
native jump state while stationary.

**STATIC FIX:** for Joypad 3 only while state networking is active,
`Joypad_EvaluateKeyboardButtonBinding` returns false. Joypad 2 keeps normal local
keyboard behavior. Joypad 3 keeps its isolated per-Joypad gamepad path, whose
normalized masks are populated from the peer packet.

Falsifiable prediction: a local keyboard jump must affect only the local Player in
that process. The replica may jump only after its peer's normalized Joypad mask is
received. If local leakage remains with Joypad 3 masks zero, another command path
bypasses both known descriptor evaluators; if network jump stops working, the
normalized mask lacks the corresponding gamepad binding bit.

**REJECTED TEST MECHANISM:** the first detour of `0x141838740` crashed both
processes at exactly `0x14183875D`. The copied 15-byte prologue begins with
`mov rax, rsp`; the original body later stores XMM state through that live RAX.
The trampoline's generic `mov rax, return_address; jmp rax` sequence destroyed the
saved entry stack pointer before returning to `0x14183874F`. This was a trampoline
register-preservation bug and does not test the keyboard-isolation hypothesis.
The replacement return sequence is a 14-byte RIP-indirect absolute jump
`jmp qword ptr [rip+0]`, which preserves RAX and all argument registers.

**CONFIRMED:** with the register-preserving trampoline, both processes survived
campaign load and gameplay, and local jumping no longer triggered the remote
replica in the same process. Per-Joypad network input remained active. This
confirms both the process-global keyboard leak and the Joypad-3-only filter.

### Measured residual position error after 0.015 post-step (2026-07-02)

**CONFIRMED:** the movement-only post-step avoids idle dragging but its initial
`0.015` cap is insufficient to converge before the peer stops.

- PID 14756 settled at approximately `0.290 m` horizontal error.
- PID 12076 briefly reached `1.66..1.69 m`, reduced to about `1.38 m` while peer
  movement resumed, then remained at `1.346 m` once `peerMove=0`.
- Both errors remained constant during idle, confirming that no hidden writer or
  idle correction was moving the replica after `apply_puppet_transform_post`.
- The decrease from roughly `1.66` to `1.38 m` confirms that the post-step point
  does affect final position; it simply lacks convergence rate.

Competing policies: restoring idle locomotion is rejected by the confirmed body
drag; snapping all errors would be visible; direction correction remains capped by
normalized movement. The next bounded policy keeps movement-only ownership but
uses 8% of horizontal error per update, capped at `0.05`, with a `0.03` deadband.

Falsifiable prediction: sustained movement must reduce horizontal error below
`0.10 m` before the peer stops, and idle logs must then remain stable below that
value. Any visible per-frame teleport rejects the `0.05` cap. Error remaining near
one metre while `peerMove>0` would show that the observed update frequency is too
low or that movement divergence exceeds this bounded correction rate.

**CONFIRMED:** the 8%-of-error policy with a `0.05` cap reduced the final measured
horizontal error to approximately `0.102 m` in PID 30316 and `0.137 m` in PID
9132. The user independently reported that position synchronization looked better.
This is a large improvement over `0.29/1.35 m`; remaining centimetre-scale error
is deferred while rotation quality is addressed.

### Exact network quaternion copy causes stepped turns (2026-07-02)

**STATIC:** the sharp remote turn is caused by replacing the native Player facing
with each discrete network quaternion after every Player update.

Independent evidence:

1. `apply_puppet_transform_post` currently performs an exact 16-byte copy from the
   latest packet quaternion to `Player+0x140` after the clone's own update.
2. The remote Player already receives the authoritative world movement vector and
   runs its native locomotion update first; that update owns smooth movement-facing
   transitions, but the post-copy overwrites its result.
3. Network packets are sampled asynchronously and the same quaternion is held
   between arrivals, then replaced by the next sample. Exact post-copy therefore
   exposes packet steps directly as angular steps.
4. Position reconciliation writes only X/Z and cannot directly produce the
   reported rotational discontinuity.

Competing policies: removing facing authority entirely risks renewed long-term
heading drift; exact copy is the observed stepped policy; the primary policy is a
shortest-arc normalized quaternion blend after native update.

**STATIC FIX:** choose the target quaternion sign by dot product, blend 18% toward
the target each update, and renormalize. Invalid local orientation still falls back
to a direct validated target copy. Position and input paths are unchanged.

Falsifiable prediction: turns must become continuous rather than snapping at
packet boundaries, while the remote actor still converges to peer facing after a
small delay. `orientationDot` should approach 1 smoothly. Renewed idle spinning
would show that native facing still consumes a non-zero idle signal; excessive
heading lag would require increasing the 18% blend factor.

**CONFIRMED (partial):** visual testing showed materially smoother remote turns
with the 18% shortest-arc blend. No renewed idle spin or crash was reported. Exact
angular lag remains to be measured during later action testing.

### Preserve one-frame remote button edges across state replacement (2026-07-02)

**STATIC:** the transmitted `buttons_pressed` field was not consumed. A short
action could therefore disappear when a newer state packet replaced its packet
before the next Joypad 3 conversion.

Independent evidence:

1. The sender samples the local Joypad's native pressed/current/released masks at
   `+0xD0/+0xE0/+0xD8` approximately every 16 ms and places all three in
   `StatePacket` v4.
2. `publish_peer_state_snapshot` retained only the newest packet. It neither
   rejected an older UDP packet nor accumulated the pressed mask before replacing
   the previous snapshot.
3. `hooked_input_scale_copy` fed only `input_buttons` into remote
   `source+0x518`; `buttons_pressed/buttons_released` were used only for logging.
4. `Input_ScaleCopyVirtual 0x14183B410` derives Joypad 3 command edges from the
   held-mask transition. If a press and release have already completed before its
   next call, no native edge can be reconstructed from the latest held mask alone.

Competing fixes: directly writing Joypad `+0xD0/+0xD8` bypasses its timestamp and
hold-state updates; mirroring animation states bypasses gameplay actions; merely
raising the UDP frequency still permits snapshot replacement. The selected fix
rejects non-increasing state sequence numbers, accumulates accepted pressed masks,
and exposes each accumulated mask as a one-conversion held pulse through the same
`source+0x518` boundary.

Falsifiable prediction: a brief jump, crouch toggle or evade press that reaches the
receiver must produce exactly one native Joypad 3 pressed edge even if a newer
zero-held packet arrives before conversion. Holding a button must not retrigger it
each frame. An action missing despite a logged accumulated edge would show a
binding/state gate above Joypad; repeated actions would show that the pulse is not
consumed once.

### Reject `collider+0xC4` as the collision-filter byte (2026-07-02)

**REJECTED:** the unused `fix_clone_collision_filter()` claims that byte
`collider+0xC4` is Havok collision-filter information and copies Chloe's observed
`0x2C` over the clone's `0x22`. Static data flow contradicts that claim.

Independent evidence:

1. `Player_InitializeCompositeBody 0x1410CF470` iterates the manager at
   `Player+0x238` in `0x110`-byte entries, tags each entry `+0x78` with the
   `"player"` hash, then calls `0x141E584D0(manager, 4)`.
2. `0x141E584D0` iterates those same entries and reads/writes the supplied filter
   value at entry `+0xC8`, not `+0xC4`.
3. On change, `0x141E584D0` updates the attached rigid body's low six filter bits
   at `body+0x4C`, mirrors them into the indexed physics table rooted at
   `0x1438945A8`, or marks the entry dirty and calls `0x141CF6ED0`.
4. The general entry state changer `0x141CF1430` also finishes through
   `0x141CF6ED0`, confirming that this is the physical publication path.

The old helper has no call sites and must remain disabled. Current alternatives
are: both Players already receive layer 4 and same-layer player collision is
suppressed by the global pair table; the clone's body is not registered; or a
separate pair-ignore relation is installed. Those alternatives must be resolved
before changing physics state in runtime.

### Player-to-player collision requires distinct layers plus one matrix pair (2026-07-02)

**STATIC:** the global Havok layer matrix deliberately prevents character layers
4 and 5 from colliding with themselves and each other. A narrow co-op policy can
preserve each compound body's internal exclusions while enabling only collision
between the two Players.

Independent evidence:

1. `Player_InitializeCompositeBody 0x1410CF470` tags its `0x110`-byte collider
   entries as `player` at `+0x78` and calls
   `CompositeBody_SetCollisionFilter 0x141E584D0` with layer 4.
2. The native network-player body builder `0x140A243C0` resolves
   `net-player-collision-settings*` (or the ally variant), tags the same entry
   field as `netcharacter`, and calls the same filter setter with layer 5.
3. `Physics_InitializeCollisionFilters 0x141EC95E0` builds the 64-row group
   matrix and calls `0x1421F86B0` with masks `0x08000070` and `0x228001072`.
   `0x1421F86B0` symmetrically clears every selected row/column pair. Decoding
   those masks proves that pairs `4<->4`, `4<->5`, and `5<->5` are all cleared.
4. The matrix begins at `g_HavokGamelibFilter+0x48`, one 64-bit row per layer.
   Setting row 4 bit 5 and row 5 bit 4 changes only `4<->5`; the self-pairs stay
   clear. Both rows otherwise have the same world-collision permissions after
   initialization.

Competing policies: enabling `4<->4` would also expose colliders belonging to the
same Player to one another; changing only the clone to layer 5 leaves `4<->5`
disabled; a manual distance push would bypass the native collision solver. The
selected implementation assigns layer 5 through the native setter and enables
only the symmetric matrix pair `4<->5`.

Falsifiable prediction: Chloe and the remote Player must stop passing through each
other while both retain floor, wall and traversal collision. No limb/self-collision
instability may appear because `4<->4` and `5<->5` remain disabled. If they still
pass through with matrix bits present and clone entries at layer 5, a later
per-pair exclusion or the Player movement probe's own authored filter is the
remaining gate. Any world fall or violent body instability rejects the layer
assignment and requires immediate rollback.

**CONFIRMED (physics only):** both processes logged the clone on layer 5 and the
enabled `4<->5` matrix pair; the user confirmed that the two Players no longer pass
through one another. Floor/world collision remained intact. The expected authored
bump animation did not play, so physical filtering and animation reaction are
separate systems.

### A one-conversion press pulse is overwritten before Player slot 16 (2026-07-02)

**REJECTED:** exposing a received short press for only the first Joypad 3
`Input_ScaleCopyVirtual` call does not reliably reach Player commands.

- Runtime logs captured the intended remote masks and, on some calls, native
  `pressed=0x10500` / `pressed=0x80000`.
- The same frame then ran additional Joypad 3 conversions, which replaced `+0xD0`
  with zero before `Player_UpdateInputCommandState 0x140C9F8B0` consumed it.
- Movement and held masks remained functional, but the user confirmed that jump
  and evade/roll did not execute.

Competing explanations: a wrong gamepad binding would never produce the matching
Joypad pressed bit; packet loss is excluded for the logged accepted edge; the
remaining concrete loss occurs between repeated conversion and Player slot 16.

**STATIC FIX:** accumulate accepted pressed bits, preserve them in Joypad 3
`+0xD0` across every repeated conversion, and clear them immediately after the
remote Player returns from `Player_UpdateInputCommandState`. Native conversion
still owns timestamps and held/released state.

Falsifiable prediction: one remote jump or evade press must remain visible through
the final Joypad 3 conversion and execute exactly once. Holding the button must not
repeat the action because the latch is cleared after one Player command update.

### Collision divergence triggers the old four-metre emergency snap (2026-07-02)

**CONFIRMED:** the new physical collision exposed a conflict with distance-only
replica recovery, causing the reported teleports.

- After player contact, one process retained approximately `3.48 m` position error
  and the other approximately `2.47 m` while idle.
- Further contact produced errors of `4.19 m` and `5.72 m`; both logged
  `hardSnap=1` under the old `>4 m` rule.
- The state transport remained live and orientation converged, excluding packet
  loss as the direct cause. Each copy's local collision solver had simply blocked
  its replica differently.

Distance alone can no longer distinguish a campaign teleport from temporary
collision divergence. The sender now marks a real positional discontinuity above
`2.5 m` in one 16-33 ms sample as packet flag bit 4. The receiver latches that flag
and snaps only for it, retaining a 12 m last-resort corruption bound.

Falsifiable prediction: ordinary player contact at 4-6 m replica error must no
longer produce `hardSnap=1`; a real checkpoint/script displacement must log
`explicitTeleport=1` and still snap immediately. Long-lived collision divergence
may remain and will require authority policy, but it must not appear as a teleport.

### Player bump action is Crowd-probe driven, not Havok-contact driven (2026-07-02)

**STATIC:** the hands-forward/inside-bump reaction belongs to
`PlayerBumpController`, and enabling the Havok pair alone cannot start it.

Independent evidence:

1. `PlayerBumpController_Update 0x140CE7ED0` runs in campaign, builds a 64-byte
   probe result through `0x140CE1E20`, tries an authored action through
   `PlayerBumpController_TryStartBumpAction 0x140CE8E10`, then falls back to
   `PlayerBumpController_UpdateCrowdAdjustment 0x140CE9530`.
2. `0x140CE1E20` derives the Player transform/movement and calls `0x1406DD350` to
   produce a `Crowd::ProbeResult`; it does not consume Havok body-contact pairs.
3. The prior attribution of all bump strings to `0x140CE9530` was wrong.
   `PlayerBumpController_BuildBumpActionCandidate 0x140CE2520` builds the
   directional `hero-bump-%s%s%s-a/b/c` hashes. `0x140CE8E10` consumes its flags
   and starts `PlayerBumpAction` through `0x140CE39E0`. `0x140CE9530` is the
   fallback crowd-movement adjustment and owns `no-adjust-to-player`.
4. Runtime confirmed physical `4<->5` collision without this animation, exactly
   separating the two pipelines.

**STATIC refinement:** missing registration is rejected as the cause.

1. `Player` spawn method `0x1410D3440` calls
   `SimpleGridHashManager_RegisterObject 0x141E36910` and stores the returned id at
   `Player+0x2D8`.
2. Main Player update `0x1410EC230` calls the source-identified
   `SimpleGridHashManager_UpdateGridHash 0x141E37080`; if validation fails it
   registers the Player again. The reissued slot-1 object is a real Player using
   this same update path, so manually registering it would duplicate native state.
3. `Crowd_UpdateProbe 0x1406DD350` accepts a prior non-self candidate by a common
   actor capability or by `SimpleNpc`. The latter is a fallback, not a mandatory
   class. Its collision-query branch likewise accepts ordinary live actors with
   the required common bit.
4. `PlayerBumpController_IsContactEligible 0x140CDCC70` does not require
   `SimpleNpc`: it checks result distance at `ProbeResult+0x30`, contact geometry,
   cooldown, and only adds target-relative velocity checks when SimpleNpc data is
   available.

Competing explanations now left are: the physical layer-5 Player never appears in
the 64-byte `Crowd::ProbeResult`; it appears but contact geometry/cooldown rejects
the candidate; or `PlayerBumpAction` starts but loses to a later Player action.
A read-only hook at `0x140CE8E10` can distinguish these without forcing state:
zero target handle selects the first explanation, a valid handle plus false return
selects the second, and a true return without a gesture selects the third.

### Idle collision divergence is left uncorrected by the current replica policy (2026-07-02)

**CONFIRMED:** the first run after removing distance-only hard snaps did not
teleport, but retained a large visible positional error after contact.

- process `2244` stabilized at approximately `1.07 m` horizontal error;
- process `9904` stabilized at approximately `1.28 m` horizontal error;
- both reported `peerMove=0`, `hardSnap=0`, `explicitTeleport=0` and
  `correction=(0,0)` while the error remained constant;
- state roles were correct: A received on `31338`, B on `31339`. The separate
  legacy input receiver's `31337` bind failure is unrelated to state snapshots.

The cause is explicit in two independent code paths: both
`compute_remote_position_correction` and the free-run branch of
`apply_puppet_transform_post` refuse any correction while peer movement is zero.
This avoided the rejected idle locomotion-vector drag, but also made collision
divergence permanent.

**STATIC FIX:** retain input-vector correction only for genuine movement. At idle,
move only the remote transform toward the authoritative position with a `0.15 m`
deadband, `4%` error fraction and `0.015 m` per-update cap; do not modify velocity,
input or animation state. Explicit teleports and the `12 m` corruption fallback
remain unchanged.

Falsifiable prediction: the observed `1.07/1.28 m` idle residuals must decay toward
`0.15 m` without `hardSnap=1`, without a run animation at zero peer input, and
without visible metre-scale teleports. Persistent error means the local collision
solver cancels every post-update step and requires a single-authority collision
policy rather than a larger correction.

**CONFIRMED refinement:** the lower bump-action function is not reached. After
removing the local distance filter, both processes installed the read-only hook at
`PlayerBumpController_TryStartBumpAction 0x140CE8E10`, but emitted no
`BUMP-PROBE` entry during repeated Player contact. Therefore the failure occurs
before candidate classification; target geometry and action priority cannot yet
be blamed.

`PlayerBumpController_Update 0x140CE7ED0` has no direct code caller. Its only
relevant reference is the exact function pointer in vtable slot `0x142A0EF08`.
The update also begins under global byte `0x143E07473`. A read-only replacement of
that vtable slot now records whether the upper update runs and the gate value,
then calls the original unchanged. Competing outcomes are: no upper log means the
virtual update is unscheduled; logs with gate 1 select the global gate; logs with
gate 0 but no lower probe select an internal frame/controller condition.

### Moving replicas outrun the 0.05 m correction cap (2026-07-02)

**CONFIRMED:** the idle-only correction reduced the former `1.07/1.28 m`
post-collision residual to approximately `0.146-0.150 m`, matching its deadband.
It did not solve moving divergence. Across 604 later samples, horizontal error
averaged `0.490 m`, peaked at `5.913 m`, exceeded `1 m` 75 times and exceeded
`2 m` 52 times. The error repeatedly rose to `4-6 m` while moving and decayed
after stopping; `hardSnap=0` and `explicitTeleport=0` throughout.

Competing causes were stale transport, incorrect idle policy, and insufficient
moving convergence. Fresh movement values continued to arrive and the idle error
closed exactly to the configured deadband, rejecting the first two for this run.
The moving branch was capped at only `8%` and `0.05 m` per update. It is changed to
`20%`, maximum `0.20 m`, with a `0.10 m` deadband; velocity and the animation state
remain engine-owned.

Falsifiable prediction: ordinary movement must no longer accumulate multi-metre
error (target peak below `1 m`), idle residual must remain near `0.15 m`, and no
`hardSnap=1` may appear. New animation jerks with low numeric error would reject
the stronger cap and require velocity/time-based reconciliation instead.

### Joypad-3 background input must not write window focus (2026-07-02)

**STATIC:** the cursor capture after switching windows was introduced by the mod's
temporary write of `1` to `g_InputWindowFocused 0x1437EA62C` around every
destination-3 `Input_ScaleCopyVirtual 0x14183B410` call.

Independent evidence:

1. `Input_ScaleCopyVirtual` reads the byte at `0x14183B467` and exits at
   `0x14183C9C1` when it is zero; destination index is already live in `r12d`.
2. The native window-focus message path is the writer of the same byte.
3. The cursor-update path independently reads the same byte before calling
   `SetCursorPos` and `ClipCursor`. Thus even a short input-side write changes
   mouse ownership outside the intended conversion.

The replacement no longer writes the global. An exact-byte patch changes only the
focus test inside `Input_ScaleCopyVirtual`: destination 3 continues in a background
copy, while every other destination and the cursor path observe the real native
focus byte.

Falsifiable prediction: after switching windows, remote Joypad 3 movement remains
active but the cursor can leave the background game window. Loss of remote movement
means the local bypass is not reached; continued cursor confinement means another
native capture path exists independently of `g_InputWindowFocused`.

### Strong transform nudging produces visible post-stop sliding (2026-07-02)

**REJECTED:** increasing moving reconciliation to `20%`, maximum `0.20 m`, did
not make two independently simulated Players converge acceptably. The user saw the
replica stop roughly two metres before the local endpoint and then slide there
without locomotion animation. Logs agree: this run reached `6.103 m` error while
peer movement was non-zero and `7.822 m` overall; after `peerMove=0`, the idle
transform correction visibly consumed the remaining metres.

This rejects coefficient tuning as the position architecture. Input becomes zero
when the authoritative Player stops, while any later direct transform change has
no corresponding locomotion action. A larger coefficient can only make that slide
shorter and sharper; a smaller one makes it longer.

**STATIC replacement:** the former exact-position implementation is split into
its two independent effects. Exact post-update position previously tracked cleanly;
it lost run animation because the same block explicitly zeroed
`Player+0x260/+0x270`. Prior write tracing proved those fields are animation/IK
outputs, while the now-working Joypad-3 path independently starts the Player
locomotion graph. The new candidate therefore applies the exact authoritative
position after native remote-Player update but does not zero velocity, input, or
animation state.

Competing alternatives are continued coefficient tuning (rejected above), replaying
movement after the peer has stopped (would invent gameplay and collision), and full
native network animation/state decoding (still the long-term route). Exact position
plus native input is the narrow test that separates position authority from
animation ownership.

Falsifiable prediction: endpoint error must remain below one packet's visible
distance without post-stop sliding, while run/turn animations continue from the
native input graph. Frozen animation means exact position itself invalidates root
motion even without velocity clearing; packet-step jitter means interpolation must
be inserted before the exact apply.

### Player bump update runs, but its per-frame condition blocks the probe (2026-07-02)

**CONFIRMED:** the upper virtual `PlayerBumpController_Update 0x140CE7ED0` runs in
both processes and global byte `0x143E07473` remains zero, yet
`PlayerBumpController_TryStartBumpAction 0x140CE8E10` is never reached. This
rejects both an unscheduled controller and the global split/co-op gate.

**STATIC:** the hook argument is a `PlayerBumpController`, not a `Player`.
`controller+0x108` is the owning Player; the same field is used throughout the
probe builder. Before calling either probe builder, `0x140CE7ED0` reads a current
token through `Player+0xE0 -> +0x10` and compares it against controller `+0x140`.
Only the passing branch reaches `0x140CE1E20` and `0x140CE8E10`.

The next read-only build logs owner/retained slot and both token values per
controller. Prediction: if the remote controller alone reports `tokenPass=0`, its
construction/reissue path failed to initialize this scheduling token; if both slots
pass but the lower hook remains absent, the decompiler's compared offset must be
corrected from exact disassembly before any state change is attempted.

### Exact writes to both local and world position destroy root animation (2026-07-02)

**REJECTED:** exact post-update writes to both `Player+0x130` and `Player+0x240`
kept positional error at millimetre scale (`0.001-0.011 m`) but visibly broke the
remote Player's animations. Leaving `+0x260/+0x270` untouched was insufficient, so
the previous theory that velocity clearing alone caused frozen animation is false.

**STATIC root cause:** common Player/NadineBuddy virtual slot `+0x2B8`, now named
`Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320`, establishes the
actual ownership boundary:

1. It reads local root position `+0x130` and local rotation `+0x140`.
2. It combines them with the optional parent transform at `+0x110/+0x120`.
3. It writes the derived world position to `+0x240`, mirrors the local root to
   `+0x250`, and writes the root/capsule result to `+0x280`.
4. Earlier runtime testing independently showed that a one-shot `+0x130` write is
   not directly presented and is overwritten by native locomotion.

Thus the rejected implementation overwrote both the native root-motion input and
its derived output every frame. The next candidate leaves `+0x130/+0x250` entirely
native and applies authoritative network position only to the derived world output
`+0x240`, after the Player update.

Competing outcomes: preserved animation plus exact display position confirms the
input/output split; broken animation means later animation code consumes `+0x240`
too; visible flicker means `+0x240` is rewritten again after the current hook.
Falsifiable prediction: endpoint error remains below one packet step, while run and
turn animation remain intact. Collision may still follow the unsynchronized local
root and is evaluated separately.

### Crowd probe reaches both Players but returns no collision target (2026-07-02)

**CONFIRMED:** after correcting the hook argument from controller to
`controller+0x108` owner, `PlayerBumpController_TryStartBumpAction 0x140CE8E10`
was reached for retained slots 0 and 1. At a measured Player distance of
`0.851872 m`, every call reported null target handle/id, `ProbeResult+0x30 =
FLT_MAX`, and `actionStarted=0`. Therefore scheduling, the global gate, action
priority and downstream animation selection are not the current failure. The
upstream Crowd spatial query omits the other Player.

**STATIC candidate filter:** the new-candidate loop in
`Crowd_UpdateProbe 0x1406DD350` accepts a collision-query result only when:

- the process handle/id is valid;
- the candidate has the capability bit whose runtime index is stored at
  `0x143D0A724`;
- `0x1406DD290` accepts its process state;
- it is not the probing actor itself.

The two Players are already registered in `SimpleGridHash` and physically collide,
so missing capability or the process-state predicate are the remaining concrete
filters. The next build only logs the relevant capability bit for the opposite
Player; it does not mutate the capability registry. If the bit is absent in both
directions, a narrowly scoped temporary capability during the Crowd query becomes
the next candidate. If present, analysis moves to `0x1406DD290` and the collision
query client rather than fabricating a `ProbeResult`.

### Remote movement was written after its consumer (2026-07-02)

**REJECTED:** writing only derived world position `Player+0x240` after the Player
update did not preserve translation. The remote model played its run animation in
place. A subsequent closed-loop chase also failed: logs showed a full movement
vector and magnitude while horizontal error remained approximately `4.15-4.56 m`.
Increasing chase strength therefore cannot fix this result.

**STATIC root cause:** the chase was installed at the return of Player vtable
method 16, after the relevant values had already been consumed:

1. `Player_UpdateInputCommandState 0x140C9F8B0` makes its sole direct call to
   `PlayerInputCommandState_Update 0x141E39FB0` at `0x140C9FE4F` with
   `Player+0x39A0`.
2. That callee derives raw movement at `Player+0x39C8`, magnitude at `+0x3A1C`,
   and world direction at `+0x3A30`; their established downstream reader is
   `PlayerMoveStick_MainUpdate 0x140F03500`.
3. Before method 16 returns, it calls the broader Player action/input consumer
   from `0x140C9FE76`. The former vtable wrapper changed the three fields only
   after this call and could therefore log the intended values without affecting
   that frame's native movement.

Competing explanations are: the call-order error is sufficient; the call at
`0x140C9FE76` does not consume movement; or another later stage overwrites the
result. The new narrow hook replaces only the five-byte direct call at
`0x140C9FE4F`: it executes the original derivation and then copies the peer's
native movement fields before control returns to the consumer. No ordinary
position correction is active in this test.

Falsifiable prediction: moving the peer must make the remote `Player+0x130` and
`+0x240` advance in the same direction, so the model no longer runs in place.
Position error may remain because reconciliation is intentionally disabled. If
`PRE-CONSUMER-MOVE` reports non-zero fields while the root remains fixed, the
consumer hypothesis is rejected and the next static pass must trace the later
reader rather than tune correction coefficients.

### Pre-consumer movement drives animation but not Character root motion (2026-07-02)

**REJECTED:** inserting the peer movement directly after the sole call to
`PlayerInputCommandState_Update` at `0x140C9FE4F` did not make the remote Player
translate. Both processes logged non-zero cardinal/diagonal input, magnitude `1`,
and changing world direction at `PRE-CONSUMER-MOVE`, while the remote
`Player+0x130` changed only by centimetres and the user observed running in place.
Therefore neither later timing in Player method 16 nor stronger movement-vector
values can solve the missing root motion.

**STATIC:** the actual Character root-application boundary is separate from
`PlayerMoveStick_MainUpdate`:

1. Shared Character caller `0x14042FB40` directly invokes
   `Character_ApplyAnimationRootTransform 0x141DE8EC0` every frame.
2. Its direct branch computes a new local transform and writes position to
   `Character+0x130` at `0x141DE9608` and rotation to `+0x140` at
   `0x141DE9610`, then calls virtual `+0x208` and `+0x1E8`.
3. Before that branch, it calls Character virtual `+0x5E8`. Player resolves this
   slot to `Player_ShouldSkipDirectRootTransformApply 0x141BC2EF0`; a true result
   bypasses the direct root write. The predicate consults the global gameplay
   manager and `Player+0x348`, identified by the initialization source string as
   `m_pNetController`.
4. `PlayerBase_SetupNpcAvoidanceCheck 0x140CC08F0`, formerly suspected as the
   final movement output, is rejected by its exact source string and writes only
   the NPC-avoidance helper at `Player+0x1A20`.

Competing explanations are now: the remote Player alone returns true from virtual
`+0x5E8`; both pass but the remote animation root source has zero translation; or
both receive a non-zero root delta and a later collision/ownership stage rolls it
back. A read-only call-site hook around `0x141DE8EC0` records the predicate,
`m_pNetController`, and root before/after for retained slots 0 and 1.

Falsifiable prediction: a remote-only `skip=1` identifies the ownership gate. If
both report `skip=0` but only local has a material root delta, the missing state is
the animation-root source feeding `0x141DE8EC0`. Equal non-zero deltas followed by
a stationary remote model instead select a later physics rollback. No field is
modified by this probe.

### Player uses a separate post-animation path; authoritative root must precede `+0x2B8` (2026-07-02)

**REJECTED:** the read-only call-site probe at `0x14042FBBA` produced no retained
Player hits while both copies moved. The latest run again left each remote Player
at its spawn point, including collision, even when the local counterpart had moved
away before the peer loaded. Therefore `0x14042FB40 -> 0x141DE8EC0` is not the
active Player scheduler and its virtual `+0x5E8` predicate must not be changed.

**STATIC:** RTTI and exact vtable-slot resolution separate the class paths:

1. `0x141DE8EC0` occupies `NavCharacter` and `AiCharacter` vtable `+0x400`.
   `Npc`/`NadineBuddy` override that slot with `0x14042FB40`; `GameCharacter`
   uses wrapper `0x1403D13E0`.
2. `Player` vtable `0x142A4BFE8 + 0x400` resolves instead to
   `Player_PostAnimUpdate 0x1410E9210`, which contains the exact source assertion
   `Player::PostAnimUpdate_Async` and does not call `0x141DE8EC0`.
3. The shared Player/NadineBuddy vtable `+0x2B8` method
   `Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320` reads local
   root `+0x130`, derives world output `+0x240`, mirrors `+0x250`, and writes the
   collision/root output `+0x280` in one pass.
4. The current mod writes peer `+0x240` only after this method and, in free-run
   mode, only performs a root write after a 12 m error. That explains both the
   visible spawn-point replica and stale collision under ordinary separation.

Competing hypotheses: a missing Player input owner prevents native integration;
late `+0x240` replacement is overwritten and misses collision; or the peer world
position must be converted through the parent transform before entering `+0x130`.
The first remains relevant to fully native locomotion, but cannot explain why an
authoritative transform mirror should remain at spawn. The implementation now
refreshes `+0x110/+0x120`, converts peer world position into parent-local space,
and writes `+0x130` immediately before `0x141BB8320`.

Falsifiable prediction: after both saves load, moving either local Player must move
its remote representation away from spawn in the other process, and the other
Player must no longer collide with the old spawn position. If the model moves but
collision remains at spawn, `+0x280` is not the collision consumer; if neither
moves while `+0x130` changes, a later writer overwrites the local root before
`0x141BB8602`.

### Exact root round-trip diagnostic separates conversion from later mutation (2026-07-02)

**STATIC:** the remaining multi-metre `PUPPET-POS` residual cannot yet be treated
as packet interpolation error. Three independent code paths agree on the transform
contract:

1. `Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320` loads
   `Player+0x130` once at entry, refreshes the parent pair `+0x110/+0x120`, and
   writes the derived world position directly to `+0x240` at the end. It does not
   overwrite `+0x130` in between.
2. `0x14014E240` is the engine's forward transform helper used by that function:
   it rotates local `+0x30` by parent quaternion `+0x20` and adds parent position
   `+0x10`.
3. `RefreshEmbeddedPlayerAuthoredDescriptorFromCompactCarrier 0x1416862B0`
   contains the inverse operation inline. Its XOR with sign mask
   `0x142880610 = {-0,-0,-0,+0}` confirms quaternion order `(x,y,z,w)` and
   conjugation of xyz before rotating `world-parent` into local space.

Competing hypotheses are now: the scalar inverse differs from the engine's exact
convention; the original method derives a different result after the pre-write; or
the packet target itself is stale/wrong while both transforms are internally
consistent. A read-only diagnostic records the packet target, calls the engine's
own `0x14014E240` after the local write to obtain a round-trip prediction, then
records actual `Player+0x240` after `0x141BB8320`.

Falsifiable prediction: a large target-to-round-trip error selects the scalar
inverse as the fault. A near-zero round-trip error followed by a large actual
error selects mutation inside or after `0x141BB8320`. If both errors are near zero
while the visible actor remains displaced, the transmitted `Player+0x240` is not
the common cross-process world coordinate and the packet source must be revised.

**CONFIRMED:** both processes produced exact equality in motion and at rest:
`target == predicted == actual`, with `roundTripHorizontal=0`,
`roundTripVertical=0`, and `parentQNormSq=1`. Thus the scalar inverse, engine
forward transform, and final `0x141BB8320` write are correct. The earlier
multi-metre residual belonged to the superseded build and is not present in this
run.

The remaining visible jitter is now isolated above the transform layer. During
movement the diagnostic sampled only about 10-12 new sequence numbers per 500 ms,
while `latest_peer_state` returned the newest packet unchanged on every render
frame. Competing causes were incorrect coordinate conversion, a later root
overwrite, or discrete packet stepping. The first two are rejected by the exact
round trip; discrete stepping remains and is also directly visible in the receive
and apply code.

The next build retains the two latest packets and interpolates position using
receiver-local arrival times with a 75 ms display delay. Sender timestamps are not
used because independent PCs do not share `GetTickCount64` epochs. Prediction:
root round-trip error remains zero, motion advances between packet arrivals rather
than holding and jumping, and release reaches the final confirmed position within
75-100 ms. Persistent animation jitter with smooth position would instead isolate
the animation/input stream as a separate fault.

**REJECTED:** the 75 ms two-packet interpolation did not materially reduce the
visible running jitter for either Chloe or Nadine. It smoothed the target of the
exact root pin, but retained the wrong ownership model: packet position still
replaced native locomotion every update.

Git history isolates the regression boundary. Commit `25637e8` was the last
runtime-validated smooth state (`0.10/0.14 m` residual): native slot-1 locomotion
owned position and the network applied at most `0.05 m` correction while moving.
Commit `a06c79c` then enabled Player layer `4<->5` collision, after which the two
independent physics simulations diverged. Attempts to repair that divergence ended
in `21d1958`, which replaced the bounded correction with exact packet position.
The later `+0x130` pre-write made that exact pin structurally consistent but did
not remove its packet-rate stepping.

Competing restoration hypotheses are: collision is the primary source of drift;
the bump probes disturb scheduling; or exact root ownership alone causes jitter.
The next build returns the complete movement policy to `25637e8` and disables all
three later additions together: layer `4<->5` collision, both bump probes, and the
experimental pre-consumer/root hooks. Prediction: ordinary running becomes smooth
again and residual returns near the earlier decimetre range; player-player
collision and bump animation will intentionally be absent. Failure would prove a
later non-collision change also participates and require a narrower commit bisect.

**CONFIRMED:** the control build removed the running jitter, selecting exact root
ownership as its cause. It also exposed `2.98 m` and `3.42 m` stable idle errors
and visibly wrong body direction. Collision and bump probes were absent, so they
are not required for either remaining defect.

The control rollback omitted an important implementation transition. In
`25637e8`, the slot-16 wrapper replaced `Player+0x39C8/+0x3A1C/+0x3A30` with the
sender's movement result plus bounded position correction. Later work moved that
replacement to the call at `0x140C9FE4F`, before the same-frame consumer, and
reduced the old wrapper to diagnostics. Parking the newer hook therefore did not
restore the old behaviour: it left the remote Player using its receiver-local
camera basis again.

Three independent facts select this cause: packet v4 still carries the sender's
world vector; `0x140C9FE4F` is immediately before its movement consumer; and the
current remote slot-16 logs show no peer replacement while positional drift grows.
The next build re-enables the pre-consumer hook, adds the established bounded
correction there, and lets native locomotion own facing while moving. Quaternion
blending remains only for idle alignment. Prediction: world direction and visible
body facing match the sender, moving error converges, and direct-position jitter
does not return.

**CONFIRMED:** the prediction passed in both processes. The user reported smooth
running, correct direction and visually excellent synchronization for Chloe and
Nadine. Final idle diagnostics measured:

- PID `17276`: horizontal `0.0433 m`, vertical `0.0020 m`;
- PID `11804`: horizontal `0.0712 m`, vertical `0.0041 m`;
- both: `peerMove=0`, `hardSnap=0`, `orientationDot=1`.

This is the current stable movement baseline and supersedes the exact-root and
two-packet interpolation branches. Its required ownership rules are:

1. `0x140C9FE4F` pre-consumer replacement supplies sender world movement and the
   bounded correction before `PlayerMoveStick_MainUpdate` consumes it.
2. Native remote-Player locomotion owns position and moving body facing.
3. Post-update position changes are limited to the validated moving-only nudge:
   `8%` of error, maximum `0.05 m`, deadband `0.03 m`; only errors above `4 m`
   use the emergency snap.
4. Network quaternion blending runs only below movement magnitude `0.15`, so it
   aligns idle facing without fighting locomotion turns.
5. Player layer `4<->5` collision, bump probes, and authoritative `+0x130` root
   writes remain parked. Re-enabling any of them requires an isolated prediction
   and must preserve this baseline as the control case.

### Native weapon appearance snapshot path (2026-07-02)

**STATIC:** visible weapon state is a structured snapshot, not a single current
weapon id. Three independent paths agree:

1. `NetEventCharacterMove_FillWeaponState 0x140BC2240` reads eight slots from the
   live `Player+0x1D88` weapon controller. It writes compact art ids to
   event `+0x3DC..+0x3F8`, per-slot state to `+0x3FC..+0x403`, selected modes to
   `+0x404..+0x414`, and four detailed weapon records at `+0x440..+0x4DC`.
2. `NetEventCharacterMove_ToNetSnapshot 0x140BC11A0` copies those fields into
   snapshot `+0x3F4..+0x504`; the bit serializer independently confirms eight
   7-bit ids, eight 8-bit states and the selected/throwable fields.
3. `NetAnimCmdGenerator_UpdateFlagsAndWeapons 0x140A07160`, specifically
   `0x140A085AF..0x140A08859`, rebuilds `NetSnapshotWeapons` from exactly those
   offsets and publishes it through `PublishStructuredStateBlockToActorVirtualD0
   0x1417F8910`.

Competing explanations were rejected: `weapon+0x218` is not an id; repeatedly
publishing bare inventory ids through Player virtual `+0xD0` only cycles inventory
maintenance and never selects a visible held/holstered model; the full animation
bitstream remains blocked by absent campaign multiplayer art-group tables.

`StatePacket` v5 now transports the contiguous native weapon region
`NetSnapshot+0x3F4..+0x504`. The receiver reconstructs the exact 0xFD-byte
`NetSnapshotWeapons` payload and republishes it only when changed. Runtime
prediction: switching a local weapon makes the remote Player show the same model
and placement without changing movement. Inventory-only change means the separate
`NetSnapshotGunHolstered` flag is still required; no change rejects the recovered
event-parameter descriptor. Runtime status: **HYPOTHESIS**, not yet tested.

### Weapon attachment region `Player+0x3500..+0x3640` and joypad gate (2026-07-03)

**STATIC:** `Player_KickPlayerDepenetrateWeapons 0x1410EDFB0` is the consumer of
the wrist-attachment xform block around `Player+0x3500..+0x3640`. It resolves
skeleton joints (`targRWrist`, `targLWrist`, `rightHand`, `leftHand`), computes
world-space offsets, and writes results through the embedded object at
`Player+0x3530` (vfunc `+0x10` with mode args `(1,1,0,7,0,0)`, then vfunc `+0x18`
into heap buffers at `Player+0x3600/+0x3608`).

It is reached every frame from `sub_1410EC230` when `Player+0x162A` bit 0 is clear
(the main per-frame Player world-update path that also runs grid-hash registration
and `Player+0x3680` sibling updates).

**STATIC:** before the optional vfunc `+0x20` refresh on the `+0x3530` object,
`InputObject_MatchesActiveLocalJoypad 0x14023AEA0` gates updates:

- requires flag bit `0x80000` at `object+0x44`;
- compares `object+0x50` (joypad index) against
  `g_LocalPlayerInputManager_Ptr+0x48/+0x50` (active local joypad slots);
- returns false for non-active joypads.

Competing explanations for the runtime dump from the prior commit:

| Candidate | Evidence against |
|-----------|------------------|
| Missing heap pointers at `+0x3598/+0x3600` | REJECTED: pointers present on both local and clone |
| Copying attachment xforms from peer is enough | REJECTED: float fields are `0x7fbbeeee` on clone while structs exist |
| `NetSnapshotWeapons` alone equips visible weapon | WEAK: routes to `Player vtable+0xD0` → `WeaponController_ApplyInventoryEntry`, but attachment xforms are a downstream draw pipeline |

**STATIC:** full visible equip also runs `WeaponController_ApplyInventoryEntry
0x141E72540` → `sub_141EB1E00` → `sub_141E74270` → `sub_141E35600`, which
materializes runtime weapon state through `SsTrackGroup` /
`ResolveOrMaterializeRuntimeStateObjectFromSelectorPacket 0x1417C3790` — the same
machinery `give-weapon 0x140F95850` ends in via
`PublishStructuredStateBlockToActorVirtualD0 0x1417F8910`.

**Current blocker chain:**

1. Sender had `have_weapon_snapshot=false` hardcoded — weapon flag `0x20` never set.
   Fix: capture via `resolve_live_player_safe(0)` (retained proxy throws in
   `FillWeaponState`).
2. Receiver `publish_peer_weapon_snapshot` may update inventory but attachment
   xforms stay uninitialized unless the clone passes
   `InputObject_MatchesActiveLocalJoypad` or the equip/materialize path runs on
   the clone itself.

Runtime prediction for next test: after re-enabling live-player capture, sender
logs `weapon snapshot captured from live player` and receiver logs
`remote NetSnapshotWeapons published`; if the gun still does not appear, next
static target is bypassing or satisfying `0x14023AEA0` on the slot-1 clone
(joypad 3 vs manager slots 0/1). Status: **HYPOTHESIS**.

**REJECTED correction:** bypassing `InputObject_MatchesActiveLocalJoypad
0x14023AEA0` is not the next weapon-visibility fix. Disassembly at
`0x1410EEA62..0x1410EEBA9` shows that this predicate gates only the optional
`Player+0x3530 vtable+0x20` refresh. Irrespective of its result, the function
continues to write both computed wrist transforms through `Player+0x3600/+0x3608`
and calls `Player+0x3530 vtable+0x18`. The uninitialized clone values therefore
mean that the producing update or its joint inputs are absent; they do not prove
that the joypad predicate blocks attachment output.

### Campaign-safe weapon inventory capture (2026-07-03)

**STATIC:** the useful prefix of `NetEventCharacterMove_FillWeaponState
0x140BC2240` can be reproduced without entering its failing multiplayer tail:

1. Disassembly `0x140BC22F0..0x140BC2646` reads `Player+0x1D88`, calls manager
   virtual `+0xB0` for slots `[0,1,16,17,2,3,20,22]`, reads each
   `Weapon+0x860`, maps it through `WeaponArtId_ToNetworkIndex 0x140A80E50`, and
   reads per-weapon state through virtual `+0x840`.
2. The campaign fire core `PlayerFireController::Fire 0x140E2C730` receives the
   active Weapon object directly and independently reads the same `Weapon+0x860`;
   runtime already confirmed this path for both local roles while `Player+0x218`
   remained null.
3. `NetAnimCmdGenerator_UpdateFlagsAndWeapons 0x140A085C3..0x140A08859`
   consumes the same ordered eight compact ids, eight state bytes, selected mode
   and four 0x28-byte detailed records when building `NetSnapshotWeapons`.

The sender now reproduces only this campaign-live prefix. It also copies the
direct weapon record fields `+0x866..+0x881` and virtual `+0x8F0` state for slots
`0/1/16/17`; it does not call the full failing helper. Competing hypotheses remain:
the reconstructed message creates the remote inventory and visible models; it
creates inventory but still needs a separate draw/holster transition; or the
recovered Event parameter descriptor is wrong and the receiver rejects it.

Falsifiable prediction: both senders log a nonzero captured snapshot, both
receivers log one successful `NetSnapshotWeapons` publication, and changing the
selected weapon changes at least the remote inventory/model. A publication with
no model selects the missing draw/holster transition; no publication despite
nonzero ids rejects the Event descriptor. Runtime status: **HYPOTHESIS**.

**REJECTED runtime result:** directly calling `WeaponArtId_ToNetworkIndex
0x140A80E50` from the campaign capture fails consistently at stage `20` (slot 0)
in both processes. The preceding manager `vtable+0xB0` call succeeds and returns a
readable Weapon, so the failure is isolated to the mapper. Its static body explains
the difference: after searching the fixed 19-entry table at `0x143E04A00`, an
unknown id enters a lazily resolved `*weapon-gameplay-defs-online*` registry path.
That multiplayer-flavoured fallback is not safe to invoke independently here.

The capture now reproduces the mapper's first, self-contained branch directly:
compare `Weapon+0x860` against the 19 standard ids and use the matching table
index. Index zero is valid and no longer mistaken for an empty slot; the existence
of the returned Weapon marks the entry present. Dynamic/online-only weapon ids are
deliberately deferred. New prediction: ordinary campaign weapons produce a sender
snapshot without stages `20..27`; failure to map all entries without an exception
means the current weapon belongs only to the dynamic registry.

**STATIC follow-up:** the inverse routine `0x140A81050` resolves indices `>=19`
through the same cached object and contains source assertions naming
`NetGameManager::GetWeaponIdByStatIndex` (`net-game-manager.cpp:534-535`). It
reads a count at cached-object `+0x0` and an art-id array at `+0x8`, while
`WeaponArtId_ToNetworkIndex 0x140A80E50` returns the matching dynamic index plus
19. The cache handle itself is the first qword at `0x143742720`; both routines
share it. This independently confirms that the fixed 19-entry table is only a
prefix, not the complete campaign mapping.

Competing explanations for the silent fixed-table test are now explicit:
(H1) the live campaign weapon id is present in the already materialized dynamic
registry; (H2) `manager vtable+0xB0` returned an object whose `+0x860` is not a
valid authored weapon id; (H3) the dynamic cache is absent in campaign. The next
probe does not call either registry initializer: it records all eight raw ids and
reads the cached `{count, ids*}` only when already valid. **Prediction:** H1 gives
a positive bounded count, a matching bit and a published snapshot; H2 gives
implausible/changing raw ids; H3 gives stable plausible raw ids with
`registryCount=-1` and no mapped bits. This distinguishes all three without
changing gameplay or booting multiplayer state.

**REJECTED / CONFIRMED runtime result:** both campaign instances report the same
stable authored ids (`8075397D`, `33E29A1B`, `0B8BD6D6`) while the cached dynamic
map remains absent (`registryCount=-1`, mapped mask zero). Weapon switching does
not alter this inventory set. This confirms H3 and rejects H1/H2: `Weapon+0x860`
is valid, but compact `NetSnapshotWeapons` ids cannot be produced in campaign
through the dormant multiplayer map.

**STATIC replacement path:** `0x1410A4D40` is
`PlayerWeaponController::GiveWeapon(WeaponInfo*,bool,bool,BuffSource*)`; the exact
signature and source file are embedded at `0x142A47990`. It is controller vtable
slot `+0x158` in the table beginning at `0x142A47F90`. The same table's `+0xB0`
slot is `0x1410A1F90`, which bounds the 23 inventory slots and returns the live
weapon process whose authored id is `+0x860`. Independently, developer-menu path
`0x140EDC710` constructs a 0xC0-byte `WeaponInfo` using `0x1401B7600`, writes the
authored id at `WeaponInfo+0x80`, and publishes campaign event `give-weapon` to
the Player. This is the native campaign path and does not need compact ids.

Competing outcomes for the first direct-controller probe: (G1) the puppet owns a
normal PlayerWeaponController and silent `GiveWeapon(info,true,false,null)` adds
the remote inventory/models; (G2) its controller is absent, producing the
explicit unavailable log; (G3) its controller exists but lacks campaign art
registries, so the call returns without a visible weapon. **Prediction:** G1
logs three unique authored ids given and the other window gains at least one
weapon model without changing movement; G2 logs no call; an exception rejects
the direct call ABI. Raw ids use custom packet flag `0x40`; the rejected compact
event remains isolated under `0x20`.

### Nadine companion look lacks player weapon-holster prop-attach configs (2026-07-03)

**STATIC:** appearance-hash swap via Player `vtable[92]` (`hooked_appearance`,
`0x140A1DCC0`) reaches the look builder `0x141BB4280`, which changes skeleton,
joint overrides and prop-attach — not just the mesh. Runtime look candidates were
exhausted: engine `StringId` hash confirms the domain (`chloe` `0x3f334972`);
real `nadine` `0xc7e2539b` black-screens; only legacy `0xd27c074f` renders Nadine
(with broken weapons). Look-swap is **REJECTED** as a fix.

**STATIC:** holstered weapons pile at the hand because the companion look does not
define player weapon-holster prop-attach configs. Per-frame attach path:
`Player_Update` `0x1410EC230` → joint resolver `0x1410B5560` resolves only
standard humanoid joints (`targSpine`, `targChest`, … — all present on Nadine).
Holster placement is standard joint + per-weapon offset from the look's
prop-attach table; missing configs fall back to the hand. Held weapon is correct;
holstered weapons overlap at the hand.

**PARKED:** asset/config-level fix only. Options: per-weapon draw suppression
(weapon-draw list still unfound), deep inject of holster prop-attach into Nadine's
look struct, or player-2 = Chloe. Authored `GiveWeapon` and `NetSnapshotWeapons`
publish remain gated off on the Nadine clone (weapons spill / cannot switch).

### Fire event UDP relay scaffold (2026-07-03)

**STATIC:** `PlayerFireController::Fire 0x140E2C730` is the campaign fire core;
`controller+0x108` owns the `Player`; R8 is the active weapon (`Player` vtable
`+0x8C0`); RDX is `FireArgs` (`0x100` bytes). `NetEventEmitProjectileBullet` is
not used in campaign. Per-character `Player`→fire-controller offsets: Chloe
`+0x15cf30`, Nadine `+0x15adb0` (owner at `controller+0x108` must equal `Player`).

**HYPOTHESIS (v2 fix, 2026-07-03):** `FirePacket` v2 carries sender
`controller_offset`; receiver `resolve_fire_controller` uses it then falls back to
known offsets so Nadine puppet no longer depends on Chloe-only global. Before replay,
`find_weapon_slot_by_art_id` + `SwitchToWeapon(in_hands=true)` aligns puppet weapon;
`FireArgs` pointer QWORDs at `+0x20..` zeroed. Falsifiable prediction: after local
fire, peer log shows `FIRE-RELAY applied seq=N ctrlOff=0x...` (not only `learned`).
Prior v1 runtime: learned offsets logged, zero applies (wrong global offset on puppet).

### Held-weapon draw path: SwitchToWeapon + inferred gun state (2026-07-03)

**STATIC:** `Player+0x15C8` gun-state controller (`SkillWaitOnGunState 0x14041C670`)
is not callable on campaign live players — SEH returns `-1` on both sender and
puppet. `GiveWeapon` alone only equips inventory/back; visible in-hands requires
`PlayerWeaponController::SwitchToWeapon 0x1410A1C10` (controller vtable `+0x40`).
`vtable+0x78` resolves to `0x1410A49E0`: it scans 18 inventory slots and only
returns one whose attachment joint is `rightHand`/`leftHand` and whose attachment
owner is this Player. Separately, `vtable+0x90 = 0x1410A4CE0` merely returns
controller `+0x130`, the slot requested by `SwitchToWeapon`; this does **not** prove
visual attachment. `find_weapon_slot_by_art_id` reuses manager `vtable+0xB0`.

**STATIC CORRECTION:** Cursor's `hands=1` log read `controller+0x130`, so it only
proved that the request was recorded. The receiver now accepts draw/fire readiness
only when `0x1410A49E0` reports the exact expected slot physically attached to a
hand. The previous early success based on `+0x130` is removed so a stalled request
can be retried through the native switch state machine.

**HYPOTHESIS:** sender `infer_gun_state` (aim `0x80000` OR in-hands slot > 0) yields
`gun=0/1` in `HELD-WEAPON-SEND`; receiver `safe_switch_to_weapon(slot, in_hands)`
after give produces `HELD-WEAPON-APPLY select=1 gunOk=1` and remote puppet shows
weapon in hands on peer draw/aim. Failure: SwitchToWeapon rejects puppet controller
(no net tracker / cover gates in `0x1410A1C10`), or slot 0 in-hands ambiguity
needs aim-bit-only inference.

### Fire relay button path — partial runtime (2026-07-03, PAUSE)

**CONFIRMED:** sender hooks `PlayerFireController::Fire`, queues `FirePacket`, receiver
logs `FIRE-RELAY button pulse=0x90000` and `REMOTE-ACTIONS branch=0x90000`.
Early pistol `return` when `!fire_ready` removed; art-mismatch strip relaxed during
`g_fire_aim_hold_until_ms` (400ms).

**CONFIRMED:** puppet slot-1 uses joypad index **3** (`Player+0x39B8`). Resolved input
blocks live at `RVA 0x66E4220 + 0x530 * joypad_index` (not `+0x530` = block[1] only).
Prior `BLOCK1 btn=0x0` diagnostics were reading the wrong block.

**CONFIRMED:** `safe_give_weapon_art_to_puppet` + `SwitchToWeapon` can yield
`HELD-WEAPON-APPLY gave=1 select=1 gunOk=1 hands=1` for pistol `0x33e29a1b`; puppet
inventory snapshot (`WEAPON-CLONE ids=`) may still read all zeros (different capture path).

**REJECTED (for now):** `g_native_fire_relay=true` — `PlayerFireController::Fire` replay
on puppet → `EXCEPTION rip=0x0` (pistol) / crash (rifle). Button-only path kept.

**REJECTED:** button delivery via `branch1+0x10` alone — does not produce non-zero
`block[N]+0x518` on puppet joypad 3 in runtime (`block3 btn=0x0` after fix attempt
`push_joypad_block_buttons` direct write to `+0x518/+0x508`).

**STATIC CORRECTION:** direct writes to resolved `block+0x508/+0x518` are invalid at
this point in the frame. `FrameState_PerSlotInputApply_Branch 0x1402893D0` reads a
**DWORD** from source `+0x10`, derives pressed/current/released masks, writes them to
destination `+0x508/+0x518/+0x510`, then immediately calls
`Input_ScaleCopyVirtual 0x14183B410`. Therefore a pre-apply direct block write is
overwritten. Cursor's QWORD write to `branch+0x10` was also structurally wrong: it
overwrote the adjacent left-stick X float at `branch+0x14`. The implementation now
writes only the native DWORD and leaves edge derivation to the engine.

**HYPOTHESIS (next):** the existing `Input_ScaleCopyVirtual` hook is the last native
boundary before Joypad command resolution and already preserves received edges in
Joypad `+0xD0`. If fire still has no visual after the corrected DWORD source write,
the remaining alternatives are the fire command being disabled for the puppet, or
the selected weapon lacking a valid active fire controller; direct resolved-block
patching is no longer a candidate.

### Fire-controller gate isolation (2026-07-03)

**CONFIRMED:** the corrected DWORD source path reaches the clone's actual Joypad 3.
The same received pulse produces `REMOTE-ACTIONS branch=0x90000` followed by
`JOYPAD3-ANALOG buttons src=0x90000 pressed=0x90000 current=0x90000`, yet no shot.
This rejects packet loss, the per-slot input apply, and a wrong Joypad index as the
current cause.

**CONFIRMED:** `PlayerWeaponController::SwitchToWeapon` completion is asynchronous.
For pistol `0x33E29A1B`, runtime first showed `requested=1 attached=0`, then later
`requested=1 attached=1` with no changed peer weapon packet. The receiver's unchanged-
packet early return prevented the later physical attachment from arming `gunOk`.
The implementation now continues polling only while attachment/readiness is pending;
the existing 500 ms switch throttle remains. Rifle `0x8075397D` separately resolves
to no remote inventory slot and is not part of this pistol test.

**STATIC:** `PlayerFireController_UpdateInputAndFire 0x140E28A30` is referenced by
the fire-controller table and performs the native per-frame chain. It obtains the
active weapon through Player vtable `+0x8C0 = Player_GetActiveWeapon 0x1410D67B0`,
reads the owner's Joypad commands into controller `+0x1E8` (hold), `+0x1E9` (press),
and `+0x1EA` (release), calls PlayerWeaponController vtable `+0x260 =
PlayerWeaponController_CanUseWeapon 0x1410B15A0`, then evaluates the input and final
shot-readiness gates.

**STATIC:** `PlayerFireController_CheckFireInput 0x140E2ACA0` independently resolves
the active weapon, validates its state, consumes `+0x1E8` or `+0x1E9` according to
weapon definition, and tail-calls `PlayerFireController_CheckActionExclusions
0x140E2ADF0`. The latter rejects authored `Grenade`, `Grenade-Add`, `Grapple`,
`Grapple-Add`, and `gun-up-layer` action states. `PlayerFireController_CheckShotReadiness
0x140E2B220` again resolves the same active weapon, rejects `Drive` and incompatible
player/controller states, then branches by weapon type to ammo and shot-delay checks.

Competing hypotheses for the next runtime are now distinguishable: (F1) the pulse
does not become fire-controller `+0x1E9`; (F2) it does, but input/action validation
returns false; (F3) input validation succeeds and final weapon readiness returns false;
(F4) both return true and failure is later than readiness. Read-only detours log
`FIRE-GATE input/readiness result=... hold=... press=...` without altering decisions.
**Prediction:** one remote trigger pulse must produce `FIRE-GATE input`; its fields and
the optional readiness line select exactly one of F1-F4. Status: **HYPOTHESIS** pending
runtime confirmation.

**CONFIRMED / REJECTED runtime:** the remote process produced no `FIRE-GATE` entry,
although its Joypad 3 logged the synthetic `pressed/current=0x90000`. In the same run,
ordinary local shots produced `FIRE-GATE input result=1`, `readiness result=1`, then
`PLAYER-FIRE`. This selects F1 and rejects weapon/action/readiness gates as the first
current failure.

**STATIC + live authored data:** `Joypad_IsCommandActive 0x141837CD0` resolves the
command hash through `Joypad_ResolveCommandBindings 0x14183AE30`, then
`Joypad_EvaluateCommandDescriptor 0x141838050` dispatches its gamepad list to
`Joypad_EvaluateGamepadButtonBinding 0x141838C90`. The latter reads Joypad pressed,
released and current masks at `+0xD0/+0xD8/+0xE0`. Its binding types are concrete:
type 2 tests current, type 4 tests pressed, and type 8 tests released.

The engine CRC/StringId algorithm was independently checked against known
`chloe=0x3F334972`, then gave `fire-hold=0x4F2051E6`,
`fire-press=0x85B15733`, and `fire-release=0x0B511B85`. Binary search through the
live 243-entry authored command table found all three exact records. Their first
gamepad descriptors are respectively `{mask=0x800,type=2}`, `{mask=0x800,type=4}`,
and `{mask=0x800,type=8}`. `aim-press=0x0152AF70` independently resolves to
`{mask=0x200,type=4}`. Thus the old generated `aim=0x80000/fire=0x10000` masks are
**REJECTED**; they can exist in Joypad state without satisfying either command.

**STATIC scheduling correction:** `PlayerFireController_UpdateInputAndFire` queries
`fire-*` after the Player input-state update. The receiver previously exchanged and
cleared the entire synthetic pressed latch immediately after
`Player_UpdateInputCommandState`, so even a correct fire bit would lose its `+0xD0`
edge before the fire controller. Only the `0x800` fire edge is now deferred and
cleared by `PlayerFireController_CheckFireInput`; all movement/traversal edges retain
their prior lifetime. The 400 ms helper window holds only aim `0x200`, not fire.

Falsifiable prediction: one relayed shot creates remote Joypad
`pressed/current=0xA00`, followed by `FIRE-GATE input press=1`. If input returns 1,
the existing readiness diagnostic must either return 1 and reach native Fire, or
identify the next weapon gate. Status: **HYPOTHESIS** pending runtime.

### Co-op Aim Animation vs Viewport HUD (STATIC, 2026-07-03)

**STATIC:** Puppet aim pose requires sustained authored aim-press `0x200` on the
per-slot input branch (`struct+0x10` → resolved block `+0x518`) plus optional right
stick (`struct+0x1C/+0x20`). Scope/sniper HUD overlay bits (`0x10000..0x80000`) must
**not** be relayed — they hijack the local viewport on the receiver instance.

**STATIC:** Fire-hold `0x800` must **not** be sustained from `StatePacket.input_buttons`
when Chloe holds trigger; each shot is delivered only through `FirePacket` native replay
or a one-shot button pulse. Relaying both caused double `PlayerFireController_Fire` and
`rip=0x0` on the remote player (`0x140E2C730`).

**Fix strategy (implemented in hook):** relay `0x200` + `look_stick`; strip scope bits;
`scrub_local_viewport_aim_hud_leak()` on local joypad when peer aims but local player
does not. Status: **STATIC** (aim anim path); **HYPOTHESIS** (crosshair scrub sufficient).

### Input Block Selector Bus: Slot 3 Shares Block 1 With the Local-Player-1 Lane (STATIC, 2026-07-04)

**REJECTED (runtime, user test 2026-07-04):** "crosshair scrub sufficient" from the
2026-07-03 entry above. `scrub_local_viewport_aim_hud_leak()` clears the LOCAL
joypad's resolved block/joypad masks, but the user test still showed the crosshair
duplicating onto the receiving copy's viewport while the peer aimed. The local pad
was never the leak path.

**STATIC (root cause):** the per-slot input block binding is NOT slot==block.
`FrameState_ConstructJoypadSlots` (`0x14179D780`) initializes the six Joypad slots
via `JoypadState_Initialize(slot, selector, mode)` (`0x1418369C0`, writes the
selector byte to `Joypad+0xF0`):

- slot 0: selector 0, mode 3
- slot 1: selector 1, mode 3
- slot 2: selector 0, mode 0
- slot 3: selector 1, mode 0
- slot 4: selector 0, mode 0
- slot 5: selector 1, mode 0

`Input_PerPlayerDispatch_MgrArg` (`0x14179E6C0`) passes
`&g_InputStateBlocks[166 * selector]` to each slot's apply. So **slot 3 (the co-op
puppet joypad) consumes input block 1 — the SAME block as slot 1, the engine's
local-player-1 lane** (slots 0/1 are the two real player lanes, mode 3). The mod
itself already forces the driven slot's selector to 1 (`drive_slot1_tick`,
`fs+0xE88+0xBF0*idx+0x18 = 1`), which is why the slot-1 branch feed reaches
joypad 3 at all.

**Consequence:** any aim `0x200` / fire `0x800` mask or look-stick axes left in
block 1 outside slot 3's own `Input_ScaleCopyVirtual` conversion are consumed by
slot 1 (and slot 5) and by screen-level HUD/camera logic as *local player 1
aiming* → aim crosshair/strafe-camera state on the receiving copy's viewport.
Cursor's earlier runtime observation (teleport_hook.cpp comment: writing
`look_stick` into the "dest=3 source block" opens the local viewport aim HUD) was
exactly this bus: that source block IS block 1.

**HUD is-aiming publish chain (STATIC, supporting):**
`PlayerHud_GlobalHandleUpdateEntry` (`0x1401656B0`, reads global player handle
`g_PlayerHandleForHudPublish` `0x143EEBE80`) → `PlayerHud_ScreenStatePublishUpdate`
(`0x140C91970`, screen index at `player+0x4010`) →
`UpdatePlayerHudAimingAndWeaponAccuracyPublishes` (`0x1413571B0`) publishes
`is-aiming` into `g_PerScreenPlayerHudState` (`0x143EF1E30`, 0x100 stride).
`is-aiming` requires BOTH a player-state boolean (`player+0x1D88 → vtbl+0x148`)
AND the screen route's current camera vtable == `off_143054560`
(= `CameraControlStrafe`, class registration `0x140066690`, factory
`0x14076E440`, ctor `ConstructLargeCameraControlStrafeBase1FA0`). So the visible
crosshair implies the strafe/aim camera state machine engaged on the receiving
screen — consistent with the block-1 leak, impossible from the (scrubbed) local
pad alone.

**Fix (implemented 2026-07-04, teleport_hook.cpp):**
1. `hooked_input_scale_copy` (dest==3): save block-1 `+0x518` and right-stick
   axes (`+0x08/+0x0C`), write peer aim/fire mask + look stick, call the
   original (joypad 3 derives its command state), then RESTORE the saved
   values. The peer state exists on the shared bus only inside the conversion.
2. `drive_remote_slot1_actions_tick`: peer look_stick no longer written into
   the slot-1 branch (`+0x1C/+0x20`); aim `0x200` / fire `0x800` stripped from
   the branch mask (`branch1+0x10`) — both now ride exclusively via the
   ScaleCopy sandwich. Movement axes/gate bits unchanged.

**Falsifiable prediction (next runtime test):** with the new DLL on both copies,
Chloe holds aim → copy 2 shows puppet aim pose (`JOYPAD3-ANALOG current=0x200+`
sustained) and **no** crosshair/scope on Nadine's viewport; `REMOTE-ACTIONS
branch=` masks no longer contain `0x200/0x800`. If the crosshair still appears,
the block-1 bus hypothesis is wrong for the remaining leak and the next candidate
is a direct slot-3 Joypad reader in HUD/camera code (would need a consumer-side
gate instead). Status: **STATIC** (bus + fix), **HYPOTHESIS** (leak fully
explained by block-1 residue) pending runtime.

### Runtime 2026-07-04 (PID 14440 sender / 8524 receiver): block-1 fix does NOT stop crosshair/scope dup — REJECTED

**Test:** обе копии с DLL 13:01, фикс «сэндвич блока 1». Пользователь: прицел
по-прежнему дублируется на приёмник, снайперский прицел дублируется **без
виньетирования**, и «целится/стреляет не в ту сторону».

**REJECTED:** гипотеза «утечка прицела = остаток бит aim/fire/look в общем блоке 1»
(из записи выше). Лог подтверждает, что фикс работает как задумано, но симптом
остаётся, значит утечка НЕ через биты ввода:
- `REMOTE-ACTIONS branch=0x9` (aim `0x200` и fire `0x800` из branch слота 1
  вычищены — правка применилась).
- `REMOTE-DRIVE ... block3 ... btn=0x0` — блок-1 `+0x518` восстановлен вне
  конверсии (сэндвич закрывается корректно).
- `JOYPAD3-ANALOG current=0x209` наблюдается 104 раза → aim `0x200` **доходит** до
  джойпада марионетки только внутри конверсии (как и хотели: поза прицеливания
  есть, биты на общей шине не задерживаются).
- Снайперский прицел всё равно виден на приёмнике → скоуп-оверлей НЕ управляется
  битами джойпада; мы их и не релеили никогда.

**Новый корень (STATIC+RUNTIME):** дублируется не ввод, а **само состояние
марионетки как полноценного целящегося Player с винтовкой** на инстансе-приёмнике:
1. **Оружие:** обе копии стреляют `art=0x8075397d` (винтовка/снайперка), не пистолет
   (`PLAYER-FIRE ... weapon=... art=0x8075397d`; пистолет `0x33e29a1b` только
   на 5-м выстреле). Винтовка `0x8075397d` ранее уже отмечена как триггер
   снайперского оверлея. Скоуп на приёмнике = марионетка целится винтовкой.
2. **Направление неверное:** `JOYPAD3-ANALOG right=(0,0)` и `source=(0,0)` во всех
   строках; `REMOTE-DRIVE ... block3 ... rawx≈0 rawy=0`. Реле правого стика (look)
   фактически **не доходит** до джойпада 3 — направление марионетки берётся только
   из `apply_peer_aim_facing_to_player` (camera-facing), поэтому прицел/выстрел
   уходят «не туда». look_stick пишется в source `+0x08/+0x0C` внутри сэндвича, но
   в обработанный правый стик джойпада (`joypad+0x50/+0x54`) не конвертируется —
   либо неверное поле блока, либо перезапись оригиналом.
3. **HUD-хэндл один:** `g_PlayerHandleForHudPublish 0x143EEBE80` в обеих копиях
   разыменовывается в ОДИН объект (локальный игрок): 14440 → `0x10e0a37480`,
   8524 → `0x10e0a2df80`. `PlayerHud_ScreenStatePublishUpdate 0x140C91970` и
   reticle-менеджер `0x140C193A0` читают именно этот хэндл, а не марионетку.
   Значит скоуп/ретикл рисуются, потому что **локальный игрок приёмника входит в
   aim/scope-состояние**, а не потому что HUD напрямую читает марионетку — либо
   через общую камеру/route, либо остаётся ещё один путь протечки aim в локального
   игрока (слот 0), не через блок 1.

**Следующий конкретный шаг:** определить, действительно ли локальный игрок слота 0
на приёмнике входит в aim/scope (прочитать его aim-флаг вживую при удержании
прицела на отправителе), ИЛИ скоуп-менеджер `0x140C193A0`/`is-scope-visible?`
резолвит текущую камеру строго `CameraControlStrafe` и марионетка делит её через
CameraManager/route. От этого зависит фикс: гейт марионетки в HUD/камере против
устранения aim-протечки в слот 0. Реле правого стика (look) — отдельный баг
направления, чинится независимо (правильное поле правого стика в блоке ввода).
Status: **REJECTED** (битовая утечка), **HYPOTHESIS** (состояние-марионетки/камера).
