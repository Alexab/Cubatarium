# Amphibious transitions и остановка у поверхности

Проверка снята текущим Release EXE
`a0872e87a1551dbe7ac0cff1346b58176743d18eba13817cb7779473415df68f`.
Она закрывает конкретные пробелы прежнего dry-only transition probe;
финальная R6, terrain routes и performance остаются открытыми.

## Проверки и найденное исправление

Новый opt-in `CreatureQaFluidTransitionProbe` задаёт десять направленных
переходов между land_idle, land_walk, fluid_idle, free_swim и bottom_tread.
Условия состояния, архетипа, inFluid, onGround и onFluidBottom заданы явно.
Для каждой пары —16 outgoing phases и21 retained UpdatePose/SubmitDraw шага.
Клипы проверяются по действительным state_map. Это не старый dry-only probe
под другим названием: переходы с тем же clip тоже сохраняются, если меняется
физическая поддержка/среда. Старый probe не изменён и не подменён новым.

`audit.json`:160 случаев/3360 CPU кадров,1680 supported frames,
minY=−0,00000005588 блока; каждый фактический draw покрыт render envelope.
Свободная водная поза может иметь отрицательный localY: это не наземный контакт.
Доказательства CPU не заменяют GPU blend/terrain проверки.

Отдельный world driver задаёт только обычный CreatureIntent: пауза,
движение по+X, пауза и по−X. Не меняет факты, позу, скорость тела, положение
или possession; мотор/коллизии/выбор состояния выполняются обычным путём один
раз. 20 секунд wall flight,1-секундный framebuffer interval, actual facts JSONL.
Aquatic `facts.horizontalSpeed` включает вертикальное перемещение, поэтому
аудитор независимо вычисляет **XZ travel из позиций**, а не считает buoyancy
доказательством горизонтального движения.

Найден product bug: NPC guard требовал HasChunk для центра и точки ниже тела.
Когда пингвин всплывал из воды63 в отсутствующий sky slice4, он замораживался,
а SettleFrozenLocomotion переводил Swim в Idle. Его камера и мировой цикл
продолжали работать. В `fluid_v3` из20 wall секунд лишь2,219 секунды дошли до
ExecuteIntent; этого недостаточно для приёмки маршрута.

Исправление использует **существующий критерий ray tracing** известного воздуха:
terrain column complete, непустая storage identity, нет pending save, missing
slice строго выше resident/disk highest data. Эта политика вынесена без смены
семантики из OpaqueVoxelRayTrace в общий helper. Resident slices принимаются
сразу; дыры внутри диапазона terrain, незавершённые columns и незагруженные
сохранённые постройки остаются unknown. NPC ниже world MinHeight не получают
нового разрешения симуляции. Это не произвольное приравнивание unloaded к AIR.

Native `fluid_v4`:350 записей/15,672 sim секунд,1,786 блока XZ перемещения в
обе стороны;272 шага, которые отверг бы прежний HasChunk guard, успешно
выполнены на доказанном sky. Frozen и solid-overlap записей нет. Новый unit test
проверяет ocean boundary, incomplete/pending-save/missing-storage, disk tower,
resident/internal holes и отрицательные высоты. Общие15 targeted Release tests
проходят. Стоимость новых metadata queries ещё не прошла matched p95 gate.

## Визуальный просмотр и ограничения

Непосредственно просмотрены три native full-turn листа —Idle и две Walk фазы,
8 yaw+top/bottom; они не называются полным Swim turntable. Также просмотрен
`runtime_fluid_candidate/frame_010.png`: настоящая горизонтальная водная поза
пингвина. Соседний frame15 содержит естественно появившегося puffin и не
выдаётся за отдельное подтверждение penguin.

Пауза ввода не обязательно даёт Idle: действительное вертикальное всплытие
может давать Swim, а выход из воды —Jump/Fall. В route есть все эти состояния.
Их частое чередование/поведение у поверхности остаётся предметом R6 review;
исправление остановки не объявляет физику плавания художественно принятой.

## История неуспешных/недостаточных прогонов

- `../fluid_v1`: process0 и реальные Swim facts; runner postcheck ошибочно
  искал PascalCase вместо enum ToString lowercase. Сохранён как failed run.
- `../fluid_v2`: parser исправлен, но postcheck ошибочно требовал Idle при
  паузе ввода во время реального вертикального движения. Сохранён failed.
- `../fluid_v3`: runner pass после исправления критерия паузы, но независимый
  разбор обнаружил остановку после2,219 sim секунд. Это **не** приёмка движения.
- `fluid_v4`: исправленная known-air readiness, полный независимый audit PASS.

У публикации есть отдельный [world_published](../world_published/audit.json):
обычные bin/models без QA override,438 записей/17,325 sim секунд,1,859 блока XZ,
345 восстановленных known-air шагов, ни frozen, ни solid overlap.
Native cases повторяют те же160 сценариев; два запуска не считаются320
различными типами переходов. Wet/dry bank routes, реальный world Tread,
GPU crossfade, другие amphibious species, остальные habitats и perf pending.
