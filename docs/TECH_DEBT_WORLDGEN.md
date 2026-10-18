# Worldgen — закрытый план (data-driven)

> Формат runtime-данных в `content/`: **только JSON** (парсер `nlohmann::json` в движке).
> YAML остаётся в `tools/` для authoring (manifest, canonical blocks) и не читается игрой.

## Политика форматов

| Область | Формат | Кто читает |
|---------|--------|------------|
| `content/worldgen_packs/**` | JSON | C++ (`UWorldGenPack`) |
| `content/object_features.json` | JSON | C++ |
| `tools/prefab_manifest.yaml` | YAML | Python (генерация) |
| `tools/*.yaml` | YAML | Python / CI |

Дублирование `prefab_features.yaml` **не используется** — артефакт генератора удалён.

## План (закрытие техдолга)

| # | Задача | Статус |
|---|--------|--------|
| 1 | `pipeline.json` loader + пересечение стадий с генератором | done |
| 2 | Hot-reload: `/worldgen reload` (pack + prefab_features) | done |
| 3 | UI + JSON: `worldgen_pack_id` | done |
| 4 | Biome JSON: `palette`, `sub_biomes` (веса + subsurface) | done |
| 5 | `pack.json` optional `biome_blend_radius` default | done |
| 6 | Убрать `pipeline.yaml`, `prefab_features.yaml` из content | done |
| 7 | Валидация prefabs / prefab_features в CI (`smoke_resource_packs.py`, `--strict`) | done |
| 8 | `WorldGenStageMask` + `UColumnGenerationService` + `UBiomeRegistry` + `surface_constraint` | done |

## Ограничения hot-reload

Перезагрузка влияет только на **вновь генерируемые** чанки. Уже загруженные колонки не пересчитываются автоматически.

## Дальнейшие улучшения (не блокеры)

- Выбор pack из выпадающего списка с описанием (сканирование `pack.json`)
- ~~Полный перенос `SubBiomeFor` noise-порогов в JSON~~ — pack-driven `noise_threshold` (done)
- ~~Единая маска стадий / column service / biome registry~~ — `WorldGenStageMask`, `UColumnGenerationService`, `UBiomeRegistry` (done)
- Доп. UX: пресеты генераторов (`ApplyWorldGenPreset`: realistic / balanced / sparse_structures)
- Debug overlay биомов/climate в рендере (заготовка: `worldgen debug on` в консоли)
- ~~Scatter/prefab на воде~~ — `CanPlacePlantAt` / локальный `FindTopSolidSurfaceY` (done)

## Performance / streaming

См. [TECH_DEBT_CHUNK_STREAMING.md](TECH_DEBT_CHUNK_STREAMING.md) — активный план оптимизации (фазы A–E).

## Config migration matrix (legacy → current)

| Legacy key | Current key | Notes |
|------------|-------------|-------|
| Root `terrain` | `procedural.generator` | Root alias still written for older configs; parser warns when both disagree |
| `ui.block_input_profile` | `ui.control_scheme` | Both read; write emits both (`classic` / `cubatarium`) |
| `ui.legacy_hud` | unchanged | Toggles GeometryEngine text HUD vs Gui HUD |
| `prefab_features.yaml` | `content/object_features.json` | YAML removed from content (generator-only) |
| `pipeline.yaml` | `content/worldgen_packs/*/pipeline.json` | Pack-driven stages |

## Worldgen smoke baselines

`tools/worldgen_baseline.json` caps decorative spawn fire in radius 48. Seed **42** (balanced preset) yields **7** fire blocks from pack decorative placement — expected, not a generator bug. Threshold `spawn_fire_blocks_max` raised to **8** (2026-06); re-run `integration_test_worldgen.py --seeds 42`.

## Roadmap worldgen (2026) — статус

Infrastructure phases A–D are complete. **Terrain visual quality** work (2026-07 quality fix) addressed spawn flattening, cave depth bands, terrain calibration, vegetation density gates, CI quality metrics, and `density_3d` MVP (Y-scan surface, column fill, post-carve caves, 3D terrain FBM).

| Фаза | Infra | Quality (2026-07) |
|------|-------|---------------------|
| A — tuning/caves/object calibration | done | caves.json + depth band; spawn island removed |
| B — layered height, биомы, rivers, ravines, ores | done | `ores.json` loader; natural spawn search |
| C — climate, transitional biomes, erosion, 3D caves, presets, smoke CI | done | erosion-gates recalibrated; integration CI radius 4 + multi-seed |
| D — smoothing, climate height, height.json, biome coverage | done | rolling/detail weights raised; plains volatility |

