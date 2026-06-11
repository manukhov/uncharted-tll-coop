# Task Pool

## P0

- Find the concrete promotion/rebind path that can move Nadine into the real top-level `Player` ownership family.
- Keep the TaskManager-side retained handoff mapped, but stop treating its
  slots as the first direct promotion hook:
  - `0x1413C04A0`
  - `0x1410F1910`
  - `TaskManager +0x238 -> 0x1413C3310` (`menu` / `main-menu` bootstrap)
  - `TaskManager +0x240 -> 0x1413BF7B0` (`spawn-flashlight` /
    `turn-on-flashlight` publish)
  - `TaskManagerPlayMovie +0x258 -> 0x1413BCAE0` (play-movie /
    retained-camera helper)
- Identify the upper virtual-family object stored at `outer_owner + 0x2018`:
  - constructor/init path
  - dominant virtual methods
  - relationship to `0x1410DC160` and `0x1410DE740`
- Fully characterize the `0x1410DB040 / 0x1410DBF10` preflight validator:
  - required pass conditions
  - failure reasons
  - fields that Nadine would need to satisfy before promotion
  - already pinned blockers now include:
    - cinematic
    - two-handed weapon
    - aiming
    - IGC
    - crouch
    - melee
    - flashlight/torch/flare
    - gesture / movemode / grappling / weapon-switch restrictions
- Confirm which top-level apply inputs are required by:
  - `0x1410DE740`
  - `0x140CC1450`
  - `0x14177F7F0`
  - `0x1418C4840`
  - real `ProcessSpawnInfo` packet family `vtable 0x14291CF98`
  - current strongest retained-side packet candidate above this seam is no
    longer the tiny output pair from `0x1410F14C0`, and no longer the
    returned materialized object from `0x1417C3790`, but the extended wrapper
    packet family at `0x1417C35A0 / vtable 0x1428D5D60`
  - this same wrapper family is already accepted by generic entity runtime
    dispatch through `0x141D21C80 -> entity virtual +0x538`
  - and it already survives into the real upper `Player` surface through
    `0x1410CF370 (Player vtable +0x718) -> 0x141BBACC0`
  - current evidence from the real Player rebuild path `0x1410DE890` says
    promotion will likely need at least three distinct upper stages:
    - seed narrow context through `Player +0x738`
    - materialize/attach active upper state through `Player +0x6F0`
    - then bind draw/control wrapper state through `Player +0x718`
  - a second neighboring upper state-owner band is now concrete, but not yet
    proven to sit inside the `+0x850` rebuild ordering:
    - `0x141BBA490` (`Player +0x700`)
    - `0x141E994B0` (`Player +0x708`, classifier/count source)
    - `0x1410CE260` (`Player +0x710`)
    - `0x141728690 / 0x141728720` building `player+0x1A0`
    - plus the later publish owner `player+0x4260`
  - strongest new chaining candidate above that band:
    - `0x141BB4AA0` explicitly orders
      `+0x738 -> +0x6F0 -> +0x700 -> +0x718`
    - this block is now confirmed to live inside
      `0x141BB4280`, a broader Player draw-control finalization stage
    - `0x1413086BC` separately drives `+0x700/+0x708` and then continues into
      the mounted Player subsystem at `+0x1E98`
    - this second block is now confirmed to live inside
      `0x141308220`, a concrete upper verb stage around
      `edge-aim-reload` / `idle-reach0(-stamina)`
    - the same sibling lifecycle then continues through:
      - `+0x6F8 -> 0x141BBA2A0`
      - `+0x768 -> 0x1401D48D0`
      - `+0x4A8 -> 0x140CC17A0`
    - correction:
      real `Player +0x740` (`0x141BCD340`) is now pinned as a draw-control /
      ambient-occluder resolver and should not stay on the shortlist as a
      first-class promotion hook
