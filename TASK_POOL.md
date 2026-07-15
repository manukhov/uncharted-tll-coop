# Task Pool

## Primary Objective

Полноценный кооп кампании между двумя копиями игры. Текущий порядок работ:

- локальная Chloe на первой копии и локальная Nadine на второй;
- полное представление удалённого игрока в обеих копиях;
- синхронизация всего состояния и всех действий игроков;
- синхронизация противников только после игроков.

## P0: Full Player Synchronization (CURRENT)

- [ ] **Build cr network discriminator (2026-07-15):** run two fresh copies and
      classify the connection failure from one-shot role/ports, first state send,
      and first state receive milestones. If both receives succeed, trace peer
      snapshot freshness and puppet apply without changing UDP role selection.
      The bounded production crash observer is parked.

- [~] **Melee native callsite relay (build cq, 2026-07-15):** static quota met.
      Natural owner tick `0x141279DF0` calls dispatcher `0x141272A50`; exact
      `melee-r1` E8 `0x141273AE2` feeds native queue insert `0x141273FA0`, and
      vtable `+0x220` independently queries literal `melee-r1` through `+0x1F8`.
      Build cq patches only R1/lmouse press callsites, sends a 150 ms pulse, and
      consumes one atomic press in slot 1. Global PlayerInput detour and manual
      dispatcher/queue/anim paths remain parked. Next: two-copy tap/hold plus
      GiveWeapon regression and frame-pacing exam.

- [x] **Grenade world-object relay (2026-07-11):** CONFIRMED v6 — `GRNE` Execute
      spawn + `GRNK` kinematics stream + derived transform write + `fuse_end=now+3s`.
      Peer sees correct arc and landing explosion.

- [~] **Breakable / destructible env relay (2026-07-11):** STATIC map + mod v1 landed.
      Campaign grenade fence uses `SimpleBreakable_ExplosionDamage` `0x88E6F0` /
      post-site `0x88DC83`, not MP `SendNetEventBreakableAttack*` (no `BRK-MASK` in
      campaign). Relay: deferred `SendNetEventSyncBreakable` → `BRKS` v2
      (`scene_hash`, `world_pos`) → peer `NetEventSyncBreakable::Execute` `0xA65160`
      + resolver override. **REJECTED runtime:** all hooks install after REX prologue
      fix, but zero `BRK-*` log lines and peer wall stays intact. Next: confirm hook
      fire in-game or pivot to `sub_140A657A0` / Havok break path.

- [~] **Puppet interact promotion Exp5c:** the four direct dispatcher transitions
      are patched through a near relay + ordinary ABI wrapper. This preserves the
      frame-from-entry-rsp function and logs dispatcher screen index. Next runtime
      result: screen 1 means the gate is inside dispatcher state; screen 0 only
      means the puppet's owning decision controller is skipped above it.

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
- [~] **Fire relay (v7):** native `PlayerFireController_Fire` replay on puppet when
      `puppet_native_fire_allowed`; sustained `0x800` never from StatePacket (FirePacket
      only). Load guards: GiveWeapon vtable `+0x158` + 5 s warmup.
- [~] **Aim sync:** default-on pose-only relay REJECTED 2026-07-09: strong running
      jitter with continuous `AIM-TARGET-WRITE`. Keep `g_puppet_aim_reticle_relay`
      OFF by default; Numpad `/` is hard-disabled after accidental ON reproduced
      the turn/run jitter. Puppet full ADS remains OFF / manual `F8`. Do not
      re-enable broad screen-1 pose-seed/source writes; that path
      freezes locomotion facing.
- [x] **Puppet melee sync — PARKED (2026-07-15, baseline co):** Entire relay OFF
      (`g_enable_puppet_melee_button_relay=false`, exec false). **CONFIRMED:** cn
      broke weapon give via `PlayerInputCommandState IsCommandActive` hook; **co**
      restores guns (user). cg button layer was CONFIRMED but anim never shipped;
      cm/ck/ch REJECTED (crash/hang). Do not re-enable without alternative to
      global `0x183ABF0` patch. Static anchors kept: queue `0x141273FA0` @
      `controller+0x150`, TryAnim `0x1410FC9F0`, net gate `g_NetModeFlags+3`.
