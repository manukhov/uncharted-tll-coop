# Task Pool

## Primary Objective

Полноценный кооп кампании между двумя копиями игры. Текущий порядок работ:

- локальная Chloe на первой копии и локальная Nadine на второй;
- полное представление удалённого игрока в обеих копиях;
- синхронизация всего состояния и всех действий игроков;
- синхронизация противников только после игроков.

## P0: Full Player Synchronization (CURRENT)

- [ ] Получить устойчивый объект удалённого игрока в обеих копиях:
      Nadine на стороне Chloe и Chloe на стороне Nadine.
- [x] Чисто проверить второй retained `Player` без ввода слота 1, второй камеры и
      split login: прицеливание Chloe работает, само наличие Player не является
      оружейным запретом.
- [x] Устранить гонку двух Player в локальной политике геймпада. Подтверждено:
      общий `joypad-2` уходит в `free_count=-1/-2` и падает в `0x141839FB0`.
      Сериализация оригинальной функции выдержала повторную загрузку и более двух
      минут работы; счётчики пула остались корректными.
- [ ] Отделить удалённый `Player` от локального ввода, камеры и проверки числа
      локальных игроков, сохранив его модель, скелет и граф действий.
- [x] Разделить камеры в проверке двух копий на одном ПК: фоновая копия больше не
      читает общий XInput-геймпад. Для двух отдельных ПК фильтр не применяется.
- [x] Устранить ложную смерть при переключении окон: удалённый `Player`
      удерживается на последнем подтверждённом положении и здоровье, пока фоновая
      копия приостановила пакеты. Повторное переключение прошло без смерти.
- [ ] Заменить текущий `NadineBuddy + MoveTo` на непосредственное применение
      состояния удалённого игрока.
- [ ] Снимки: положение, поворот, скорость, режим движения, направление взгляда
      и прицела, текущее оружие, здоровье.
- [~] Положение, поворот, скорость и здоровье уже передаются. Сырые данные по
      цепочке `Player+0x1A8 -> +0x10` проверены и отвергнуты: это 32-байтовые
      записи эффектов с межпроцессными указателями, не кости персонажа.
- [ ] После восстановления движения: в одной копии игры получить 4096-битный поток команд
      анимации локального игрока через `0x140BC4E60`, разобрать его штатным
      путём и подать в `NetAnimCmdGenerator` клона по `Player+0x1B30`.
- [x] Найти настоящий источник выстрела кампании:
      `PlayerFireController::Fire = 0x140E2C730`, владелец в
      `controller+0x108`, оружие передаётся отдельным аргументом. Сетевое событие
      пули `0x140A46D70` в кампании не вызывается.
- [~] Здоровье игрока добавлено в `StatePacket` v3. Исправлена начальная ложная
      смерть удалённого `Player`: его HealthSystem создавался с `0/100`, теперь
      перед включением ввода получает фактическое здоровье второй копии. Текущее
      здоровье Player находится в `HealthSystem+0x38` (не enemy-смещение `+0x3C`).
      При alt-tab последнее подтверждённое здоровье сохраняется после истечения
      свежести пакета; действия удалённого игрока при этом обнуляются. Дополнительно
      клон замораживается на последнем положении, а четыре локальных входа смерти
      `Player +0xBE8..+0xC00` блокируются, пока настоящий игрок сообщает HP > 0.
- [ ] Надёжные события: выстрел, перезарядка, смена оружия, ближний бой, прыжок,
      уклонение, взаимодействие, получение урона, смерть и возрождение.
- [~] Подать сетевую маску кнопок в штатный ввод слота 1. Первый слой добавлен:
      удерживаемые прицеливание/огонь идут через `FrameState+0x6FC8+0x28`, при
      устаревшем пакете ввод очищается. Надёжная доставка одиночных событий ещё
      требуется.
- [x] Сравнить полный объект `Joypad` слота 1 по правильному адресу
      `FrameState+0xDB0+0xBF0`: обе копии дали `JOYPAD-DIFF total=0`, устойчивого
      поля активации там нет. Массовая аппаратная точка на горячем обработчике
      завершила обе игры и отвергнута.
- [x] Безопасно сняты восемь чисел `block[1]`. Круговая ветка дала
      `[cos,sin,0.5,-0.72]`, программная `[sx,sy,0.5,0]`; воспроизведение
      правого стика `0.5/-0.72` в программной ветке клон не оживило. Значения
      блока ввода не являются недостающим включателем.
- [x] Найден настоящий слой чтения движения: `StickMoveInterface` vtable
      `0x142A30740`, основной обработчик `PlayerMoveStick_MainUpdate
      0x140F03500`.
