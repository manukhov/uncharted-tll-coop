> **Current status & findings: see [COOP_STATUS.md](COOP_STATUS.md) (updated 2026-06-28).**

# TLL Co-op Mod Roadmap

Target build: `tll.exe 1.4.21058.0`

## North Star

Сделать полноценный кооп для кампании `The Lost Legacy` на двух ПК:

- player 1 играет за Chloe;
- player 2 играет за Nadine;
- у каждого свой инпут и своя камера;
- host остаётся authority для campaign state, checkpoints, combat, scripts и saves.

## Single Focus (2026-06-28)

Единственный текущий фокус:

**полностью синхронизировать двух игроков между двумя копиями игры.**

Порядок обязателен:

1. устойчивое представление удалённого игрока в каждой копии игры;
2. положение, поворот, скорость и все режимы перемещения;
3. направление взгляда и прицела, оружие, выстрелы, перезарядка и боезапас;
4. ближний бой, урон, здоровье, смерть и возрождение игроков;
5. прыжки, укрытия, лазание, уступы, верёвка, взаимодействия и транспорт;
6. контрольные точки, сцены и переходы между главами;
7. только после этого — синхронизация противников.

Непрерывные величины передаются снимками состояния. Разовые действия передаются
отдельными нумерованными событиями с подтверждением доставки. Поле
`StatePacket::anim_state` сейчас содержит лишь кнопки и не считается
синхронизацией анимации или действий.

Текущий технический блокер: в каждой копии нужен устойчивый объект удалённого
игрока. `NadineBuddy + MoveTo` не воспроизводит все возможности игрока. Старый
вывод, что само наличие второго `Player` ломает прицеливание, оказался получен при
автоматически работающей второй камере. Сначала проводится чистое разделение трёх
признаков: объект `Player`, ввод слота 1 и камера/второй экран.

Синхронизация противников и передача урона противникам сохранены как исследованный
материал, но выключены по умолчанию и не развиваются до завершения игроков.

## Previous Architecture Findings (reference, not current priority)

Главная feasibility-проверка ПРОЙДЕНА:

- second screen-route entity / second view path жив в кампании;
- `g_maxLocalPlayers` global = `0x1437CF310` (low dword limit);
- `net-is-second-player-available?` = `0x140C021C0` (gate byte `0x143071038`);
- `net-login-split-screen` = `0x140C02220`;
- **runtime-тест на game thread доказал:** при
  - `max_local_players = 2`
  - `second-player-available byte = 1`
  login path УСПЕШНО boot'ит native local-player-1 / split-screen machinery
  прямо в campaign (`login_state=3`, query=1, cam0/cam1 resolved).

То есть preferred architecture реальна:
**движок способен поднять свой родной local-2-player / split-screen путь в
кампании без полноценной multiplayer net-сессии.**

Новый главный блокер уже узкий — и ВСЕ линии сошлись в одну (см. GRAND
SYNTHESIS в initial-findings.md):

**spawn игрока, local-player resolver и камера — все параметризованы ОДНИМ
screen-route index, читая two-screen route table `0x14697AD50` (route 0/1).
В обычной кампании route 1 не поднят, поэтому slot 1 везде пуст.**

Цепочки:
- spawn: `ScriptSpawnPlayer 0x140F73910` -> `SpawnPlayerFromRetainedDescriptor
  0x1410F1910` -> slot index из `0x141C077B0(route index, assert<=1, читает
  0x14697AD50)` -> `CommitResolvedPlayerSpawn 0x1410F14C0` -> store pair в
  `0x143EEBE80[index]`
- read: `(*(0x143FE1070))->+0xA8 (0x140298D70)` -> campaign `0x1410F0F60` читает
  `0x143EEBE80[index]`
- raw запись slot1 пары -> CRASH (route 1 entity не поднят)

Корневой gate ОДИН: **поднять валидный screen-route entity 1 в `0x14697AD50`**
(валидность = байты route+0x55/+0x56 + бит по селектору `0x14697AE34`). После
этого существующий spawn pipeline сам заполнит slot 1, и local-player + камера
разрешат Nadine без ручного poke.