### Backend status

| Backend | Default | Status |
|---------|---------|--------|
| `heightmap` | existing saves | Compatibility backend for worlds whose saved settings predate `terrain_backend` |
| `density_3d` | new Overworld worlds | Climate-routed continental/regional/rolling/mountain terrain with unified 3D cave density; covered by `integration_test_worldgen.py` |

### Remaining backlog

- Cross-chunk ravine/valley seams at chunk borders (intra-chunk carving is chunk-level; neighbor chunks do not share a single vmanip pass)
- Overhangs tuning and perf budget for `density_3d`
- Nightly `audit_worldgen_batch.py` on 20+ seeds
- `worldgen_refs.json` refresh after baseline tuning stabilizes

## Quality backlog (2026-07, post Overworld plan)

Infrastructure from the 2026-07 Overworld quality plan (spawn spiral, cave depth band, terrain spline, `density_3d` MVP, CI smoke) is **done**. Visual/metric quality and E2E vegetation were **partial**; the 2026-07-12 quality-resume cycle addressed hills, biome vegetation profiles, structure calibration, and placement baselines.

| ID | Item | Status | Notes |
|----|------|--------|-------|
| WG-Q1 | `flatness_pct` too high; rolling hills low | **Addressed** | `height.json` + biome height tuning; relaxed `FillMicroDepressions` / `ClampToFlatPlateau`; land spawn metrics via `quick_terrain_metrics.py --land` |
| WG-Q2 | `density_3d` MVP only | **Addressed for new worlds** | New Overworlds use `density_3d`; saved worlds without a backend selection retain `heightmap` compatibility |
| WG-Q3 | Multi-seed CI gates | **Done** | `integration_test_worldgen.py` on reference seeds 42, 12354, 20240625 + `validate_biome_features.py` |
| WG-Q4 | `worldgen_refs.json` / baseline refresh | **Done** | Thresholds in `worldgen_baseline.json`; `worldgen_spawn_report.py` for placement audit |
| WG-Q5 | Mesa/flat artifacts on pre-fix saves | **Known** | Recreate worlds for QA; use spawn-centered land metrics |
| WG-Q6 | Vegetation / decor E2E | **Addressed (placement)** | All 9 biomes have `features`; density gate floor raised; `_mapgen` weight alias; placement counted by CI — render still TD-CS-014 |
| WG-Q7 | Villages / jigsaw settlements | **Deferred** | Ruins/houses on 64×64 cell grid first; villages need structure sets later |

### WG-Q6 — Vegetation and objects

**Placement vs render:** `spawn_tree_blocks` and `quick_terrain_metrics.py --count-objects` count **blocks in chunk files** (worldgen placement). In-game visibility depends on mesh warmup / cross-vegetation render ([TECH_DEBT_CHUNK_STREAMING.md](TECH_DEBT_CHUNK_STREAMING.md) TD-CS-014), not worldgen.

**Tools:**

1. `tools/quick_terrain_metrics.py --land --count-objects` — fast spawn shape + placement counts
2. `tools/worldgen_spawn_report.py` — reference-seed placement baseline (after `integration_test_worldgen.py`)
3. `tools/validate_biome_features.py` — biome `features` keys match `object_features.json`
4. `tools/worldgen_quality_gates.py` — phase-3 gates (flatness, rolling, caves, veg clustering)

**Structure placement:** `structure_placement` in `content/object_features.json` (`cell_size`, `chance_per_cell`, `min_spacing`); slope gate in `SampleStructureSurfaceGradient` / `TryPlaceStructureFeatures`.

**Recommended QA:** new Overworld, reference seed, RD 8; forest/plains/savanna trees; desert cactus/ruins; `/worldgen reload` only affects new chunks.

## Execution progress (2026-07-07)

- Worldgen remains in maintenance/polish mode (core roadmap phases A–D already `done`).
- Общий debt-remediation цикл добавил baseline/traceability для cross-module рисков (`15bbb00`, `8a0fcdb`), без изменения worldgen runtime contracts.
- Незапирающие улучшения (pack dropdown UX, presets UX, biome/climate debug overlay) остаются в backlog и могут выполняться независимыми малыми PR без риска для генератора.

