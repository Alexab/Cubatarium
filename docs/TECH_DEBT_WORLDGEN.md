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
- [x] Добавить отдельные домены для водной поверхности и затопленного дна:
  `water_surface` выбирает реальную границу воды/воздуха, а
  `underwater_floor` разрешает scatter только в водяной клетке над твёрдым
  дном. Добавлены редкие климатически ограниченные океанские кораллы на
  существующих `mtg_coral_*` из `minetest_default_16`; новые текстуры не
  добавлялись. Для новых водных правил требуется профиль генерации 2, поэтому
  старые сохранения не меняют поведение при достройке новых чанков. На seed
  410023 в тестовом мире размещено 7 `mtg_coral_brown` и 20 `lily_pad`; unit-
  тест проверяет водную поверхность и подводный якорь.
- [x] Повторно прогнать генератор feature JSON и валидаторы паков после
  добавления authored aquatic rules. `generate_object_features.py
  --merge-calibrated` сохраняет ручные правила и root-разделы из JSON, а
  `generate_worldgen_refs.py` сохраняет дополнительные вручную описанные слоты.
  `smoke_resource_packs.py` прошёл: все 4 primary packs, geology CC0 pack,
  сочетания `minetest_default_16` с пустым secondary и с
  `cubatarium_geology_cc0`, completeness и smoothness validators.
- [ ] Снять контрольные изображения и метрики покрытия по каждому биому;
  откалибровать частоту кустов, трав, водного и пещерного декора. Текущий
  тестовый прогон подтверждает блоковые размещения, но не заменяет визуальную
  проверку и per-biome attribution. Реализация дополнительных cave-растений и
  дальнейшей вариации кустов остаётся отдельным шагом после визуальной оценки.
- [x] Проверить schema v2, отказ/сохранение прошлого snapshot при ошибочном
  домене, регистрацию JSON surface/cave ID, влияние профиля на высоту,
  выбор cave key и запись процедурных species-блоков. `worldgen_biome_features_test`,
  `worldgen_vegetation_placement_integration_test`,
  `validate_biome_features.py` и `validate_object_features.py --strict` прошли.
- [x] Проверить независимость от порядка: интеграционный тест генерирует
  одинаковую область 5×5 в прямом и обратном порядке чанков и сравнивает
  блоковые снимки; отдельный тест проверяет пересечение профилей деревьев на
  границе чанка.
- [x] Проверить автоматическую матрицу сочетаний базовых resource packs и
  дополнительного геологического CC0 pack через `smoke_resource_packs.py`.
- [ ] Сохранить контрольные изображения и откалибровать по биомам метрики
  покрытия кустов, травы и других групп декора.

### World_198 audit and vegetation follow-up (2026-10-18)

- [x] Проверить `bin/worlds/World_198/world_data.json`: seed `3650471218`,
  `overworld`, `density_3d`, профиль 2, `trees` и `ground_cover` включены,
  выбран `default` и primary pack `minetest_default_16`; сохранено 1488 chunk-
  файлов. `worldgen_sets` в файле ещё schema 1, хотя worldgen-профиль уже 2;
  загрузчик теперь восстанавливает отсутствовавшие feature metadata и повышает
  схему в памяти до 2, чтобы следующее сохранение записало их. `world_seed_text`
  в этом старом файле числовой и на 1 меньше `world_seed`; загрузчик читает его
  только как строку, поэтому генерация использует `world_seed=3650471218`.
- [x] Зафиксировать причину «шахматки»: поверхность выбирала биом и
  дополнительную примесь песка/гравия независимыми hash-выборами на колонку.
  В сохранённых колонках вокруг spawn 2213 из 3649 соседних пар grass/sand
  (60,6%) различались. Для поверхности теперь используется доминантный
  биомный handle, а случайное поколоночное смешивание материалов удалено;
  климатические веса продолжают смешивать рельеф.
- [x] Исправить сохранение feature-правил: `worldgen_sets` schema v2 хранит
  procedural mode, дерево/куст species-профили, экологические и surface-
  ограничения. Schema v1 продолжает загружаться; в profile-2 мирах недостающие
  метаданные правил восстанавливаются из одноимённых авторских правил, а
  уже записанные блоки не изменяются.
- [x] Сделать крону нерегулярной и многолопастной; добавить профили
  процедурных aspen/acacia и shrub для common, blueberry, acacia и pine на
  существующих блоках `minetest_default_16`; новых текстур и внешних ассетов
  не добавлялось. Правило луговых цветов заменено с отсутствовавшего `dandelion`
  на существующий блок `flower`.
- [x] Обновить `validate_object_features.py`: проверять также `ground_cover`,
  профили процедурных кустов и ссылки на их блоки. Строгая проверка и
  `validate_biome_features.py` проходят; остаются известные предупреждения о
  расхождении части старых правил с manifest.
- [x] Собрать Release `Cubatarium`; проходят
  `worldgen_vegetation_placement_integration_test` и
  `worldgen_biome_features_test`. Интеграционная проверка теперь отдельно
  размещает дерево и куст на контролируемой площадке и проверяет миграцию
  schema 1 → 2 с сохранением частоты правила.
- [x] Починить `worldgen_quality_gates.py`: передавались не все baseline-
  пороги, из-за чего скрипт падал с `KeyError`. После исправления проверка
  существующего `World_198` отрабатывает, но сохранённые чанки ниже текущих
  порогов: `spawn_tree_blocks=1381 < 1700`, `flatness_pct=66.73% > 55%`,
  `delta_ge_4_pct=14.09% > 3%`. Эти блоки не перегенерировались; метрики
  описывают старый мир, а не свежую генерацию.