- Find the hidden shared/base path that seeds the descriptor/id pair consumed
  by `0x140CC1450`:
  - embedded subobject rooted at `player + 0x100`
  - authored key fields at:
    - `player + 0x108`
    - `player + 0x10C`
  - `0x1416860A0` now proves this pair is consumed very early from the
    embedded `+0x100` subobject and used to resolve `0x14386FD00` before the
    later `0x140CBFF50 -> 0x141E994C0` handoff
  - packet-level refinement:
    `0x1417C32A0` writes the shared source link into `packet+0x20`, and
    `0x1417C35A0` preserves that same `+0x20` field in the extended wrapper
    family later consumed by `0x1416860A0`
  - retained/runtime refinement:
    `0x141D21C80` already accepts that same extended wrapper and forwards it
    through entity virtual `+0x538`, while `0x1413BF090` is now a concrete
    retained-side producer of the wrapper family
  - retained-slot refinement:
    `0x1410F3B20` also manually builds the same base packet plus extended
    wrapper family before entering the upper retained manager path
  - net-tracker refinement:
    that upper retained manager path already goes through a real two-slot
    `NetGameManager::GetMeNetPlayerTracker` family (`0x140A90E80`,
    `manager+0x3A0`, screen indices `0..1`)
  - correction:
    the exact call target `0x141E7C5F0` after the early refresh is only a
    stub `ret`; the real body starts at `0x141E7C600`, so there is still no
    hidden mutating post-refresh helper there
  - direct rel32 scans also show `0x1416860A0` is broadly reused outside the
    Player init path, so this embedded `+0x100` object should be treated as a
    shared authored-descriptor carrier, not a local Player scratch struct
  - visible `Player`-only stages
    (`0x1410C8FD0 / 0x1410DC160 / 0x1410CF370 / 0x1410CF470`)
    still do not write these fields directly
  - `0x140CBFF50` does copy shared external state from `0x143FE1088`, but
    that path feeds the upper Player band `+0x18B0..+0x1910`, including
    `source+0x108 -> player+0x18E0`, not `player+0x108`
  - nearby direct `+0x108` writes are currently classified as:
    - owner-local `+0x3BD0` subobject init (`0x1410DE520`)
    - local packet layout init (`0x140CC1080` region)
    - retained/continue packet layout init (`0x1410F21AF` region)
  - strongest remaining candidates are now:
    - an earlier base/packet init stage that seeds the embedded
      `player+0x100` authored-descriptor object before `0x1416860A0`
    - the deeper shared/base handoff through
      `0x140CBFF50 -> 0x141E994C0`
      where:
      - `0x140CBFF50` seeds `+0x15A4/+0x15A8/+0x15AC/+0x15B0`,
        `+0x161A`, `+0x1B10`, and `+0x18B0..+0x1910`
      - `0x141E994C0` expands that packet into the broader upper lifecycle,
        including `0x141BB4280`, `player->virtual +0x788`, and
        materialization of `player+0x4A8`
    - or the retained/apply wrapper family immediately above
      `0x1410DE740 -> 0x140CC1450`
  - this pair is also reused broadly after apply:
    `0x1410D10D5 / 0x1410D171D / 0x1410D3575 / 0x1410D4F45 / 0x1410D505C /
    0x1410D5123` all resolve it through `0x14386FD00` and refresh
    `player+0x110`, so the promotion target is a stable upper authored-record
    key, not a one-shot apply field

## P1

- Rebuild a truly clean packed `ida/tll.i64` from retail `tll.exe` and replay
  `scripts/ida_apply_labels.py` onto it.
  - current cutover is complete in the "one base" sense
  - but the remaining packed `ida/tll.i64` still reproduces corruption
    warnings on open
  - so future rebuild work must target "single clean base", not just "single
    base"
- Deepen the owner-local registry at `+0x3BD0`:
  - writer paths
  - population order during top-level init/finalize
  - required entry families for a valid playable actor
- Map the sibling upper owner containers around:
  - `+0x2018`
  - `+0x3BD0`
  - `+0x3D98`
  - `+0x3C20`
  - `+0x1DF0`
- Separate generic runtime-state infrastructure from Player-specific bridging:
  - `0x14177F7F0` and `0x1418C4840` are generic
  - `0x140CC1450` is the Player-facing bridge
  - remaining job is to find the promotion handoff above that bridge

## P2

- After promotion path is pinned, pick the first real gameplay validation verb:
  - `investigate/read-mode`
  - `cover/evade`
  - `melee`
  - then `aim/fire`
- Revisit vehicle passenger/driver verbs only after Nadine can pass the top-level Player qualification path.
- Revisit second camera ownership only after Nadine survives the same upper Player ownership/apply path as Chloe.

## Immediate Runtime Staging

- [ ] Use the existing live slot-`1` tracker before forcing any new `AddOrUpdate` path.
- [ ] Inject a staged probe that:
  1. reads tracker fields only;
  2. calls `BuildRespawnTransformBlock` only;
  3. calls `NetPlayerTracker::NetRespawn` only after the first two stages survive.
- [ ] Keep `AddOrUpdateNetPlayerTracker` disabled in the probe until the pre-existing
      slot-`1` tracker path is proven insufficient.

## Current Best Anchors