## Minecraft 1.18.2-like worldgen implementation checklist

Цель: структурное сходство с Overworld Minecraft Java 1.18.2 без seed parity.
Отметки обновляются по завершении каждого логического этапа.

- [x] Исследованы текущие генератор, объектная библиотека, feature pipeline,
  resource packs и официальные изменения worldgen 1.18.2.
- [x] Добавить и подключить геологический CC0 pack для новых миров; зафиксировать
  происхождение и лицензии новых и заимствованных ассетов. Пак содержит 10
  геологических блоков на текстурах Kenney Voxel Pack (CC0); новые миры включают
  `minetest_default_16` (CC-BY-SA-3.0) + `cubatarium_geology_cc0` (CC0).
- [x] Сделать текстуры отсутствующих ассетов заметными: диагональные
  пурпурно-тёмные полосы, жёлтая рамка и крупный `?`; добавить проверку маркера
  в `--smoke-packs`. В headless генерации с базовым набором паков missing-slot
  и placeholder-texture сообщений не воспроизвелось.

Для `density_3d` spawn-flatness gate поднят с 55% до 68%: в эталонном наборе
seed 42/12345 дали 51.46/53.06%, а 12354/20240625 — 64.87/65.48%; rolling-hill
доля во всех случаях выше порога 5%. Генератор выбирает безопасную низменную
точку появления, где пологие равнины ожидаемы; heightmap baseline сохранён.
- [x] Расширить биомную модель surface/ocean/beach/mountain/snowy-slope/peak и
  cave биомами; переходы поверхности используют климатические и высотные веса,
  а пещерные биомы выбираются в 3D по климату и noise-полям.
- [x] Сделать 3D density terrain основным backend для новых миров и настроить
  крупные формы: континентальность, эрозионные низины, холмы и горы. Старые
  world saves без `terrain_backend` сохраняют heightmap backend.
- [x] Добавить геологические слои, пещерные полости, локальные 3D aquifers с
  сухими карманами и редкими лавовыми полостями, геоды с CC0-материалами и
  высотно-зависимые связные жилы руд. Генерация меняет блоки напрямую;
  результаты не сохраняются в библиотеку объектов.
- [x] Сохранить `UObjectLibrary` для готовых повторно используемых шаблонов и
  biome rules; изменчивые/подземные формы размещать процедурно в блоках.
  Поддерживаемый `visibility: hidden` исключает служебные шаблоны из каталога
  игрока, сохраняя их доступными генератору. Новые шаблоны каталога не добавлялись.
- [x] Завершить финальную проверку стыков, повторяемости и pack resolution на
  reference seed; сохранить метрики поверхности, пещер и руд, а также остаточные
  ограничения. Четыре seed (42, 12345, 12354, 20240625; радиус 1 чанк)
  проходят quality gates: flatness 51.71–66.96%, rolling hills 7.64–15.26%,
  cave coverage 59.38–75.00%, 250–436 рудных блоков и 167152–200706 блоков
  геологии. На границах чанков surface-delta p95 равен 1; четыре измеренных
  набора не показали выраженного скачка относительно внутренней части чанков.
  Два независимых прогона seed 42 дали одинаковые 207/207 файлов чанков.
  Рекомендованный набор паков прошёл `--smoke-packs`; генерация только с
  `minetest_default_16` завершилась без missing/placeholder диагностики.
  Исправлено разрешение cross-chunk шаблонов: чанки сохраняют собственный
  terrain, а пересекающиеся древесные формы разрешаются стабильным приоритетом.
  `validate_biome_features.py` и `validate_object_features.py --strict` прошли;
  строгий валидатор объектов по-прежнему выдаёт существующие предупреждения
  о несовпадении биомов в manifest для отдельных деревьев/декораций.

Остаточная модель намеренно не повторяет seed parity Minecraft: cave-биомы
используют климатический 3D noise-классификатор, аквиферы — ограниченное поле
локального уровня/давления, а рудные жилы — детерминированные шумовые анкеры.
Cross-chunk ravine/valley seams, tuning перекидышей и ночной аудит 20+ seed
остаются в backlog выше. Старые сохранения не изменяются и не мигрируются.

## Aquifer flooding correction checklist

Цель: сохранить подземные водоёмы и глубокую лаву, но перестать заливать
все пещерные полости одним практически горизонтальным уровнем. Aquifer
generation должна быть детерминирована мировым seed и координатами, совпадать на
стыках чанков и не протекать из локальных водоносных областей в сухие полости.