Следующий шаг: найти конструктор route entity 1 при split-screen и что делает
его «валидным».

Новый supporting result на этом же слое:
- second-camera attach path already calls the same resolver seam:
  - `0x141C0DFD0 -> (*(qword*)0x143FE1070)->virtual +0xA8(screen_index)`
- campaign-visible manual-camera cluster is real:
  - `Manual Camera 1/2`
  - `Joypad 1/2 Controls Game`
  - `Swap Joypad Screens`
  - `Bind Manual Cam To Player(/Parent)`
- the remaining camera-side blocker is now downstream:
  - resolved actor-pair cache `0x143D76C10/18`
  - manual/debug follow-state writer `0x141C07E20`
  - or a manager1/local-player1 analog to the mature manager0 retained-camera path

## Previous Nadine Elevation Plan (reference)

What is already strong but NOT yet sufficient as a foundation:
- campaign can boot the engine's native split/local-player state;
- second screen-route entity / second view path is live in campaign;
- the strongest current second-view owner-side anchor is now the
  `CameraManagerCustom` / `CameraControlStick` overlay family around
  `0x14073C8A0 / 0x14073CDD0`;
- `local-player-1` can be resolver-bound to `NadineBuddy` by hook;
- stock campaign resolution for `local-player-1` can still stay null even after
  split login is active;
- render layer reacts when forced.
- `ctxA` and `ctxB` are separate startup-built context classes;
- `ctxB` is the stock player-2 source-selection / summary controller, not yet
  the final Nadine actor/camera ownership layer.

This is valuable evidence, but the real acceptance criteria are still narrower:

### 1. Direct player-like control of Nadine (primary)

Need:
- identify Nadine's real control/locomotion ownership path (NOT `MoveTo` / nav goal fallback)
- keep the current strongest live seams together:
  - `0x141DE9E60(owner, record, mode, flag)` as the record-driven owner apply bridge
  - shared `owner+0x15B8` mode/snapshot layer
    (`0x141E0ED10 / 0x141E0EDF0`)
  - indirect `BehaviorScavenge` materialized-state install path under `owner+0x1F80`
    (`0x1404B7F00 / 0x140473590`)
- treat `0x1403F5B60` and the surrounding `0x140545000..0x14056B000` writers
  as a prepared NPC action layer, not as the clean first seam for direct play
- keep the strongest known live non-script branch in focus:
  - `0x14058B900`
  - `0x14058BA40` (mode 1 snapshot apply)
  - `0x14058BC70` (mode 2 snapshot apply + follow-up)
- but do NOT treat the higher `0x1406B3D70 / 0x1406B3640 / 0x1406B3940` family
  as the direct playable seam:
  - RTTI resolves it to `Patrol@ScriptSkill`
  - so that layer is AI patrol logic
- likewise, the second direct caller above `0x141DE9E60` is not a player-input
  producer:
  - `0x140486EB0` sits in `BehaviorTeleport`