- `0x1410DC160`
- `0x1410DE740 -> 0x140CC1450`
- `0x1410CF370 (Player vtable +0x718) -> 0x141BBACC0`
- `0x140CBFF50 -> 0x141E994C0`
- `0x1410F14C0 -> ProcessSpawnInfo vtable 0x14291CF98`
- `0x1417C35A0 / vtable 0x1428D5D60`
- `0x141D21C80 -> entity virtual +0x538`
- `0x1410F3B20 -> 0x1417C32A0 + local 0x1428D5D60 wrapper`
- `0x140A9AAD0 -> NetGameManager::RespawnPlayer`
- `0x140A9AAD0` effective live args:
  - `rdx = tracker`
  - `r8d -> tracker+0x840`
  - `r9d -> tracker+0x844`
- `0x140A90550 -> NetGameManager::AddOrUpdateNetPlayerTracker`
- `0x140A90550` direct call shape from real callers:
  - `rcx = 0x143DD70C0` manager
  - `rdx = 0x143E08C80` shared context/global
  - `r8  = source_obj`
  - `r9d = slot`
  - stack arg 5 = flag
- `0x140A90550` concrete field writes:
  - `tracker+0x7C0..+0x7DF <- incoming+0x00..+0x1F`
  - `tracker+0x7E0 <- incoming+0x20`
  - `tracker+0x7B8 <- incoming+0xE4`
  - `tracker+0x1DC <- incoming+0xE8`
  - `tracker+0x7BC <- incoming+0xF0`
  - `tracker+0x1E0 <- requested slot`
  - `tracker+0x1E4 <- hash(NetCharacter-...)`
- real callers show `0x140A90550` is only the low worker, not the whole
  stable bringup sequence
- dual-slot caller post-call band:
  - `tracker+0x220 = -1`
  - `tracker+0x455`
  - `0x140A59540`
- late-join bridge post-call band:
  - `tracker+0x220 = -1`
  - `+0x455/+0x88C/+0x850/+0x817`
  - conditional `+0xA10/+0x814/+0x88D..+0x88F`
  - then wider external-record copy and `0x140B3D7D0`
- direct disabled scaffold now also tries missing tracker creation through:
  - `0x140A90550(..., sourceA[0], 1, 0)`
- current P0 net-player blocker is now split into two concrete unknowns:
  - which `source_obj` is safe on a live run for `0x140A90550`
  - which minimal post-call init band is required after that low worker
- `0x140A90A00 -> AddOrUpdateNetPlayerCinema wrapper`
- `0x140A58190/0x140A58310 -> late-joiner-%d tracker bridges`
- `0x140A58310 -> external record -> tracker reuse/create bridge`
- `0x140A58310` wider record layout now concrete through:
  - `+0x20..+0x3C`
  - `+0x40` word array
  - `+0x66` signed-byte array
  - `+0x7C/+0x80/+0x84/+0x88/+0x89/+0x8C/+0x90/+0x91/+0xB4/+0xC0/+0xC8`
- `0x140A4DDA0 -> thin direct wrapper above AddOrUpdateNetPlayerCinema`
- `0x140A4E7DC -> bulk 10-entry wrapper above AddOrUpdateNetPlayerCinema`
- `0x140B347C0 -> NetPlayerTracker::NetRespawn`
- `0x140B346B0 -> NetPlayerTracker::StartOrQueueSpawnPlayer`
- `0x140B31F60 -> NetPlayerTracker::SpawnPlayer`
- `0x140B346B0` immediate/deferred split:
  - clears `tracker+0xA7C/+0x814`
  - immediate tail-call to `0x140B31F60`
  - deferred queue through `tracker+0xA00/+0xA04/+0x4C0/+0x4C8`
- `0x140B31F60` early concrete inputs:
  - `tracker+0x1E0` -> retained slot
  - `tracker+0x1E4` -> later upper authored/runtime state id
  - `tracker+0x840/+0x844` mirrored into `+0x848/+0x84C`
- `0x140B44EC0` immediate transform source:
  - prefers `tracker+0x8/+0x10` -> `live_actor+0x100/+0x120/+0x140`
  - fallback through `tracker+0x20`
- `0x140B347C0` direct bypass path:
  - `rcx = tracker`
  - `edx = respawn mode`
  - `r8b = has_prebuilt_0x50_block`
  - `r9 = prebuilt_0x50_block`
  - when `r8b=1`, copies `r9` straight into `tracker+0x500..+0x540`
- live bypass callers:
  - `0x140BC0425`
  - `0x140BDB23F`