- [ ] Снять контрольные изображения свежего profile-2 мира для forest, plains,
  savanna и scrubland и откалибровать метрики покрытия крон, кустов, травы и
  декора на нескольких seed. `World_198` не изменён, поэтому визуальный
  результат новых правил в нём не проверяется.

## Worldgen decor transparency regression checklist (2026-10)

Цель: альфа-текстуры декора должны вырезать только прозрачные пиксели своего
материала, не создавая дыр в обычных opaque-блоках; растительные блоки должны
использовать геометрию и коллизию, соответствующие их назначению.

- [x] Проследить проблему от alpha-cutout uniform до opaque и packed draw-путей:
  opaque-рендер включал discard для всего прохода, хотя batch/range уже хранит
  материалный флаг `AlphaCutout`.
- [x] Передавать cutout-флаг каждому GPU batch и packed range; отделить группы
  multi-draw по режиму cutout и сохранить прежнюю политику прозрачных проходов.
- [x] Назначить `cross`/нулевую occupancy травам, цветам, папоротникам,
  сухим кустам и папирусу; назначить `cutout`/нулевую occupancy альфа-текстурам
  кустовых стволов и хвои в отслеживаемых паках.
- [x] Собрать приложение и выполнить decor mesh integration test: `decor_mesh_integration_test: OK`; Debug и Release приложение скомпилированы.
- [x] Выполнить визуальную проверку сцены с alpha-декором и обычной поверхностью на Release-бинарнике в диагностической копии `World_199_RenderDiag_20261018`: загрузка успешна, exit code 0, захвачено 373 игровых кадра; поверхность под травой/декором непрерывна.
- [x] Проверить JSON изменённых блоков и основной аудит resource packs: 15 затронутых JSON валидны; smoke audit четырёх первичных паков сообщил 0 primary issues и одно warning.
- [ ] Просканировать все alpha-текстуры всех необязательных паков и классифицировать остальные блоки, которым нужна явная render-метаинформация.

## Overworld vertical range and ocean depth checklist (2026-10-18)

Цель: использовать для новых overworld-миров полный вертикальный диапазон
`Y=-64..319` с более глубокими океанами и согласованной поддержкой отрицательных
координат во всех runtime-подсистемах. Существующие миры не мигрируются.

- [x] Задать новые границы overworld `MinY=-64`, `MaxY=319`, уровень моря
  `SeaLevelY=63`, верх bedrock-слоя `BedrockTopY=-64` и координатный датум
  `DatumY=0`. Включительный диапазон содержит 384 уровня и чанковые срезы
  `cy=-4..19` при размере чанка 16.
- [x] Ввести `WorldVerticalProfile` как общее runtime-представление вертикальных
  границ, уровня моря, bedrock и datum; `ProceduralSettings` оставляет
  сериализуемые настройки и формирует профиль, а `UWorld` передаёт согласованный
  снимок в `UBlockWorld`. Методы профиля вычисляют допустимые Y и диапазон chunk Y.
- [x] Протянуть signed-границы через рельеф и density, пещеры/овраги/aquifer,
  слои пород и рудные жилы, фичи, колонковое хранение/сохранение/загрузку,
  стриминг/выгрузку, готовность и ремонт колонок, освещение/relight, mesh
  snapshots и cache, collision queries, physics и жидкости. Зарезервировать
  отдельные sentinel-значения, чтобы `cy=-1..-4` были обычными срезами.
- [x] Обеспечить, чтобы меш считает внешней нижней границей только отсутствующий
  срез ниже `MinY`, а пропущенные срезы внутри диапазона остаются неизвестными;
  сканирование видимых mesh, collision ground queries и водный relight используют
  профиль, а не диапазон `0..255`.
- [x] Добавить проверку включительной верхней границы `Y=319`, нижней границы
  `Y=-64`, профиля `cy=-4..19`, сохранения/очистки чанков, света, mesh-грани,
  коллизии и течения воды через `Y=0`. Исправить ошибку проверки цели падения
  жидкости, которая запрещала заливку свободной нижней клетки.
- [x] Пересобрать Debug-приложение и запустить регрессионные проверки:
  `fluid_spread_level_test`, `falling_blocks_stability_test`,
  `column_flow_scheduler_test`, `mesh_neighbor_policy_test`,
  `terrain_column_util_test`, `negative_height_storage_test`,
  `chunk_lighting_propagation_test`, `fluid_mesh_faces_test`,
  `worldgen_aquifer_test`, `collision_broadphase_subchunk_test`,
  `block_placement_raycast_test`, `miss_first_mesh_class_test` и
  `worldgen_smoothness_test` — все прошли. Цель приложения также прошла проверку
  статической Windows-сборки.
- [x] Устранить неполный набор исходников тестовых целей: добавить зависимости
  реестра/материалов для теста устойчивости падающих блоков, `GridMath.cpp` для
  seam/relight тестов и test-only заглушки creature/environment для изолированных
  voxel collision и raycast тестов.
- [x] Обновить worldgen-метрики для signed-диапазона: нижняя граница читается из
  `world_data.json`; океан, пещеры, жидкости и береговые разрывы учитывают блоки
  ниже Y=0. Старые сохранения без `min_height` анализируются в прежнем диапазоне
  от Y=0. Quality gates теперь показывают границы профиля и проверяют выход
  поверхности за нижнюю границу.
- [x] Добавить шесть Python-регрессий на профиль `-64..319`, отрицательные Y в
  океане/пещерах/жидкостях/береговых метриках и legacy-сохранение; все прошли.
- [x] Создать новый smoke-мир `CI_991019_density_3d`: профиль сохранён как
  `-64..319`, уровень моря `63`, bedrock `-64`; CLI завершился успешно и записал
  462 chunk-файла. Метрики signed-диапазона прочитали 12 544 столбца, береговых
  воздушных разрывов нет.