- [x] Подтверждена привязка: в обеих копиях основной Player имеет
      `joypad_index=2, screen_index=0`, клон — `joypad_index=3,
      screen_index=1`. Номера retained-таблицы 0/1 нельзя использовать как
      номера объектов `Joypad` 2/3.
- [x] Player-метод №16 `0x140C9F8B0` вызывается и для клона, но до исправления
      маршрута публиковал нулевое движение: Joypad 3 терял оси внутри
      `Input_ScaleCopyVirtual 0x14183B410`.
- [x] Найден и подтверждён последний блокирующий фильтр: фоновая копия имела
      `byte_1437EA62C=0`. Узкий обход только на время обработки Joypad 3 сохраняет
      оси в `Joypad3+0x48`, после чего штатный `PlayerInputCommandState` публикует
      ненулевые `Player+0x39C8/+0x3A1C/+0x3A30`. Joypad 2 не изменяется.
- [ ] БЛИЖАЙШАЯ ЗАДАЧА: заменить погоню клона за сетевой позицией на передачу
      штатного результата движения локального Player. Определить минимальный
      устойчивый набор полей из `PlayerInputCommandState` и точку их применения
      к клону после `0x140C9F8B0`, не смешивая движение с коррекцией позиции.
- [ ] Сложные режимы: укрытия, лазание, уступы, верёвка, транспорт и совместные
      действия.
- [ ] Жизненный цикл кампании: контрольные точки, сцены и переходы между главами.
- [ ] Проверить длительную работу двух копий без аварийного завершения и
      накопления расхождения.

Сейчас `StatePacket` v3 передаёт положение, поворот, скорость, здоровье, обе пары
осей и полную 64-битную маску кнопок с фронтами нажатия/отпускания. Найденный
глобальный блок ввода в кампании остаётся нулевым, поэтому поля действий пока не
содержат настоящего локального ввода. Для разовых действий нужен отдельный
надёжный упорядоченный поток с номерами и подтверждениями.

## P1: Enemy Synchronization (DEFERRED)

- [ ] Не продолжать до закрытия P0.
- [x] Сохранить найденные безопасные пути урона и `npc-die` в коде, отчёте и IDA.
- [x] Выключить отправку/применение противников и передачу их урона по умолчанию.

## Previous Tracks (reference)

### ACHIEVED (2026-06-15): Real second playable local player in campaign

Driving `SpawnPlayerFromRetainedDescriptor(desc, idx=1, ctx, ...)` (captured args
from a real Chloe spawn via HW-breakpoint, reissued with index=1, hotkey F12)
produced a SECOND CONTROLLABLE PLAYER in single-player campaign (a Chloe clone).
No crash. This validates the converged model and is the project's first real
second player. Repro in initial-findings.md "MAJOR BREAKTHROUGH".

### P-next: Turn the second player into Nadine co-op
- [x] Runtime check: clean `block[1]` feed and clean `FrameState+0x6FC8+0x28`
      branch1 feed are insufficient. They either do not move the slot-1 clone,
      or bleed into Chloe. Do not repeat this input-struct path as primary.
- [x] Runtime check: spawn descriptor + immediate ctx/a4/out/stack do NOT carry
      direct `chloe-base`, `coop-chloe`, or `nadine-base` hashes. Text hits
      containing `nadine` in ctx pointer targets are level/script names, not the
      player model selector. Do not repeat this descriptor/near-ctx hash scan.
- [x] Runtime check: patching slot0 `PlayerSnapshot+0x144` to `nadine-base`
      before reissuing slot1 spawn is unsafe/unproven. Reissue returned, then
      `tll.exe` was gone; no post-reissue slot dump possible. Do not use this as
      the primary spawn-as-Nadine path.
- [x] Static: find the builder/materializer that fills `player+0xB0`
      (`PlayerSnapshot`) and writes `snapshot+0x140/0x144 = chloe-base`.
      Post-spawn `PlayerSnapshot+0x144 = nadine-base` is stable but visually
      ineffective; the Nadine model must be selected before or during Player
      materialization. Result: `PlayerSnapshot` is downstream. The live source
      is `player+0x190`, seeded in `0x141BB4280` from an upper visual-state pair
      before `Player+0x6F0` builds the visual graph.
- [x] Runtime: one-shot `player+0x190` override at `0x141BB4B14` during slot-1
      spawn fires and writes Nadine's state id. Result was partial: only hair
      remained on the clone, which proves `player+0x170` and `player+0x190`
      must be patched as one visual pair.
- [x] Runtime: validate the paired `teleport_hook.cpp` override:
      `0x141BB4B01 -> player+0x170` and
      `0x141BB4B14 -> player+0x190`. Logs confirmed both
      `VISUAL look seed hit ... PATCHED` and
      `VISUAL state seed hit ... PATCHED`.
