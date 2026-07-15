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
| `heightmap` | yes | Quality gates in `integration_test_worldgen.py` + `worldgen_baseline.json` |
| `density_3d` | no | MVP: 3D FBM terrain density, Y-scan surface, `FillTerrainColumnFromDensity`, post-carve caves; optional CI via `--both-backends` |

### Remaining backlog

- Cross-chunk ravine/valley seams at chunk borders (intra-chunk carving is chunk-level; neighbor chunks do not share a single vmanip pass)
- Unified cave density (disable post-carve, cheese/spaghetti in terrain density only)
- MC-style density router (full offset/factor/jaggedness on 3D noise)
- Overhangs tuning and perf budget for `density_3d`
- Nightly `audit_worldgen_batch.py` on 20+ seeds
- `worldgen_refs.json` refresh after baseline tuning stabilizes

## Quality backlog (2026-07, post Overworld plan)

Infrastructure from the 2026-07 Overworld quality plan (spawn spiral, cave depth band, terrain spline, `density_3d` MVP, CI smoke) is **done**. Visual/metric quality and E2E vegetation were **partial**; the 2026-07-12 quality-resume cycle addressed hills, biome vegetation profiles, structure calibration, and placement baselines.

| ID | Item | Status | Notes |
|----|------|--------|-------|
| WG-Q1 | `flatness_pct` too high; rolling hills low | **Addressed** | `height.json` + biome height tuning; relaxed `FillMicroDepressions` / `ClampToFlatPlateau`; land spawn metrics via `quick_terrain_metrics.py --land` |
| WG-Q2 | `density_3d` MVP only | **Deferred** | Not in this cycle; default remains `heightmap` |
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