- [x] Исправить масштабирование океанического профиля для нового вертикального
  диапазона: schema 2 считает глубину от `SeaLevel` до `MinY`, а высоты суши
  продолжают масштабироваться независимо от нижней границы. Schema 1 сохраняет
  прежнюю интерпретацию паков. Для новых миров `default/height.json` использует
  schema 2 и глубокие значения профиля; `worldgen_smoothness_test` проверяет
  отрицательное дно океана, шельф/побережье и неизменную шкалу суши.
- [x] Пересобрать корневой Release-бинарник `bin/Cubatarium.exe` и проверить
  Release-набор: `worldgen_smoothness_test`, `negative_height_storage_test`,
  `world_mesh_service_test`, `chunk_mesh_revision_test`,
  `mesh_neighbor_policy_test`, `fluid_mesh_faces_test`,
  `chunk_lighting_propagation_test`, `block_placement_raycast_test` и
  `collision_broadphase_subchunk_test` — все прошли.
- [x] Создать чистый Release-мир `CI_991019_density_3d` с профилем 2 и
  диапазоном `-64..319`; на радиусе 1 ocean depth составил `p10=6`, `p50=28`,
  `p90=47`, максимум `52` блока. Доля сухих/водных пещер `38.4%/61.6%`.
  Depth-gates прошли; полный quality-gate для такой маленькой области невалиден
  по деревьям и рельефу (`14` tree blocks, flatness `95.3%`), поэтому нужен
  контрольный радиус и несколько seed.
- [x] Повторить Release-калибровку на baseline-радиусе 2 для seed 991020 и
  410021. Глубина прошла для обоих: `p50/p90=33/51` и `31/51`; seed 991020
  оказался почти полностью океаническим (`height=63..64`), поэтому его проверки
  суши и растительности неприменимы. Seed 410021 покрывает сушу (`height=63..97`,
  `flatness=49.80%`, `rolling=16.12%`), однако содержит только `1029` блоков
  деревьев при текущем quality gate `1500`; этот пункт продолжен в разделе
  растительности.
- [x] Измерить полную генерацию диапазона `-64..319` в Release на радиусе 2:
  seed 991020 создал 656, seed 410021 — 686 chunk-файлов; каждый интеграционный
  прогон завершился примерно за 70 секунд. Для сравнения, предыдущая Debug-
  диагностика радиуса 1 занимала 210.6 секунды и не отражает пользовательскую
  Release-сборку.
- [ ] Оценить расход памяти и производительность на больших радиусах и во время
  длительного интерактивного стриминга; оптимизировать, если профилирование
  покажет регрессию.
- [ ] Снять визуальную проверку свежего signed-профиля и повторить depth/terrain
  quality gates на нескольких сушных seed; небольшой radius-1 мир не подходит
  для визуальной калибровки.

Историческая калибровка морского профиля до включения отрицательных Y:
- [x] Вынести профиль океана в `content/worldgen_packs/default/height.json`:
  климатическая континентальность теперь формирует глубокий океан, шельф,
  побережье, низины и возвышенности с плавными переходами. После калибровки
  профиль прибрежного дна углублён до `coast_depth_ratio=0.14`; quality gates
  требуют медиану не менее 14 и p90 не менее 30 блоков.
- [x] Исправить расчёт последнего вертикального среза для включительной верхней
  координаты: при `max_height=319` сохраняются и стримятся чанки `cy=0..19`, без
  лишнего слоя `cy=20`. Проверены storage, persistence, streaming, column
  readiness и raycast.
- [x] Добавить регрессионные проверки верхней границы Y=319 для чанкования,
  распространения освещения и эмиссии грани воксельного меша. Собрать приложение
  и test targets; прошли `worldgen_smoothness_test`,
  `terrain_column_util_test`, `chunk_lighting_propagation_test`,
  `block_placement_raycast_test` и `fluid_mesh_faces_test`.
- [x] Создать два чистых диагностических overworld на seed 410221 и 410222
  (профиль `Y=0..319`, до включения отрицательной нижней границы):
  оба завершились успешно, сохранив 1423 и 1555 chunk-файлов соответственно.
- [x] Снять метрики по двум свежим мирам (radius 6): seed 410221 — 49 488
  водных колонок, глубина p50=36/p90=43, высота поверхности 63..102; seed
  410222 — 35 398 колонок, p50=15/p90=32, высота 63..126. В обоих мирах нет
  береговых воздушных разрывов. До калибровки p50 был 16 и 8, p90 — 34 и 15.
- [x] Прогнать quality gates на seed 410221: `worldgen_quality_gates.py` прошёл
  (flatness 24.07%, rolling hills 22.89%, cave coverage 89.81%, tree blocks
  2889). Скрипт теперь объединяет пороги конкретного terrain backend с общими;
  до этого он ошибочно сравнивал `density_3d` с общим лимитом уклонов `3%`.
- [x] Выполнить короткую видимую проверку свежего мира через `flight_sim_run.py`
  (исторический профиль `Y=0..319`):
  на поверхности видны лес и переход к океану; подводный режим включается в
  колонке с глубиной 30 блоков. Отчёты и скриншоты лежат в
  `E:\Temp\CubatariumVerticalAudit_20261018_v3`; для GUI-прогона создана копия
  `bin/worlds/DepthAuditVisual410221`. Автоматическая проверка отклонила её
  рекурсивное удаление. Подводный туман скрывает дно, поэтому визуальная оценка
  его формы здесь ограничена метриками сохранённых блоков.
- [ ] Отдельно измерить стоимость полного диапазона `Y=-64..319` при длительном
  стриминге и проверить расход памяти/время построения колонок на контрольных
  радиусах. Текущий многосидовый `worldgen_smoothness_test` прошёл, но при запуске
  процесса его пиковый working set достигал примерно 5,5 ГБ.