- [x] Visual check: paired override fired in live runtime but still produced
      floating Nadine hair only. `tll_slot_dump` confirmed slot1 has Nadine
      `+170/+190`, so those two fields are necessary but not sufficient.
- [ ] IMMEDIATE: identify the body/rig source still left on the player path.
      Current hard clues:
      `player+0x174` is copied from `player+0xD8` at `0x141BB4A50`;
      slot1 still has `+174=0xcde4b742` and `+180=0xde7baff5`;
      the visual graph is produced through `player->virtual +0x6F0` and
      `+0x700`, with result stored at `player+0x1A8`.
- [ ] Make 2nd player look/character = Nadine (descriptor character/look fields,
      or post-spawn retarget) instead of Chloe clone.
- [ ] Independent camera for player 2 (screen-1 route now has a valid slot-1 actor).
- [ ] Route separate input (PC2) to player 2.
- [ ] Stability: build a clean spawn descriptor instead of reusing captured stack copy.
- [ ] Checkpoints/death/respawn behavior with 2 players.

### P0: Rebind the real second-player lifecycle  [CLOSED — reframed]

- [x] Find the consumer/drain path for the deferred queue rooted at `0x1437E9700`.
  - Queue = generic **ndjob** worker pool; always drains. Not the blocker.
- [x] Prove when the queued callback `0x1409F9B20` actually executes.
  - Scheduled only by `NdiNetInit` (`0x1409FBA60`, `game/net/flow.cpp`), gated on
    `PAGE_MULTIPLAYER_CONNECTING`. Campaign never runs it.
- [x] Explain why live campaign can still end up with `ctxB+0x8 == 0`.
  - ctxB rebind is multiplayer-net-init gated; campaign never re-binds it.

Reframed conclusion: do NOT chase the ctxB owner bind in campaign (that means
booting multiplayer net). Pivot second-player bringup to **ctxA**
(`0x143E08C80`) / net-player manager, which `AddOrUpdateNetPlayer` already uses
as the real source context.

### P0b: Second-player bringup via ctxA path (new active track)

- [x] Map how `AddOrUpdateNetPlayer (0x140A90550)` populates a player slot from
      a ctxA source entry. ABI: rcx=manager 0x143DD70C0, rdx=ctxA 0x143E08C80,
      r8=source_entry, r9d=slot, [rsp+0x20]=is_local; rax=slot (10 × 0x43D0).
      Source resolved by name/id against ctxA's 12-entry table (ctxA+0x800,
      stride 0x1C0). Remote path = `sub_140A58190` (is_local=0); also a
      "late-join" variant `0x140A90A00`.
- [x] DECISIVE runtime check: NetGameManager is **DORMANT** in campaign.
      `tll_netplayer_probe.exe` in confirmed campaign gameplay -> 0/10 slots,
      Chloe not registered through the manager. ctxA net-player path is
      multiplayer-only. => P0b net route abandoned for campaign co-op.

P0b verdict: both net-player seams (ctxB owner via NdiNetInit, ctxA via
NetGameManager) are multiplayer-dormant in campaign. Do NOT pursue them; that
means booting multiplayer net infra. New primary track = P1 (Nadine elevation).

### P0c: Activate the engine's native split-screen / local-2-player in campaign [DONE]

- [x] Runtime activation probe: `CameraManager(1)` is live in campaign.
- [x] Runtime activation probe: the second screen-route entity / second view path
      resolves live in campaign.
- [x] Find `g_maxLocalPlayers` global: `0x1437CF310` (live low dword limit = 1).
- [x] Decode split-screen activation chain:
      - `net-is-second-player-available?` = `0x140C021C0` (gate byte `0x143071038`)
      - `net-login-split-screen` = `0x140C02220`
      - `net-logout-split-screen` = `0x140C02420`
- [x] Prove native local-player-1 boot in campaign.
      Runtime diagnostic on game thread: with
      `max_local_players=2` + `avail_byte=1`, login path succeeds in campaign
      (`login_state=3`, query=1, cam0/cam1 resolved).

Verdict: the engine's own local-2-player / split-screen machinery CAN be raised
in campaign without a full multiplayer session.

### P1: Populate retained-player slot 1 in `g_RetainedPlayerSlotTable 0x143EEBE80` (CURRENT primary)

RESOLVED model (reader + writer both pinned):
- Campaign local-player resolution funnels:
  `(*(0x143FE1070))->vtable+0xA8 (0x140298D70)` -> split gate `0x143E07473`
  inactive -> `0x1410F0F60 ResolveRetainedPlayerHandleBySlot(index)` ->
  reads pair `0x143EEBE80 + index*0x10` {ptr+0, id+8}, validates
  `ptr->(+8)->+0xBC==id`.