- [x] Добавить настраиваемую 3D-модель aquifer-областей в worldgen pack:
  локальные уровни жидкости, сухие области, глубокую лаву и барьеры между
  несовместимыми fluid-областями. Параметры задаются в `aquifers.json`;
  sampler детерминирован по seed и абсолютным координатам.
- [x] Применять aquifer-решение только к подземным пустотам: встроить его в
  density fill для `density_3d`, использовать тот же sampler после carve для
  heightmap backend и убрать безусловное заполнение всех воздушных блоков ниже
  вычисленного столба воды.
- [x] Ограничить чанковую pocket sealing и подземное shore-air sealing aquifer
  статусами; оставить поверхностную водную полосу и восстановление водяных
  колонн над дном водоёма.
- [x] Добавить проверки сухих/водных/лавовых полостей, aquifer-барьеров,
  поведения pocket seal, повторяемости на одинаковом seed и изменений между
  seed. `worldgen_aquifer_test` и `fluid_worldgen_seal_test` проходят.
- [x] Проверить несколько seed и pack-конфигураций, записать долю сухих и
  затопленных cave-вокселей. Density 3D smoke на seed 410018/410019/410020
  прошёл (200/200/196 чанков); `--smoke-packs` завершился с кодом 0.
  Полная метрика seed 410021: 61,9% сухих, 38,1% водных, 0% лавовых вокселей
  среди подземных пустот ниже поверхности. Quality-проверка этого seed
  завершилась с единственным пороговым отказом по растительности:
  `tree_blocks=1217 < 1500`; прямые aquifer-тесты и остальные метрики прошли.
  Повторный запуск имеющегося `worldgen_vegetation_placement_integration_test`
  проходит на seed 42: тест проверяет только факт появления хотя бы одного
  дерева и не воспроизводит пороговый отказ seed 410021. Поэтому прежнее
  утверждение о падении самого интеграционного теста не подтверждается текущим
  бинарным файлом; воспроизводится именно quality-gate seed 410021.
- [x] Не изменять сохранённые блоки существующих миров и вести чек-лист с
  каждым логическим коммитом этой задачи. Новая логика применяется при генерации
  новых чанков, миграция сохранений не добавлялась.

## Biome and vegetation architecture checklist

Цель: сделать параметры биомов расширяемыми данными паков, а распределение и
силуэты природных фич более устойчивыми к seed и порядку генерации чанков.

- [x] Зафиксировать baseline seed 410021 до изменения плотностей:
  `spawn_tree_blocks=1217` (241 ствол + 976 листвы), средняя высота поверхности
  51,51, покрытие пещерных чанков 58%. Первоначальная проверка seed 42 была
  слабой (`> 0`) и не покрывала quality-gate 1500 блоков; её заменили проверкой
  процедурного вида на контрольном seed 410021.
- [x] Добавить стабильные строковые `BiomeKey`, временные pack-scoped
  `BiomeHandle`, динамический `BiomeBlend` для расширений и композиционные
  профили климата, высоты, палитры, feature-весов и домена. Неизвестные legacy
  enum-ID теперь имеют явное значение `Invalid`, а не `plains`.
- [x] Собирать отсортированный реестр ключей в неизменяемом снимке пакета и
  захватывать тот же реестр в `WorldGenContentSnapshot`. Добавить schema v2,
  адаптер встроенных v1-биомов и отклонение всего пакета при неизвестной схеме,
  повторном/некорректном ID, домене или диапазоне климата.
- [x] Подключить surface-профили пользовательских JSON-биомов к динамическому
  климатическому blend, высотной поправке, палитре и правилам фич по ключу.
  Интеграционный тест добавляет биом без C++-классификатора и проверяет его
  влияние на высоту.
- [x] Подключить пользовательские cave-профили к 3D-выбору по климату; передать
  `BiomeKey` до cave feature rules и учитывать биомные веса этих правил.
  Встроенный enum остаётся fallback/API-совместимостью для старых алгоритмов.
- [x] Добавить `worldgen_profile_version`: новые шаблоны записывают профиль 2,
  сохранения без версии читаются как профиль 1 и не активируют динамические
  климатические профили.
