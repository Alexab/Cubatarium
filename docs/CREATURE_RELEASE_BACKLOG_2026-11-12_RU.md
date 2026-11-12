# Последующая доводка существ — исключено из текущего90-minute выпуска

По прямому указанию пользователя12ноября эти пункты документируются и **не
удерживают текущую цель открытой**. Их наличие не означает завершённой R6;
текущий результат называется ограниченным выпуском с известными остатками.

| ID | Область | Что остаётся и как продолжить |
|---|---|---|
|ART-01|Все56+5|Полная individual original/current приёмка силуэта, материалов и всех фаз; текущие массовые листы — release triage, не исчерпывающая художественная экспертиза|
|ART-02|puffin/owl, treeman/tree_monster, whale/seahorse|Складывание крыльев и ground posture, толщина/выразительность ветвей, пластика хвоста/тела; сохранить уже опубликованные улучшения|
|MOT-01|badger|Повороты, slopes/steps/terrain normals, retained fades/start-stop/action/Run; текущий запечённый Walk проверен на flat straight routes, не runtime terrain IK|
|MOT-02|badger|Foot local rotation до≈103°, knee swing≈129°: художественные joint limits/heel-toe/four-beat gait требуют отдельной оценки|
|MOT-03|B/R и остальныеG|Индивидуальная фиксация лап/когтей, несущие фазы и переходы. Distance clock убирает ошибку темпа, но сам не создаёт качественный footplant|
|MOT-04|Полёт/плавание/специальные состояния|Довести видовые clips и взаимодействие с водой/дном. Наземный clock не применяется автоматически к Fly/Swim/Idle; не замедлять независимые взмахи вслепую|
|PERF-01|badger и весь каталог|Новый badgerBIN≈373kB против прежнего≈54kB; adaptive compression/cache/memory/load/CPU/GPU budgets. Длинные noisy benchmark sweeps отложены|
|PHY-01|Исторические17 bounds findings|Повторная строгая проверка visual/physics envelopes/contact. Список: badger,dirt_monster,dungeon_master,golem,land_guard,lobster,oerkki,orc,owl,penguin,puffin,sand_monster,seahorse,stone_monster,treeman,wasp,whale. Это старый диагностический список, не утверждение17 текущих crashes|
|FP-01|humanoid/skins/equipment|Камера, walls/framing, атаки и все presets; native arm-buffer/UV проверки и save smoke не заменяют полную игровую FP приёмку|
|UI-01|Control/UI-pack|Полная ручная матрица управления, паков интерфейса и пользовательских skin/loadout состояний|
|COMP-01|User/cooperative worlds|Расширенная совместимость пользовательских сохранений, recovery/missing resources и долгие игровые сессии; не только isolated entity smoke|
|PLAT-01|Android/package|Android build/device checks, clean distribution/installer и platform performance|
|CLEAN-01|Inactive legacy files|Отдельно разобрать неиспользуемый owl.orig.bin и прочие архивные хвосты перед публичной упаковкой; активные generated assets и provenance проверяются сейчас|
|TEST-01|Три старых Python fidelity checks|В полном прогоне46/49 PASS. Три находки повторены на recipe из backup до выпуска: arboreal stance window; lobster pincer hardcoded tip slice даёт пустой набор; warrior speed check предполагает один stride и не учитывает multi-rate Orc. Сохранены оба лога; отдельно обновить проверки и разобрать соответствующее движение, не объявлять весь suite зелёным|

Новые найденные шероховатости записываются в итоговый61-resource ledger с этим
ID либо отдельным примером. В рамках текущего выпуска исправляются только
ошибки, делающие сборку/ресурс неработоспособным, и явные новые регрессии.