- Slot 0 = Chloe; slot 1 empty in campaign => `local-player-1 == 0`.
- WRITER of these pairs = `0x1410F14C0 CommitResolvedPlayerSpawn` (retained
  player-spawn commit; ProcessSpawnInfo packet, materialize via 0x1417C3790,
  store +0xA8/+0xBC into 0x143EEBE80).
- Earlier leads now closed as NOT the blocker: CharacterManager registration
  (Nadine already char 1), resolver-struct `0x143FE1070 +0xE8/+0x104` id slots
  (different registry), AddCharacter `0x141D63420`.

- [x] DIRECT runtime test: wrote Nadine pair into slot 1 (0x143EEBE90/98).
      Pair format was VALID (matches reader), but game CRASHED -> consumers read
      slot 1 and need a full materialized player runtime, not a raw pair.
- [x] Decoded `0x1410F14C0 CommitResolvedPlayerSpawn` store contract:
      slot index = arg r8 (->r12); pair `{obj+0xA8,obj+0xBC}` where obj =
      `0x1417C3790` materialized player object; built after ProcessSpawnInfo
      packet (0x1417C32A0, vtable 0x14291CF98) + record resolve (0x1417651D0).
- [x] Mapped full spawn pipeline (index-parameterized):
      `ScriptSpawnPlayer 0x140F73910` (script command via LEA @0x14009AE01;
      player index in esi) -> `SpawnPlayerFromRetainedDescriptor 0x1410F1910` /
      `SpawnPlayerFallback 0x1410F3350` -> `CommitResolvedPlayerSpawn 0x1410F14C0`
      (slot=r8) -> materialize `0x1417C3790` -> store pair into 0x143EEBE80[index].
      Engine structurally supports spawning into slot 1; raw poke crashed because
      it skipped this pipeline.
- [x] GRAND SYNTHESIS: spawn slot index = SCREEN-ROUTE index (not a plain player
      number). `ScriptSpawnPlayer` hardcodes index 0 (xor esi,esi); slot index is
      derived via `0x141C077B0(route index, assert<=1)` reading two-screen route
      table `0x14697AD50`. spawn + local-player slot table 0x143EEBE80 + camera
      all hang off the SAME route 0/1. Route 1 inactive in campaign => slot 1
      empty everywhere; raw slot poke crashed because route-1 entity not stood up.
- [x] CORRECTION: `CameraManager::Init 0x141C04F70` builds BOTH route entities
      (loop ebx 0..1) into `0x14697AD50`. Route entity 1 ALREADY EXISTS
      (slot1 {0x10E0200188, 0x1A} live). Blocker is NOT a missing entity.
- [ ] ROOT GATE (narrowed): mark route 1 ACTIVE. `0x141C077B0` requires
      `route->+0xB0` child with `+0x55!=0` and `+0x56!=0` plus a bit via selector
      `0x14697AE34`. Need runtime diff route0(0x10E0200160 active) vs
      route1(0x10E0200188 inactive): +0xB0 child, +0x55/+0x56, selector bit.
- [ ] Find the route-activation function that sets those + whether split-screen
      login drives it; then let the spawn pipeline fill slot 1 (no poke).

### P1-old: Nadine camera ownership (subsumed by P1 above)

- [ ] Prove campaign-native independent camera ownership for Nadine.
- [~] Type the screen-route owner family that carries `route+0x124`.
      Current strong anchor:
      - `0x14073C8A0` = per-screen camera-overlay priority updater
      - `0x14073CDD0` = per-screen camera-overlay follow-state builder
      - dynamic state bank `0x143D763E0` with stride `0x2B0`
      - static tuning bank `0x1430732F8` with stride `0xC0`
      - RTTI-adjacent owner family:
        `CameraManagerCustom / ICameraManagerCustom / CameraControlStick`
      - new split:
        - `0x143053850` = CameraManagerCustom-typed global bundle
        - `0x14073B6E0` = alloc/init wrapper for live `0x1FA0` overlay object
        - `0x140772E20` = shared CameraControlStrafe-typed large constructor base
      - `CameraManagerCustom` live vtable policy slots:
        - `+0x10 = 0x14073C8A0`
        - `+0x18 = 0x14073CDD0`
        - `+0x20 = 0x14073C400`
        - `+0x28 = 0x14073B650`
        - `+0x30 = 0x14073C620`
        - `+0x38 = 0x14073C760`