- `0x141685C30` builds the reusable direct `0x50` respawn block
- live bypass seed layout for `0x141685C30`:
  - `+0x0  = 0x7FCCCCCC`
  - `+0x4..+0xB = 0x7FCCCCCC`
  - `+0xC  = respawn mode/state id`
  - `+0x10 = zero vec4`
  - `+0x20 = default vec4 from 0x1428805D0`
- live bypass reference transform source:
  - `rdx = tracker+0x20`
- correction for the first patch:
  - `tracker+0x20` is only a `16-byte` reference-position carrier
  - `0x141685C30` can therefore be fed directly from `Nadine+0x100`
    without needing a pre-seeded tracker-local pair object
- sibling tracker split:
  - `tracker+0x20` = reference-position carrier for respawn-block build
  - `tracker+0x60` = optional live actor pair consumed later by
    `NetPlayerTracker::NetRespawn`
- current focused bringup slices do not write `tracker+0x60`, and the compact
  pair helper family is not called from the narrow
  `0x140A90550 / 0x140B31F60 / 0x140BC0425` chain
- in the focused dumped `NetPlayerTracker::SpawnPlayer` body, only
  `tracker+0x1E0` keeps recurring directly from the fresh add/update payload;
  no direct reads showed up there for `+0x7C0..+0x7E0`, `+0x7B8/+0x7BC`, or
  `+0x1E4`
- `0x140A90A00` hard-forces `tracker+0x1E0 = 0`; use it only as a local
  payload template, not as a slot-`1` seam
- `0x140B411F0` / `0x140B412D0` are post-spawn selectors for
  `player+0x854/+0x858`, not first-order tracker blockers
- `0x140B6B970 -> net-event handler above RespawnPlayer`
- `0x140BDB620 -> SendNetEventRespawnPlayer`
- `0x140BDBF70 -> SendNetEventSignalRespawnPlayer`
- `0x140A90E80 -> NetGameManager::GetMeNetPlayerTracker`
- `0x140A90F40 -> broader runtime-object tracker lookup by hashed Player identity`
- direct global manager anchor for the DLL:
  - `module_base + 0x3DD70C0`
- disabled-by-default slot-`1` scaffold already exists in
  `scanner/src/teleport_hook.cpp` for:
  - manager anchor
  - `GetMeNetPlayerTrackerByScreenIndex`
  - `BuildRespawnTransformBlockFromReferenceTransformAndSeedRecord`
  - `NetPlayerTracker::NetRespawn`
- current P0 blocker narrowed to:
  - exact `0x140A90550` creation/update ABI for a slot-`1` tracker that does
    not already exist
- `tracker+0x1DC/+0x1E0` as the live tracker identity pair matched by `0x140A58310`
- `tracker+0x1E4` as the fixed `NetCharacter-` family/state id on the cinema/local wrapper path
- note: `0x140A4DDA0` and `0x140A4E7DC` have no direct `.text` callers; treat them as table-dispatched helpers, not primary public seams
- `late-joiner-%d` token family above `0x140A58190/0x140A58310`
- `cinema-player-%d` token family above `0x140A4E7DC`
- note: `0x140A58190` and `0x140A58310` also have no direct `.text` callers; treat the whole cinema/late-joiner helper band as table-dispatched under the manager layer
- `0x140A9AAD0` explicit respawn seam: active handoff to `0x140B346B0`, deferred queue via `tracker+0xA00/+0xA04`, timing via `+0x4C0/+0x4C8`
- `0x140B6B970` only direct `.text` caller above `NetGameManager::RespawnPlayer`
- `0x140BBFD70` tracker-side deferred respawn consumer around `0x140BC0425`
- heavier direct callers above `0x140A90550` belong to session/matchmaking flow (`joined-tournament-match`, `return-to-search`, `map-chosen`, `matchmaking-failed`, `timed-out`, `start-game-failed`)
- `0x1413BF090 -> 0x1417C35A0 -> 0x141D21C80`
- `0x1413C04A0 -> 0x1410F1910 -> TaskManager-family retained handoff`
- `outer_owner + 0x2018`
- `outer_owner + 0x3BD0`
- `outer_owner + 0x3D98`
- mounted dispatch hub `outer_owner + 0x1FB0`

## Corrections Held As Baseline

- Real `Player` vtable slots now treated as fixed:
  - `+0x300 -> 0x141BB8BC0`
  - `+0x738 -> 0x140CC18D0`
  - `+0x850 -> 0x1410DE890`
- The strongest current real-Player lifecycle evidence comes from
  `0x1410DE890`, where the upper rebuild path orders:
  - `+0x738` context seed
  - `+0x6F0` active-state materialization
  - `+0x718` wrapper/context bind