- [~] **Production performance baseline (build cp, runtime pending):** fixed the
      Numpad7 permanent logging enable; compile-time parked force logs, hot
      input/Joypad/weapon diagnostics, spawn DR0 capture, and pass-through
      GunMoveShield probes. Cloth repair retains a cheap vtable/parent heartbeat
      without DrawControl mesh scans. Test two fresh copies before reopening
      melee; see `reports/runtime_hot_path_audit_20260715.md`.
- [~] **Puppet ADS visual aim (PARKED 2026-07-14):** Upstream CONFIRMED; downstream
      eval/render gap. **REJECTED:** palette/skinning/pubState/PerJoint workspace paths.
      **cd:** stable hook, user visual unchanged. Inject OFF in `ce`.
      Reports: `player_aim_palette_render_chain_20260714.md`,
      `player_aim_workspace_static_20260714.md`.
- [ ] **Head / camera look sync:** STATIC done (`reports/lookat_field_map.md`).
      PARKED — spine twist unacceptable (`g_puppet_head_look_sync=false`).
- [ ] **Puppet controller model (NEW):** STATIC map in
      `reports/player_decision_node_controllers.md`. Nearby-list writer DONE;
      decision-node scheduler DONE (`0x14042D120` / `0x1404223F0` / `0x140CD3760`).
      Next: `player->vtable+1512` + `player+0x1808` selection per slot +
      decision-node scheduler for screen 1. Unblocks pickup gesture / aim-pitch /
      interact SM — not per-feature replay.
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
- [~] Надёжные события: выстрел, перезарядка, смена оружия, ближний бой, прыжок,
      уклонение, взаимодействие, получение урона, смерть и возрождение.
      СТАТИКА 2026-07-05 (реестр «Reliable-events map»): все кнопочные действия
      резолвятся из масок Joypad `+0xD0/+0xD8/+0xE0` (дерив из `block+0x518`), т.е.
      прыжок/уклонение/перезарядка/ближний бой/смена оружия/взаимодействие приходят
      БЕСПЛАТНО как ввод (нативное действие+анимация в каждой копии). Отдельного
      кода на событие не нужно. Остаётся согласовать побочные эффекты: направление
      (pose-seed v8), какое оружие/боезапас (held-weapon relay + pre-give), цель
      ближнего боя/взаимодействия (фаза врагов + позиция + возможный InteractEvent).
      Урон/смерть/возрождение — HP уже в StatePacket v3 (см. выше).
- [~] Гранаты (план готов, статика 2026-07-05, реестр «Grenade sync»): направление
      дуги/броска = тот же per-screen pose seed, что у пуль
      (`PlayerGrenadeController_UpdateAimArcAndToss 0x140E17AC0`, 10× выборка по
      `owner+0x4010`) → общий фикс: StatePacket v8 c 32-байтным сидом
      (`*(CameraManager(0)+0xB0)+0x60`) + хук
      `ResolveScreenRoutePoseSeedOrDefault 0x141C077B0` (override при idx==1).
      Кнопка броска уже релеится (биты 0x100/0x400 не срезаются). Остаётся:
      боезапас гранат у Второго игрока (pre-give / Infinite-Grenades флаг для
      теста), проверка `CheckActionExclusions 0x140E2ADF0` (глотает authored
      Grenade в части состояний), возможная утечка HUD-дуги
      (`general/hud/grenade-arc`) — гейтить по `state+0xF8` как ретикл.
