# R6: анатомическая посадка экипировки

Статус: **accepted sampled anatomical equipment fit**, полная R6 — **pending**.
Актуальный набор находится в `final/`, арт-наблюдения с SHA —
[visual_review.json](visual_review.json), листы — [index.md](index.md).
[Сравнение](comparison.png) показывает предыдущий affine fit и текущую
экипировку в одинаковой позе human/skeleton для трёх материалов.

## Что исправлено

Движение предмета и его статическая посадка разделены. Оба drawer используют
реальную cached world bone matrix B visual, включая общий clearance root.
Новый optional `GetBoneLocalBounds` измеряет actual cube vertices в rest-local
координатах anchor. Пустой anchor получает геометрию первого непустого родителя
через inverse(restAnchor) × restParent. Сами кости, pivots и позы не меняются.

В 18 bundled `wear.json` явно включён `fit_mode: bone_volume`. Шесть слотов
получают собственные области головы, torso, верхней части конечности,
ладони, верхних 70% ноги и нижних 30% ноги. Старый feet offset −.15 убран.
У обуви только 2 mm бокового запаса; rest sole совпадает с body sole.
Это не доказательство отсутствия проникновения на любом terrain/rotation.

Head gear привязан к `head`: у bot `hat` содержит тонкий козырёк, а не объём
головы. Подгонка к нему сплющивала корону. Существующая геометрия козырька
и прочие детали существа сохранены.

У исходных copper/iron inventory meshes один файл включает нагрудник и
наплечники. Размещение всего набора на каждом суставе уменьшало реальную
защиту, дублировало детали и давало плохую посадку. Один AABB fit также не
гарантирует, что округлая броня находится снаружи кубической анатомии.
Промежуточные component/projection варианты не получили финальную приёмку.

Для chest/arms/legs трёх материалов добавлены девять **отдельных worn-only**
моделей: тонкая полая оболочка, фаски, открытые концы конечностей и бортики
толщины. Каждый mesh имеет 64 triangles и 32 уникальные позиции. Собственные
атласы 128×64 содержат панели, швы и небольшой угловой мотив. Для узких рук
и ног рисунок авторится в 8×16 texels на грань и увеличивается nearest:
частые линии 32×32 давали пёстрые полосы при уменьшении. Вертикальная UV-ось
согласована на передних и боковых гранях. Это решение для rigid B humanoids,
а не замена анатомии или универсальная одежда для всех backend.

`wear.json.model` выбирает sibling `wear.gltf`; `fit_bounds` задаёт опорную
область оболочки [−1,1]. Исходные inventory/FP model.gltf, buffers, textures,
content definitions и параметры предметов сохранены. Поля optional:
сторонние packs с прежними explicit wear offsets сохраняют прежний режим.
Генерация: `python tools/research/build_creature_wear_variants.py --surface-cage`.
Без флага остаётся диагностический export source component, который не
прошёл художественную приёмку.

Для wood_sword/wood_torch добавлен отдельный `wield_grip.json`: сохраняется
authored origin, задаются ориентация и положение относительно нижней ладони.
Worn/world/preview используют общую политику; FP renderer этим файлом не
перенастраивается. Прямую FP-приёмку это не заменяет.

## Исправленный дефект общего кэша предметов

Реальный GPU probe воспроизвёл коллизию `ItemGltfTextureCache`: ключом была
папка, поэтому model.gltf и wear.gltf смешивали таблицы материалов.
У copper_chest исходный цвет [95,69,12,255] заменялся [68,52,44,255], и оба
запроса возвращали один texture ID. Ожидаемо неуспешный запуск и JSON:
`cache-regression/native_before/equipment/texture_model_isolation.json`.

Ключ теперь содержит normalized полный glTF path. В каждом final equipment
запуске probe обращается к обоим файлам всех девяти предметов в чередующемся
порядке, читает source RGBA из actual GPU texture через FBO, проверяет
различие texture IDs и стабильность повторных запросов. Независимый Python
audit сверяет RGBA с исходными material factors. Все девять проходят.
Это проверка изоляции материалов в одном процессе, не cold-cache/root reload
и не скриншот FP. Wrapper теперь отвергает отсутствующий/старый probe,
незавершённую manifest и отсутствующие PNG.

## Проверки и наблюдения

- Release EXE `d699089f4382a4001117ae82742e13949d561c0ee8563ac10f70a88ded597412`.
  Сборка выполнена требуемой командой `cmake --build build/desktop-msvc
  --config Release --target Cubatarium --parallel 8`; полёты выполняет
  `run_creature_pilot_qa.py` через `tools/flight_sim_run.py` на `bin/Cubatarium.exe`.