- [ ] Identify the resolver-owner contract behind `0x143FE1070->virtual +0xA8`.
- [~] Identify the campaign caller that can drive
      `0x141C0DFD0 / 0x141C075DC` with a Nadine-compatible override.
      Strong candidate found:
      - gameplay camera-priority cluster around
        `0x14073C9E3 / 0x14073CB4D / 0x14073CBF6 / 0x14073CCBC`
      - repeatedly selects camera child through `0x141C0D4B0`
      - compares against descriptor/sentinel objects
      - pushes stack-built descriptors through `0x141C0DFD0`
      - now typed as stock camera-control overlays including:
        `kCameraStrafe`, `kCameraSwim`, `kCameraZoom`,
        `kCameraMelee`, `kCameraMeleeOverride`,
        `kCameraContextualLook`
- [~] Keep the route-local camera lifecycle pinned as the main owner path:
      - `0x141C04C50`
      - `0x141C04CE0`
      - `0x141C077B0`
      - `0x141C078D0`
      - `0x141C06F20`
      - `0x141C0E9F0`
- [ ] Prove whether `screen 1` has a distinct route updater, or only a
      descriptor-only attach path above `0x141C0DFD0`.
      Current correction:
      - direct `0x141C078D0` gameplay callers checked so far still resolve `screen 0`
      - but `0x14073E330` is a generic per-screen route push
      - and `0x1413CB6F0..0x1413CB80C` already drives that seam in a stock
        two-screen loop for the `kCameraContextualLook`-side descriptor family
      - sibling loop `0x1413CF013..0x1413CF14A` does the same under the
        `CameraControlFollow` string/type family
      - downstream of that, `0x141C0E590 -> 0x141C0ECC0` already performs
        mode-based route-child selection and heavy route-state recompute
- [ ] Identify the active follow/target/owner object that drives Nadine's view.
- [~] Determine whether the screen-route family can be cleanly re-targeted to Nadine.
      Current concrete seam:
      - `0x141C04F80` builds the two-entry route table `0x14697AD50`
      - `0x141C06420` resolves a live route entity from that table
      - `0x141C0DFD0` resolves actor through `(*(qword*)0x143FE1070)->virtual +0xA8(route+0x124)`
      - `0x141C075DC` writes the attached target pair into `route+0x2A8/+0x2B0`
- [ ] Determine whether split/local-player camera seams can stably HELP this path without treating split-screen as solved architecture.
- [ ] Separate stock `local-player-1` resolver-owner path from split/session summary helpers; current resolver bundle = `0x143FE1070`, stock player-2 feed = `ctxB`.
- [ ] Keep the manager0 retained-camera lifecycle ready as fallback:
      `0x1410F3350 -> 0x1410F3DD0 -> 0x141C078D0 -> 0x141C074E0`
- [ ] Treat the retained/director bridge conservatively:
      the currently proven live bridge uses `0x1410F0D10(0)` twice before
      `returned_entity+0x36C0 -> 0x140237760`; no symmetric slot-`1`
      director bridge is proven yet.
- [ ] Keep `0x140C34AC0` in the right layer:
      it advances contribution/ring state from the already-seeded secondary
      pair `+0x70/+0x78` and is not a writer for `+0x60/+0x68` or
      `+0x70/+0x78`.
- [ ] Split the pair-writer search correctly:
      - upward from `0x140C30290`, now knowing that
        `0x1410DDED3` passes the top-level `Player` itself as `rdx`, so the
        remaining question there is how to substitute or mirror that canonical
        Player pair for Nadine
      - separately upward from the still-unseeded primary pair
        `director+0x60/+0x68`
- [ ] If the route-entity family stalls, identify where the manager0 retained-camera lifecycle can be reused
      or adapted for Nadine.
- [x] Decode exact campaign-visible manual-camera cluster:
      - `Manual Camera 1/2`
      - `Joypad 1/2 Controls Game`
      - `Swap Joypad Screens`
      - `Bind Manual Cam To Player(/Parent)`
- [x] Reclassify `0x141C07E20`:
      manual/debug follow writer, not yet the final gameplay camera seam.

### P1a: Nadine direct control (parked but open)

- [~] Find Nadine's real control/locomotion ownership layer (NOT goal-based `MoveTo`).
      Quick probes done: `Type7` writes but does not move; `Type4` gave no usable effect;
      owner mode `+0x1748` and setter `0x141DE5A10` stay within buddy-family behavior.
- [x] Reframe the strongest current control-side seams above raw `MoveTo`:
      - `0x141DE9E60(owner, record, mode, flag)` = record-driven owner apply
      - shared `owner+0x15B8` mode/snapshot layer
        (`0x141E0ED10 / 0x141E0EDF0`)
      - indirect `BehaviorScavenge` materialized-state install path under
        `owner+0x1F80` (`0x1404B7F00 / 0x140473590`)
- [x] Separate the prepared NPC action layer from the stronger live control layer:
      - `0x1403F5B60` plus the surrounding `0x140545000..0x14056B000` writers
        now read as prepared NPC action/behavior commands
      - they are useful, but not the first clean seam for direct playable control