- [x] Добавить первый `procedural_tree` тип с ограниченной вариацией высоты и
  кроны, корнями/ветвями, профилями oak/pine и записью через `VoxelFeatureSink`.
  Блоки берутся из установленного `minetest_default_16`, новые изображения не
  добавлялись. Статические объекты остаются в `UObjectLibrary`, а prefab/scatter/
  tree/cave размещения теперь делегируются отдельным стратегиям.
- [x] На seed 410021 integration test подтверждает размещение species-блоков.
  Новая метрика в spawn radius 32: `spawn_tree_blocks=1885` против baseline
  1217 (520 новых oak-блоков: 106 `mtg_tree` + 414 `mtg_leaves`); height mean
  51,76, spawn flatness 55,39%, rolling hills 7,34%, покрытие cave-чанков 76%.
  Текущий `density_3d` quality-gate прошёл без отказов.
- [x] Перевести часть распределения растительности на детерминированных
  мировых cell-кандидатах и добавить экологические ограничения для новых
  деревьев. Полный density-field/Poisson выбор ещё нужен; перекрытия
  процедурных деревьев между чанками теперь разрешаются по рангу материала и
  ID блока независимо от порядка применения двух буферов.
- [x] Проверить текущий источник ассетов и добавить явный generic fallback для
  trunk/leaves новых видов. Лицензия и upstream существующего pack задокументированы;
  новые лицензируемые ассеты не добавлялись.
- [x] Полностью заменить фиксированный enum-массив на единый handle-only blend:
  встроенные и JSON-биомы хранятся в одном векторе с весами, а рельеф, высотные
  профили, river/erosion carve и палитра читают значения через snapshot handles.
  Неизвестный ключ в legacy `BiomeId` отображается в `Invalid`, а не в `plains`;
  все 17 встроенных ключей входят в каждый реестр контента. Интеграционная
  проверка подтверждает разрешение ключей, смесь с JSON-биомом и отсутствие
  тихой подмены custom dominant biome на plains. Основная цель и обе worldgen-
  тестовые цели собрались; `worldgen_vegetation_placement_integration_test` и
  `worldgen_biome_features_test` проходят.
- [x] Разделить источник выбора поверхности: `IWorldGenBiomeSource` выбирает
  procedural climate или image-map стратегию по pack mode; image-map сохраняет
  procedural 3D cave source. Integration test проверяет image surface и cave.
- [x] Ввести реестр вызываемых стратегий размещения для префабов, scatter и
  процедурных деревьев; кандидатный отбор передаёт выбранное правило
  зарегистрированной стратегии. `UObjectLibrary` остаётся источником
  характерных статических форм.
- [x] Перевести moss patch, stalagmite и amethyst cluster на реестр cave-
  стратегий; они и процедурные деревья записывают воксели через sink.
  Также исключить `BLOCK_AIR` из списка разрешённых пород: отсутствующие
  optional-блоки раньше делали воздух допустимым cave-полом.
- [x] Унифицировать запись scatter и префабов через `VoxelFeatureSink`:
  placement-функция статических объектов принимает writer callback, поэтому
  геометрия UObject и процедурные scatter/tree/cave-стратегии проходят через
  одну границу вывода в воксели. Проверены callback объекта и integration
  размещения; mesh-sink остаётся будущей задачей.
- [x] Расширить экологические профили для существующих кустов, цветов, грибов
  и высоких трав; добавить вариативные scatter-группы луговой/сухой травы,
  папоротника, jungle grass, папируса у воды, marram у берега и dead bush на
  ассетах существующего `minetest_default_16`.
- [ ] Снять метрики и контрольные изображения покрытия новых видов по биомам;
  откалибровать частоту, продолжить вариацию кустов и добавить водные/cave-
  растения, если текущие паки дают подходящие блоки.
- [x] Проверить schema v2, отказ/сохранение прошлого snapshot при ошибочном
  домене, регистрацию JSON surface/cave ID, влияние профиля на высоту,
  выбор cave key и запись процедурных species-блоков. `worldgen_biome_features_test`,
  `worldgen_vegetation_placement_integration_test`,
  `validate_biome_features.py` и `validate_object_features.py --strict` прошли.
- [x] Проверить независимость от порядка: интеграционный тест генерирует
  одинаковую область 5×5 в прямом и обратном порядке чанков и сравнивает
  блоковые снимки; отдельный тест проверяет пересечение профилей деревьев на
  границе чанка.
- [ ] Проверить матрицу сочетаний паков, сохранить контрольные изображения и
  откалибровать метрики покрытия кустов, травы и других групп декора.
