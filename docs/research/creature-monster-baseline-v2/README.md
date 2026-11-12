# Baseline монстров — исследование перед R5

Четыре ракурса всех 13 оставшихся на начало этой волны исходных монстров,
revision b35f00a75. Листы `baseline_1/2/3.png` используют одинаковую шкалу
между ракурсами каждого вида, но масштаб между разными видами меняется;
не использовать их как сравнение роста разных существ.

`baseline_audit.json` содержит размеры в блоках, действительные weighted
joints отдельно от palette joints, исходные клипы и runtime definitions.
Это reference-only данные, без копирования сеток/пикселей в production.
Исходные sand/stone/dirt дополнительно сняты в текущем Release через
`tools/flight_sim_run.py`: `native_baseline`, `context_elemental_baseline.json`.
Чёрные грани sand воспроизводятся и в native renderer; это не основание
повторять дефект в собственных UV/текстурах.

Oerkki/orc/ogre также сняты в текущем Release: `native_baseline`,
`context_warriors_baseline.json`. Почти чёрный oerkki подтверждён нативно;
собственный вариант сохраняет тёмную анатомию с читаемыми плоскостями лица.

Эти листы не являются приёмкой нынешнего production-вида монстров.
Принятая новая sand/stone/dirt тройка и её доказательства находятся в
`../creature-elemental-bipeds-v2`; принятая тройка oerkki/orc/ogre —
в `../creature-warriors-v2`. Dungeon_master/land_guard также проверены
нативно (`context_guardians_baseline.json`) и переработаны:
`../creature-guardians-v2`. Остальные пять видов ещё требуют R5.


### Дополнение: исходные tree_monster / treeman / golem

`arboreal_baseline` — отклонённый диагностический запуск: native preview
был доступен, но фактический treeman не появился в мире. Preview не доказывает
спавн. После исправления QA-выбора сухого места весь объём тела проверяется
через штатный CanSpawnCreatureAt; исходные gameplay bounds не уменьшаются.
`arboreal_clear_land_baseline` подтверждает настоящий спавн всех трёх
исходников в текущем Release. Их native_baseline ракурсы прямо просмотрены.
Поиск земли относится к QA, а не меняет штатный gameplay spawn. Полные
маршруты и clearance при смене yaw остаются R6.


### Дополнение: последние mese_monster / lava_flan

`crystal_lava_baseline` — диагностический запуск, отклонённый как world
acceptance: mese_monster появился, lava_flan на сухом прибрежном участке
закономерно не прошёл habitat check. Полученные native previews обеих
исходных моделей доступны и прямо просмотрены (включая стороны); они
доказывают работу preview, но не существование lava actor в мире.
У mese_monster нужно сохранить низкий крылатый объём, разнесённые трёхзвенные
крылья, переднюю челюсть и многозвенный хвост с парой сходящихся захватов,
включая исходный die. Гуманоидный рецепт этому исходнику не соответствует.
Lava flan — вертикальный слоистый горячий столб с глазами и открывающейся
пастью, а не плоский blob. До world acceptance требуется корректный лавовый
участок и фактический спавн без обхода штатных habitat/collision правил.


### Успешный лавовый baseline

`lava_habitat_baseline` отклонён: скопированный world_data.json ещё называл
мир World_254, и защита fixture сработала до изменения блоков. Копии
присвоено отдельное world_name CreatureQaLava_254. `lava_fixture_baseline`
после этого прошёл: исходный lava_flan действительно появился, свежий dump
фиксирует src=3; world/native кадры прямо просмотрены. В дальнейшем для
нового кандидата бассейн уточнён до source-fed flowing levels и face camera:
финальная приёмка находится в creature-crystal-lava-v2. Полная обычная
lava-навигация не считается проверенной этим небольшим fixture.