- current cleaner shared control frontier is therefore:
  - `0x140416B20`
  - `0x141DE9E60`
  - `0x141E0ED10`
  - `0x141E0EDF0`
  - `ActionPack`-side source lifecycle under `owner+0x15C0`:
    - `0x141E18350`
    - `0x141E18450`
    - `0x141E18700`
    - `0x141E189C0`
  - backup `ActionPack` handles feeding `owner+0x2F8`:
    - `owner+0x258`
    - `owner+0xE90`
  - higher owner-side chooser over `owner+0x80`:
    - `0x141E1A040`
    - with moving-state branch `0x141E15C80`
    - mode map now practical:
      - `1 = StopAndStand`
      - `2 = MoveToLocation`
      - `3 = MoveToActionPack`
      - `4 = MoveAlongSpline`
  - already typed gameplay branches over the same machinery:
    - `0x141E15A00` = stop/stand
    - `0x141E1C820` = resume
  - concrete shared snapshot/publish block `0x141E18A70..0x141E18BA3`
    (`source+0x230/+0x240/+0x250` -> `(owner+0x1D0)+0x15B8`)
  - sibling owner-local publish block `0x141E18D00`
    (`owner+0x258/+0x2F8/+0x300` -> same publication tail)
  - shared prepared movement setter:
    - `0x141E14890`
    - accepts `owner`, `mode id`, prepared descriptor
    - writes:
      - `owner+0x444`
      - `owner+0x290/+0x2A0/+0x2B0`
      - `owner+0x2D0`
    - ordinary gameplay callers already feed it directly with mode ids
      `0,1,2,3,4`
  - large ordinary-Chloe chooser above it:
    - `0x141E1BA20`
    - unpacks queued entries through `0x141E06C00`
    - compares against `owner+0x290`
    - then feeds `0x141E14890` with live mode ids `0..4`
  - broader ordinary-Chloe owner update loop above both:
    - `0x141E19F90 = NavStateMachine::Update(bool)`
    - internal main label:
      - `0x141E1A000`
    - runs `0x141E1BA20`
    - conditionally dispatches `owner+0x80` through `0x141E15C80` or `0x141E158B0`
    - mirrors the resolved descriptor into `owner+0x120..+0x1B0`
    - refreshes `owner+0x228/+0x218`
    - then handles traversal/action-pack follow-up and publication flags
  - second-stage path/result chooser inside that loop:
    - `0x141E1AA20`
    - works from `owner+0x444`
    - compares current and backup action-pack handles
      (`+0x2F8`, `+0xE90`, `+0x258`)
    - releases invalid/disappeared action-packs
    - rebuilds compact descriptors through `0x141E06C00`
    - feeds replacement modes `0`, `1`, `2`, and `4` into `0x141E14890`
  - strongest ordinary moving-state family found so far:
    - `0x141E1E4F0`
    - identified by `UpdateStateMoving` strings
    - feeds `0x141E14890` with modes `2`, `3`, and `0`
    - checks arrival, waypoint changes, and moving-command success/failure
  - weaker neighboring families now separated:
    - `0x141E1F300` = post-root / interrupted-state update
    - `0x141E18120` = abandon interrupted state
      - `0x141E20280` = enter-animation update
  - practical correction:
    - `owner+0x80` still matters
    - but it is starting to look more like a mirrored command-state description
      than the cleanest upstream input seam
    - the stronger live center is now `0x141E14890` plus `owner+0x444` and
      `owner+0x290/+0x2A0/+0x2B0`, with `0x141E1A000` now looking like the
      broader ordinary owner update loop above them
- keep the stronger prepared-state path in front of the metadata roots:
  - `NavCharacter::SetStrafeMode`-style branch around `0x1404BA9C0`
  - `owner->virtual +0xC48`
  - `0x141DE4D20 -> 0x141E15030`
  - `0x141DE5190`
  - `0x141DE4F90`
- feed direct stick/button-style input into that layer
- current outer gameplay edge is now visible:
  - `NavCharacter::Update` block around `0x141DE8B80..0x141DE8E60`
  - specifically `0x141DE8CB1 -> 0x141E19F90`
- climb above `NavCharacter::Update` / `0x141DE8B80..0x141DE8E60` before
  spending more time on narrower direct callers of `0x141E14890`
- climb above `0x14058B900` and identify which live owner/state family chooses
  the mode-1 and mode-2 branches
- expand verbs in order:
  - locomotion
  - traversal
  - interact
  - melee
  - weapon/aim/fire

### 2. Independent camera ownership for Nadine (primary)

Need:
- find the campaign-native camera ownership/follow path for Nadine
- type the screen-route owner family that carries `route+0x124`
  - current strong anchor: per-screen camera-overlay state machine
    `0x14073C8A0 / 0x14073CDD0`
  - dynamic state bank: `0x143D763E0` stride `0x2B0`
  - static tuning bank: `0x1430732F8` stride `0xC0`
  - owner family is now known to split into:
    - `CameraManagerCustom` global bundle at `0x143053850`
    - live `0x1FA0` overlay object from `0x14073B6E0`
    - shared `CameraControlStrafe`-typed constructor base `0x140772E20`
  - `CameraManagerCustom` vtable live policy slots now pinned:
    - `+0x10 = 0x14073C8A0`
    - `+0x18 = 0x14073CDD0`
    - `+0x20 = 0x14073C400`
    - `+0x28 = 0x14073B650`
    - `+0x30 = 0x14073C620`
    - `+0x38 = 0x14073C760`
