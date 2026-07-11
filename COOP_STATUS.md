# TLL Campaign Co-op — Status & Findings (2026-06-28)

## Turn note (2026-07-10, Codex - Exp 2/3 scheduler correction)

**REJECTED:** the latest Cursor/Claude direction treated `dword_143D3C0D0` as the
active Player scheduler list and `0x14042FCF0` as a Player update. Static RTTI and
call-graph checks disprove both links:

- `dword_143D3C0D0` is an `NpcManager` spot/path-search list; its tick reaches
  source-identified `Npc::UpdatePathSearch` `0x14043B830`.
- `0x14042FCF0` is `Npc/Buddy/Human` vtable `+0x410`; `0x1404901B0` is Alcazar's
  override. Real `Player` vtable `+0x410` is `Player_Update` `0x1410EC230`.
- `0x14042D120` is `Active@Npc` vtable `+0x20`, not a Player animation callback.
- `Player+0x1A0` is the upper Player state owner (`+0x700/+0x710` pipeline), not
  proven animation-instance/job state.

The factual runtime read remains useful: puppet is absent from the NPC spot list.
It does not justify registering the puppet there. The two crash-prone rejected
writes (direct `42FCF0(puppet)` and forced `+0x15A9 bit1`) are disabled in code.

**STATIC next path:** `AnimManager_AsyncObjectUpdateJointBlendJob` `0x141A92A00`
resolves a retained owner and, under job flag `0x20`, dispatches owner vtable
`+0x410/+0x418`. Trace creation/flag writers for that job and map it to slot0 vs
puppet before a read-only runtime comparison. This is the supported route toward
the missing pickup animation.

Details: `reports/real2p_exp3b_active_player_gate.txt`.

## Turn note (2026-07-10, Cursor — full pickup native recipe in hook)

**Сделано в `teleport_hook.cpp` / `hook_offsets.h`:**

- `seh_run_puppet_pickup_recipe`: `Player_ResolveWeaponPickupInteractAnim`
  (`0x140C7FF40`) → `InputScreenBlock_ActivateCommand` → optional
  `sub_141C937D0` radius prep + `InteractControl_GetAvailability` (`0x141C9A360`)
  when nearby `WeaponPickup` resolves → `Player_InteractDispatcher` tick → fallback
  `RequestInteract` + `Player_PublishRequestedInteract`.
- **Timing fix (2026-07-10 runtime):** gesture больше не армится при `STORED`;
  `queue_relay_pickup_gesture_after_mirror` ждёт `SLOT-MIRROR gave=1`, затем
  `PICKUP-GESTURE-ARM puppet_has_art=1`. `apply_puppet_pickup_gesture` /
  `apply_peer_pickup_animation` пропускают кадры пока art не на кукле.
- Sender: `capture_weapon_pickup_for_relay` **до** `GiveWeapon` (все 6 screen
  dispatchers + interact-control +0x10/+0x80) → лог `PICKUP-ID-CAPTURE pre-give`.
- Исправлен баг: `GetAvailability` вызывался с `pickup` вместо `puppet` как arg2.
- Gesture window: 700ms → 1200ms per-frame re-activate.

**Билд:** `scanner/build/tll_teleport_hook_v2.dll` OK. Inject в PID 14224/18988 —
boot lines в `locomotion_hook.log`.

**Прогноз in-game теста:** подбор M-30 на host → `GIVEWEAPON-RELAY-STORED` затем
`SLOT-MIRROR ... gave=1` → `PICKUP-GESTURE-ARM puppet_has_art=1` →
`PICKUP-ANIM-APPLY resolved_cmd=1` (если C7FF40 резолвит после give на кукле).

**RUNTIME REJECTED (2026-07-10, PID 24424/27880):** timing fix сработал
(`PICKUP-GESTURE-ARM puppet_has_art=1` после `SLOT-MIRROR gave=1`), но bend нет:
`resolved_cmd=0 dispatch=0 pickup=0 pickup_id=0x0` — `C7FF40` на кукле не резолвит
cmdId даже с M-30 в слоте; nearby `WeaponPickup` на peer отсутствует; interact SM
куклы не крутится (та же стена, что aim-pitch). **PICKUP-ANIM ПАРКНУТ** — инвентарь
через `WeaponGivePacket`+`SLOT-MIRROR` работает; дальнейшие попытки через command/
dispatcher требуют materialize slot-1 interact controller (отдельный трек).

**Не переоткрывать:** play-simple-gesture, play-animation relay, force `+0x15A9` bit1,
direct `42FCF0(puppet)`, RegisterSpotPlayer для puppet.

## Turn note (2026-07-10, Cursor — Exp 3b spot-player enumerator pinned)

**Статика:** канонический **spot-player enumerator** — глобал `dword_143D3C0D0`
(RVA `0x3D3C0D0`). `*(gw+0)` = `m_spotPlayerCount` (loop bound во всех
`i < m_numPlayers` потребителях). Список handle-пар @ `gw+0x10` (stride 16);
каждая запись требует `*(player+0xBC) == meta_screen`.

**Связь с Exp1–3:** `resolve(1)==puppet` не добавляет puppet в этот список.
Per-player early update (`vtable+0x70` → `0x1404901B0` → `42FCF0`) диспатчится
виртуально из enumerator-контекста; `sub_1403E3E30` тикает тот же список в
world frame (не прямой caller `4901B0`, но тот же набор игроков).

**Runtime READ (безопасный):** в REALSECOND observe (Numpad 7) добавлен лог
`REALSECOND-EXP3B`: `spot_count`, `max_local`, `resolver_active` (`FE1070+0x495C`),
`screen_mask` (`gw+0x5D08`), entries, `puppet_in_list`.

**Прогноз:** после split-login `spot_count=1`, `puppet_in_list=0`, `max_local=2`.

**RUNTIME CONFIRMED (2026-07-10):** две копии loopback, inject v2, Numpad 7.
Host PID 27992 после spawn puppet: `spot_count=1 max_local=2 puppet_in_list=0
bit1=0 hub+anim OK resolve(1)==puppet`. H1 подтверждена.

Детали: `reports/real2p_exp3b_active_player_gate.txt`, `locomotion_hook.log`.

**Writer (STATIC):** `NpcManager_RegisterSpotPlayer` `0x1403E84F0` — единственный
writer `*(gw+0)++` + pair @ `gw+0x10`. Вызывается из `NpcSpawn_FinishSetup`
`0x1403FF740` @ `0x140400451` (gate `[entity+0x1F70]+0x58!=0` @ `0x140400435`).
Split-login `0x140C02220` **не** пишет в spot-list. Puppet в CharacterManager
(`0x143898630`), но не проходит Register → `spot_count=1`.

**Следующий шаг:** почему net-spawn puppet не доходит до `0x140400451` или падает
на gate `+0x1F70/+0x58`; runtime hook на `3E84F0` для slot-1 spawn.


## Turn note (2026-07-10, Cursor — 421150 = real 422760 caller; +5545 bit1 gate)

**CORRECTION:** `Player_PreAnimHubAndAnchorSync` `0x140422BB0` **не** вызывает `422760`
(только sniper aim-pos). Реальная цепочка:

`sub_14042FCF0` → `sub_140421150` → `Player_SyncControlHostEntityRootOnNetSkip` `0x140422760`.

**Новый gate (STATIC):** для **живого** игрока `421150` зовёт `422760` **только** если
`player+0x15A9` (+5545) **bit1** (`& 2`). Иначе bit0 на hub entity не ставится →
утренний `144610` сбрасывает bit4 → `TickDecisionNode` не проходит.

Доп. gate: весь `421150` — no-op если `player+0x1A0` (+416) null (anim-instance).

**Открыто:** кто пишет `+5545` bit1; алиасит ли `*(player+1FB0+16)` entity с записью в
`worldMgr+0x5A0` hub (единственный живой `144610` для Player).

Детали: `reports/player_421150_entity6e_gate.txt`.

**Следующий шаг:** xref-writer на `player+0x15A9` bit1; сравнение slot0 vs puppet при
spawn; runtime `5545` + `entity+0x6E` на `421150`/`4223F0`.


## Turn note (2026-07-10, Cursor — player+0x1FF0 = Skill; 422760 clear REJECTED)

**Статика:** `player+0x1FF0` = **текущий Skill*** (AI skill think:
`RefreshOrSelectCurrentStateObject_1FF0` `0x14041F1A0`, строки `"Entering skill"` /
`"THINK! desired skill"`).

**REJECTED:** гипотеза что `422760` сбрасывает bit0x4 через `Skill+vtable+96`:
`Skill::vtable+96` → `Skill_IsAuthorityForRootTransform` `0x140664690` **всегда
return 1** (проверено Skill/ScriptSkill/IdleSkill/NetSkill). Clear-ветка
`422760` **не достижима** для стандартных Skill.

**Уточнённая стена bit0x4:**

- Единственный writer `|= 4` на hub entity: `ControlHostHub_RefreshEntity6EProbeFlags`
  `0x142144610` (нужен bit0).
- Per-player вызов на `player+0x1FB0` из `0x1403E59D0` **загейтован**
  `Player::vtable+4256` `0x1401D4F10` → **всегда 0** → цикл по игрокам не
  вызывает `144610` на `+1FB0` hub для класса Player.
- `422760` else-путь ставит только **bit0**, не bit4; `TickDecisionNode` в том же
  кадре требует bit4 → **frame-order** или отсутствие promotion на puppet hub.

**Следующий шаг:** откуда slot 0 получает bit4 на `*(player+1FB0+16)`; runtime
`entity+0x6E` slot0 vs puppet на входе `4223F0`.

Детали: `reports/player_1ff0_skill_entity6e_revision.txt`.

## Turn note (2026-07-10, Cursor — entity+0x6E gate mapped)

**Статика:** кто выставляет/сбрасывает `entity+0x6E` на control-host
`*(player+0x1FB0+16)` — **найдено**.

**Setter (global tick):** `GameplayWorld_RefreshControlHostEntity6EFlags`
`0x1403E59D0` ← `sub_1403E4AF0` (world frame tick) → для каждого Player с
vtable+4256: `ControlHostHub_RefreshEntity6EProbeFlags` `0x142144610` на
`player+0x1FB0`. Требует `entry+0x6E bit0` и `entry+108 >= 0`, затем `|= 4`
и условно `|= 8`.

**Clearer (per-player anim-phase, до decision-node tick):**
`Player_PreAnimHubAndAnchorSync` `0x140422BB0` →
`Player_SyncControlHostEntityRootOnNetSkip` `0x140422760`: когда
`player+0x1FF0` (gameplay-state selector) существует, его `vtable+96` false, и
`Player_ShouldSkipDirectRootTransformApply` true → **`entity+0x6E &= 0xFA`**
(сбрасывает bit0x4). Сразу после этого в том же `AnimPhaseOwnerUpdate`
`Player_TickDecisionNodeFromControlHost` требует bit0x4 → **puppet на net-skip
пути не тикает `+0x1808`**.

**Вторичная стена:** `InputObject_MatchesActiveLocalJoypad` `0x14023AEA0` —
`object+0x50` vs `g_LocalPlayerInputManager+0x48/+0x50` (смещения +72/+80 в
деcompile); блокирует `entry+24` на nearby-list.

**Метки IDB:** +5 функций (`422760`, `422BB0`, `3E59D0`, `142144610`, `1B7500`);
939 total, `save_database OK`.

**Следующий шаг (статика):** что за объект `player+0x1FF0` на puppet vs slot 0
(`RefreshOrSelectCurrentStateObject_1FF0` `0x14041F1D0`); порядок global tick vs
anim-phase в кадре; joypad indices в co-op.

**Рантайм-прогноз (квота почти набрана):** hook `0x1404227B0` — puppet каждый
кадр; slot 0 нет. На входе `0x1404223F0` у puppet `entity+0x6E & 4 == 0`.