- [~] Keep the strongest known live non-script branch in front:
      - `0x14058B900`
      - `0x14058BA40` = mode-1 snapshot apply into `owner+0x15B8`
      - `0x14058BC70` = mode-2 snapshot apply plus nested-control follow-up
      - both already drive `0x141E0EDF0 / 0x141E0ED10`
- [x] Reclassify the higher `0x1406B3D70` family:
      - vtable block resolves to `Patrol@ScriptSkill`
      - `0x1406B3D70 / 0x1406B3640 / 0x1406B3940` are AI patrol logic,
        not the main direct-control seam for a playable Nadine
- [x] Reclassify the second direct caller above `0x141DE9E60`:
      - `0x140486EB0` belongs to `BehaviorTeleport`
      - so this path is also AI behavior logic, not raw player input
- [ ] Shift the main control frontier down to the shared controller layer:
      - `0x140416B20`
      - `0x141DE9E60`
      - `0x141E0ED10`
      - `0x141E0EDF0`
      - `ActionPack` lifecycle under `owner+0x15C0`:
        - `0x141E18350`
        - `0x141E18450`
        - `0x141E18700`
        - `0x141E189C0`
      - concrete shared snapshot/publish block `0x141E18A70..0x141E18BA3`
        (`source+0x230/+0x240/+0x250` -> `(owner+0x1D0)+0x15B8`)
      - sibling owner-local publish block `0x141E18D00`
        (`owner+0x258/+0x2F8/+0x300` -> same publication tail)
- [ ] Find the real producer of the source object for `0x141E18A70`:
      - now known to be a live `ActionPack` object
      - who materializes and selects it
      - who fills backup handles `owner+0xE90` and `owner+0x258`
      - who chooses between stop/stand, resume, and the other `ActionPack`
        branches over the same machinery
      - how the higher chooser at `owner+0x80` (`0x141E1A040`) routes Chloe
        into the moving-state branch `0x141E15C80`
      - how Chloe's live command-state mode map is written:
        `1=StopAndStand`, `2=MoveToLocation`, `3=MoveToActionPack`,
        `4=MoveAlongSpline`
      - who fills `source+0x230/+0x240/+0x250`
      - and whether Chloe's producer can be reused for `NadineBuddy`
- [ ] Treat `0x141E14890` as the new shared setter above those branches:
      - it accepts `owner`, `mode id`, and a prepared descriptor
      - writes:
        - `owner+0x444`
        - `owner+0x290/+0x2A0/+0x2B0`
        - `owner+0x2D0`
      - but still does not write `owner+0x80` directly
- [ ] Treat `owner+0x80` as likely downstream mirror, not the only live ingress:
      - ordinary gameplay callers already feed `0x141E14890` directly
      - current direct mode feed observed in callers:
        - `0`
        - `1`
        - `2`
        - `3`
        - `4`
      - descriptor sources already seen:
        - `owner+0x260`
        - `owner+0x290`
        - stack-built temporary descriptors
- [ ] Treat `0x141E1A000` as the broader ordinary-Chloe movement update loop:
      - correction: `0x141E1A000` is an internal label inside
        `0x141E19F90 = NavStateMachine::Update(bool)`
      - it runs `0x141E1BA20`
      - conditionally dispatches `owner+0x80` through `0x141E15C80` or `0x141E158B0`
      - mirrors the resolved descriptor into `owner+0x120..+0x1B0`
      - refreshes `owner+0x228/+0x218`
      - then handles traversal/action-pack follow-up and publication flags
- [ ] Treat `0x141E1AA20` as the second-stage path/result chooser inside that loop:
      - works from `owner+0x444`
      - compares `owner+0x2F8`, `owner+0xE90`, and `owner+0x258`
      - releases invalid/disappeared action-packs
      - rebuilds compact descriptors through `0x141E06C00`
      - feeds replacement modes `0`, `1`, `2`, and `4` into `0x141E14890`
- [ ] Treat `0x141E1E4F0` as the strongest ordinary moving-state family above the setter:
      - anchored by `UpdateStateMoving` strings
      - feeds `0x141E14890` with modes `2`, `3`, and `0`
      - checks arrival, waypoint changes, and moving-command success/failure
- [ ] Keep neighboring families separated so they do not become false primary seams:
      - `0x141E1F300` = post-root / interrupted-state update
      - `0x141E18120` = abandon interrupted state
      - `0x141E20280` = enter-animation update
- [ ] Climb above `0x141E14890` through the ordinary-Chloe chooser:
      - `0x141E1BA20`
      - unpackers `0x141E06940 / 0x141E06C00`
      - determine how it chooses live mode ids `0..4`
      - then test whether the same feed can be redirected into `NadineBuddy`