- identify the resolver-owner contract above `0x143FE1070->virtual +0xA8`
- identify the campaign caller that can drive
  `0x141C0DFD0 / 0x141C075DC` with a Nadine-compatible override
- route-local camera lifecycle is now pinned around:
  - `0x141C04C50`
  - `0x141C04CE0`
  - `0x141C077B0`
  - `0x141C078D0`
  - `0x141C06F20`
  - `0x141C0E9F0`
- current direct `0x141C078D0` gameplay callers still resolve `screen 0`;
  treat `screen 1` as a separate unresolved updater/attach problem
- new correction:
  - `0x14073E330` is a generic per-screen route push, not a screen-0 helper
  - `0x1413CB6F0..0x1413CB80C` already drives that seam in a stock two-screen
    loop over `ebx = 0..1`
  - `0x1413CF013..0x1413CF14A` is a sibling stock two-screen loop on the same seam
  - both loops sit under the `CameraControlFollow` string/type family
- current proven stock two-screen content is still descriptor-routing /
  contextual-follow refresh, not yet stable Nadine follow ownership
  - descriptor routing already feeds a heavier mode-based route-child update
    stack:
    - `0x141C0E590`
    - `0x141C0ECC0`
- keep the manager0 retained-camera lifecycle as fallback if the route-entity
  family stalls
- prove a stable second view can follow Nadine without relying on dormant MP-only assumptions
- treat split/local-player camera seams as supporting evidence, not solved architecture

### 3. Split/local-player activation as supporting seam (secondary)

Keep using the proven activation path where useful:
- `net-login-split-screen` can boot local-player/split state in campaign
- `local-player-1` can be hook-bound to `NadineBuddy`
- render/HUD split builders exist and react
- stock player-2 content under that path is still refreshed from the `ctxB` /
  `NetPlayerData` family, not from a native Nadine camera owner path
- stock campaign actor resolution for `local-player-1` is still a separate
  problem from the split/session summary path
- `0x143FE1070` is a startup-copied local-player routing block, not a direct
  live actor pointer
- `0x141C04F80` builds the two-entry screen-route pair table at `0x14697AD50`
- `0x141C06420` resolves a live screen-route entity from that table; it is not
  a raw `CameraManager(index)` getter
- `0x141C0DFD0` in the attach path already resolves actor through
  `(*(qword*)0x143FE1070)->virtual +0xA8(route+0x124)`
- first non-manual gameplay caller above that seam is now visible:
  - `0x14073C9E3 / 0x14073CB4D / 0x14073CBF6 / 0x14073CCBC`
  - repeatedly selects route children through `0x141C0D4B0`
  - compares against camera descriptor/sentinel objects
  - then pushes stack-built descriptors through `0x141C0DFD0`
- that caller cluster now resolves to a concrete per-screen camera-overlay
  family:
  - `0x14073C8A0` updates overlay priority state keyed by `route+0x124`
  - `0x14073CDD0` builds the overlay follow-state from route descriptors
  - RTTI anchor:
    - `CameraManagerCustom`
    - `ICameraManagerCustom`
    - `CameraControlStick`
  - confirmed descriptors include:
    - `kCameraStrafe`
    - `kCameraSwim`
    - `kCameraZoom`
    - `kCameraMelee`
    - `kCameraMeleeOverride`
    - `kCameraContextualLook`
- `0x141C07420 / 0x141C074E0 / 0x141C07640` are the concrete lane-state and
  attach/detach helpers for the manual-camera path
- manual-camera layer caches resolved actor pair in `0x143D76C10/18`, writes
  attached target pair into `route+0x2A8/+0x2B0`, and only then drops into
  follow-state logic; `0x141C07E20` still looks debug/manual-heavy
- the mature gameplay-facing retained-camera lifecycle is still the manager0 path
  around `0x1410F3350 / 0x1410F3DD0 / 0x141C078D0 / 0x140EDE5E0`