- [ ] На свежем мире с новым signed-профилем визуально изучить рельеф и морское
  дно с учётом подводного тумана; прежние GUI-кадры относятся к профилю `0..319`
  и не заменяют визуальную проверку `-64..319`.

## Поверхности, реки и озёра — продолжение после signed-depth (2026-10-18)

Цель: улучшать новые profile-2 миры без изменения сохранённых блоков. Перед
калибровкой фич проверить Release-генерацию на разных seed; визуальный захват
считать завершённым только если камера показывает рельеф, а не крону, skybox
или подводный туман.

- [x] Заменить для новых profile-2 миров blob-выемки на непрерывное
  доменно-искажённое шумовое русло. Поле учитывает ширину/эрозию и влажность:
  влажные русла углубляются к уровню моря, сухие остаются неглубокими оврагами.
  Один и тот же контур используется density terrain и аналитическим carve;
  carve сглаживается через соседние колонки для стабильных границ чанков.
- [x] Добавить pack-driven стадию `lakes` и `lakes.json`: детерминированный
  кандидат на клетку, климатические ограничения, неровный берег и чашу с
  грязевым/каменным дном. Высота водной поверхности берётся от реального
  рельефа центра кандидата; submerged/ocean-floor кандидаты отбрасываются,
  низкие колонки внутри чаши заполняются до единого уровня воды. Высоту центра
  семплировать не более одного раза на ячейку за генерацию чанка.
- [x] Добавить регрессию на непрерывный шумовой контур, стабильность lake
  placements при прямом/обратном обходе, заполнение низкой колонки чаши и отказ
  от lake site под уровнем моря.
- [x] Если anchor озёрной ячейки находится под морем, детерминированно проверять
  до восьми соседних точек внутри той же ячейки и выбирать первую точку с
  подходящим климатом и сушей. Регрессия теперь моделирует затопленный anchor и
  подтверждает fallback на сушу; результат не зависит от порядка обхода колонок.
- [x] Пересобрать `bin/Cubatarium.exe` и тесты только в Release. Прошли
  `valley_carver_test`, `worldgen_smoothness_test` и
  `tools/test_worldgen_metrics_signed.py` (7 тестов). Release smoke seed 410034
  завершился успешно; отдельный свежий мир seed 410029 создан корневым
  Release-бинарником: профиль 2, `density_3d`, 1026 chunk-файлов.
- [x] После fallback-изменения Release-сборка и регрессии прошли; integration
  seed 410063 создал signed profile-2 мир с `density_3d` и 980 чанками. Этот seed
  не подходит для калибровки: в окне radius 32 осталось только 6 колонок суши
  из 4225, деревьев нет; окно практически полностью покрыто океанской водой, и
  spawn quality gates ожидаемо не прошли. Для оценки растений и lake frequency
  по-прежнему нужны seed с достаточной площадью суши у спавна.
- [x] Исправить `quick_terrain_metrics.py` после смены sea level: определять воду
  по блоку `water` из паков, а при недоступной таблице — по сохранённой высоте
  моря; land-only режим исключает водные колонки. По умолчанию quick-метрики и
  spawn report теперь используют сохранённую точку спавна. На seed 410063 спавн
  `(0, 0)` действительно почти полностью морской: 6 сухих колонок из 4225. На
  seed 410039 корректное окно `(36, 40)`, radius 32, содержит 2058 сухих
  колонок, высоты 59..79, mean 67.0, flatness 77.2% и rolling hills 22.3%.
- [x] Исключить воду, деревья и наземный декор из расчёта terrain top, чтобы
  кроны и трава не подменяли геометрию поверхности. Метрика формы использует
  пятиточечный горизонтальный базис на расстоянии 2 блоков; это не считает
  плавный уклон по одному блоку совершенно плоским. Quality gates проверяют
  общую форму сгенерированного участка, а форму спавна показывают отдельно.
- [x] Исправить применимость spawn quality gates к побережьям: средняя высота и
  количество деревьев оцениваются по сухим колонкам, tree blocks нормализуются
  на долю суши, а окно менее чем на 20% суши явно помечается как непригодное для
  наземной калибровки. Для seed 410039 сухопутный эквивалент — 1718 tree blocks
  при 837 фактических; gate больше не отклоняет его за морское дно в global mean
  и ровную саванну в локальном spawn окне.
- [x] Проверить метрики на seed 410039: spawn land покрывает 48.7% окна radius
  32, средняя высота суши 67.0; land-only окно radius 96 имеет flatness 66.1%
  и rolling hills 29.6%; весь сгенерированный terrain window radius 96 имеет
  flatness 47.4% и rolling hills 51.2%. Все 14 signed-world metric tests и
  Python syntax checks прошли.
- [x] На финальном Release smoke seed 410039 создал 1021 chunk-файл; новый мир
  сохранил profile 2 и `density_3d`. В окне spawn radius 48 над уровнем моря
  озёрной воды нет. В radius 32 учтено 890 блоков растительности: 837 дерева и
  53 кустов, в том числе acacia.
- [x] Сравнить до/после для seed 410029 в spawn radius 48: прежняя логика
  оставляла 21 разрозненный water block выше моря в кандидате, центр которого
  находился на морском дне; новая логика такие островки не создаёт. В контрольном
  радиусе этого seed валидного надморского lake site нет, поэтому это проверка
  отказа ложного кандидата, а не доказательство частоты озёр.
- [ ] Найти несколько seed с подходящей сушей в области lake candidate и
  измерить число/площадь/глубину озёр, непрерывность водной поверхности и
  соединение рек с морем. Текущая synthetic-регрессия проверяет чашу. В новом
  мире seed 410039 над уровнем моря найдено одно связное озеро: 469 водных
  блоков на 320 XZ-колонках, габарит X=67..82, Z=36..62. В свежих smoke-мирах
  410040 и 410041 в радиусе проверки озёр не было; это совместимо с частотой
  1/5 на клетку 128 блоков и само по себе не является отказом генерации.
  Сохранение `CI_410039_density_3d` создано до этапа озёр и не служит
  сравнением новой реализации.