- [~] Выстрел: UDP `FirePacket` + штатный импульс кнопок. Прежняя неверная маска
      `0x90000` доходила до Joypad 3, но не соответствовала командам огня. Исправлена
      асинхронная смена оружия: после `requested=1 attached=0` код теперь ждёт
      фактический `attached=1`, а не прекращает проверку. Текущий шаг: различить
      возвраты `0x140E2ACA0` (активное оружие/нажатие/запрет действия) и
      `0x140E2B220` (состояние оружия/боезапас/задержка). Прямой повтор
      `PlayerFireController::Fire` остаётся выключен из-за прежних падений.
      Последняя проверка локализовала разрыв раньше этих проверок: удалённый
      контроллер стрельбы не получал фронт. Таблица команд дала правильные маски
      `aim=0x200`, `fire=0x800`; фронт огня теперь сохраняется после обновления
      движения до отдельного обновления контроллера стрельбы. Проверка `0xA00`
      ожидает `FIRE-GATE input press=1`.
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
- [x] Заменить погоню клона за сетевой позицией на передачу
      штатного результата движения локального Player. Определить минимальный
      устойчивый набор полей из `PlayerInputCommandState` и точку их применения
      к клону после `0x140C9F8B0`, не смешивая движение с коррекцией позиции.
- [x] Статическая граница определена: сырой ввод `Player+0x39C8`, величина
      `Player+0x3A1C`, мировой вектор `Player+0x3A30`; единственный расчёт
      выполняет `0x141E397C0`, потребитель — `0x140F03500`.
- [~] Пакет v4 передаёт правильный мировой вектор, но прежнее применение после
      метода №16 оказалось запоздалым: `0x140C9FE4F` рассчитывает состояние, а
      вызов в `0x140C9FE76` потребляет его до возврата метода. Это объясняет
      последний результат «бежит на месте». Текущая проверка подставляет поля
      сразу после `0x140C9FE4F`, до потребителя, без поправки координат.
- [~] Проверка до остатка метода №16 отвергнута: обе копии получали величину
      `1.0` и правильный мировой вектор, но удалённый Player оставался у места
      появления. RTTI также отверг старый перехват `0x14042FB40 -> 0x141DE8EC0`:
      это путь Npc/NavCharacter, не Player. Текущая проверка пишет сетевую позицию
      в родительских локальных координатах в `Player+0x130` до общего метода
      `0x141BB8320`, чтобы он штатно обновил `+0x240/+0x250/+0x280` и коллизию.
- [~] Применение через `+0x130` вернуло движение, но последний запуск оставил
      рывки и визуальное расхождение примерно `1-1.5 м`; внутренний остаток
      `PUPPET-POS` доходил до `7-10 м`. Статически подтверждены прямое
      преобразование `0x14014E240`, обратное преобразование внутри `0x1416862B0`
      и конечная запись `0x141BB8320`. Следующая проверка сравнивает сетевую цель,
      точный повторный расчёт движком и фактический `Player+0x240`; коэффициенты
      сглаживания до разделения этих причин не менять.
- [~] Точная проверка завершена: в обеих копиях при движении и в покое
      `target == predicted == actual`, обе ошибки равны нулю. Преобразование и
      поздняя запись исправны. Остался дискретный поток примерно `20-24` пакета/с,
      который раньше без промежуточных положений подавался на каждый кадр.
      Добавлено выравнивание между двумя подтверждёнными позициями с задержкой
      `75 мс`; требуется визуальная проверка рывков и остановки.
- [~] Выравнивание `75 мс` отвергнуто: сильные рывки остались у обоих персонажей.
      История локализовала регрессию между последней плавной версией `25637e8` и
      включением коллизии `a06c79c`; точная покадровая позиция появилась позже как
      попытка компенсировать расхождение двух физических симуляций. Текущая
      контрольная сборка возвращает политику `25637e8` и отключает коллизию,
      наблюдатели столкновения, прямую подачу перед потребителем и запись корня.
- [~] Контрольная сборка убрала рывки, но оставила `2.98/3.42 м` ошибки и неверное
      направление корпуса. Причина локализована: старый слот-16 уже не подставляет
      сетевой мировой вектор, а новый перехват перед потребителем был ошибочно
      отключён вместе с опытами записи корня. Он возвращён с ограниченной поправкой
      позиции; поворот во время движения снова принадлежит штатной локомоции.