- [ ] Shift the next climb one level higher:
      - use the now-found outer edge
      - `0x141DE8CB1 -> 0x141E19F90`
      - treat the surrounding block `0x141DE8B80..0x141DE8E60` as
        `NavCharacter::Update`-side control flow
      - determine whether that is the real Chloe movement tick we want to redirect
- [ ] Climb above the `BehaviorScavenge` registration/dispatch family instead
      of blind flat-caller hunting:
      - `RegisterBehaviorScavengeMetadataRoots` (`0x1404B8180`)
      - metadata root `0x142952020`
      - live installer `0x1404B7F00`
- [ ] Climb above `0x14058B900` and identify the live owner/state family that
      chooses its mode-1 and mode-2 branches; this is now a better first
      playable-switch frontier than more work on prepared NPC command writers.
- [ ] Prefer the stronger prepared-state branch over metadata-root chasing:
      - `NavCharacter::SetStrafeMode`-style branch around `0x1404BA9C0`
      - `owner->virtual +0xC48`
      - `0x141DE4D20 -> 0x141E15030`
      - `0x141DE5190`
      - `0x141DE4F90`
- [ ] Resume from the nested-control tag / fallback `+0xD30` layer when camera work is blocked.
- [ ] Expand verbs later:
  - locomotion
  - traversal
  - interaction
  - combat
  - script-facing actor qualification
- [ ] Validate checkpoints / death / respawn on host.

### P1b: Split/local-player seam as SUPPORTING path

- [x] `local-player-1` is empty before split login.
- [x] `local-player-1` can be resolver-bound to live `NadineBuddy`.
- [x] stock campaign resolver can still return `0x0` for `local-player-1` even
      after split login is active; Nadine appears there only through the hook
      on resolver vfunc `+0xA8`.
- [x] split render layer reacts (artifact / blink) when activated.
- [x] stock split refresh still sources player-2 content through the old
      `ctxB` / `NetPlayerData` family rather than a campaign-native Nadine owner.
- [x] `ctxA` and `ctxB` are separate startup-built context classes.
- [x] `ctxB` is the stock player-2 source-selection / summary controller, not the
      final local-player actor resolver.
- [x] second-camera attach path already uses the same resolver seam as the hook:
      `0x141C0DFD0 -> (*(qword*)0x143FE1070)->virtual +0xA8`
- [x] `0x141C04F80` builds a two-entry screen-route pair table at `0x14697AD50`.
- [x] `0x141C06420` resolves a live screen-route entity from that table; it is
      not a raw `CameraManager(index)` getter.
- [x] `0x141C0D4B0` selects a preferred camera child from a route entity's
      `+0x100` child list.
- [x] `0x141C0DFD0` already has a non-manual gameplay caller cluster around
      `0x14073C9E3 / 0x14073CB4D / 0x14073CBF6 / 0x14073CCBC`.
- [x] that cluster now resolves to a concrete per-screen camera-overlay family:
      `0x14073C8A0 / 0x14073CDD0`.
- [x] the overlay family is no longer anonymous:
      `CameraManagerCustom / ICameraManagerCustom / CameraControlStick`
      sit in the same RTTI/vtable block.
- [x] manager0 has a mature gameplay-facing retained-camera lifecycle:
      `0x1410F3350 / 0x1410F3DD0 / 0x141C078D0 / 0x140EDE5E0`
- [ ] Determine whether second camera / HUD can be made stable enough to HELP the
      primary camera-ownership question, without treating split-screen as solved architecture.

### P2: Convert host-side second player into actual co-op

- [ ] Feed player-2 input from the second PC into the real Nadine player path.
- [ ] Replicate the minimum host-authoritative state needed for stable co-op.
- [ ] Bring up second-PC camera/render path.
- [ ] Validate playable two-PC loop in campaign.

## Current Anchors

Use these first:

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
  - `0x1437E9700`
- bind/service:
  - `0x143E08C80`
  - `0x143E0EF00`
  - `0x143E0EF08`
  - `0x143880318`
  - `0x14194D5F0`
  - `0x14194E3B0`
  - `0x14194E510`
  - `0x14194C870`
  - `0x14194C980`
- net-player path:
  - `0x140A90550`
  - `0x140A9AAD0`
  - `0x140B347C0`
- local-player resolver/routing:
  - `0x143FE1070`
- second-camera manual path:
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
- retained-camera gameplay path:
  - `0x1410F3350`
  - `0x1410F3DD0`
  - `0x141C078D0`
  - `0x140EDE5E0`

## Parked

Do later, not now:

- [ ] Clean packed `ida/tll.i64` rebuild.
- [ ] Return to buddy-control polish.
- [ ] Revive old `slot1_probe*.dll` style runtime probing.
- [ ] Camera polish before real second-player ownership exists.
- [ ] Second-PC rendering work before host-side second-player path is proven.