- `+0x700/+0x708/+0x710` are now tracked as a neighboring upper Player
  state-build/publish band reusing `player+0x1A0/+0x1A8`, but they are not yet
  proven to sit inside the real `+0x850` ordering
- `0x141BB4AA0` is now the strongest sibling upper orchestrator because it
  explicitly chains `+0x738 -> +0x6F0 -> +0x700 -> +0x718`
- and then continues through `+0x6F8`, `+0x768`, and `+0x4A8`
- later correction:
  - `0x141BBACC0` is downstream upper behavior/draw-control binding over
    `player+0x198`
  - `Player +0x718` is therefore no longer treated as an upstream identity
    source
  - `0x140CBFF50 -> 0x141E994C0` is now the stronger shared/base upper
    handoff to keep on the promotion shortlist

- [ ] Re-validate slot-`1` tracker lookup without `0x140A90E80`.
  The last runtime probe produced a code pointer from that callable surface, so
  the next staged probe should read `manager + 0x3A0 + slot*8` directly.
- [ ] Type the live object at `manager + 0x3A0 + slot*8` before any further respawn staging.
  Two separate probes now show `0x7FF689ED4D80`, and both crash paths happen only after that object is treated like a tracker/respawn carrier.
- [ ] Keep both `BuildRespawnTransformBlock` and `NetRespawn` disabled until the slot object at `0x7FF689ED4D80` is identified.
- [x] Re-validate slot-`1` storage without `0x140A90E80`.
  Safe runtime probe confirmed the real per-slot object at
  `manager + 0x3C0 + idx*0x43D0`.
- [ ] Re-test `BuildRespawnTransformBlock` using the confirmed inline slot path,
      still without `NetRespawn`.
- [x] Isolate whether the crash came from wrong slot storage or from the respawn builder.
  Result: inline slot storage is readable and stable; the first confirmed crash
  surface is `BuildRespawnTransformBlock`.
- [ ] Find the correct source/context contract for `0x141685C30`, or bypass it
      by seeding a known-good `0x50` respawn block directly.
- [ ] Reconstruct the exact caller-side seed/context contract for `0x141685C30` from the live `0x140BC03FF` path before any further runtime builder tests.

- [ ] Re-run builder-only probe with the exact live caller seed layout for `0x141685C30` (`DWORD 0x7FCCCCCC`, `QWORD 0x000000007FCCCCCC`, mode dword, zero vec4, default vec4).
- [ ] Add a read-only probe for the confirmed inline slot object to dump `slot+0x20/+0x30/+0x40` and nearby pre-call state before touching `BuildRespawnTransformBlock` again.
- [ ] Run a builder-only probe against `source0 + 0x20` with the exact live seed layout, to test whether the crash is specific to the uninitialized slot-`1` source band.
- [ ] Reconstruct the source-band preparation lifecycle above `0x140BC03FF` before any more runtime builder probes. `source0+0x20` and slot-state sources both still crash the builder.
- [x] Reclassify the `0x140A90E50` slot seam before more tracker probes.
  Read-only runtime state at `manager+0x3C0+idx*0x43D0` contains session/map/mode
  string bands (`NET_MAP_REMNANTS_FOREST_VIP_TDM`), so this helper returns an
  inline per-slot session band, not the materialized tracker object itself.
- [ ] Use the new read-only external dumper (`tll_runtime_code_dump.exe`) on a
      live process before any more injected builder probes.
  The next step is to dump code bytes directly around:
  `0x140BBFD70`, `0x140B44EC0`, `0x141685C30`, `0x140B42310`, `0x141C8D740`
  and reconstruct the missing lifecycle from real process code rather than the
  corrupted IDB ranges.
- [x] Re-validate the `BuildRespawnTransformBlock` contract with a truly packed
      live seed layout.
  The previous crashes were caused by C++ alignment in the probe struct. After
  switching to raw bytes, the builder succeeds on `source0+0x20`,
  `source1+0x20`, and `Nadine+0x100`.
- [x] Rule out `manager+0x3A8` as an obvious `NetRespawn` receiver.
  The probe reaches `NetRespawn` and never returns, so this pointer band still
  looks like upper indirection rather than a real `NetPlayerTracker` object.
- [ ] Find the real receiver object for `0x140B347C0`.
  The respawn block is no longer the blocker. The next seam to resolve is the
  concrete object that should be passed in `rcx`.
- [ ] Resolve the real `source_obj` contract for
      `0x140A90550(manager, context, source_obj, 1, flag)`.
  A direct call with `source0` and `flag=0` enters the built-in manager seam
  and does not return, so the slot-`1` bringup now clearly depends on a
  stronger caller-side preparation contract than the bare function signature.
