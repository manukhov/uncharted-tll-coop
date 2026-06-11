# TLL Co-op Mod Roadmap

Target build: `tll.exe 1.4.21058.0`

## North Star

Сделать полноценный кооп для кампании `The Lost Legacy` на двух ПК:

- player 1 играет за Chloe;
- player 2 играет за Nadine;
- у каждого свой инпут и своя камера;
- host остаётся authority для campaign state, checkpoints, combat, scripts и saves.

## Single Focus

Единственный текущий фокус:

**поднять Nadine в реальный second-player path движка, а не улучшать buddy/NPC control.**

Пока это не сделано, всё остальное вторично.

## Current Gate

Сейчас главный блокер один:

- после верхнего net/front-end shutdown path `0x1409FD360`
- restart path не вызывает startup bind напрямую
- он ставит callback `0x1409F9B20` в deferred queue через:
  - `0x1415B43B0`
  - `0x1415B4870`
  - `0x1415B47C0`
  - `0x1415B4560`
- queue rooted at `0x1437E9700`

Текущий вопрос therefore узкий:

**какой consumer drain'ит эту queue, и при каких условиях queued `0x1409F9B20` реально исполняется и снова bind'ит `ctxB`.**

Пока это не закрыто, полноценный second-player bringup остаётся неполным.

## Active Plan

### 1. Restore the real second-player bind/restart lifecycle

Need:

- найти drain/consumer path для deferred queue under `0x1437E9700`
- доказать, когда исполняется queued callback `0x1409F9B20`
- подтвердить, что из него реально доходит до:
  - `0x1409FA94E`
  - `service->virtual +0x20(&ctxB)`
- объяснить, почему live campaign всё ещё может оказаться в состоянии
  `ctxB+0x8 == 0`

### 2. Convert that lifecycle into host-side second-player bringup

После восстановления правильного bind/restart path:

- довести Nadine до real player-like ownership path
- завести verbs:
  - locomotion
  - traversal
  - interact
  - melee
  - weapon/aim/fire
- завести independent camera ownership

### 3. Turn host-side second player into actual co-op

Только после пункта 2:

- input transport from second PC
- host-authoritative state replication
- second-PC camera/render path

### 4. Harden campaign compatibility

После working co-op loop:

- checkpoints
- respawn/death
- chapter transitions
- vehicles
- dual interactions
- script-locked scenes / cutscenes

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
  - `0x143E0EF00`
  - `0x143E0EF08`
  - `0x143880318`
- second-player net path:
  - `0x140A90550`
  - `0x140A9AAD0`
  - `0x140B347C0`

## De-Prioritized Until Further Notice

Do not make these the main track right now:

- polishing `NadineBuddy` local control
- reviving old `slot1_probe*.dll` iterations
- clean packed IDA database work
- camera polish before real second-player ownership exists
- transport/render work before host-side second-player path is real

## Notes

- Detailed reverse-engineering evidence belongs in
  `reports/initial-findings.md`.
- Reproducible labels/comments belong in `scripts/ida_apply_labels.py`.
- `ROADMAP.md` should stay short and directional, not become a reverse log.