## Already Closed Enough

Do not reopen these as the first move:

- [x] Startup bind exists:
  - `0x1409F9B20`
  - `0x1409FA94E`
  - `0x143880318 -> virtual +0x20`
- [x] `ctxB` missing owner is not explained by "startup never ran"
- [x] `0x1409FD360` is already typed as upper shutdown/teardown
- [x] `0x1409FBA60` already narrowed to deferred restart scheduling
- [x] restart work already narrowed to queue root `0x1437E9700`
- [x] old `slot1_probe*.dll` loop is intentionally retired
- [x] `BuildRespawnTransformBlock` is not the current primary blocker
- [x] low net-player seams `0x140A90550 / 0x140A9AAD0 / 0x140B347C0` are not the first-return track right now
- [x] `ctxB` is not the final local-player actor/camera owner path; it is the stock
      player-2 source-selection / summary family feeding split refresh.
- [x] stock second-screen gameplay routing exists below `CameraControlFollow`, but
      the current stable frontier is the mounted animated-camera owner at
      `player+0x36C0`, not the older split summary layer
- [x] `player+0x36C0` now has a concrete class identity:
      `PlayerCameraAnimationDirector`
- [x] `player+0x100` hidden authored-descriptor refresh is now separated from
      the canonical Player live pair `+0xA8/+0xBC`
- [x] The canonical `+0xA8/+0xBC` pair is now explained as generic
      runtime-object identity:
      - base ctor `0x1416A3330` clears `+0xA8` and seeds `+0xBC`
      - materializer `0x1417C3790` later writes the live node into `+0xA8`
        and recomputes `+0xBC`
- [x] `0x140238800` directly touches `PlayerCameraAnimationDirector +0x60/+0x68`,
      but only as a consume-and-clear path:
      - it uses the transient pair during route/descriptor update
      - then writes a zero inline pair back into `+0x60/+0x68`
- [x] the formal `PlayerCameraAnimationDirector` vtable surface is now typed
      enough to rule out internal pending-pair setters:
      - `+0x08 = 0x140C303E0` only adjusts contribution-ring base `+0xD8`
      - `+0x28 = 0x140C30760` and `+0x30 = 0x140C30930` only derive timed
        outputs from the stable pair `+0x70/+0x78`
      - `+0x38 = 0x140C35CF0` is a no-op
      - `+0x18/+0x20` only return helper code pointers
- [x] `0x140237E60` belongs to the same fallback/reset side of the mounted
      animated-camera lifecycle
- [x] The broad gameplay caller around `0x1410E9D80..0x1410EA0D2` does not
      seed a non-zero director pending pair before `0x140238800`; its
      post-call `(ptr,id)` loop goes through `0x1403D4070` into `player+0x100`
      / hidden-descriptor work instead
- [x] One live `PlayerCameraAnimationDirector` predicate path already comes
      back through retained slot `0` via `0x1410F0D10(0)` and then
      `returned_entity+0x36C0 -> 0x140237760`
- [ ] Find which runtime-materialization path produces the `Player` object
      whose generic identity later seeds `PlayerCameraAnimationDirector`, then
      test whether director stable camera state can be redirected to
      `NadineBuddy` through that path
- [ ] Find the upstream **non-zero** seeder for the transient primary pair
      `PlayerCameraAnimationDirector +0x60/+0x68` before
      `0x140238800` consumes and clears it; the current best assumption is
      that this seeder lives in an external camera/animation helper above the
      director rather than in the director's own virtual interface
- [x] The first stronger-than-`MoveTo` control ingress is now pinned on the
      shared `owner+0x15B8` layer:
      - `0x141E0ED10` switches locomotion-controller mode at `+0x34`
      - `0x141E0EDF0` applies a full snapshot into `+0x40/+0x50/+0x60`
      - a shared caller around `0x141E18B03` feeds both from
        `source+0x230/+0x240/+0x250`
- [x] `0x141DE9E60(owner, record, mode, flag)` remains the stronger real
      control/application bridge above raw `MoveTo`:
      - validates the incoming record against `0x14386FD00`
      - copies `record+0x30/+0x40` into owner `+0x130/+0x140`
      - runs owner virtual `+0x1E8/+0x208`
      - then feeds the shared `owner+0x15B8` mode/snapshot layer
- [ ] Climb above that `owner+0x15B8` mode/snapshot path and determine which
      live source path materializes the incoming state record for
      `0x141DE9E60`, then determine whether it can be redirected from
      Chloe-like control input to `NadineBuddy` for the first playable switch
      demo

## Rule

If a task does not directly move the project toward **полноценный кооп**, it should stay parked.