Детали: `reports/player_decision_node_controllers.md`,
`reports/entity_6e_*.txt`. Метки: `ida_apply_labels.py`.

## Turn note (2026-07-10, Cursor — vtable+1512 + Active decision node)

**Статика:** `player->vtable+1512` = `Player_ShouldSkipDirectRootTransformApply`
`0x141BC2EF0` (тот же слот `+0x5E8`, что и root-transform). **Не** жёсткий
off-switch: обе ветки `Player_AnimPhaseOwnerUpdate` вызывают
`Player_TickDecisionNodeFromControlHost`. Net-путь: `NetController_ShouldSkipAnimPhaseDirectPath`
`0x1419136E0` (child `vtable+24`).

- Locomotion decision node: `PlayerActiveDecisionNode_Tick` `0x14126B830`
  (vtable `0x142E47BC0`) → `BuildInteractDispatchState` + dispatch.
- `player+0x1808`: clear `0x141DE181A`, store `0x141DE2604` via `vtable+0xDE8`;
  leaf impl `0x14126C6A0` void — откуда берётся указатель, **открыто**.
- **Стена puppet (уточнено):** скорее `entity+0x6E` control-host (bit0x4/0x8) и
  joypad-gate на nearby probes, не vtable+1512.

**Следующий шаг:** кто выставляет `entity+0x6E` bits для slot 1; joypad indices
`g_LocalPlayerInputManager+0x48/+0x50`.

Детали: `reports/player_decision_node_controllers.md`. Метки: `ida_apply_labels.py`.

## Turn note (2026-07-10, Cursor — decision-node scheduler mapped)

**Статика:** scheduler decision-node найден.

- `Player_AnimPhaseOwnerUpdate` `0x14042D120` → `Player_TickDecisionNodeFromControlHost`
  `0x1404223F0` → `(*(player+0x1808))->vtable+0` (активный decision node).
- `Player_DecisionNodePublishGate` `0x140CD3760` → `PublishRuntimeStateFromDecisionNodeLikeObject`
  (= `Player::Active::Update`) после проверки `player+0x1FC8` vtable+168.
- Offsets: `+0x1808` decision node, `+0x1FB0` (8112) dispatch hub, `+0x1FC8` (8136)
  scripted interact, `+0x1F28` (7976) interact sub-controller.
- **Стена puppet:** scheduler не тикает (vtable+1512 / control-host flags), хотя
  global nearby-list tick (`0x1401611F0`) работает → `ok=1 dispatch=0`.

**Следующий шаг:** идентифицировать `player->vtable+1512` и что пишет `player+0x1808`
для slot 1.

Детали: `reports/player_decision_node_controllers.md`.

## Turn note (2026-07-10, Cursor — nearby-list writer + joypad gate)

**Статика завершена:** писатель nearby-list найден.

- `InteractDispatcher_RebuildNearbyList` `0x140C837F0` — сбрасывает count@+0,
  собирает кандидатов (`0x141C94340` + sphere-cast пути), пишет 32-байт entries@+8
  через `InteractDispatcher_AppendNearbyEntry` `0x140C817F0`.
- Глобальный тик: `InteractDispatcher_TickAllScreenBlobs` `0x1401611F0` →
  `InteractDispatcher_TickNearbyList` `0x140C87FB0` на `0x143EC4EE0` (screen 0) и
  `0x143EC84A0` (screen 1 в co-op). `sub_140C858F0` — только prune, не writer.
- `Player_InteractDispatcher` ставит `dispatcher+0x3570=1` в эпилоге → rebuild на
  следующем кадре.
- **Ведущая гипотеза `dispatch=0`:** `InputObject_MatchesActiveLocalJoypad`
  `0x14023AEA0` блокирует sphere-cast на entry → `entry+24` не выставляется для
  joypad 3 / screen 1 puppet.

**Следующий шаг:** scheduler decision-node (`player+0x1F28` / `+0x1FB0`); runtime
подтверждение joypad-индексов на `g_LocalPlayerInputManager+0x48/+0x50` только после
квоты (или как часть materialize-puppet-controller теста).

Детали: `reports/player_decision_node_controllers.md`. Метки в `ida_apply_labels.py`.

## Turn note (2026-07-10, Cursor — продолжение Codex/Claude: карта decision-node контроллеров)

**Где остановились агенты (2026-07-10):**
- **Codex:** direct-start pickup fallback в билде, runtime не подтверждён; направление =
  materialize puppet как stock local-player-1 controller.
- **Claude:** pickup-anim + aim-pitch = одна стена (per-player контроллеры куклы);
  baseline чистый, 3 экспериментальных тогла OFF.
- **Работает:** локомоция, cover `bk`, jump/roll `bh`, weapon give/display,
  fire direction (pose-seed), health, transport.

**STATIC (этот ход):** разобрана цепочка interact decision-node → dispatch.
9 функций (`reports/player_decision_node_controllers.md`, дамп
`reports/full_slot1_controller_decompile.txt`) вызывают
`Player_BuildInteractDispatchState` + `Player_InteractDispatcher` из vtable-ticked
decision-node объектов (`player+0x1F28` и др.). Ключевые:
`PlayerInteractController_UpdateReviveAndDispatch` `0x140EC75D0`,
`PlayerScriptedInteractController_UpdateAndDispatch` `0x140CD3C00`.
Все гейтятся `!BYTE2(dword_143073DA8)`.

**Почему `dispatch=0` на кукле:** команда активируется (`ok=1`), но dispatcher не
находит eligible nearby — список на `0x143EC4EE0+0x35C0*screen` вероятно не
заполняется для transform-pinned puppet. Direct-start обходит это, ища `WeaponPickup`
вручную.

**Следующий статический шаг:** найти писателя nearby-list диспетчера и scheduler
decision-node тиков для screen 1 (не ещё один runtime pickup тест).

## Turn note (2026-07-10, Codex - screen-1 interact tick and direct pickup action)

Static analysis corrected the apparent shared-controller root. The event list at
`Hud+0x320 -> +0x1930` is a **HUD pickup-notification queue**, not the character
interaction state machine. Both 0x368-byte Hud objects are constructed and
initialized, while the stock frame wrapper updates only screen 0; ticking screen 1
was implemented once, then removed from the active source because it does not own
the visible pickup bend.

The real interaction path is now mapped one level deeper:

- `Player_BuildInteractDispatchState` (`0x140C805A0`) builds the required 0x15C-byte
  command/pose block and snapshots the indexed screen's `+0x590` command list.
- `Player_InteractDispatcher` (`0x140C85D20`) consumes that block and, for a weapon
  pickup, queries `InteractControl_GetAvailabilityForPlayer` (`0x141C9A360`), then
  calls control vtable `+0x28` `RequestInteract` and publishes the request.
- Runtime with the missing builder+dispatcher call restored for screen 1 produced
  `PICKUP-GESTURE ... ok=1 dispatch=0`: the command is active, but the dispatcher
  does not select an eligible nearby interaction. This rejects the claim that the
  absent dispatcher tick alone was the wall.

The original fallback found the same-art `WeaponPickup`, but stopped after the
availability query and therefore never submitted the action. The corrected relay
now calls `RequestInteract` and publishes the request. Duplicate `OnInteract` /
GiveWeapon completion remains suppressed; inventory is still owned by
`WeaponGivePacket + SLOT-MIRROR`. The distinguishing log is now
`availability=1 requested=1 published=1`; visual confirmation is pending.

## Turn note (2026-07-10, Codex — direct authored pickup-animation relay REJECTED; full slot-1 controller model opened)

**Runtime result:** two instrumented builds hooked the local Player script-event
handler (`0x140DCDEA0`) and relayed both `play-simple-gesture` (`0xC1F3F991`) and
direct `play-animation` (`0x09A3F435`). Across repeated real M-30 pickups,
`GiveWeapon ret_rva=0xDD4208` and `WeaponGivePacket + SLOT-MIRROR` fired, but neither
animation event appeared on the sender. Therefore the visible bend is not carried
by either authored Player event and this direct relay cannot synchronize it.

The experimental packet/hook/apply code was removed; the stable gameplay path is
unchanged. Reusable IDA tools were added for CRC preimages and narrow disassembly
ranges. **New direction requested by user:** materialize the puppet as the stock
local-player-1 controller model, identifying the scheduler/ownership difference
between the already-full retained slot-1 `Player` (`screen=1`, Joypad 3) and the
engine's dormant local second-player path. Pickup/aim should then be consequences
of the same ticking per-player controllers rather than per-feature replay.

## Turn note (2026-07-10, Claude — pickup-anim + aim-pitch мапнуты до архитектурной стены; забанкали рабочее)

**Итог хода.** Диагностировал застревание Codex по анимации подбора и размотал
питч-прицеливание до конца. ОБЕ фичи упираются в одну стену: у transform-pinned
куклы с выключенным AI её per-player контроллеры (interact-стейт-машина, aim-стейт/
камера) либо не крутятся, либо конфликтуют с локомоцией.

**Анимация подбора:** триггер Codex (RequestInteract 0x1408C8A40) НЕ вызывается при
наземном подборе (идёт через скрипт give-weapon 0x140DD4208). Нашёл настоящий рецепт:
`Player_ResolveWeaponPickupInteractAnim → cmdId` + `InputScreenBlock_ActivateCommand`
(0x14183A650) активирует interact-команду в +0x590, обходя блокер кнопок. Технически
`ok=1`, но кукла не анимируется — interact-стейт-машина не обрабатывает (AI off). Даже
удержание команды 700мс не помогло. `g_puppet_pickup_gesture` OFF.

**Питч-прицеливание:** цепочка `стик→aim-углы→camera pose-seed(blk+0x60)→player+0x1720/
+0x1730→рендер`. Все уровни (player+0x1570/+0x1730, camera seed, resolver output,
camera-блок +0x60/+0x230/+0x240) = ВЫХОДЫ, пересчитываются каждый кадр. Чистого
driveable pitch-входа нет; писать общий screen-1 seed = freeze локомоции. Материализация
screen-1 камеры (0x238800) безопасна, но инертна. Все aim-тоглы OFF.

**Забанкано и работает:** смена/holster/подбор-дисплей оружия (WeaponGivePacket +
SLOT-MIRROR), направление огня, горизонтальный aim (через facing-blend), локомоция с
разворотами, транспорт, здоровье. Baseline чистый, 3 экспериментальных тогла OFF,
диагностика Numpad+ (AIMROW/CAMROW) оставлена.

**Следующий ход (вне стены):** взять ко-оп фичу, не завязанную на per-player
контроллеры куклы (см. TASK_POOL.md). Питч/жест возвращать ТОЛЬКО при
фундаментальной смене модели куклы (реальный второй-игрок aim-стейт вместо
transform-pin) — это большой отдельный проект, не инкремент.

## Turn note (2026-07-09, Codex — aim sync default-ON REJECTED, stable OFF restored)

**Aim sync resumed.** IDA MCP is reachable at `http://172.21.144.1:8745/mcp`; verified
`decompile_function` for `PlayerAimController::GetTargetPointFromReticle`
`0x140CB1720`, `Player_PostAnimUpdate` `0x1410E9210`, and
`ResolveScreenRoutePoseSeedOrDefault` `0x141C077B0`.

**Runtime rejection:** default-ON `g_puppet_aim_reticle_relay=true` produced strong
running jitter. Logs showed continuous `AIM-TARGET-WRITE` / `POSESEED-CALLERS` on
both processes while the user observed the run jerk. Restored
`g_puppet_aim_reticle_relay=false` by default; Numpad `/` is now hard-disabled for
this path so accidental retests cannot reintroduce the jitter mid-run. Full puppet
ADS input remains **OFF** by default and is still an explicit `F8` A/B toggle.

