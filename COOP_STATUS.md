# TLL Campaign Co-op — Status & Findings (2026-06-28)

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