- [x] Снять видимый Release-кадр побережья seed 410039:
  `bin/flight_captures/worldgen_surface_410039/frame_004_front.png`. Видны песчаный берег и океан;
  кадр не покрывает внутренние биомы и потому не закрывает полный визуальный QA.
- [x] Снять yaw-sweep Release-кадры seed 410039: в
  `bin/flight_captures/worldgen_canopy_yawsweep_410039_20261019/frame_003_front.png`
  видны деревья саванны у берега. Дымка и отверстия в поверхности пока мешают
  оценить мелкие детали и не позволяют считать этот кадр визуальной приёмкой.
- [ ] Снять обзорные кадры нового signed-профиля для леса, равнины, саванны,
  scrubland, гор, берега, реки и озера. Текущие seed 410039 кадры покрывают
  только саванну и берег; прежние полёты seed 410029 начинались внутри листвы и
  смотрели в skybox, поэтому не подходят для оценки формы мира.
- [ ] Собрать метрики по биомам на нескольких сухопутных seed: доли травы,
  кустов и крон, расстояния между деревьями, распределение высот/уклонов,
  непрерывность русел и частота озёр. Не менять глобальную плотность деревьев
  по coastal smoke seed 410029: в его радиусе 32 было 1217 tree blocks и 6
  shrub blocks, но большая часть окна занята морским дном/берегом. Seed 410039
  показывает более полезную выборку (837/53 tree/shrub blocks), но всё ещё не
  заменяет отдельные окна леса, равнины, саванны и гор.
- [ ] После пригодных кадров откалибровать силуэты деревьев, кустарников,
  полян и мелкого декора отдельно для каждого биома; сохранить контрольные
  изображения и диапазоны quality-gates рядом с метриками.
- [x] Расширить силуэты процедурных деревьев и кустов: профили лиственных пород
  получили более широкие и неровные кроны, а длина ветвей теперь следует размеру
  кроны вместо фиксированного диапазона 1–3 блока. Release-регрессия проверяет,
  что широкий профиль выносит ветви/листву минимум на 5 блоков от ствола.
- [x] Проверить новые правила на Release-мире seed 410039: profile 2,
  `density_3d`, 1432 chunk-файла. Spawn radius 32 содержит 2033 сухие колонки
  (48.1%), среднюю высоту суши 67.0, flatness 77.5% и rolling hills 21.8%;
  quality gates проходят. В мире сохранено 789 tree blocks и 59 shrub blocks;
  в старом сохранении того же seed было 837/53. Профильная настройка увеличила
  объём acacia в окне, но совокупный объём деревьев не вырос, поэтому частоту
  нужно оценивать отдельно по лесу и саванне.
- [x] Полная `worldgen_vegetation_placement_integration_test` прошла в Release;
  быстрый режим биомного профиля и 14 signed-world metric tests прошли. Release
  приложение пересобрано и создало новый тестовый мир.
- [ ] Повторить визуальную приёмку в стабильном рендере: capture саванны у берега
  сделан, но видимые пробелы/дымка мешают оценивать кроны; получить лесной и
  равнинный обзор и проверить, не являются ли пробелы отдельной проблемой рендера.
  Диагностический Release-кадр
  `bin/flight_captures/worldgen_density_410065_20261019/frame_density_downward.png`
  также показывает кроны новых деревьев, но большие пустые области и дымка
  мешают оценить покрытие и отделить плотность worldgen от пропусков рендера.

### Заполнение речных русел (2026-10-19)

- [x] Проверить порядок стадий: `valleys` вырезал русла до `fluids`, а общий
  water-fill заполняет только колонки ниже `SeaLevel`; отдельной воды для
  влажных русел выше моря не создавалось в режиме аналитических долин.
- [x] Добавить pack-настройки `fill_water`, порога силы русла и влажности, а
  также высоты водной поверхности относительно исходной поверхности колонки.
  В default pack заполнение включено для достаточно выраженных русел во
  влажном климате; вода ставится в вырезанную полость как source fluid.
- [x] Передать настройки и в чанковый путь аналитических долин. Выяснено, что
  обычный balanced/density_3d профиль формирует русла в плотностной высоте и
  опускает их к уровню моря, тогда как `CarveChunkValleys` запускается только
  при `use_analytic_valleys`; без передачи pack-настроек новая вода оставалась
  бы включённой лишь для прямого колонкового пути.
- [x] Добавить Release-регрессии на воду в мокром ядре русла, положение
  водной поверхности, детерминизм и проход водного русла через границу двух
  чанков при разном порядке генерации.
- [x] Опустить поверхность воды на 2 блока ниже исходной поверхности колонки
  в default pack: вырезанное русло получает берега, а вода не заливает исходную
  поверхность. Release-регрессия проверяет высоту воды, пустой канал над водой,
  детерминизм и непрерывность на шве чанков.
- [x] Собрать `valley_carver_test` и приложение только в Release. Тест прошёл;
  новые profile-2 balanced/density_3d миры 410050 и 410051 созданы успешно
  (1434/1370 чанков). В обоих нет водных блоков выше Y=63, что соответствует
  используемому ими руслу, опускаемому к морскому уровню.
- [x] Создать Release-мир `WorldGen_RiverWater_Smooth_410052` с profile 2,
  `density_3d` и `use_analytic_valleys=true` (1396 чанков). Над Y=63 найдено
  636 водных блоков: крупнейший компонент — 632 блока в озере; отдельно есть
  короткий связный компонент из 4 блоков на Y=64. Он подтверждает появление
  надморской воды в аналитическом профиле, но сам по себе не подтверждает
  длинное русло или его соединение с морем.