**Safety:** this does **not** re-enable the REJECTED screen-1 broad pose-seed/source
write. `g_puppet_aim_override_frame` stays false, and the old
`write_peer_seed_to_screen1_resolver_source()` path is not called. Scoped-rifle
remote aim now scrubs puppet scoped ADS unless full ADS relay is explicitly enabled.

**Next:** aim sync needs a narrower aim-additive input or movement-aware gating before
it can be default-on. Stable locomotion has priority; leave the pose-only visual relay
OFF unless explicitly reworking it in code.

## Turn note (2026-07-07, Cursor — PAUSE aim relay; новый P0 = синхронизация поворота головы)

**Aim/ADS relay — на паузе** (билд `as`, user-test не дал стабильного ADS). Не трогать
F8/pose-seed до отдельного запроса.

**Новая задача (user):** когда на своей копии крутишь камеру и персонаж смотрит в
сторону взгляда — на второй копии это тоже должно быть **видно** (голова/верх корпуса,
не обязательно полный ADS).

**Что уже есть в протоколе:**
- `StatePacket.quat` @ `Player+0x140` — **корпус**, всегда шлётся; пуппет блендит его.
- `camera_quat` + flag `0x0100` — ориентация `CameraManager(0)+0x1B0`, сейчас **только
  при strict RMB/ADS** (иначе пуппет крутился при беге).
- `look_stick` — относительный дельта yaw/pitch камеры vs корпус; на приёмнике **не
  кормится** в right stick (jitter, 2026-07-05).

**Почему сейчас не работает «голова следует за камерой»:** при free-look камера
отъезжает от `quat`, а в пакет уходит только корпус. Нативный путь —
`PlayerLookAtIkController` на **`Player+0x1E38`** (vtable `0x142A2D550`).

**STATIC (2026-07-07, готово):** `reports/lookat_field_map.md` — поля `+0x3F0`
(look point), `+0x420` (enable), virtual **SetLookPoint** `0x140EDA5F0`,
**UpdateIk** `0x140EDA680`. `+0x1DD0` = wall-hand IK, `+0x1E48` = perch.

**Head look sync — PARKED (2026-07-07, user revert):** `g_puppet_head_look_sync=false`
(buil `bc`). IK path worked but puppet spine twist unacceptable.

**Jump/roll relay (2026-07-08bh):** `bg` user-rejected: standing jump silent; standing roll silent; run+jump showed as roll. Root cause CONFIRMED in logs: `TRAVERSAL-PAD-MASKS jump=0x20 evade=0x80` (authored face pads). Sender ORd those into the packet (`LOCOMOTION-SEND trav=0x20`), and the Joypad3 force-hook matched `0x20` as evade / `0x80` as crouch -> jump->roll bleed.

**Fix `bh`:** protocol + force-hook use ScaleCopy INTENT bits only (`jump=0x4000000`, `evade=0x800000`, `crouch=0x400000`). Face pads stripped on send and receive. Init `2026-07-08bh`. Predict: trav=0x4000000 for jump, trav=0x800000 for roll; standing jump+roll work; run+jump is jump not roll.

**Cover relay (2026-07-08bi→bj→bk):** `bj` stuck puppet near walls: `try-enter-cover` (approach probe) was latched as in-cover. **Fix `bk`:** sustained `0x0800` = `Player+0x1510` bit0 only; one-shot `0x1000` = `cover-enter` command edge (not try-enter). No try-enter on puppet force-hook. Init `2026-07-08bk`.


---

## Turn note (2026-07-07, Cursor — peer-only puppet aim, build `as`)

**Продолжено с остановки Claude/Codex** на aim-relay (user: F8 даёт ложную анимацию
прицела с пистолетом; в настоящий ADS/прицел не входит; направление не то).

**REJECTED (повторно зафиксировано в коде):** per-frame
`write_peer_seed_to_screen1_resolver_source` (blk+0x60) + `g_puppet_aim_override_frame`
→ замораживает facing локомоции куклы (коммит `93d1420`).

**Билд `2026-07-07as`:**
1. Все пути «peer целится» = `peer_packet_aim_active` (`0x200` + `pose_seed` + свежий
   пакет <250 ms) — убирает ложный aim при пистолете в руках без RMB пира.
2. Убран REJECT-путь screen-1 seed write из `drive_remote_slot1_actions_tick`.
3. F8 + peer RMB → scoped resolver override на `0x10E934B` / `0xCB17D6` (как Numpad
   `/`, но без ручного тоггла) + `apply_puppet_sustained_aim_input` (joypad `0x200`).
4. На входе в peer-aim — one-shot `materialize_screen1_camera_once` (0x238800).
5. F8 в коопе не переключает legacy WASD-NPC.

**Модель (user-approved):** пуппет на копии 1 целится **только** когда пир реально
жмёт RMB на другой копии. Локальный RMB на этой копии пуппета не крутит.

**Тест:** copy1 F8 ON → copy2 Nadine RMB (пистолет) → на copy1 Nadine входит в
stance + смотрит на peer; copy1 Chloe RMB → Nadine **не** целится; F8 без RMB нигде
→ нет aim-анимации. Лог: `2026-07-07as`, `AIM-POSE-OVERRIDE site=0x10E934B`.

**Открыто:** вертикальный pitch / reticle HUD; снайпер scope vanish; weapon fountain.

---

## Turn note (2026-07-07, Cursor — aim-поза: writer chain + build `af`)

**Продолжено с остановки Claude/Codex на aim-pose** (коммиты `c3825a2`…`d69a7ad`):
live-дифф нашёл `Player+0x1720/+0x1730`, пост-запись инертна, POSESEED-CALLERS
гистограмма собиралась.

**STATIC (квота выполнена):**
1. `sub_1410E9210` (`Player_PostAnimUpdate`) вызывает resolver **ret_rva=0x10E934B**,
   затем `vmovups` в `+0x1720/+0x1730` — это и есть писатель aim-позы.
2. Тот же `0x10E934B` — доминирующий hit в POSESEED-CALLERS при ADS куклы.
3. Согласовано с `pseudo_camowner.txt` и xref `*(owner+0x4010)` screen index.

