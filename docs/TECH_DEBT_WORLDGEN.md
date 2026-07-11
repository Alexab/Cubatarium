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

- Unified cave density (disable post-carve, cheese/spaghetti in terrain density only)
- MC-style density router (full offset/factor/jaggedness on 3D noise)
- Overhangs tuning and perf budget for `density_3d`
- Nightly `audit_worldgen_batch.py` on 20+ seeds
- `worldgen_refs.json` refresh after baseline tuning stabilizes

## Quality backlog (2026-07, post Overworld plan)

Infrastructure from the 2026-07 Overworld quality plan (spawn spiral, cave depth band, terrain spline, `density_3d` MVP, CI smoke) is **done**. Visual/metric quality and E2E vegetation remain **partial**. Resume after P0 perf/lighting/streaming fixes (see [TECH_DEBT_CHUNK_STREAMING.md](TECH_DEBT_CHUNK_STREAMING.md) TD-CS-021, [TECH_DEBT_DAY_NIGHT_WEATHER_LIGHTING.md](TECH_DEBT_DAY_NIGHT_WEATHER_LIGHTING.md) TD-LIGHT-001).

| ID | Item | Status | Recommendation |
|----|------|--------|----------------|
| WG-Q1 | `flatness_pct` land ~58–83%; CI gate 38% not met; full `analyze_world` ~2h on large saves | Open | Move CI metric to **spawn-centered land-only** (`tools/quick_terrain_metrics.py`, `worldgen_quality_gates.py`); tune `FillMicroDepressions` / `ClampToFlatPlateau` only after P0 |
| WG-Q2 | `density_3d` MVP only (column Y-scan); unified cave density, MC-router, overhangs | Deferred | Step 2: shared density field for caves+terrain; step 3: surface router; keep default `heightmap` until metrics stable |
| WG-Q3 | Multi-seed CI gates (`integration_test_worldgen.py` without `--smoke`) | Open | Run 3–5 reference seeds in CI after P0; nightly `audit_worldgen_batch.py` (20 seeds) as separate workflow |
| WG-Q4 | `worldgen_refs.json` / baseline refresh | Open | Refresh after spawn metrics and vegetation E2E stabilize |
| WG-Q5 | Mesa/flat artifacts on pre-fix saves (e.g. World_112, World_106 ~95% flatness) | Known | Saves do not migrate; QA should recreate worlds; compare with `quick_terrain_metrics.py` not full-disk `analyze_world` |
| WG-Q6 | Vegetation / decor objects may not appear in-game | Open | See subsection below |

### WG-Q6 — Vegetation and objects

**Symptom:** After `TargetColumnDensity` / `ObjectFeaturePlacer` tuning, trees and decor may still be absent in-game (E2E not verified). Batch audit (World_106): `spawn_tree_blocks: 0`, `spawn_bush_common_footprints: 0`.

**Likely causes and fixes (in order):**

1. **`PassesNoiseDensityGate` / spacing** still reject columns on some seeds → lower gate or add biome floor density; verify with `tools/quick_terrain_metrics.py --count-objects`.
2. **`EnableTrees` / stage mask** — `RunVegetationStage` skipped when `procedural.trees=false` or mask lacks `Vegetation` → log stage mask at create-world.
3. **`CanPlacePlantAt` + seal/prune** after fluids — `worldgen_fluid_vegetation_pipeline_test` does not cover all biomes → add scatter-count integration test.
4. **Object pack / library** — pack id mismatch or empty object registry → assert non-zero placements in headless create (see [OBJECT_WORLDGEN.md](OBJECT_WORLDGEN.md)).
5. **Render vs placement** — cross-vegetation mesh (TD-CS-014) is separate from block placement; if blocks exist but no mesh, fix mesh warmup / lighting (TD-LIGHT-001), not worldgen.
6. **CI gate `spawn_tree_blocks_min: 5000`** in `tools/worldgen_baseline.json` — re-run on reference seeds after P0 lighting so meshes are warm.

**Recommended order (after P0):** headless placement audit → in-game verify → tune gates → refresh baseline.

## Execution progress (2026-07-07)

- Worldgen remains in maintenance/polish mode (core roadmap phases A–D already `done`).
- Общий debt-remediation цикл добавил baseline/traceability для cross-module рисков (`15bbb00`, `8a0fcdb`), без изменения worldgen runtime contracts.
- Незапирающие улучшения (pack dropdown UX, presets UX, biome/climate debug overlay) остаются в backlog и могут выполняться независимыми малыми PR без риска для генератора.