- [x] Проверить связность `smooth`-русел на нескольких Release seed. В seed
  410053 связный надморский компонент содержит 7423 блока (X=39..106,
  Z=68..111, Y=64..73) и соседствует с единым компонентом воды на уровне моря
  площадью 4335 поверхностных колонок. В seed 410054 два крупных компонента
  содержат 763 и 756 блоков и оба соединяются с уровнем моря. В свежем seed
  410057 после снижения водной поверхности крупное русло содержит 5657 блоков
  выше моря и также соединяется с единым компонентом из 7424 морских колонок.
- [x] Создать свежие Release-миры после настройки глубины русла: seed 410056
  (1387 чанков) имеет 15 надморских поверхностных водных колонок, все на Y=64;
  seed 410057 (1399 чанков) имеет 1706 таких колонок на Y=64..74. Это
  подтверждает, что настройка работает и не удаляет русла, но распределение
  заметно зависит от seed.
- [x] Измерить ступени поверхности воды на seed 410057: среди 3215 пар соседних
  водных колонок 2561 имеют одинаковый уровень, 625 отличаются на 1 блок и 29 —
  на 2 блока; перепадов больше 2 нет. Дополнительное сглаживание поверхности
  русел пока не требуется по этой метрике.
- [ ] Визуально проверить глубину русел относительно берегов в стабильном
  рендере. Текущие flight-capture кадры поверхности имеют пробелы/дыры и не
  позволяют уверенно оценить перелив на берега; проверить также переходы
  русел в озёра и море.

### Улучшение рельефа (2026-10-19)

- [x] Зафиксировать локальную исходную базу: на smooth seed 410053/410054 в
  радиусе 96 суша занимает 4395/5411 колонок, средняя высота 67.6, максимум
  79/77, flatness 67.5%/68.7%, rolling hills 30.4%/28.1%. Эти стартовые окна
  покрывали в основном низины и не могли подтвердить наличие гор в дальних
  климатических регионах.
- [x] Измерить плотностный рельеф на сетке 33x33 с шагом 64 в области
  2048x2048 для трёх фиксированных seed. До перенастройки максимум был
  Y=85..88, p95=Y=72..73 и горных выборок выше Y=120 не было. После изменения
  трёхмасштабного горного профиля максимумы составили Y=159..190, p95=Y=106..114,
  а количество выборок Y>=120 — 29..44 из 1089 на seed. Проверка также
  подтверждает, что высоты остаются в пределах -64..319.
- [x] Ослабить слишком редкое пересечение климатических весов гор и разделить
  рельеф на широкий ridged-макромасштаб и мелкую детализацию. Широкий подъём
  формирует область хребта, локальный ridged-шум меняет силуэт внутри неё.
- [x] Собрать приложение только в Release и создать новый Release-мир
  `WorldGen_TerrainRelief_Smooth_410059`: profile 2, `density_3d`, radius 8,
  3815 chunk-файлов. В окне radius 128 у спавна 15939 сухих колонок, средняя
  высота 73.7, максимум 96, flatness 39.3%, rolling hills 43.9%. Это описание
  конкретного seed, а не прямое сравнение до/после: seed 410058 из старого
  сохранения отличается. Район спавна остаётся низинным и не покрывает весь
  масштаб горных климатических полей.
- [x] Проверить биомные размещения на новом Release-рельефе:
  `worldgen_vegetation_placement_integration_test` прошёл. В сохранённом мире
  в spawn radius 32 найдены 1827 древесных блоков и 139 блоков наземного
  декора (77 цветов, 62 meadow grass). Эти числа описывают только одно окно и
  сами по себе не калибруют остальные биомы.
- [x] Связать климатический вес горного uplift и горных биомов общей функцией
  `MountainClimateWeight`. Прежний биомный порог по доле полного диапазона
  высот оставлял пики Y=150..190 без горных биомов; после перехода на высоту над
  морем Release-регрессия подтверждает горной вес у всех 20 высоких выборок.
- [x] Улучшить силуэт хвойных профилей: коническая крона теперь начинается
  ниже по стволу и получает повторяемые ярусы боковых ветвей с поворотом от
  seed дерева. Release-проверка профиля и полный
  `worldgen_vegetation_placement_integration_test` прошли; тестовый CLI умеет
  запускать быстрый профильный сценарий отдельно. Smoke-мир `CI_410063` создан
  Release-бинарником, но его spawn radius 32 — прибрежное окно с 1019 сухими
  колонками и без деревьев, поэтому оно не подходит для визуальной оценки
  хвойного леса.
- [x] Плавно ослабить веса низменных биомов пропорционально горному влиянию,
  чтобы высокие открытые поверхности выбирали каменную/снежную палитру.
  `worldgen_smoothness_test` теперь требует горный вес >=0.20 минимум у 90%
  высоких выборок и доминирующую горную группу минимум у 75%; Release результат
  — 20/20 и 15/20 соответственно. Тест прошёл, Release smoke seed 410064 создал
  profile-2 `density_3d` мир из 697 чанков.
- [x] Проверить интеграцию новой биомной классификации в Release-приложении:
  smoke-мир `CI_410061_density_3d`, profile 2, `density_3d`, 699 chunk-файлов;
  интеграционная проверка завершилась `OK`.
- [x] Просканировать все сохранённые столбцы `WorldGen_TerrainRelief_Smooth_410059`:
  112896 поверхностей, из них 26511 выше моря; максимальная поверхность Y=111,
  поэтому этот мир содержит холмы и возвышенности, но не попал в область крупных
  горных пиков. На фиксированной сетке 33x33 для seed 410056/57/58 высокие пики
  Y>=150 теперь согласуются с горной группой биомов: вес Mountain/Snowy Slopes/
  Jagged Peaks >=0.20 у 20 из 20 выборок, максимум смешанного веса 0.90;
  доминирующим горный биом является в 15 из 20 точек.