- [x] Re-check whether `0x140A90550` wants the raw `0x143DD7480` base or an
      actual source entry pointer.
  Retail callers pass entry pointers (`rdi`/`rsi`), not the base. The next
  runtime probe should use `0x143DD7480 + 0x43D0`-style entries instead of the
  raw base.
- [x] Rule out the naive `0x143DD7480 + 0x43D0` source entry for campaign.
  Probe15 showed it is all zeros at `+0x1DC/+0x1E0/+0xE8` and still does not
  survive `AddOrUpdate`.
- [ ] Probe the fast-path source candidate from `0x14194D5F0(..., r8b=0)`.
  The next runtime seam is `context + 0x1D00`, which the dual-slot caller uses
  when the shared context is in its single-entry fast path.
- [x] Rule out raw `context + 0x1D00` without the producer step.
  Probe16 showed the band is still zeroed if `0x14194D5F0` is skipped.
- [ ] Probe the exact retail sequence:
  `0x14194D5F0(context, out_list, 0)` -> `entry0` -> `0x140A90550(...)`.
  This is now the narrowest remaining manager-side seam before trying any more
  respawn work.
- [x] Rule out calling `0x14194D5F0` on the raw shared context directly.
  Probe17 shows `context+8 = 0` and the builder does not return.
- [ ] Probe the upstream shared-context preflight call from the same retail
      dual-slot caller:
  `*(0x143E08C88)->virtual + 0x78 (&0x143E08C80, 1)` before `0x14194D5F0`.
- [x] Check whether the `0x143E08C88` shared-context owner is even live in the
      current campaign state.
  Probe18 showed it is null, so this seam is inactive here.
- [ ] Type the sibling live context family at `0x143E0EF00/08` with a read-only
      probe before any more active calls. Nearby upper branches already use it
      when the `0x143E08C88` family is absent.
- [x] Type both shared-context families read-only in campaign.
  Probe19 shows both `0x143E08C80` and `0x143E0EF00` are live objects whose
  embedded `+0x8` owner field is null.
- [ ] Find the method that populates the shared-context object's embedded
      owner field at `+0x8`.
  The main unknown is now the setter/lifecycle for that field, not a missing
  external owner global.
- [x] Reclassify `0x143E0EF18`.
  It is `ctxB + 0x18`, the current-selected pointer inside
  `0x143E0EF00`, not a standalone global.
- [x] Reclassify `0x140B1E400`.
  It is a pending-selection publisher for the ctxB family: compares the
  candidate against `ctxB + 0x18`, stages the new candidate into
  `0x143E10E90`, mirrors vector state into `0x143E0F618..+0x38`, and calls
  `0x140BF1FF0(1,0)` without committing `ctxB + 0x18`.
- [ ] Find the final commit path from pending `0x143E10E90` into `ctxB + 0x18`.
  Current ctxB consumers clear/use the pending pointer, but the campaign path
  still has no confirmed writer that turns the staged selection into the live
  `ctxB + 0x18` pointer.
- [x] Find the first ctxB pending -> commit handoff.
  The branch at `0x140B1ECBC` is now confirmed to load
  `pending = 0x143E10E90`, call `*(0x143880318)->virtual + 0x48(&ctxB, pending)`,
  and only then clear the pending pointer.
- [ ] Trace the global commit service behind `*(0x143880318)->virtual + 0x48`.
  This is now the narrowest unresolved piece between staged ctxB selection and
  the final live `ctxB + 0x18` state.
- [x] Type the startup / teardown lifecycle for `0x143880318`.
  Startup `0x1409FA94E..0x1409FAA23` installs either `0x141973750` or
  `0x1419796F0` and binds it through `0x1409EB500`; teardown
  `0x1409FDD70..0x1409FDDEE` calls `virtual +0x18`, then `virtual +0x0`, and
  clears `0x143880318/0x143880300`.
- [x] Type the visible ctxB object surface.
  The current `ctxB` vtable slots now cover dispatch/rebuild/reset/publish
  work (`0x14194DE00`, `0x14194DEE0`, `0x14194E0D0`, `0x14194E0E0`,
  `0x14194C550`, `0x14194C5F0`, `0x14194C690`, `0x14194C730`,
  `0x140B20760`, `0x140B20770`, `0x140B1FAF0`) and still show no plain direct
  writer for `ctxB + 0x18`.