- [x] Текущий эталон движения подтверждён в обеих копиях: рывков нет, направление
      корпуса верное, конечная ошибка `0.043/0.071 м`, `hardSnap=0`, поворот в
      покое сходится до `orientationDot=1`. Не менять вместе следующие владельцы:
      мировой вектор до потребителя, штатная локомоция позиции/поворота при беге,
      ограниченная поправка только при движении, сетевой кватернион только в покое.
- [ ] Коллизию игроков и анимацию столкновения возвращать только после остальных
      действий и отдельной проверкой поверх зафиксированного эталона. Ветки
      `4<->5`, bump-наблюдатели и точная запись `+0x130` сейчас намеренно отключены.
- [x] Убрать крупное расхождение без жёсткой покадровой записи координат:
      ограниченное схождение во время движения уменьшило измеренный остаток до
      примерно `0.10/0.14 м`.
- [~] Поправка уменьшила расхождение, но выявила вращение на месте: свободный
      режим выходил до применения `Player+0x140`, а микропоправки продолжали
      включать движение около цели.
- [~] Проверенный сетевой поворот применяется независимо от режима позиции,
      микропоправки отбрасываются. Вращение на месте стало заметно меньше;
      окончательная длительная проверка ещё нужна.
- [x] Найдена причина асимметричного скольжения в покое: при нулевом вводе
      записывался ненулевой мировой вектор поправки, поэтому физика двигала тело
      без анимации. Поправка координат теперь действует только во время настоящего
      движения; в покое остаётся лишь аварийный перенос при ошибке свыше 4 м.
- [x] Первый предел `0.015` был недостаточен: измерены остатки `0.29 м` и
      `1.35 м`; при движении ошибка уменьшалась, в покое оставалась неизменной.
      Политика заменена на 8% ошибки, максимум `0.05`, мёртвая зона `0.03`.
- [x] Усиленное схождение подтвердилось: остаток снизился примерно до `0.10 м` и
      `0.14 м`; дальнейшую доводку сантиметров отложить до исправления поворота.
- [~] Плавное сетевое направление визуально стало лучше: вместо полной записи
      кватерниона
      каждый кадр применяется нормализованное сближение 18% по кратчайшей дуге.
      Вращение в покое не вернулось; точное запаздывание ещё не измерено.
- [x] Изоляция общей клавиатуры подтверждена: локальный прыжок больше не запускает
      клона в том же процессе. Joypad 3 получает действия только через переданные
      индивидуальные маски; Joypad 2 сохраняет обычную клавиатуру.
      Первый запуск не проверил гипотезу: обе копии упали в `0x14183875D`, потому
      что обратный переход трамплина затёр живой `RAX=entry RSP`. Переход заменён
      на не меняющий регистры `jmp [rip+0]`; повторить тот же прогноз.
- [~] Штатные маски кнопок Joypad 2 передаются и подаются в `source+0x518` до
      обработки Joypad 3. Добавлено накопление коротких фронтов нажатия и
      отбрасывание старых пакетов; требуется проверка прыжка, приседания, переката,
      ближнего боя и переходов удержания/отпускания.
- [~] Коллизия игроков: старая запись `collider+0xC4` отвергнута. Обычный Player
      использует слой 4, сетевой персонаж слой 5; глобальная матрица запрещает
      `4↔4`, `4↔5`, `5↔5`. Реализована узкая схема: клон получает 5 штатным
      установщиком, разрешается только `4↔5`; физическая коллизия подтверждена.
- [~] Убрать телепорты после столкновения: подтверждено, что старый порог 4 м
      срабатывал на расхождение двух физических миров (`4.19/5.72 м`). Сценарный
      перенос теперь помечается отправителем отдельным флагом; обычная ошибка
      контакта не вызывает перенос. Новый запуск выявил постоянную ошибку в покое
      `1.07/1.28 м`, потому что обе ветки коррекции отключались при нулевом вводе.
      Медленное сведение в покое подтверждено: остаток упал до `0.146-0.150 м`.
      Усиление до `20%/0.20 м` отвергнуто: ошибка достигла `6.103 м` при движении,
      после остановки модель доскальзывала без анимации. Настройка коэффициентов
      больше не рассматривается как решение. Точная запись одновременно в
      `Player+0x130/+0x240` дала миллиметровую позицию, но сломала анимацию даже
      без обнуления скорости. Статически доказано: `0x141BB8320` использует
      `+0x130` как вход корневого движения и выводит мировой результат в `+0x240`.
      Запись только внешнего `+0x240` также отвергнута: удалённый Player бежал на
      месте. Обычная покадровая запись координат отключена; сначала проверяется
      перенос движения в правильной фазе метода №16.