**Реализовано (build `2026-07-07af`):**
1. Resolver override расширен: call-site `0x10E934B` → полный peer pose-seed 32B;
   `0xCB17D6` (reticle) — quat+puppet origin. Тоггл **Numpad /** =
   `g_puppet_aim_reticle_relay`.
2. **Shift+Page Up** — write-bp на live `Player+0x1730` (верификация writer RIP).
3. Скрипт `scripts/ida_find_player_1730_writers.py`.

**Фальсифицируемый прогноз:** Numpad `/` ON, Chloe ADS двигает прицел → Nadine верх
корпуса следует; локальный aim Chloe **не** замирает; fire ray без регрессии.

**Параллельно (оружие, build `ad` в working tree):** fountain-fix не подтверждён
user-test — отдельная ветка.

---

## Turn note (2026-07-07, Cursor — продолжение Codex: class-slot swap в relay give, build `t`)

**Продолжено с места остановки Codex** (коммиты `3d7d52e`…`7aabc23`):
статика нашла корень фонтана = M-30 садится в manager-слот 1 (коллизия с пистолетом),
план = REMOVE старого оружия в peer-слоте перед GiveWeapon.

**Реализовано (build `2026-07-07t`):**
1. `seh_remove_weapon_in_slot` — **исправлен** примитив: `GetWeaponBySlot` (vt+0xB0) +
   `ProcessKillTree`, не vt+0x188 (это `GetWeaponByClass`, class-index ≠ slot-index).
2. `apply_peer_weapon_give` — перед one-shot relay give: `peer_slot_of_art(art)` из
   raw snapshot (слоты 0/1/16/17/2/3/20/22) → remove чужой art в том же слоте → give.
3. Диагностика: `GIVEWEAPON-RELAY-APPLY target= removed= removedArt= slotAfter=`.

**Стабильная ветка сохранена:** `g_relayed_block_give` default OFF (Numpad -);
one-shot pickup relay через `WeaponGivePacket` "COPG" + sender hook GiveWeapon+9
(`ret_rva=0xDD4208`) работает без пер-тик re-give из store.

**Фальсифицируемый прогноз теста:**
- Подобрать M-30 на copy 1 → `GIVEWEAPON-RELAY-SEND` / на copy 2
  `GIVEWEAPON-RELAY-APPLY target=16 removed=16 removedArt=0x8075397d slotAfter=16`
  (net-режим: long-gun класс 16 → слот 16, не 1).
- Фонтан из Надин **не** должен появиться; пистолет остаётся в слоте 17/1.
- Если `target=-1` (snapshot отстаёт) — remove пропускается; тогда возможен
  частичный регресс → нужен fallback по def-классу.

**Следующий ход:** runtime confirm; при успехе — включить `g_relayed_block_give` с
give-once memo; при `target=-1` — fallback remove по WeaponArtDef+8 через
`sub_141094420` net remap.

## Turn note (2026-07-06, Cursor — стабилизация build `s`, fountain откат)

**Зафиксировано по запросу пользователя** после регрессии «фонтан оружия» на билдах
`o`/`p`/`q`.

**Откат (build `2026-07-06s`):**
- Убран **WeaponInfo relay** — хук `PlayerWeaponController::GiveWeapon`, пакет
  `WeaponInfoPacket`, `apply_peer_weapon_info`.
- `puppet_weapon_give_allowed` снова **строгий whitelist** (3 art: pistol / rifle /
  scoped pistol). Подобранное с земли оружие **не синхронизируется в руках** — это
  осознанный trade-off ради стабильности.
- **Оставлено:** pickup relay (`OnInteract` → `PickupPacket` → native Apply),
  `force_log_line` для `PICKUP-*` / `HELD-WEAPON-*`, verified give memo, pose-seed
  fire path (native fire OFF), held-weapon slot sync для whitelist arts.

**Следующий ход (когда продолжат):** статически понять, почему `GiveWeapon` с полным
`WeaponInfo` для pickup arts (`0x3019cbee` и др.) снова вызывает fountain на puppet;
возможные пути — relay только через `NetEventSyncWeaponPickups` без повторного
`GiveWeapon`, или отдельный draw path без inventory mutation.

## Turn note (2026-07-06, Cursor — продолжение Claude: fire-caller pose-seed + pickup relay, build OK)

**Собрано и заинжекчено** (PID 25372 / 16372, `tll_teleport_hook.dll`). Логи по
умолчанию OFF (F11).

**Сделано поверх незакоммиченного WIP Claude:**
1. **StatePacket v8** — sender шлёт `pose_seed` (32 байта, flag `0x0200`) из
   `CameraManager(0)→*(+0xB0)+0x60` (уже было в WIP).
2. **Weapon pickup relay** — sender хук `WeaponPickupInteractControl::OnInteract`
   `0x14157AA70` → UDP `PickupPacket`; receiver `NetEventSyncWeaponPickups_Apply`
   `0x140BCE440` на игровом потоке (WIP Claude, **ещё не runtime-проверено**).
3. **Fire direction (redesign по LESSON 2026-07-05):** широкий override resolver
   заменён на **fire-caller gate**:
   - хук `PlayerFireController_BuildFireArgsAndFire` `0x140E2BCD0` выставляет
     `thread_local g_pose_seed_fire_only_override` только когда owner == puppet
     (slot 1);
   - хук `ResolveScreenRoutePoseSeedOrDefault` при этом guard подменяет **только
     quat** (`seed+0x10`), позицию луча оставляет от puppet resolver;
   - `g_puppet_relay_pose_seed` default **OFF**, toggle **VK_APPS** (context menu).

**Прогноз теста (pose-seed):** VK_APPS ON → puppet стреляет по направлению прицела
пира, aim camera **не** замирает (в отличие от broad override 2026-07-05).

**Прогноз теста (pickup):** поднять оружие на copy A → в логе (F11) `PICKUP-SEND` /
на copy B `PICKUP-APPLY`; pickup исчезает у второй копии. Если object id не
совпадают между инстансами — нужен name-hash fallback.

**Открыто:** Chloe vanish при ADS снайпера на copy 2; weapon attach `attached=0`;
grenade arc — тот же pose seed, но отдельный caller (пока не захукан).

**Следующий ход:** runtime confirm pose-seed (VK_APPS) + pickup; при успехе —
grenade caller; sniper vanish — статика scope camera / model cull screen-1.

## Turn note (2026-07-05, Claude — FIRE RAY источник найден: pose seed CameraManager+0xB0→+0x60, фикс build-ready)

**Статика замкнула цепочку «стреляет не в ту сторону» до конца** (реестр: "FIRE RAY
SOURCE FOUND"). Оба камера-резолва ВНУТРИ Fire — посторонние (шум выстрела для NPC
и стат `unzoomed-sniper-shots`). Реальный луч: единственный вызывающий Fire —
`PlayerFireController_BuildFireArgsAndFire 0x140E2BCD0` — сидит луч через
`ResolveScreenRoutePoseSeedOrDefault(&seed, *(player+0x4010))` `0x141C077B0` =
`CameraManager::GetHandle(idx)`: `blk=*(mgr+0xB0)`, гейты `blk+85/+86` + бит
`dword_14697AE34`, **сид = 32 байта `blk+0x60` = [pos float4][quat float4]**;
при провале гейтов — **pos(0,0,0)+identity** → ровно наблюдаемое «не туда».

**Фикс к следующему ходу (build-ready):**
1. Sender: читать свой сид целиком (`*(CameraManager(0)+0xB0)+0x60`, 32 байта) и
   слать в StatePacket (нужно новое поле — camera POSITION в сиде есть, в текущем
   пакете нет).
2. Receiver: хук `ResolveScreenRoutePoseSeedOrDefault` — при `idx==1` и peer-aim
   писать эти 32 байта в out и return (движковое состояние не трогаем, гейты не
   нужны).
Прогноз: выстрелы Второго игрока ложатся по прицелу пира в обеих копиях.

**IDB:** +PlayerFireController_BuildFireArgsAndFire, +ResolveScreenRoutePoseSeed
OrDefault (детальный комментарий), 870 функций, save True; MCP обновлён.
Параллельно ранее: снайпер-скоуп гейт (`286b6f1`), см. прошлые заметки.

## Turn note (2026-07-04, Claude — LAG root cause = per-frame VirtualQuery, CONFIRMED FIXED)

**Причина сильных лагов найдена и устранена (подтверждено пользователем).** Без мода
игра летала, с модом — фризы при низком CPU/GPU (главный поток ЖДАЛ). Корень:
`readable_bytes` звал `VirtualQuery` на каждую проверку указателя (280 мест,
десятки/кадр); `VirtualQuery` берёт процессный лок адресного пространства, который
потоки стриминга/аллокатора игры держат постоянно → контенция → стоп главного
потока каждый кадр.

- **Фикс (`10ece75`):** `readable_bytes` fast-path = SEH-проб по байту на страницу
  (без syscall/лока), возвращает доступный размер до `0xB000`. Тумблер
  `g_readable_bytes_fast` (VK_NEXT / Page Down) — откат на `VirtualQuery` для A/B.
- **Логи (`ea77bcb`/`46ab041`):** постоянный дескриптор вместо open/close на строку;
  `g_logging_enabled` по умолчанию OFF, F11 включает. (Оказалось вторичным.)
- **Две копии на 16 ГБ RAM** свопят (по 10 ГБ каждая) — отдельное ограничение
  локального теста, не связано с этим лагом.

**Хоткеи (актуально):** VK_HOME = DLL inert; F11 = логи вкл/выкл; VK_NEXT = fast/slow
readable_bytes; F8 = puppet aim-press (ADS) relay; F7 = legacy move-gate.

**Игра запущена (PID 6276), новый DLL заинжекчен, лаги ушли.** Кооп-задачи
(HUD-гейт скоупа для не-основного экрана, реле направления взгляда) — следующий ход.


## Turn note (2026-07-04, Cursor — crosshair dup: ADS-флаг гейт REJECTED, новый гейт по screen index в per-screen HUD паблишерах)

**Тест (PID 21040/4488):** гейты `is-scope-visible?` + reticle-паблишеров, условие
`remote_player_ads_active()` (`Player+0x1548 != 0`) — прицел всё ещё дублируется, и
в логе **ноль** строк `SCOPE-VISIBLE gated`/`RETICLE-HUD gated`, при том что
марионетка держала aim (`JOYPAD3 current=0x209`). → **REJECTED:** `+0x1548` ставится
только при scoped-RIFLE ADS; при обычном (пистолетном) прицеле он 0. Как общий
предикат «удалённый целится» НЕ использовать.

**Новая статика (см. initial-findings, запись «Per-screen HUD reticle publishers»):**
обычное перекрестие спавнится per-screen HUD паблишерами над
`g_PerScreenPlayerHudState 0x143EF1E30` (stride 0x100), **индекс экрана/слота в
`state+0xF8`**:
- `0x14135A650` `PlayerHudScreen_ReticleProcessPublisher` (спавнит `ss-hud-reticle`);
- `0x14135AF20` `PlayerHudScreen_AmmoReticlePublisher` (тоже спавнит `ss-hud-reticle`,
  вызывается из `UpdatePlayerHudAimingAndWeaponAccuracyPublishes`);
- `0x14135C050` — blindfire, жёстко screen-0 (алиасы `0x143EF1F20/0x143EF1F28`).
Доказательства индекса: `GetRetainedPlayerEntityBySlot(*(a1+248))` + джойпад
`0xBF0*(slot+2)` (совпадает со slot0→joypad2, slot1→joypad3) + фикс-алиасы screen-0.

**Фикс (teleport_hook.cpp, собрано+заинжекчено):** хуки на `0x14135A650` и
`0x14135AF20` — скип паблиша, если `*(state+0xF8) != 0`
(`hud_state_is_non_primary_screen`, лог `RETICLE-HUD gated: screen!=0` /
`RETICLE-AIM-HUD gated: screen!=0`). Не зависит от оружия/ADS. Гейт
`is-scope-visible?` (снайперский оверлей) оставлен по remote-ADS `+0x1548` — для
винтовки флаг работает (диф это показал).

**Прогноз теста:** пир держит прицел (любое оружие) → на приёмнике поза
прицеливания у марионетки ЕСТЬ, перекрестия НЕТ, в логе `RETICLE-*HUD gated:
screen!=0`; свой локальный прицел рисуется как обычно. Если перекрестие всё же
остаётся при наличии gated-строк — есть ещё один HUD-путь (искать соседей
`0x1413B42E0`/`0x1413B4670` семьи).

**Игра запущена (PID 22372 role A / 11672 role B), новый DLL заинжекчен.**
IDB размечен (`apply_labels OK: 864 functions`, `save_database returned True`),
MCP-копия обновляется.

---

## Turn note (2026-07-04, Cursor — crosshair dup CONFIRMED fixed; native-fire краш → disabled; sniper-scope puppet vanish открыт)

**Результаты рантайма (PID 22372/11672):**
1. **Дублирование прицела — CONFIRMED FIXED** (screen-index гейт, `RETICLE-HUD
   gated: screen!=0` в логе).
2. **Краш приёмника при стрельбе — причина найдена, пофикшено:** `EXCEPTION
   rva=0x152CB38` = `ProjectileBulletInstant::ProbeKick` (`sub_14152C9B0`), сразу
   после `FIRE-RELAY native seq=3 art=0xc3181ebd`. Native `PlayerFireController::Fire`
   replay на кукле возвращает OK внутри SEH, но отложенный projectile-probe на
   след. тике падает ВНЕ `__try` на мусорном указателе из sender-aim-векторов в
   `packet.fire_args`. **Фикс:** `g_native_fire_relay = false` — вся стрельба через
   button-pulse (стабильно, использует собственное состояние куклы).
3. **Хлоя пропадает на копии 2 при снайперском прицеле копии 1 — ОТКРЫТО
   (HYPOTHESIS):** только винтовка `0x8075397d`. Вероятно scope-камера/модельный
   cull при scoped-ADS куклы, отдельно от HUD-гейта. Следующий ход: трейс
   sniper-ADS → cull модели screen-1, гейт этого пути, ЛИБО pose-only для снайпера.

**Собрано+заинжекчено (PID 19868 role A / 26028 role B).** Прогноз теста:
стрельба любым оружием больше не крашит приёмник (`FIRE-RELAY button`, нет
`FIRE-RELAY native`, нет ProbeKick); прицел не дублируется. Снайпер-исчезновение
Хлои — известный открытый баг.

---

## Turn note (2026-07-04, Claude — RUNTIME: block-1 fix REJECTED, корень = состояние марионетки, HANDOFF) [SUPERSEDED выше]

**Тест (PID 14440 отпр. / 8524 приёмн.):** прицел всё ещё дублируется, снайперский
прицел дублируется **без виньетирования**, «целится/стреляет не в ту сторону».
→ гипотеза «утечка = биты aim/fire/look в общем блоке 1» **REJECTED** (детали и
адреса в `initial-findings.md`, запись "Runtime 2026-07-04 … REJECTED").

**Что доказал лог:** фикс блока-1 работает как задумано (`REMOTE-ACTIONS branch=0x9`
без 0x200/0x800; `JOYPAD3 current=0x209` только внутри конверсии; блок восстановлен),
НО симптом остаётся → утечка НЕ через биты ввода. Три установленных факта:
1. Обе копии стреляют винтовкой `art=0x8075397d` (не пистолет) → снайперский оверлей =
   марионетка целится винтовкой.
2. `JOYPAD3 right=(0,0)` всегда — реле правого стика (look) НЕ доходит до джойпада 3 →
   направление марионетки только из camera-facing → «не туда». Отдельный баг.
3. `g_PlayerHandleForHudPublish 0x143EEBE80` в обеих копиях = ОДИН объект (лок. игрок);
   reticle-менеджер `0x140C193A0` читает его, а не марионетку.

**РЕШЕНО чтением живой памяти (прицел удержан):** локальный игрок и его per-screen
HUD корректны — скоуп течёт через **глобальный singleton `ss-hud-sniper-reticle-manager`**.
- `g_PlayerHandleForHudPublish 0x143EEBE80` → `0x10e0a2d180` = **локальный слот 0
  (Nadine)**, а не марионетка (`live1=0x10e0ddf600`). Значит подмены хэндла нет.
- Марионетка (джойпад 3 / слот 1) держит `current=0x209`, `right=(0.028,0.286)` —
  это ОНА в скоуп-прицеле с винтовкой. (look-реле, кстати, доходит при прицеле;
  прошлый `right=(0,0)` был из окна без прицела. Неверное направление =
  деривация look/facing, отдельный баг.)
- `is-scope-visible?` (`sub_140C177F0`) читает глобальный менеджер через реестр
  `dword_146466FE0`, поля `mgr+168/+188` — НЕ per-player. Марионетка входит в
  scoped-ADS → глобальный scope-visible → полноэкранный оверлей на вьюпорте
  приёмника (без виньетки, т.к. это сырой оверлей менеджера, а не композит ADS
  локального игрока).

**Фикс на следующий ход (фальсифицируемо):** марионетка не должна поднимать
глобальный scope/ADS. Приоритетно:
1. Релеить прицел как ПОЗУ, не вооружая scoped-ADS/zoom оружия марионетки.
   Прогноз: поза прицеливания есть, скоупа на приёмнике НЕТ. Если скоуп остаётся —
   менеджер поднимается самим aim-press → пункт 2.
2. Гейт `is-scope-visible?`/менеджера: игрок с `screen_index != 0` (марионетка)
   не может поднять scope-visible.
НЕ трогать биты ввода (блок-1 REJECTED). look/направление — чинить отдельно.

**Игра запущена (PID 14440/8524), DLL заинжекчена** — `scanner/build/tll_read.exe --pid N <addr> <len>`.

---

## Turn note (2026-07-04, Claude — crosshair dup гипотеза: shared input block 1) [SUPERSEDED выше]

**User test result:** прицел всё ещё дублировался на копию-приёмник при прицеливании
пира → гипотеза «scrub локального джойпада достаточно» **REJECTED**.

### Root cause (STATIC, реестр: "Input Block Selector Bus")
- `FrameState_ConstructJoypadSlots 0x14179D780` задаёт селекторы блоков
  (`Joypad+0xF0`) = `{0,1,0,1,0,1}`: **слот 3 (джойпад марионетки) читает блок 1 —
  тот же, что слот 1 = штатный лэйн локального игрока №2** (mode 3 у слотов 0/1).
- Все наши записи aim `0x200`/look в branch слота 1 и в source хука ScaleCopy
  (dest=3) оседали в общем блоке 1 → движок читал их как «локальный игрок 1
  целится» → strafe-камера/ретикл на локальном вьюпорте приёмника.
  (`is-aiming` HUD-publish `0x1413571B0` требует камеру `CameraControlStrafe`
  vtable `0x143054560` на экранном маршруте — см. реестр.)

### Fix (в этом коммите, build OK)
- `hooked_input_scale_copy` (dest=3): **сэндвич** — сохранить блок-1 `+0x518` и
  правый стик `+0x08/+0x0C`, записать peer aim/fire + look, вызвать оригинал
  (джойпад 3 успевает вывести command state), **восстановить** блок. Peer-ввод
  существует на общей шине только внутри конверсии.
- `drive_remote_slot1_actions_tick`: look_stick больше не пишется в branch
  (`+0x1C/+0x20`); aim/fire срезаются из branch-маски `branch1+0x10` (едут только
  через сэндвич). Движение/gate-биты не тронуты. Скраб локального пэда оставлен.

### User test checklist (next turn)
1. Обе копии с новой `tll_teleport_hook_v2.dll`, ≥10 c после спавна.
2. Chloe держит прицел → на копии 2: поза прицеливания у марионетки, лог
   `JOYPAD3-ANALOG ... current=` содержит `0x200`, и **нет** ретикла/зума у Нэдин.
3. `REMOTE-ACTIONS branch=` больше не содержит `0x200/0x800` — это норма.
4. Выстрелы: как раньше (FIRE-RELAY native / пульс) — путь не менялся, но
   проверить, что пистолетный выстрел доходит (fire теперь только через сэндвич).
5. Если ретикл вернулся → гипотеза остатка в блоке 1 неверна; следующий кандидат —
   прямой читатель Joypad-объекта слота 3 в HUD/камере (гейт на стороне читателя).

## Turn note (2026-07-03, Cursor — fire dedup + load guards + aim anim restore, HANDOFF)

**Зафиксировано в этом коммите.** Следующий ход: user-test aim pose + crosshair isolation.

### Fire relay (CONFIRMED fixes)
- **Double-fire crash:** sustained peer `0x800` in `StatePacket` reached Joypad 3 while
  native `FIRE-RELAY` also fired → `rip=0x0`. Fix: `remote_puppet_actions_from_peer`
  strips fire from state; fire only via `FirePacket`; `suppress_puppet_fire_buttons(400)`
  after native success; block `try_start_fire`/`check_shot_readiness` on puppet while
  suppressed.
- **Load crash (no shot on copy 2):** `GiveWeapon` vtable `+0x158` null during spawn →
  `EXCEPTION rip=0x0` art `0x33E29A1B`; native fire too early after `gunOk=1`. Fix:
  `puppet_weapon_give_vtable_ready`, 5 s puppet warmup, 5 s give-fail backoff,
  `puppet_native_fire_allowed` (+1 s after confirmed attach) gates native replay.

### Aim animation (restored this commit, needs user test)
- **Regression cause:** `kRemoteViewportButtonMask` incorrectly stripped aim `0x200`
  along with scope HUD bits → no sustained aim-press on puppet joypad → no aim pose.
- **Fix:** split masks — relay aim `0x200` + peer `look_stick` on branch `+0x1C/+0x20`;
  strip only scope bits (`0x10000..0x80000`); `scrub_local_viewport_aim_hud_leak()`
  clears aim/scope from **local** joypad when peer aims but local player does not
  (prevents crosshair duplication without killing puppet anim).
- **Still gated:** fire `0x800` never sustained from state packet (by design).

### User test checklist
1. Load both copies, wait ≥10 s after level spawn.
2. Chloe hold aim (no shot) → remote Chloe aim pose on copy 2; **no** crosshair on
   Nadine viewport; log `REMOTE-ACTIONS branch` should include `0x200`.
3. Chloe fire 3–5 pistol shots → copy 2 survives; one `FIRE-RELAY native` per shot.
4. If crosshair dup returns → tune `scrub_local_viewport_aim_hud_leak` only (do not
   re-strip `0x200` from puppet).

## Turn note (2026-07-03, Cursor — copy-2 crash on Chloe pistol fire)

- **CONFIRMED (logs PID 24340/9980):** copy 2 (role B) died right after first relayed
  pistol shot. Last copy-2 lines: `REMOTE-ACTIONS held=0x809` → `FIRE-GATE
  input/readiness result=1` on remote player `0x10e0e13f80` →
  `FIRE-RELAY native seq=1 art=0x33e29a1b hands=1`; copy 2 then stops logging while
  copy 1 continues.
- **CONFIRMED:** same session had earlier `EXCEPTION rip=0x0` on pistol art
  `0x33E29A1B` during `HELD-WEAPON-APPLY` / GiveWeapon (SEH survived); final crash
  correlates with **double delivery** — native `safe_replay_player_fire` plus peer
  fire button `0x800` still armed on Joypad 3 in the same frame window.
- **Root cause:** `apply_queued_fire_events` native success still set
  `g_fire_button_hold_until_ms`, and `drive_remote_slot1_actions_tick` also forwarded
  peer `input_buttons` fire (`0x809`) → second native `PlayerFireController::Fire`
  on the remote player → `rip=0x0` AV.
- **Fix:** on native relay success, call `suppress_puppet_fire_buttons(250)` instead
  of extending button hold; strip `0x800` in `drive_remote_slot1_actions_tick` and
  `Input_ScaleCopyVirtual` while suppressed; require `puppet_weapon_sync_ready`;
  use packet `controller_offset` + weapon-art match before replay; clear suppress on
  native-fail so button fallback still works.
- **Runtime:** built `scanner/build/tll_teleport_hook_v2.dll` OK.
  **User test:** Chloe pistol shot → copy 2 survives; log shows one `FIRE-RELAY native`
  and **no** `FIRE-GATE` fire-press on remote player in the same 250 ms window.

## Turn note (2026-07-03, Cursor — rifle fire relay drop + aim direction fix)

- **Root cause:** copy 2 (role B / Nadine local) also logged `PLAYER-FIRE` and sent
  fire packets with rifle art `0x8075397d` while Chloe held pistol — receiver
  button fallback armed rifle → sniper scope overlay after first pistol shot.
  Button-only relay ignored `FirePacket.fire_args` aim direction.
- **Fix:** `send_fire_packet` / `hooked_player_fire` gated to role A + retained slot 0
  only; `coop_fire_packet_matches_peer_weapon` drops rifle packets when peer holds
  pistol; rifle give/switch blocked when peer has pistol; `apply_peer_aim_facing_to_player`
  before button pulse; aim `0x200` armed when peer aims or recent aim + gun out;
  native pistol replay enabled (`g_native_fire_relay`) with full `0x100` fire_args via
  `puppet_fire_ready`.
- **User test:** Chloe pistol first shot → no sniper scope on Nadine; log should show
  `FIRE-RELAY drop: rifle art but peer holds pistol` for stale rifle packets;
  `FIRE-RELAY native` for pistol when ready; muzzle direction matches Chloe crosshair.

## Turn note (2026-07-03, Cursor — block+0x518 button send fix)

- **CONFIRMED (logs PID 10040/20400):** `REMOTE-ACTIONS held=0x0` while Chloe aims —
  sender read Joypad `+0xE0` (layer bits like `0x20000`) instead of resolved block
  `+0x518` (command descriptors: aim=`0x200`, fire=`0x800`). Receiver synthesized only
  brief `branch=0x200` from `g_fire_aim_hold_until_ms` (400ms post-fire), not sustained
  aim-press → **no Nadine aim animation** while Chloe holds aim.
- **CONFIRMED:** `JOYPAD3-ANALOG right=(0,0)` every frame — mouse look never landed in
  `look_stick`; `camera_quat` path existed but aim-press never stayed active without
  correct held mask in the packet.
- **CONFIRMED:** `capture_held_weapon_art_id` reported rifle `0x8075397d` while pistol
  drawn in hands (slot 1) → peer sent rifle art intermittently; pistol visual switch lagged.
- **Fix:** sender now packs `input_buttons` from `joypad_resolved_input_block+0x518`;
  receiver `Input_ScaleCopyVirtual` (dest=3) sustains aim `0x200` while `peer.gun_state==1`
  or fire-relay hold; applies peer `look_stick` to source block axes; pistol-in-hands
  (slot 1) wins over rifle active-weapon query.
- **Runtime:** built + injected PIDs 11616/21412. **User test:** hold aim on Chloe → log
  should show `held=0x200` (not `0x0`) on receiver `REMOTE-ACTIONS`; Nadine aim pose;
  pistol switch → `HELD-WEAPON-SEND art=0x33e29a1b`; shoot direction follows camera.

## Turn note (2026-07-03, Cursor — Nadine aim direction + aim anim sync)

- **CONFIRMED (logs PID 4480/16108):** `JOYPAD3-ANALOG right=(0,0)` on every frame while
  Chloe fires with `src=0x90000` — aim/fire **buttons** reach Nadine's Joypad 3, but
  **camera/right-stick look was never relayed**. Nadine used her local CameraManager yaw,
  so shots and aim pose did not match Chloe's crosshair.
- **CONFIRMED:** weapon-mismatch gate stripped **both** aim (`0x200`) and fire (`0x800`) when
  `puppet_can_fire_art` failed (`WEAPON-CLONE armed=0`). That blocked sustained aim-press
  animation even while Chloe held aim.
- **Fix (StatePacket v7):** sender now packs `camera_quat[4]` (Chloe CameraManager(0) @+0x1B0,
  flag `0x100`) plus `look_stick` right-stick pair; receiver applies peer camera in
  `drive_remote_slot1_actions_tick` and `Input_ScaleCopyVirtual` (dest=3), forwards right
  stick to branch `+0x1C/+0x20`, relays aim continuously while peer holds `0x200` (fire still
  gated on weapon match), blends body quat harder while peer aims.
- **STATIC:** shot direction should follow peer camera quat once both instances run v7 DLL.
  Aim animation needs sustained `0x200` + gun-out; rifle draw on Nadine may still lag
  (`HELD-WEAPON-APPLY`) — separate from look relay.
- **Runtime:** built `scanner/build/tll_teleport_hook_v2.dll`; injected PIDs 10040/20400.
  **User test:** Chloe aim (hold) → Nadine aim pose; Chloe shoot → Nadine muzzle direction
  matches; log should show non-zero `right=(…)` on JOYPAD3-ANALOG and peer camera applied.

## Turn note (2026-07-03, Cursor — rifle puppet give/switch unblock)

- **Root cause (prior run):** `puppet_weapon_give_allowed` blocked rifle
  `0x8075397d`; `puppet_weapon_switch_allowed` blocked `slot<=0` and rifle art.
  Peer rifle packets showed `HELD-WEAPON-APPLY slot=-1 gave=0 select=0` — puppet had
  no rifle inventory; fire relay still worked via buttons.
- **Fix (this build):** allow rifle give/switch (slot 0); `ensure_puppet_has_peer_weapons`
  pre-gives coop arts from peer raw-id snapshot (flag `0x40`); apply path gives + re-resolves
  slot when missing; rifle-out forces switch even when `weapon_selected_mode==0`; sender
  prefers rifle art when pistol hand-attached but active/requested slot is rifle.
- **Runtime:** build OK (`tll_teleport_hook_v2.dll`); two instances injected (PIDs vary).
  **Needs user test:** pick up rifle → expect `HELD-WEAPON-APPLY art=0x8075397d slot=0
  gave=1 select=1` on puppet side; visual should show rifle (Nadine holster overlap may
  still apply).

## Turn note (2026-07-03, Cursor — weapon display: hand-attached art vs GetActiveWeapon)

- **Likely bug:** `capture_held_weapon_art_id` used `Player_GetActiveWeapon` (+0x8C0)
  first; it can keep reporting rifle `0x8075397d` while pistol is physically attached
  (slot 1 / `0x1410A49E0`). Peer then never switches puppet draw to pistol.
- **Fix:** sender/receiver now prefer `capture_weapon_selected_mode` (hand-attached
  slot) → `get_weapon_from_slot` → art@+0x860; holster previous attached slot before
  `SwitchToWeapon` to new slot; holster on `peer_gun==0`.
- **Still parked:** Nadine look `0xd27c074f` holster prop-attach — holstered rifle
  mesh may overlap pistol even when logical slot is correct (asset-level).

## Turn note (2026-07-03, Codex — fire button reaches Joypad, internal gates next)

- **CONFIRMED:** the received `0x90000` mask reaches the clone's real Joypad 3:
  `REMOTE-ACTIONS branch=0x90000` and then
  `JOYPAD3-ANALOG src/pressed/current=0x90000`. Network delivery and per-slot
  input application are no longer candidates for the missing shot.
- **CONFIRMED:** pistol switching completes asynchronously. The first frame logged
  `requested=1 attached=0`; a later frame logged `requested=1 attached=1` without
  another peer state change. The old early return therefore left `gunOk=0` after
  successful attachment. The receiver now keeps checking until the expected slot
  is physically attached, then arms fire readiness without repeatedly switching.
- **STATIC:** the remaining chain is
  `PlayerFireController_UpdateInputAndFire 0x140E28A30` ->
  `PlayerWeaponController_CanUseWeapon 0x1410B15A0` ->
  `CheckFireInput 0x140E2ACA0` -> `CheckActionExclusions 0x140E2ADF0` ->
  `CheckShotReadiness 0x140E2B220` -> native `Fire 0x140E2C730`.
  Read-only return diagnostics were added to the two final fire-controller gates.
- **CONFIRMED / REJECTED:** the remote pulse produced no `FIRE-GATE` line at all,
  while local shots produced `input=1`, `readiness=1`, then native `PLAYER-FIRE`.
  The break is therefore before the fire controller, not in weapon readiness.
- **STATIC + live data:** authored command records resolve `aim-press` to mask
  `0x200/type 4`, and all `fire-hold/press/release` forms to mask `0x800` with
  types `2/4/8`. The old synthetic masks `0x80000/0x10000` were unrelated.
  Additionally, `hooked_player_input_state_update` cleared the generic press latch
  before the separately scheduled fire controller queried Joypad `+0xD0`.
  Fire now uses `0x800` and remains latched only until `CheckFireInput`; the 400 ms
  helper hold keeps only aim `0x200`, preventing manufactured repeated shots.
- **Next prediction:** remote pulse `0xA00` yields Joypad `pressed/current=0xA00`
  and then `FIRE-GATE input press=1`. Its return and the readiness return identify
  any remaining native rejection.

## Previous turn note (2026-07-03, Cursor — fire relay PAUSE, button path partial)

**Статус: ПАУЗА.** Краши устранены; визуальная синхронизация выстрела на кукле **ещё нет**.

### Что подтверждено рантаймом (PID 10428/15952, 15028/9984, …)
- **CONFIRMED:** `FIRE-RELAY button seq=N pulse=0x90000` — UDP-пакет доходит, button
  fallback срабатывает (ранний `return` для пистолета при `!fire_ready` убран).
- **CONFIRMED:** `REMOTE-ACTIONS branch=0x90000` на 1–2 кадра — `drive_remote_slot1_actions_tick`
  получает aim+fire в `branch1+0x10`.
- **CONFIRMED:** `HELD-WEAPON-APPLY gave=1 select=1 slot=1 gunOk=1 hands=1` после
  `safe_give_weapon_art_to_puppet` + `SwitchToWeapon` (пистолет `0x33e29a1b`).
- **CONFIRMED:** кукла slot-1 привязана к **joypad 3** (`PLAYER-INPUT-BIND joypad_index=3`).
- **CONFIRMED:** `WEAPON-CLONE ids=[0,0,…]` на кукле до give; `GiveWeapon` vtable `+0x158`
  иногда `rip=0` (SEH ловит, игра не падает).
- **REJECTED (this path):** `g_native_fire_relay=false` — native `PlayerFireController::Fire`
  на кукле давал `EXCEPTION rip=0x0` (пистолет) и краш на винтовке.

### Что НЕ работает (блокер)
- **`block3 btn=0x0`** даже при `branch=0x90000` — кнопки не доходят до resolved input
  block, который потребляет Player. Диагностика раньше смотрела `0x66E4220+0x530`
  (block[1]); кукла на joypad 3 → нужен `+0x530*3`. Добавлен `push_joypad_block_buttons`
  (прямая запись `block+0x518` held / `+0x508` press) — **ещё не дало визуала**
  (`block3 btn` остаётся 0 в логе).
- **`puppet_can_fire_art` часто false:** `active=0x0` или `mode=0` при holstered pistol
  на кукле; винтовка (`0x8075397d`) blocked для `SwitchToWeapon` slot≤0 на кукле.

### Следующий ход (после паузы) — статика перед игрой
1. **Проверить штатную границу `Input_ScaleCopyVirtual`:** статика подтвердила, что
   `0x1402893D0` сам переносит DWORD `branch+0x10` в resolved block и затем вызывает
   `Input_ScaleCopyVirtual`. Прямая запись Cursor в block удалена как перезаписываемая;
   QWORD-запись в branch исправлена на DWORD (раньше портила stick X по `+0x14`).
2. Если импульс доходит до Joypad `+0xD0`, проверить запрет команды fire для куклы и
   наличие активного fire-controller, а не вызывать per-slot apply второй раз.
3. **Альтернатива:** снова осторожно native fire только при `gunOk=1 hands=1 select=1
   mode>0 active_art match` (не `hands=20`, не rifle).
4. **Винтовка** — отдельная задача (slot 0 blocked на кукле).

### Сборка / инжект
- DLL: `scanner/build/tll_teleport_hook_v2.dll`
- Запуск: `taskkill /F /IM tll.exe` → две копии → ~18s → `tll_injector.exe --pid PID -- tll_teleport_hook_v2.dll`
- Peer: `reports/coop_peer.txt` = `127.0.0.1`
- Лог: `reports/locomotion_hook.log`

## Turn note (2026-07-03, Cursor — fire relay per-character controller offset)
- **Root cause (prior runtime):** `FIRE-RELAY learned controller offset=0x15cf30` (Chloe) /
  `0x15adb0` (Nadine) logged on send, but **zero** `FIRE-RELAY applied` — receiver used
  global `g_player_fire_controller_offset` from local slot-0 only; puppet is the *other*
  character so `get_player_fire_controller` failed silently.
- **This turn:** `FirePacket` v2 adds per-shot `controller_offset`; receiver
  `resolve_fire_controller(player, preferred)` tries packet offset then known
  `{0x15cf30, 0x15adb0}`; `apply_queued_fire_events` switches weapon by art-id before
  replay, sanitizes pointer QWORDs in `FireArgs[0x20..]`, logs throttled skip reasons.
  v1 wire compat preserved (`controller_offset=0` → fallbacks). **Build OK.**
  **Runtime test pending** — need in-campaign fire with both instances injected.

## Turn note (2026-07-03, Cursor — held weapon draw/select fix)
- **Runtime CONFIRMED (prior build):** `GiveWeapon` puts weapon on puppet's back;
  `read_gun_state` / `safe_request_gun_state` always fail (`gun=-1`, `gunOk=0`);
  `weapon_selected_mode` (manager `vtable+0x78` = controller `+0x130` in-hands slot)
  changes on peer switch but was never applied on receiver.
- **This turn:** sender `infer_gun_state()` — fallback when `+0x15C8` throws: aim bit
  `0x80000` and/or nonzero in-hands slot. Receiver: `find_weapon_slot_by_art_id` +
  `PlayerWeaponController::SwitchToWeapon` `0x1410A1C10` (vtable `+0x40`) on mode/
  art/gun change; logs `select=` and `slot=`. **Runtime test pending.**

## Turn note (2026-07-03, Cursor — held weapon sync v6)
- **Held-weapon path implemented** (`StatePacket` v6, flag `0x80`): sender captures
  active `Weapon+0x860` via `Player` vtable `+0x8C0` (fallback: selected manager slot)
  plus `gun_state` from `Player+0x15C8` gun-state controller. Receiver applies ONE
  `GiveWeapon` per art-id change (not full inventory spam) then requests
  `GunHolstered`/`GunOut` via `+0x15C8 -> +0x58 -> +0xA0`. Logs:
  `HELD-WEAPON-SEND` / `HELD-WEAPON-APPLY`. Old F6 authored-give + NetSnapshot
  publish remain OFF. **Runtime test pending** — Nadine holster attach may still
  break visuals even if inventory path succeeds.

## Turn note (2026-07-03, Cursor — continues Claude)
- **Claude last commit:** `Nadine weapon: final root cause = look lacks player holster
  prop-attach config` — holster issue **PARKED** at asset level; weapon-give and
  publish remain gated off (F6).
- **This turn:** added UDP `FirePacket` relay — sender hooks
  `PlayerFireController::Fire`, learns `Player`→controller offset from first local
  shot, replays captured `FireArgs` on remote retained `Player` each net tick.
  Recorded as **HYPOTHESIS** in `initial-findings.md`; runtime test pending.
- **Still open P0:** jump/roll/melee via button latch (needs in-game verify);
  anim mirror blocked by campaign art-group wall; Nadine holster display.

## Turn note (2026-07-03, Claude)
- **Grenade-on-move: FIXED & CONFIRMED.** The peer's clone threw grenades while
  the peer only ran = synthetic `0x500` gate OR-ed into `branch1+0x10` (the clone
  joypad HELD mask; press edges derived from it by real-input branch `0x1402893D0`).
  Locomotion is stick-driven and does not need it. Removed; F7 restores it.
- **Weapon replication (Codex authored-give): GATED OFF (F6), default baseline.**
  It spilled weapons (the Nadine clone can't attach given weapons → they drop) and
  can't switch the held weapon. Publish path gated too. Stable non-spilling co-op is
  the default now.
- **Nadine weapon display: root cause CONFIRMED, look-swap fix DISPROVEN.** The
  appearance-hash swap (`hooked_appearance`, vtable[92]) changes the SKELETON +
  joint-override + prop-attach via the look builder `0x141BB4280`, not just the mesh,
  so weapons mis-attach (pistol renders as rifle, empty back, crooked aim). Engine
  StringId hash confirmed the domain (chloe 0x3f334972 matches) but real
  nadine=0xc7e2539b BLACK-SCREENS, and every other look variant breaks the skin; only
  the legacy `0xd27c074f` renders Nadine (with broken weapons). So the fix is NOT a
  look hash — it must correct the weapon attach ON 0xd27c074f (deep, open).
- **Repro tooling (single copy, no peer):** F5 = force local player to Nadine
  (`g_force_local_nadine`); F4 = cycle Nadine look candidates; F8 = WEAPON-REGION/
  SCAN/GUN-STATE dumps; F6 = authored weapon-give on/off; F7 = legacy move gate.
- **Nadine weapon — root cause fully nailed, but fix is asset-level (not a quick code
  hook).** ALL weapons pile at the HAND overlapping (pistol pokes out of the rifle,
  etc.); the held weapon is correct but HOLSTERED weapons have no valid attach and fall
  back to the hand. Per-frame update `sub_1410EC230` → joint resolver `sub_1410B5560`
  resolves only standard humanoid joints (targSpine/targChest/…, all present on Nadine),
  so it is NOT a missing joint. The holster is "standard joint + per-weapon OFFSET from
  the look's prop-attach CONFIG", and Nadine's companion look does not define the PLAYER
  weapon-holster prop-attach configs → fallback to hand. That config is asset data loaded
  with the look; no clean runtime hook found (apReference fns are anim-spec parsers;
  player-hide-weapons is script-command indirection). Realistic options: per-weapon draw
  suppression (weapon-draw list still unfound), inject the holster prop-attach config into
  Nadine's look (deep asset-struct RE), or player-2 = Chloe. Details in memory
  `nadine-weapon-skeleton`. Dedicated multi-session effort.



Goal (north star): a fully-playable **second player as Nadine** in the *Uncharted:
The Lost Legacy* campaign (`tll.exe`), via runtime DLL injection — eventually a
two-PC networked co-op.

All work is **runtime injection** (`scanner/src/teleport_hook.cpp` + `tll_injector.exe`).
No asset/.pak modding (rejected as out of scope and because it would change *both*
players, not just the clone).

---

## CURRENT PRIORITY: complete player synchronization first

The current acceptance criterion is no longer a moving positional proxy. Both
copies must reproduce the other player completely: locomotion and traversal,
look/aim, weapons and firing, melee, interactions, health, death, respawn, and
campaign lifecycle. Enemy synchronization is explicitly deferred until this is
complete.

Remote movement is now the protected stable baseline. Both copies passed visual
testing with smooth running and correct facing; final horizontal residuals were
`0.043 m` and `0.071 m` without a hard snap. Sender world movement plus bounded
correction is applied immediately after `0x140C9FE4F`, before the real consumer.
Native locomotion owns position and facing while moving; network quaternion
blending runs only at idle. Do not replace this with exact `+0x130/+0x240` writes.

Player layer `4<->5` collision, both bump probes, and authoritative root pinning
are parked because that sequence caused physics divergence and packet-rate jitter.
The next blocker is full action/state reproduction: traversal, jump/roll, weapons,
firing effects, melee and interactions. Player collision returns later as an
isolated task against this movement baseline.

The present `StatePacket` v5 carries position, quaternion, velocity, both stick
pairs, the complete 64-bit button mask with press/release edges, health, and the
native `NetSnapshotWeapons` region. Weapon appearance publication is implemented
on the receiver; sender capture was hard-disabled because `FillWeaponState` throws
on the retained-table proxy. Static pass (2026-07-03): capture must use the live
resolver player (`+0x1D88` manager). The full helper still cannot be called, so
the sender now reproduces only its campaign-live inventory prefix. The proposed
attachment bypass at `0x14023AEA0` was rejected statically: that predicate gates
only an optional refresh, not the transform writes. Runtime test is pending.

The immediate blocker is the remote actor. `NadineBuddy + MoveTo` cannot represent
all Player actions. A second retained `Player` has the required graph and mechanics.
The earlier claim that its mere presence gates local aim/fire was not cleanly
isolated: the supposedly passive test automatically ran the slot-1 camera owner
every frame. Player presence, slot-1 input, and the second camera must now be tested
independently.

The corrected first matrix case is complete: with slot-1 input, split login, and
camera 1 all off, Chloe can aim normally while a second retained `Player` exists.
The next failure is narrower: two Player jobs concurrently mutate the shared
`joypad-2` disabled-command manager. Its fixed free count underflows to `-1/-2` and
crashes at `0x14183A090`. Both per-slot pools were initialized correctly. The mod
now serializes the original mutation function without changing command semantics.
A fresh load with slot 1 survived for more than two minutes with valid pool counts,
so this crash is closed.

The current build creates a remote retained `Player` only after fresh packets from
the second copy arrive. Role A renders that object as Nadine; role B keeps it Chloe.
State application continues while either process is in the background, and the old
NPC `MoveTo` network avatar is disabled to avoid duplicate remote actors.

Latest static correction: the real per-slot `Joypad` starts at
`FrameState+0xDB0+slot*0xBF0`; `+0xE88` is only a dispatch cursor `0xD8` bytes into
it. The last runtime comparison therefore inspected the wrong memory and its
heading/activation conclusions are rejected. The circular-motion branch's unique
global writes are the built-in Infinite Ammo/Grenades flags, not activation. The
next controlled run compares all `0xBF0` bytes at the corrected address with the
circular and scripted branches kept mutually exclusive.

That comparison is now complete: both processes reported `JOYPAD-DIFF total=0`,
so the persistent-field activation theory is rejected. A mass hardware breakpoint
on the hot copy routine terminated both processes and has been removed. The safe
`block[1]` capture is also complete: matching the circular branch's differing
right-stick values in the scripted branch still did not move the remote Player.
A later claim that `CameraManager(1)` is absent was rejected because both screen-route
entities were already confirmed live in campaign. The current static task is to find
the movement consumer that combines `Player+0x4010` with the selected route pose.

In the one-PC two-instance test, both processes were polling the same XInput user 0,
which coupled their local cameras despite no camera state being applied over the
network. Loopback mode now neutralizes controller input only in the background
process. This test-only filter is disabled for real two-PC connections.

The alt-tab false-death path is now validated fixed. When a background instance
pauses packet production, its remote replica keeps the last confirmed transform
and health instead of falling under local physics. Repeated window switching no
longer killed either player; both processes remained responsive. Four native Player
death entries (`+0xBE8..+0xC00`) are additionally guarded for a live peer, but none
was reached in the successful run, confirming transform hold as the effective fix.

Enemy snapshots, enemy damage relay, and mirrored enemy death remain preserved but
are disabled by default during player work.

---

## Previous Direction: Variant B (superseded reference)

The earlier conclusion that a second retained Player inherently breaks local aim
was disproved by the clean matrix test. The actual delayed crash was the concurrent
joypad command-pool mutation described above. This section remains only as history;
the active route now uses a full remote Player in each process.

**Variant B (correct co-op model):** each PC runs its OWN single full local Player;
the other player is a lightweight non-Player **avatar** (visual mirror via the net),
NOT a local Player. On player-2's PC the local player is made **Nadine** (full Chloe
mechanics + Nadine body) — the original goal, achieved on the 2nd player's own machine.
Each machine has 1 local player => no gating.

- **Step 1 — DONE & VALIDATED:** apply the appearance hook to the LOCAL player (not a
  clone) -> the local player renders as Nadine AND weapons/aim/everything work. Flag
  `g_local_make_nadine` (Player vtable[92] -> 0xd27c074f); no clone is spawned.
- **Step 2 — TODO:** lightweight remote avatar (non-Player; base on NPC NadineBuddy or
  render-only), driven by replicated state; does not trip player-count gating.
- **Step 3 — TODO:** net layer (replicate transform + pose/input); dev/test via a
  fake-peer loopback since Steam blocks two `tll.exe` on one PC.

(The "clone" sections below are the prior approach, kept for reference.)

---

## ✅ WHAT WORKS (validated in-game)

### 1. Second player spawn (the "clone")
A real second `Player`-class actor is spawned in campaign via the retained-spawn
pipeline (`SpawnPlayerFromRetainedDescriptor`, RVA `0x10F1910`), reissued with
`idx=1` using spawn args captured by a HW breakpoint. The clone is a genuine
`Player` (vtable RVA `0x2A4BFE8`) — full player mechanics when controlled.

### 2. Clone renders as NADINE ★ (the headline result)
- The player **appearance hash** lives at `object+0x15A4` (returned by Player
  `vtable[92]` = `sub_140A1DCC0`), and feeds the look builder
  (`sub_141BB5E00` → `sub_141804170`, `ndlib/render/look.cpp`) → appearance-record
  lookups → **mesh**. This is the look/mesh layer, *separate* from the skeleton
  record.
- Chloe appearance hash = `0x3f334972` (`hash("chloe")`); Nadine = `0x0f6d80f3`
  (`hash("nadine")`).
- **FIX**: hook Player `vtable[92]` → `hooked_appearance`; during the clone's spawn
  window (`g_appearance_swap_active`, set around the reissue `spawn()` call) force
  the appearance hash to Nadine's. Main Chloe (slot 0) is untouched. Logged as
  `APPEARSWAP armed ... 0xd27c074f`.
- Earlier wrong levers (recorded so we don't repeat): `+0x170/+0x190` = hair/skeleton
  metadata (hair-only / broke body); Look2-rdx swap fed the wrong arg; record-resolve
  redirect broke the body; co-op gate crashed.

### 3. Clone collision
- The clone's composite/collision body (`player+0x238`, 12 colliders) is **built and
  byte-identical to Chloe's** (`InitializeCompositeBody` RVA `0x10CF470` runs for it).
- The "walks through walls / ghost" symptom was an **artifact of the transform-drag**
  (we used to teleport the clone by writing its position = bypasses collision sweeps).
- **FIX / rule**: drive the clone by its **own locomotion via input** (not by writing
  its transform) → it collides with walls/props/NPCs correctly. The old
  `apply_puppet_transform_post` drag is disabled by default.
- Remaining minor: clone passes through **Chloe** (player-vs-player) but collides with
  NPCs — likely a havok same-group filter; usually intentional in co-op.

### 4. Clone animation (incl. from player-1's view)
- The clone animates fully when driven by its locomotion. The "frozen from player-1's
  view" symptom was ALSO the transform-drag (a teleported clone can't animate). With
  locomotion-driven input it animates regardless of which camera is active.

### 5. Control of the clone (local)
- `NUMPAD3` (play-as-clone): camera → clone (via `swap_local_player_slots`) + scripted
  slot-1 input drives it (`drive_slot1_tick`, reads WASD).

---

## ⚠️ OPEN / KNOWN-LIMITED (local single-PC artifacts)

These are **artifacts of testing on one PC with one input set**, NOT architectural
problems for the real two-PC co-op:

- **WASD moves both** Chloe and the clone: the game routes all local input to the
  active player (Chloe). Reliably suppressing Chloe locally failed (input-suppression
  is mistimed; position-pin crashed on freed objects). In **networked** co-op this
  does not occur — each PC drives only its own local player; the remote clone is
  driven by **replicated input** injected only into slot-1.
- **Janky movement direction**: locally we mapped the stick through the clone's
  unsynced screen-1 camera. In networked co-op we replicate **world-space** movement
  (camera-independent), so this specific jank does not recur (but the network drive
  must be built correctly).
- **FPS drops ~2x when the camera is switched to the clone**: tied to the camera-swap
  (a second viewport/camera). In networked co-op the camera **never** switches to the
  remote player (each PC's camera stays on its own player), so this does not occur.
- **Clone is one-shot**: it does not re-spawn after a location reload (safe respawn
  needs fresh-but-valid spawn args + an asset-stream wait, and must never write freed
  objects — a naive version caused a bald-Chloe clone + crash).

---

## ❌ HARD WALLS (engine architecture / dormant systems)

- **Body via data**: a Player's base body is the `"Player"` char-class (= Chloe),
  data-bound. Every runtime lever to change the *base* body failed; the **look/
  appearance** layer (above) is what we use instead. (.pak modding would change both
  players — rejected.)
- **Manual enemy kill via `AiHitController`**: closed as unsafe. HP lives at
  `enemy+0x4A8 -> HealthSystem`, current HP `+0x3C`, but HP=0 does not itself
  transition the enemy to a corpse. `Die(false)` only knocks down; replayed
  `TakeHit` crashes; `Die(true)` crashes. Static reverse confirms the proper
  HealthSystem setter still only updates HP/history and does not perform death.
  The native upper path is now found: `0x14027A270(AttackInfo*, target)` publishes
  the authored Attack event, `0x14040E7C0` extracts its AttackInfo, and character
  vtable `+0x1170` (`0x140409F70`) performs armor/health/knockdown/death logic.
  AttackInfo is polymorphic and large (`0x860` base, `0x880` melee, `0x8C0`
  projectile); the old `0x100`-byte replay was invalid and is disabled. Each
  type has a virtual heap clone at `+0x8`. The complete
  clone -> `0x14027A270` -> virtual delete prototype is implemented and builds.
  More importantly, the fresh generic `AttackInfo` path has now been validated
  live: it removed enemy health, returned normally, and required no captured
  template. Nadine's live process handle was accepted as the source. The test
  switch is disabled again after validation. Publisher lifetime is confirmed
  synchronous. Static accessors show the stale fields explicitly:
  attacker handle at `+0x08/+0x10`, damage-point count at `+0x24`, points at
  `+0x30` stride `0x50`, each point's hit-object handle at `+0x30/+0x34`, and
  point damage at `+0x40`. The fresh-object path is now recovered: total damage
  is `AttackInfo+0x20`, ordinary armor calculation is `0x1402942A0`, and an
  unmapped zero-handle point preserves full health damage. The probe now chooses
  the nearest live enemy by 3D distance, logs HP before/after, and triggers only
  once per physical key press; this target-selection correction still needs one
  in-game confirmation. Production relay must still provide validated target
  identity across the network.
- **Two-instance enemy death:** a client kill reached the host and synchronized
  back, proving the complete damage relay route executes. The original Y=`-10000`
  death representation has been removed. A process-tagged run then isolated the
  remaining client crash exactly: access violation at `RequestDisableAi+0x17`
  (`RVA 0x3F5D47`) while applying an enemy snapshot. Client enemy synchronization
  no longer calls `RequestDisableAi`; a follow-up role-A termination immediately
  after the same call for Nadine showed that runtime AI toggling itself is unsafe
  during scene construction. All automatic AI enable/disable calls are now off.
  Health/transform replication and native MoveTo remain active. A discarded
  pointer-identity cooldown and an over-broad focus gate temporarily suppressed
  synchronization; background copies now keep sending read-only state while only
  the foreground copy mutates its world. Final death presentation still needs a
  stable implementation and confirmation.
- **Current combat correction (2026-06-28):** role B had been relaying enemy damage
  while suppressing the same hit locally, which explains enemies disappearing on the
  host but surviving on the client. Role B now both relays and applies its native hit;
  locally dead ragdolls are no longer overwritten by enemy snapshots. The NPC damage
  vtable hook is no longer installed on role A, where it was unnecessary and was the
  main remaining suspect in a delayed host-only combat termination. Live confirmation
  is pending.
- **Current world-sync correction:** `RequestDisableAi` was statically confirmed to
  dereference unfinished NPC subobjects without checks. Calls now require the complete
  `npc+0x1FC0` / `npc+0x1F88` chain to be readable. Client enemy pausing is currently
  off because it also suppresses all reactions to local Nadine; pausing NadineBuddy
  is disabled after a host fatal exit.
  The same run caught an initial `FLT_MAX` player position reaching native movement;
  all player/enemy positions are now bounded and checked at every network boundary.
  Production
  state application also no longer stops merely because its game window is in the
  background; the foreground test remains only on manual diagnostics. Live two-window
  confirmation is pending.
- **Enemy death presentation:** lethal base `AttackInfo` is closed after a client
  fail-fast during its kill branch. Replicated lethal damage now mirrors campaign
  `npc-die`: full HealthSystem damage followed by a new pointer-free 0x70-byte
  HitDescription and `AiHitController::Die(true)`. `npc-kill` was rejected because
  it only destroys the process tree. Live confirmation is pending.
- **MoveTo hardening:** the native navigation-frame validator `0x141DEF6C0` is now
  mandatory; the NPC clock/controller/radius/target are checked and refresh returns
  to the previously stable 250 ms interval.
- **Co-op mode gate** `HIBYTE(dword_143E07470)` + `byte_143E03568`: flipping it (mid-
  session OR at init before load) **CRASHES** — campaign levels have no co-op data/
  spawners, so co-op mode dereferences uninitialized co-op session state.
- **Split-screen / 2nd local player**: `net_login_split_screen` / `max_local_players=2`
  are invoked but the 2nd screen/player does not materialize in campaign (dormant,
  like the co-op gate). `0x238800` (materialize 2nd camera) returns failure.

---

## 🧭 REALISTIC SCOPE (agreed direction)

Fully replicating Chloe's **entire** ability set on the clone — weapons, weapon/ammo
**pickup** from enemies & crates, rope/lasso, vehicle entry, interactions — is, in
effect, **completing a full second playable player**, which the campaign engine was
never built for. It is **not a single fix** but a **long, multi-session build with
runtime iteration**, and parts of it run into **dormant/crash-prone systems** (the
co-op gate, split-screen, 2nd-local-player registration).

Likely common root for "world interaction" gaps (weapons/pickup/interacts): the clone
is spawned as an actor but is **not registered as a 2nd "local player"** that the
gameplay/interaction systems iterate over (they serve player 0). Two strategies going
forward:
- **Leverage**: register the clone as a genuine 2nd local player (could unlock several
  abilities at once — but risks the same dormancy/crashes as split-screen).
- **Per-ability**: wire each system manually (e.g. weapons via the `give-weapon`
  handler `sub_140F95850`, which resolves the player by slot via
  `GetRetainedPlayerEntityBySlot(slot)` and a weapon-art-def registry). Slower but
  safer.

The clean long-term target remains **two-PC networked co-op** (replicated input →
clone locomotion), which structurally avoids the local input/FPS artifacts above.

---

## Key addresses / offsets (quick reference)
- Player vtable RVA `0x2A4BFE8`; NadineBuddy vtable `0x2949638`; shared update slot 87.
- Appearance hash: `object+0x15A4`; getter `vtable[92]` = `sub_140A1DCC0` (RVA `0x0A1DCC0`).
  Chloe `0x3f334972`, Nadine `0x0f6d80f3`.
- Spawn: `SpawnPlayerFromRetainedDescriptor` `0x1410F1910`; appearance hook gated to
  the reissue window.
- Collision body: `player+0x238` (`InitializeCompositeBody` `0x1410CF470`).
- Look builder `sub_141BB5E00`; look-apply `sub_141804170` (`ndlib/render/look.cpp`).
- give-weapon handler `sub_140F95850`; `GetRetainedPlayerEntityBySlot(slot)`.
- Co-op gate `dword_143E07470` (RVA `0x3E07470`) + `byte_143E03568` (RVA `0x3E03568`) — CRASHES.
- Tooling: `scripts/launch_inject.sh` (direct `tll.exe`, no Steam launcher; Steam
  blocks 2 instances on one PC). IDA decompile via `scripts/ida_dump_pseudo_env.py`
  with `TLL_IDA_TARGETS`/`TLL_IDA_OUTPUT`.
## 2026-07-10: puppet animation tick confirmed; pickup request sequence fixed

**CONFIRMED (two-instance read-only Exp4):** both slot 0 and the network puppet
have live `Player+0x1A8` `FgAnimData` objects. Their retained owner pairs resolve
back to the expected Player, `FgAnimData+0x10 == Player+0x1A0`, runtime payload
`+0x230` is non-null, and phase bits `flags & 0x38` are all set. Thus the animation
manager does schedule the puppet and dispatches Player vtable `+0x410` to
`Player_Update` `0x1410EC230`. The old "second Player does not tick" explanation
is rejected.

**Pickup correction:** the prior fallback mislabeled `0x141C9A360` as
`Player_StartInteractAction`. It only queries availability through control vtable
`+0x8`; it never submitted the interaction. The relay now matches stock dispatch:
availability `== 1` -> `WeaponPickupInteractControl::RequestInteract` vtable
`+0x28` (`0x1408C8A40`) -> `Player_PublishRequestedInteract` (`0x140C87D70`). The
existing puppet-side `OnInteract` inventory suppression remains active so this
authors the animation without applying the weapon twice. Visual confirmation of
the pickup bend is the next runtime check.
## 2026-07-11: NetEventEmitGrenade receiver path found; relay build ready

The last grenade "descriptor/spawn ceiling" is superseded. `0x1429DCCE0` was the
class-name string, not a vtable. RTTI resolves `NetEventEmitGrenade` vtable
`0x1429FF448`; its `Execute` method `0x140BE63A0` already performs the complete
`ProcessSpawnInfo` + descriptor materialization required on the receiver.

The sender constructor `0x140BE5D30` and symmetric serializer/deserializer
`0x140BE6260/0x140BE6140` prove that event bytes `+0x18..+0xB5` are the portable
POD recipe. A versioned UDP `GrenadeEventPacket` now relays those bytes, rebuilds
the event with default ctor `0x140BE5C80`, and executes it on the game thread.
Peer-spawned `ProjectileGrenade::Init` is echo-suppressed.

First runtime attempt reached a real local throw but logged `built=0`, before any
packet send. The next build separates `ProjectileGrenade_ResolveOwner` from the
event constructor and logs the owner pointer, distinguishing resolver failure from
constructor ABI/state failure. Two copies (PIDs 26696/7396 at handoff) are injected
and waiting for one in-save grenade throw.
## 2026-07-11: grenade Execute fault isolated; interact callsite probe installed

**Descriptor/process wall:** native `NetEventEmitGrenade` POD relay now reaches
`built=1 sent=1` and peer `RECV`. Campaign-unsafe
`WeaponArtId_ToNetworkIndex` was removed from capture; raw grenade art
`0x0B8BD6D6` is transported separately and temporarily mapped through fixed index
18 during receive. Setup stages 1..3 pass; runtime stopped at `EXEC stage=4`, before
peer `ProjectileGrenade::Init`, proving the remaining exception is inside
`NetEventEmitGrenade_Execute 0x140BE63A0`. Current build logs exact exception RVA.

**Puppet-controller wall:** four stock transitions to
`Player_InteractDispatcher 0x140C85D20` were mapped: tail-jmp `0x140C8829B` and
calls `0x140CD4526`, `0x140D74893`, `0x140EC7CBA`. All encode screen index in
`RCX = 0x143EC4EE0 + 0x35C0*screen`. A near relay now forwards them through an
ordinary ABI wrapper and calls the original entry normally, avoiding the broken
partial-prologue trampoline class. Current injected PIDs 29520/30660 report the
four-site probe installed; runtime screen 0/1 samples await save load.

**Runtime result:** screen 0 dispatched repeatedly; screen 1 never did. The puppet
is conclusively skipped above `Player_InteractDispatcher`, so the next target is
the common decision-controller scheduler, not dispatcher internals.

Grenade peer Execute fault was captured at RVA `0xA97393`, exception
`0xC0000094`, in the empty-registry ownerless lookup `sub_140A97380`. The synthetic
event now selects the bounded NetCharacter-owner branch with an unmatched id; that
branch safely continues to descriptor materialization without the division.