- [ ] Find the first non-null writer into `ctxB + 0x8`.
  Reset/unbind paths are now pinned, but the live bind path into
  `ctxB + 0x8 -> owner->virtual +0x50` is still missing.
- [x] Identify the first real writer to the shared-context embedded owner
      field at `ctx + 0x8`.
  Service `virtual +0x20` variants `0x1419739E0 / 0x141979A60` both bind a
  context into a service entry array and then write `ctx + 0x8 = service`.
- [x] Find the first `service->virtual +0x20(&ctxB=0x143E0EF00)` call-site.
  Retail startup in `0x1409F9B20` does it at `0x1409FAA78` after
  `0x1409FA6DE` sets `rsi = 0x143E0EF00`. The same startup function also binds
  `0x143E08C80` and `0x143E0AC40`, and the current `.pdata`/Capstone scan of
  retail `.text` shows no additional later `service->virtual +0x20(&ctxB)`
  site.
- [x] Tighten the exact fallback-owner record contract behind `ctxB + 0x8`.
  `0x14194D5F0` single-entry fallback clones `owner+0x8..+0x27` plus dword
  `owner+0x28` into `ctxB+0x1D00/+0x1D20`. That exact layout matches startup
  helper `0x1409EB500`, which seeds the live global selection service header
  at `0x143880318+0x8..+0x28`. This strongly supports the reading that the
  intended fallback owner can be the service object itself.
- [x] Trace where campaign makes the service header reachable through
      `ctxB + 0x8`.
  Startup reaches it directly through the confirmed
  `service->virtual +0x20(&ctxB)` bind at `0x1409FAA78`.
- [x] Re-check whether the strongest dual-slot `AddOrUpdate` caller really uses
      `ctxB` as the source context.
  No: the upper caller around `0x140A392EC / 0x140A3936A` does inspect `ctxB`
  through `0x14194D5F0` and compares `ctxB`/`ctxA` availability, but the
  actual `0x140A90550` calls still pass `rdx = &0x143E08C80` plus
  `r8 = source_entry_from_ctxA_list`.
- [ ] Map exactly how `ctxB` influences the dual-slot manager caller if it is
      not the direct `AddOrUpdate` source context.
  The current best reading is that `ctxB` feeds availability/selection gates
  above the real ctxA-based tracker creation path.
- [x] Trace the deeper service-entry callbacks under `0x141975870 / 0x14197AE50`.
  Variant A still looks packet-oriented, but variant B `0x14197AE50` is now a
  real pending->commit worker: it packages `ctx+0x1ED0/+0x1ED4`, stores
  `ctx + 0x18 = pending`, waits through `service->virtual +0x10`, then
  resolves through `ctx + 0xB8 / +0xBC`.
- [x] Type the active campaign request path into the global service.
  `0x140B1EE70` calls `ctxB->virtual +0x28 = 0x140B1DF30` first, gets fixed
  token `0xA`, then forwards that token into
  `*(0x143880318)->virtual +0x28(service, &ctxB, token, 1)`.
- [x] Correct the local `ctxB->virtual +0x28` slot mapping.
  Direct vtable decode from `0x140B1DF80 -> 0x1429EF890` showed that
  `ctxB->virtual +0x28` is `0x140B1DF30` (`mov eax, 0xA; ret`), not the
  neighboring heavy worker `0x140B204E0`.
- [ ] Identify which service variant is actually installed in campaign at
      `0x143880318` when the live ctxB path runs.
  Startup now looks variant-A by default, so this is mainly a confirmation
  task unless the fallback/test gate is hit.
- [x] Decode the exact `service->+0x28` vtable slots for both variants.
  `.rdata` now pins:
  - variant A `+0x28 = 0x141974E10`
  - variant B `+0x28 = 0x14197ABC0`
  so the live call at `0x140B1EE70` is exact, not inferred.
- [x] Decode the startup variant-selection rule for `0x143880318`.
  Startup `0x1409FA94E` first installs variant A, copies the upper packet via
  `0x1409EB500`, runs `service->virtual +0x8`, and keeps A when init succeeds
  and `0x1430641DC != 0x14`. Only failing init or `0x1430641DC == 0x14`
  forces variant B and overwrites `0x143880318`. `0x1430641DC` is the
  `Net Init Error Testing` selector.
- [x] Trace the first real campaign writer for `ctxB + 0x8`.
  The first confirmed writer is no longer hypothetical:
  startup `0x1409FAA78` reaches `service->virtual +0x20(&ctxB)`, and the
  concrete service workers `0x1419739E0 / 0x141979A60` both do
  `ctx + 0x8 = service`.