- [ ] Добавить штатную реакцию руками при столкновении. Найден
      `PlayerBumpController_BuildBumpActionCandidate 0x140CE2520`; прежняя
      атрибуция жестов функции `0x140CE9530` исправлена. Удалённый Player уже
      штатно поддерживает свой индекс `SimpleGridHash` через `0x1410EC230`, поэтому
      повторно регистрировать его нельзя. Следующий шаг: только чтением записать
      цель/дистанцию из `Crowd::ProbeResult` и результат `0x140CE8E10`, разделив
      отсутствие цели, отказ геометрии контакта и вытеснение уже начатого действия.
      Верхний обработчик через `0x142A0EF08` вызывается, глобальный флаг равен
      нулю. После исправления владельца `0x140CE8E10` достигается для обоих слотов,
      но при расстоянии `0.851872 м` запрос всегда пуст: цель `0`, дистанция
      `FLT_MAX`. Новый кандидат в `Crowd_UpdateProbe 0x1406DD350` обязан иметь бит
      возможности с индексом из `0x143D0A724`; следующий журнал только читает этот
      бит у противоположного Player, не меняя состояние.
- [~] Фоновый ввод Joypad 3 больше не подменяет общий флаг фокуса
      `0x1437EA62C`. Узкий обход проверки внутри `0x14183B410` сохраняет ввод
      второго игрока, но оставляет системному обработчику настоящий фокус окна.
      Проверить, что курсор освобождается при переключении окон.
- [ ] Сложные режимы: укрытия (**CONFIRMED `bk` 2026-07-08**), лазание, уступы, верёвка, транспорт и совместные
      действия.
- [ ] Жизненный цикл кампании: контрольные точки, сцены и переходы между главами.
- [ ] Проверить длительную работу двух копий без аварийного завершения и
      накопления расхождения.

Сейчас `StatePacket` v4 передаёт положение, поворот, скорость, здоровье, движение
осей и полную 64-битную маску кнопок с фронтами нажатия/отпускания. Найденный
глобальный блок ввода в кампании остаётся нулевым, поэтому поля действий пока не
содержат настоящего локального ввода. Для разовых действий нужен отдельный
надёжный упорядоченный поток с номерами и подтверждениями.

## P0.5: Build-ready design backlog (STATIC done 2026-07-05; pending runtime pins)

Все пункты ниже разобраны статически end-to-end; детали и адреса — в
`reports/initial-findings.md`, рантайм-чтения собраны в
`reports/runtime_pins_checklist.md`. Коммиты указаны для быстрого grep лога.

**Боевые действия / направление:**
- [~] Направление aim/fire/гранаты: fire-ray = per-screen **pose seed**
      `CameraManager::GetHandle(idx) -> *(mgr+0xB0)+0x60` (32Б pos+quat), резолвер
      `ResolveScreenRoutePoseSeedOrDefault 0x141C077B0`. Фикс: relay сида (StatePacket
      v8, +32Б) + хук резолвера, override при idx==1. Гранаты используют ТОТ ЖЕ сид
      (`PlayerGrenadeController_...UpdateAimArcAndToss 0x140E17AC0`). (`820d89a`,
      `425c163`, `4cd55ca`, `d6c4a41`, `a873085`) Runtime-пин #2.