- На каждый материал: шесть ресурсов human/bot/zombie/skeleton и два human
  skins; Walk/Run × .22/1/1.5 скорости × 16 фаз плюс deepest-clearance фаза
  × четыре loadouts = **2448 core PNG** с actual anchor/bounds observations.
  Дополнительно **576 PNG**: восемь yaw в двух противоположных фазах бега
  и deepest-clearance фазе при Run1.5. Всего **9072 PNG** для трёх комплектов;
  shader alpha coverage не касается границ, anchor error0.
- Copper restart повторил **3024/3024 PNG** и pose/anchor manifest точно.
  GPU texture IDs сравниваются по смыслу и RGBA, не по номеру между процессами.
- Прямо просмотрены detail и combined eight-yaw sheets всех 18 пар
  resource/material, Run1.5 sheets human/skeleton трёх материалов, четыре
  main/off yaw-листа и три actual world zombie кадра. Copper observations
  перенесены на новый EXE только по exact parity всех3024 кадров;
  iron/leather просмотрены на текущем EXE. Не заявляется индивидуальный
  художественный просмотр всех9072 PNG.
- На просмотренных позах корона находится на голове, панели разделены по
  анатомическим областям, обе руки удерживают предметы за рукояти, боковые
  и задние поверхности читаются. Полая оболочка сохраняет исходные движения
  и не требует удаления body cubes. Узкие конечности skeleton сохранены.
- World fixture использует обычные inventory APIs одного actual Qa zombie:
  шесть соответствующих armor IDs, active wood_sword и offhand wood_torch.
  Три свежих `qa_equipment_inventory.json`, relocation src=3 и frame002
  сверены. Это не world-приёмка всех шести ресурсов и всех маршрутов.
- Полный стандартный capture61: **5288/5290 RGBA PNG** побайтно прежние,
  включая все3660 body matrix PNG. Два ожидаемых изменения — human31/32 с
  экипировкой. `final/body_parity.json` явно фиксирует разные EXE и одинаковые
  shaders; никакие изменения body/skeleton/creature PNG/animations не приняты
  за «разрешённые исключения».
- `cage_geometry_audit.json`: все actual triangles отделены общей плоскостью
  от declared cage; shell edges двухмногообразны, oriented volume положителен.
  Это локальное геометрическое доказательство, не общий collision solver.
- `wear_repro.json`: повторная генерация девяти моделей для ношения даёт36 точных
  файлов в текущем Python/Pillow. `khronos/summary.json`: девять worn glTF
  без errors/warnings/infos; `creature-khronos-current/summary.json`: G35 тоже0.
- `ledger.json`: все61 exact R5 reviews/source contexts/file records/
  published+bin payload matches актуальны; manifest56+5, runtime global mirror
  точен. 11 выбранных Release C++ tests проходят, включая equipment fit.
  Две неактивные bin-only owl .orig копии остаются известными исключениями
  прошлого этапа; обычная Release сборка не объявляется чистым package.

## Практики и границы применения

[Epic Skeletal Mesh Sockets](https://dev.epicgames.com/documentation/unreal-engine/skeletal-mesh-sockets-in-unreal-engine)
описывает отдельный attachment frame относительно кости, включая mesh-specific
sockets. Применение здесь: не считать pivot геометрическим центром одежды,
не компенсировать ошибочную посадку изменением skeleton.
[Blender Shrinkwrap](https://docs.blender.org/manual/en/2.80/modeling/modifiers/deform/shrinkwrap.html)
описывает внешний surface offset. Это основание отдельно контролировать
внешнюю посадку; наш полый shell generator — собственное решение, не вызов
Blender и не рекомендация проецировать любую сложную броню на box.
Опыт промежуточных вариантов добавляет обязательные проверки внутренней
полости, боковых/задних видов и частоты рисунка при уменьшении.

Остаются обязательными: остальные состояния/переходы/terrain и all applicable
world equipment, прямые FP кадры, старый save → новый save → reload,
appearance resolved keys/skin ownership/overlays, live cache/root/pack reload,
маршруты и habitats всех backend, culling, clean package и matched CPU/GPU/
memory measurements. Screenshot FPS при выключенных performance gates не
является доказательством бюджета. Полный оставшийся список — в [R6 README](../README.md).

Каталоги `components/`, `cage/`, `textured/`, `shell/` и первичные material
папки — исторические диагностические этапы. Их успешный экспорт/числовые
проверки не подменяют текущий [visual_review.json](visual_review.json).