- `0x140C34AC0` is only a downstream contribution-ring worker over the
  already-seeded secondary pair `+0x70/+0x78`; it does not establish either
  director pair family

But do NOT let this displace the primary questions above until Nadine control and
camera are both real.

### 4. Two-PC delivery / transport (deferred)

Only after 1 and 2 are proven:
- second-PC display delivery
- input transport
- host-authoritative packaging
- Remote Play / other transport decisions

## Current Working Anchors

Keep these as the shortest useful anchor set:

- shutdown:
  - `0x1409FD360`
- restart scheduling:
  - `0x1409FBA60`
  - `0x1409F9B20`
  - `0x1409FA94E`
- deferred queue:
  - `0x1415B43B0`
  - `0x1415B4870`
  - `0x1415B47C0`
  - `0x1415B4560`
  - global `0x1437E9700`
- ctxB / service side:
  - `0x143E08C80`
  - `0x143E0EF00`
  - `0x143E0EF08`
  - `0x143880318`
  - `0x14194D5F0`
  - `0x14194E3B0`
  - `0x14194E510`
  - `0x14194C870`
  - `0x14194C980`
- second-player net path:
  - `0x140A90550`
  - `0x140A9AAD0`
  - `0x140B347C0`
- local-player resolver/routing:
  - `0x143FE1070`
- second-camera manual path:
  - `0x141C04F80`
  - `0x141C051F0`
  - `0x141C06420`
  - `0x141C0D4B0`
  - `0x141C0DFD0`
  - `0x14073E330`
  - `0x1413CB6F0`
  - `0x141C0E590`
  - `0x141C0ECC0`
  - `0x140238500`
  - `0x140238800`
  - `0x140C30290`
  - `0x141C04CE0`
  - `0x141C07420`
  - `0x141C074E0`
  - `0x141C07640`
  - `0x141C07E20`
  - `0x143D76C10`

## De-Prioritized Until Further Notice

Do not make these the main track right now:

- polishing `NadineBuddy` local control
- reviving old `slot1_probe*.dll` iterations
- clean packed IDA database work
- camera polish before real second-player ownership exists
- transport/render work before host-side second-player path is real

## Already Established

These points are already established and should not be re-reversed first:

- startup bind for shared contexts does exist
  - outer callback: `0x1409F9B20`
  - inner bringup block: `0x1409FA94E`
  - service bind path: `0x143880318 -> virtual +0x20`
- the main live blocker is not "startup never binds `ctxB`"
- `0x1409FD360` is broad shutdown/teardown, not a tiny local helper
- `0x1409FBA60` does not jump straight into startup
  - it schedules deferred work instead
- deferred restart delivery already narrows to the queue rooted at `0x1437E9700`
- blind `slot1_probe*.dll` style runtime pokes are no longer the primary path
- `BuildRespawnTransformBlock` is not the current main blocker
- the current frontier is local-player resolver ownership plus Nadine camera ownership,
  not deferred net restart/bind work
- the live camera frontier now sits above generic route helpers in the mounted
  animated-camera owner at `player+0x36C0`
- that owner now has a class identity:
  `PlayerCameraAnimationDirector`
- `player+0x100` hidden authored-descriptor refresh is now separated from the
  canonical Player live pair at `+0xA8/+0xBC`
- the canonical `+0xA8/+0xBC` pair is now explained as generic runtime-object
  identity:
  - base ctor `0x1416A3330` clears `+0xA8` and seeds `+0xBC`
  - materializer `0x1417C3790` later writes the live node into `+0xA8` and
    recomputes `+0xBC`
- the `PlayerCameraAnimationDirector` ctor already seeds the stable secondary
  pair:
  - `0x1410DDED3` passes `rdx = rsi`
  - `0x140C30290` copies that Player's `+0xA8/+0xBC` into `+0x70/+0x78`
  - while leaving the primary pair `+0x60/+0x68` zeroed
- `0x140238800` is now confirmed as the first direct writer of
  `PlayerCameraAnimationDirector +0x60/+0x68`, but only as a
  consume-and-clear path:
  - it uses the transient pair during animated-camera route update
  - then writes a zero inline pair back into `+0x60/+0x68`