- [x] Снайпер-скоуп HUD-гейт: по локальному ADS (`Player+0x1548`), `286b6f1`.
- [~] Снайпер-скоуп «Хлоя пропадает»: DrawControl-скрытие меша куклы при scoped-ADS;
      форсить видимым. Runtime-пин #6 (`26bb1ad`). Обход: pose-only.
- [~] Эффекты/взрывы/трассеры/импакты: НЕ отдельный канал — нативные следствия
      настоящих снарядов в каждой копии (`add9bf2`). Гранаты/анимация броска —
      нативно из реле кнопки (`ee01be5`, `b698b67`).

**Надёжные события:** карта — все кнопочные действия приходят бесплатно через реле
маски Joypad (`+0xD0/+0xD8/+0xE0`); мили-цель локальна → фаза врагов (`d934aa8`).

- [x] Звук: НЕ отдельный канал — шаги/фоли = anim-EFF (EffectControl), играют с
      анимацией куклы; тир-4 `NetSnapshotEffectParser` переигрывает их точно;
      выстрелы/взрывы нативно; диалоги = DialogManager; 3D-локализация верна через
      позицию куклы. Опциональный SoundEvent-релей только для не-аним/не-экшн
      one-shot'ов (пока не нужен) (`418757d`).

**Нарратив (вариант 1 «Хлоя — драйвер сюжета»), всё без net-сессии:**
- [~] Прогрессия: релей команд `GameTaskGraphMgr` + apply через
      `AddCommand 0x141CB95B0` (список `0x146A514A0`, 40Б записи). Runtime-пин #3.
      (`80d4e18`, `666c7f7`)
- [~] Осмотры/взаимодействия: релей флага `state+0xD0 бит1`
      (`ScriptQuery_InteractableHasBeenInspected 0x1408B1350`). Runtime-пин #4.
      (`f15063b`)
- [~] Подавление триггеров follower'а: `suppress-interactable-system 0x1408AC710`
      + per-command joypad disable (`1474ab9`).
- [x] Диалоги/бантер: бесплатно per-copy (DialogManager — не AI), кукла остаётся
      объектом с отключённым AI (`b6c2687`).
- [~] Камера головоломки: follower видит через синхронизированную сцену (Path B)
      или релей транформа экрана-0 (Path A). (`ad342b1`)

**Spectate / асимметричный кооп:**
- [~] Spectate/pause-hold локального игрока (move-stick/transitions off + подавление
      + существующие 4 death-хука `Player+0xBE8..+0xC00` + пин). (`7e67a1a`)
- [~] Игрок 2 наблюдает за игроком 1 в периоды отсутствия Нэдин (пролог): детект
      через `g_last_nadine_object` + идентичность локального == Хлоя; spectate +
      релей камеры копии 1; переход при спавне/деспавне Нэдин. (`e0cd5bc`)

**Машина:**
- [~] Пассажирское сиденье: `Npc_TeleportIntoVehicleSkill 0x1406B14D0` (кукла — Npc).
      Двухкопийная модель: водитель ведёт нативно на своей копии, релей транформа
      машины, пассажир едет через parenting сиденья. Runtime-пин #5.
      (`aa5f550`, `58fecf2`, `441a3e1`)

**Анимация (полная синхронизация):**
- [~] Тир 1 (input-driven) — уже работает через реле ввода. Тир 2 (скрипты) — с
      нарративной синхронизацией. Тир 3 (рагдолл/downed/оружие) — релей флагов
      NetSnapshot. Тир 4 (полная точность) — ДОСТИЖИМ: capture
      `FillAnimCmdBitStream 0x140BC4E60` напрямую (512Б) из `Player.AnimControl`,
      apply через `AddSnapshot`→`ProcessPendingSnapshots` на Player-embedded
      генераторе `Player+0x1B30`. Runtime-пин #1. (`59d93be`, `b8e564e`, `9017fdc`)