- [ ] Explain why live campaign can still end up with `ctxB + 0x8 == 0`
      despite the confirmed startup bind.
  This is now the real lifecycle blocker. The strongest already-known clear
  sites are:
  - `0x1409FDD34`
  - `0x140B1E0E8`
  The direct caller map is now narrower too:
  - `0x1409EE7D0` hashes `added-to-stage` / `popped`
  - `0x1409F78B0` hashes `update`
  - `0x1409F7C60` hashes `update` / `popped`
  - `0x1409EE9B0` sits in explicit multiplayer/front-end material
  - `0x141CE32E9` gates the shutdown on `edx == 4`
  The startup side is tighter too:
  - `0x1409FBA60` does not directly call the bind block
  - but it does package `0x1409F9B20` as a callback and hand it through
    `0x1415B43B0/0x1415B4870/0x1415B47C0`
  That callback path is now narrowed further:
  - `0x1415B43B0` allocates a deferred work batch
  - `0x1415B4870` copies the `0x80`-byte callback record into it
  - `0x1415B47C0` finalizes the batch
  - `0x1415B4560` queues it into a per-lane scheduler rooted at
    `0x1437E9700`
  So the next step is now:
  find the consumer/drain path for that deferred lane queue, and determine
  when the queued `0x1409F9B20 -> 0x1409FA94E` startup callback actually
  executes after shutdown.
- [x] Classify the main direct callers above `0x1409FD360`.
  The heavy callers are now pinned as upper front-end/net lifecycle
  transitions, not local ctxB helpers:
  - `0x1409EE7D0` -> stage-token handler (`added-to-stage` / `popped`)
  - `0x1409F78B0` -> `update`-token restart path
  - `0x1409F7C60` -> `update` / `popped` sibling restart path
  - `0x1409EE9B0` -> multiplayer front-end shutdown flow
  - `0x141CE32E9` -> high-level state gate for the same shutdown family
- [x] Prove whether the restart side points back at the startup bind surface.
  It does, but indirectly:
  - `0x1409FBA60` takes the address of `0x1409F9B20`
  - stores it into a local callback record
  - and passes that record through
    `0x1415B43B0 -> 0x1415B4870 -> 0x1415B47C0`
  So the remaining blocker is now callback delivery/dispatch, not the absence
  of a re-entry surface.
- [x] Classify the restart-side delivery mechanism for `0x1409F9B20`.
  It is deferred work, not an inline call:
  - `0x1415B43B0` allocates the batch
  - `0x1415B4870` copies the `0x80`-byte records
  - `0x1415B47C0` finalizes the batch
  - `0x1415B4560` enqueues it into a per-lane `0x2200`-stride queue under
    `0x1437E9700`, with a visible `0x400`-entry cap
- [x] Decode the concrete local `ctxB` vtable slots above the owner bind.
  Direct `0x1429EF890` decode now pins:
  - `ctxB->virtual +0x20 = 0x1407DF190`
    - getter for `ctxB+0x18` gated by byte `+0xC8`
  - `ctxB->virtual +0x38 = 0x140B204A0`
    - toggles `ctxB+0x200`, then tail-calls `owner->virtual +0x78`
  - `ctxB->virtual +0x40 = 0x140B204D0`
    - returns inverted bit state from `ctxB+0x200`
  This rules out the old idea that upper `ctxB->+0x20` callers are hidden
  bind sites.
- [ ] Trace the immediate post-bind consumer band under `ctxB + 0x8`.
  `0x140B21E70` and `0x140B21F10` both require `ctxB + 0x18` plus
  `ctxB + 0x8`, then immediately call `0x14194D5F0(&ctxB, out_list, 0)`.
  One path continues through `0x14194E510` and checks resolved-object byte
  `+0x42`. This is the first concrete downstream consumer band once the owner
  bind exists.
- [ ] Type the concrete owner interface behind `ctxB + 0x8`.
  The bound owner is now known to expose at least:
  - `virtual +0x50`
  - `virtual +0x58`
  - `virtual +0x70`
  - `virtual +0x78`
  The missing bind search should now target the concrete owner family that
  implements this interface, not just any arbitrary pointer store.
- [x] Extend the local ctxB owner-wrapper map above the already known consumer
      band.
  Small wrappers now pin additional slots:
  - `0x14194DB30 -> owner +0x58`
  - `0x14194DB70 -> owner +0x60`
  - `0x14194DB90 -> owner +0x98`
  - `0x14194DBB0 -> owner +0xA0`
  - `0x14194DBD0 -> owner +0xA8`
  - `0x14194DBF0 -> store ctxB+0x200, then owner +0x78`