- the formal `PlayerCameraAnimationDirector` vtable surface is now typed
  tightly enough to rule out hidden pending-pair setters inside the class
  contract itself:
  - `+0x08 = 0x140C303E0` only adjusts contribution-ring base `+0xD8`
  - `+0x28 = 0x140C30760` and `+0x30 = 0x140C30930` only derive timed
    outputs from the stable pair `+0x70/+0x78`
  - `+0x38 = 0x140C35CF0` is a no-op
  - `+0x18/+0x20` only return helper code pointers
- `0x140237E60` belongs to the same fallback/reset side of that lifecycle
- the broad gameplay caller around `0x1410E9D80..0x1410EA0D2` still does not
  seed a non-zero pending pair before `0x140238800`; its post-call candidate
  loop feeds `player+0x100` / hidden-descriptor work through `0x1403D4070`,
  not `player+0x36C0`
- one live director predicate path already comes back through retained slot `0`
  twice via:
  - `0x1410F0D10(0)`
  - `0x1410F0D10(0)`
  - `returned_entity+0x36C0`
  - `0x140237760`
- the next concrete camera task is to find which runtime-materialization path
  produces the incoming source object passed as `rdx` into
  `ConstructPlayerCameraAnimationDirectorSubobject`, and separately to find
  the upstream non-zero seeder for the transient primary pair
  `+0x60/+0x68` before `0x140238800` consumes and clears it; that seeder now
  looks more likely to be an external helper above the director than a hidden
  virtual slot inside it
- on the direct-control side, the strongest new playable-Nadine seam sits above
  raw `MoveTo`:
  - `0x141E0ED10` switches locomotion-controller mode at `owner+0x15B8`
  - `0x141E0EDF0` applies a full snapshot block into
    `+0x40/+0x50/+0x60`
  - a shared caller around `0x141E18B03` feeds both from
    `source+0x230/+0x240/+0x250`
  - that makes the `owner+0x15B8` mode/snapshot layer a stronger first-demo
    control ingress than the older nav-goal backend
- one level above that, `0x141DE9E60(owner, record, mode, flag)` remains the
  stronger real control/application bridge:
  - validates the incoming state record
  - copies its transform into owner `+0x130/+0x140`
  - runs owner virtual `+0x1E8/+0x208`
  - then drops into the shared `owner+0x15B8` mode/snapshot layer
  - later re-dispatches synthesized state through the resolved live actor

Full evidence and chronology remain in `reports/initial-findings.md`.

## Notes

- Detailed reverse-engineering evidence belongs in
  `reports/initial-findings.md`.
- Reproducible labels/comments belong in `scripts/ida_apply_labels.py`.
- `ROADMAP.md` should stay short and directional, not become a reverse log.

## DECISION (2026-06-19): Clone-Player route for playable Nadine

Chosen approach (user decision):
- Use the spawned slot-1 **Player** clone (correct player class / control stack).
- Make it look like Nadine via **spawn-as-Nadine**: set the character look id
  to nadine at SPAWN time so the engine streams Nadine's full assets
  (model + skeleton + collision). Post-spawn `snapshot+0x144` patch only swaps
  the base partially => bald Chloe (model/collision are streamed at spawn).
- Then **delete the NPC NadineBuddy** so there is only one Nadine.

Rationale: a live NadineBuddy cannot be converted to Player (fixed C++ class/
vtable/layout). Controlling the NPC works but is NPC-nav movement; the clone
gives the real player control class. Clone currently lacks model+collision until
spawned as Nadine.

Open sub-tasks on this route:
1. spawn-as-Nadine: find where the spawn reads the char look id (descriptor has
   no hash per Codex; check ctx/a4/out or the source player's snapshot copied by
   materialize 0x1417C3790) and set it to nadine-base/nadine-default before
   spawn so assets stream.
2. clean directional control of the slot-1 Player (bind spawned Player <-> per-
   slot input consumer / feed the real-input branch struct sub_1402893D0 reads).
3. delete / hide the NPC NadineBuddy.
4. camera on the clone (retained-slot swap moves the camera already).