- [ ] Оружие Нэдин: СТЕНА — asset/skeleton-AP (холстер-точка из дефа оружия требует
      сустав на скелете look'а). Не code-fixable (`487a3c3`).

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

## P2 — Weapon pickup sync (добавлено 2026-07-06, после закрытия weapon-display кластера)

**Контекст:** оружейный кластер (смена/holster/спавн) решён и user-verified (commit
b3549d8). Подобранное с земли оружие пока показывается приближённо (свой длинноствол
по слоту): точный арт требует полного weapon-info блока — give с одним art@0x80 для
не-whitelisted артов НЕ прилипает (движок спавнит-и-роняет; см. REJECTED-фонтан в
initial-findings 2026-07-06).

**Задача A — точный арт подобранного оружия.** Реверсить схему weapon-info блока
(0xC0 байт, initialize_info 0x14... в hook_offsets kInitializeWeaponInfoRva):
какие поля кроме art@0x80 обязаны быть валидны, чтобы give прилип для произвольного
арта. Кандидат: скопировать блок из реального пикапа сендера (hook на give котроллера
локального игрока), а не синтезировать.

**Задача B — анимация подбирания на кукле.** ВАЖНО: Player_TryStartBumpAction
0x140CE8E10 — это CROWD-bump (толкание NPC), НЕ подбор (проверено декомпилом
2026-07-06). Реальные якоря подбора:
- `PickUpController` строка 0x142953600, ref 0x1404C132D (фабрика/ctor контроллера —
  он крутит анимацию подбора у игрока);
- `WeaponPickup` строка 0x1429118D0, refs в sub_1400C5560 (класс объекта-пикапа
  на земле; там же "'%s' is NULL or not a WeaponPickup");
- `ProcessPickup` 0x1429397C0, `instant-pickup`/`disable-pickup` скрипт-флаги.
План: sender ловит старт своего PickUpController → шлёт событие с идентификацией
пикапа (name-hash/позиция) → receiver находит зеркальный WeaponPickup и запускает
PickUpController куклы. Сначала квота статических доказательств (CLAUDE.md).

**Уточнение задачи B (статический разбор 2026-07-06, поздний заход):**
- Подбор идёт через ГЕНЕРАЛЬНЫЙ интеракт-диспетчер игрока sub_140C85D20
  (строки player-pressed-interact-nearby / player-interact-hold-*; 4+ вызывающих
  контроллера). Ветка пикапа: sub_140C7FF40 резолвит из def'а пикапа anim-ids
  (weapon-pickup-swap / weapon-pickup-when-no-weapon) И command-id интеракции
  (out-параметр a6).
- Проверка нажатия: sub_14183A3D0(g_LocalPlayerInputManager_Ptr + 3504 +
  3056*screen_idx, cmd_id) сканирует ДИНАМИЧЕСКИЙ СПИСОК command-id по
  +0x590 (1424) в пер-скриновом блоке — это НЕ битовая маска +0x518, которую мы
  уже релеим. Поэтому у куклы интеракции не срабатывают вовсе (объясняет и
  отсутствие случайных катсцен от куклы — сохранить это свойство!).
- Два пути релея пикапа: (1) инжект command-id в список экрана-1 (найти писателя
  списка; ср. kAddJoypadDisabledCommandsRva 0x1839FB0 — команды в этом мире
  списковые); (2) прямой вызов конца ветки пикапа (функция «start pickup action»
  после инпут-чека, ~0x140C87xxx после LABEL_181/HIBYTE(g_NetModeFlags)) с
  (player, pickup) — обходит инпут вовсе, но надо доразобрать её сигнатуру.
  Путь (2) предпочтителен: точечный (только пикап, катсцены не задеваются).
- sub_140C88130(disp, cmd_id, table): чек нажатия — сканит таблицу кандидатов
  (записи 12 байт: {cmd_id, hold_time float, +9 pressed, +11 hold-mode}; hold
  сверяется с engine clock через disp+13720). Таблица строится диспетчером из
  пер-скринового инпута каждый кадр. => Точки инжекта: (1) писатель списка
  +0x590 пер-скринового блока (до диспетчера) или (2) прямой вызов start-pickup
  (после чека). Следующий шаг: найти писателя +0x590 списка (xref на поле) И
  дотрассировать ветку диспетчера от v323=1 до вызова старта пикапа.