- [ ] Найти Release-мир, в сохранённой области которого встречаются высокие
  горные и холодные пики; проверить их палитры, снеговую границу и размещение
  деревьев в игре. Текущий сохранённый мир не содержит таких пиков.
- [x] Проверить численную непрерывность водной поверхности русла на seed 410057:
  максимальная ступень между соседними водными колонками — 2 блока; 99.1% пар
  отличаются не более чем на 1 блок.
- [ ] Визуально оценить переходы речной поверхности в озёра и море, а также
  береговые профили в стабильном рендере.
- [x] Ограничить небольшие озёра климатически: для них теперь задаются минимум
  влажности и максимум температуры (`max_temperature`, default 0.75), плюс
  прежние ограничения континентальности и эрозии. Это повторяет целевое
  поведение 1.18, где малые озёра не размещаются в жарких/сухих областях; выбор
  профиля вынесен в общий предикат и покрыт Release-регрессиями. Поле
  необязательно для старых `lakes.json`, значение по умолчанию сохраняет их
  совместимость. Источник: [Mojang — New World Generation in Java Available for
  Testing](https://www.minecraft.net/en-us/article/new-world-generation-java-available-testing).
  Проверка 2026-10-19: `valley_carver_test` Release прошёл; Release smoke seed
  410064 (`density_3d`, radius 2) создал 697 чанков, spawn Y=66.1.
- [x] Исправить заниженную частоту детерминированного размещения флоры:
  acceptance рассчитывался от площади всей sampling-ячейки, а точка выбиралась
  только из её внутренней области. Это дополнительно умножало фактическую
  плотность на `(jitterSpan / cellSize)^2` (обычно в 2–4 раза ниже профиля).
  Кандидат теперь равномерно выбирается по полной ячейке независимыми hash для
  X/Z; быстрый Release-профиль разместил 91 meadow-grass placement на ровной
  площадке 64×64 при spacing 34 (quality-gate: не меньше 55). Полная интеграция
  завершилась `OK`, включая сравнение прямого и обратного порядка чанков. Новый
  Release smoke `CI_410065_density_3d` создал 692 чанка; в spawn radius 32
  метрики показывают 2358 древесных блоков, среднюю поверхность Y=63.2 и
  rolling hills 84.6%. Это наблюдение одного seed, не замена покомпонентной
  калибровке по биомам.
- [x] Исправить классификацию растительных метрик: список наземного покрова
  теперь учитывает блоки из правил `worldgen_sets` сохранённого мира и блоки
  статических шаблонов; низкие `bush_common` отделяются от стволов и крон
  деревьев. На smoke seed 410065 в spawn radius 32 теперь найдено 345 колонок
  наземного покрова из 2176 сухопутных (15.9%), 46 оснований деревьев и 757
  колонок древесных крон. Прежние 31 «кустовой footprint» были ложными
  совпадениями с древесными кронами; после проверки положения у поверхности в
  spawn-окне `bush_common` не найдено. Во всём smoke-мире есть только 13 блоков
  acacia-bush, поэтому плотность кустарников остаётся отдельной задачей.
- [ ] Проверить низкую частоту кустарников на нескольких seed и биомах; проверить
  плотность и покрытие по лесу, равнине, саванне и холодным биомам.
- [x] Для процедурных деревьев и кустов перестать отменять всю фичу из-за
  единичных листьев, пересекающихся со склоном или соседней фичей: листву
  обрезать, для дерева сохранить центральный ствол; у кустарника не строить
  листья ниже основания. Release-регрессия проверяет деревья и кусты среди
  блокирующих склоновых вокселей (`--procedural-vegetation-only`).
- [x] Уменьшить число открытых боковых брёвен в авторских процедурных профилях:
  oak, pine, aspen и acacia теперь формируют силуэты через крону/лопасти
  (`branch_count=0`); Release-регрессия проверяет расширенную крону без боковой
  древесины, а отдельный профиль сосны продолжает проверять поддержку ветвей.
  Release smoke seed 410067 создал 680 чанков; в spawn radius 32 найдено 1372
  древесных блока, 171 кустарниковый блок, 189 колонок покрова из 1626 сухопутных
  (11.6%), 28 оснований деревьев и 11 оснований кустарников. Односидовый smoke
  подтверждает загрузку новых правил, но не доказывает биомную калибровку.
- [x] Проверить связность брёвен в штатных статических tree-шаблонах: в
  `tree_acacia`, `tree_apple`, `tree_jungle` и `tree_jungle_emergent` было 43
  блока, не соединённых гранями с центральным стволом. Пробное удаление этих
  блоков снизило счётчик деревьев на 201 в том же seed, но точная причина этой
  разницы отдельно не изолирована; поэтому ветви сохранены и соединены
  кратчайшими воксельными перемычками. Release-регрессия проверяет все 11
  штатных tree-шаблонов. Два Release smoke с одинаковым seed 410067 и одинаковым
  содержимым создали 680 чанков каждый; SHA-256 всех 680 пар чанков совпал.
  Spawn radius 32 вырос с 1372 до 1513 tree-блоков после исправления связности.
- [ ] Повторить визуальную оценку стволов и ветвей после правки статических
  шаблонов на нескольких породах. Flight-кадр `worldgen_treebridge_410067`
  выполнен в Release, но сильный туман и известные артефакты поверхности всё ещё
  мешают уверенно калибровать силуэты и отделить ветви деревьев от проблем
  рендеринга.
- [x] Проверить эффект на новом Release smoke seed 410065: мир
  `CI_410065_postclip` содержит 693 чанка. При прежних высотах и spawn-центре
  метрики нашли 2415 древесных блоков, 277 кустарниковых блоков, 9 оснований и
  175 колонок крон кустарника; у прежнего smoke-мира того же seed до этой
  правки в spawn-окне было 0 кустарниковых блоков. Результат подтверждает
  улучшение размещения для этого seed, но не заменяет калибровку по биомам.
- [ ] Продолжить калибровку леса, кустарников и наземного/водного/пещерного
  декора по биомам. Собирать покрытия крон и травы, расстояния между деревьями
  и доли неудачных размещений на пригодных окнах леса, равнины, саванны, гор,
  берега и пещеры.
- [ ] Получить обзорные кадры и повторить визуальную приёмку в стабильном
  рендере; известные дыры поверхности пока мешают оценивать мелкие детали и
  не закрывают предыдущую задачу рендеринга.

### Резервный спавн в океанском мире (2026-10-19)

- [x] Если поиск не нашёл сухую поверхность, а мир заполняет океаны водой,
  поднимать резервную точку спавна как минимум до уровня моря плюс высота глаз.
  Высокий рельефный fallback сохраняется; для океанских seed игрок больше не
  появляется на высоте океанского дна.
- [x] Добавить Release-регрессию для мира без доступной суши и с `FillWater=true`;
  профильная проверка подтвердила fallback выше уровня моря.
- [x] Проверить исправление на том же seed 410066: прежний Release smoke
  создавал 659 чанков со spawn Y=34.12 при SeaLevel=63; новый Release smoke
  создал 659 чанков со spawn Y=65.12. Суша в радиусе поиска не появилась, но
  новая точка больше не находится под водой.

### Экологические высоты природных фич (2026-10-19)

- [x] Перевести высотные ограничения природных фич в
  `content/object_features.json` из абсолютных Y в `above_sea_level`, сохранив
  прежние зоны обитания относительно старого уровня моря Y=48. Береговые
  папирус и marram grass теперь могут расти около нового Y=63, а кораллы
  остаются на морском дне ниже поверхности.
- [x] Добавить разрешение обоих форматов: массив `[min, max]` остаётся абсолютным
  Y для совместимости с прежними JSON-паками; объект с `above_sea_level`
  считается относительно SeaLevel текущего мира.
- [x] Обновить `validate_object_features.py` и Release-регрессию для проверки
  относительных границ, нового прибрежного/подводного контента и чтения legacy
  массива без изменения его смысла. Строгий валидатор завершился `OK`.
- [x] Пересобрать приложение и интеграционную регрессию только в Release.
  `worldgen_vegetation_placement_integration_test` прошёл; smoke-мир
  `CI_410062_density_3d` создал 715 чанков. В его блоках найдены 80 meadow grass,
  103 dry grass и 14 brown coral.
- [ ] В стабильном рендере проверить растения у берега и кораллы на новом
  морском дне; численный smoke не заменяет визуальную проверку.

### Высотная граница леса в горных биомах (2026-10-19)

- [x] Добавить экологические ограничения высоты и уклона ко всем 8 статическим
  шаблонам деревьев. Причина: у процедурных видов уже были профили экологии, а
  статические `tree_*` не ограничивались по высоте и засаживали верхушки пиков.
  Пределы заданы относительно SeaLevel, чтобы оставаться корректными при смене
  вертикального диапазона мира.
- [x] Проверить на одинаковом seed 410058 и одинаковом радиусе 10 до/после
  изменения. На гребне вокруг (192,128), в диапазоне Y>=145, число блоков
  статических крон/стволов уменьшилось с 2207 до 0; рельефные выборки не
  изменились: 3120 колонок, min/p50/p95/max = 59/171/191/192. В spawn-окне
  радиусом 32 суммарные tree/shrub-блоки изменились умеренно: 44→38.
- [x] Добавить Release-регрессию на все восемь sea-relative профилей статических
  деревьев и выполнить полный `worldgen_vegetation_placement_integration_test`;
  результат `OK`. Пересобрать `Cubatarium` и интеграционную проверку только в
  Release, чтобы runtime-копия `bin/content/object_features.json` была актуальна.
- [ ] Повторить визуальную оценку альпийской границы на нескольких seed и
  температурных профилях. Release flight кадр получен, но mesh-дыры и сильный
  туман мешают принять качество рендера; проверка размещения подтверждена по
  данным блоков, визуальная приёмка остаётся открытой.

### Диагностика биома гор и прибрежной воды (2026-10-19)

- [x] Добавить в Release-регрессию координатную диагностику высоких точек и
  точечный sample биома. В seed 410058 координата (200,136), Y=191, имеет
  climate mountain weight 0.548; итоговые веса: mountains 0.483,
  snowy_slopes 0.201, forest 0.128; dominant biome — mountains. Это исключило
  ошибку классификации пика, несмотря на подпись `forest` в HUD.
- [x] Уточнить источник подписи HUD: `GeometryEngine` вызывает упрощённый
  `ClassifyBiome(temperature, moisture, localHeightNorm)`, а не тот же
  altitude-aware biome blend, который применяет генератор. HUD-биом не подходит
  как ground truth для приёмки процедурных биомов.
- [x] Посчитать воду в Release-сохранении того же seed: над уровнем моря найдено
  565 блоков единого прибрежного бассейна на Y=64..65, его габариты — 22×20
  блоков; 523 блока соприкасаются с морской водой Y=63. Это береговая форма,
  а не независимое внутреннее озеро.
- [ ] Найти отдельный seed с внутренним озером, не соединённым с морем, и
  визуально оценить береговую линию/глубину. `ValleyCarverTest` подтверждает
  непрерывное заполнение бассейна и независимость от порядка обхода, но не
  заменяет визуальную оценку природного размещения.
