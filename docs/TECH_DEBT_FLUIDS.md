# Tech debt: Fluids (flow-level refactor)

> Review at end of each phase (F0–F6, R1–R3). Close items when implemented or explicitly wont-fix.
> Total closed: 30 | Open: 4

## Open

| ID | Added in | Item | Why deferred | Target phase |
|----|----------|------|--------------|--------------|
| TD-FL-003 | R1 | Level-based basin / cliff face rules in `GreedyMesher` | R1 uses pre-F0 full-cube culling (fluid→solid hidden) | R4 |
| TD-FL-012 | R1 | `GreedyMeshEmitter` level-truncated fluid height | Simulation levels only until shore mesh tuned | R4 |
| TD-FL-022 | R4 | Luanti-style sloped fluid mesh (`drawLiquidNode`) | Depends on stable transform sim | R4 |
| TD-FL-027 | placement | Liquid-on-liquid hotbar source placement (Classic preview vs `IsAir` click) | Kind-aware `ApplyFluidFill` done; liquid-on-liquid policy still open | backlog |

Phase 9 backlog note: TD-FL-003, TD-FL-012, and TD-FL-022 remain on feature-branch backlog.

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-FL-034 | 2026-07 | v2 shore policy behind `render.below_surface_fog_v2` (default off); desktop fog A–C closed, D wont-fix; **Android AND-17 FAIL** (GLES surface film) — follow-up open |
| TD-FL-001 | F2 | Replaced `LiquidSimulationSystem` with `UFluidSpreadSystem` (source/flowing levels) |
| TD-FL-002 | F5 | `MarkFluidRegionDirty` + immediate remesh near player |
| TD-FL-003 | F4 → R1 | Basin heuristic (reverted R1; see Open) |
| TD-FL-004 | F2 | Removed `BlockedReturnCells` / move-copy liquid model |
| TD-FL-005 | audit | `JsonChunkSerializer` writes/reads optional `fluid` byte per voxel |
| TD-FL-006 | F1 | Full `fluid_data[4096]` per chunk (no RLE) — acceptable size |
| TD-FL-007 | audit | `fluid_chunk_io_test` binary v1/v2 round-trip via `UBinaryChunkSerializer` |
| TD-FL-008 | audit | `WorldStateHasher` includes packed fluid in region hash |
| TD-FL-009 | audit | `UFluidSpreadSystem` records `ULiquidDebugTrace` on spread |
| TD-FL-010 | classic-placement | Superseded by unified Classic placement (`hit+normal`); pocket API kept for future bucket tool |
| TD-FL-011 | classic-placement | Superseded by unified Classic placement in `AddObjectByView` + `UpdateIntersection` |
| TD-FL-012 | audit → R1 | Level height stub (reverted R1; see Open) |
| TD-FL-013 | audit | `fluid_blocks_per_tick_max` in `config.json.example` + `Core.cpp` |
| TD-FL-014 | classic-placement | `fluid_placement_test` + `block_placement_raycast_test` cover Classic pit/wall/ravine/water-pit scenarios |
| TD-FL-015 | audit | `fluid_mesh_faces_test` counts GreedyMesher fluid faces |
| TD-FL-016 | audit | `ChunkMeshSnapshot` shell fluid layer + `GetFluid()` |
| TD-FL-017 | audit | `UBlockWorld::SetBlock(liquid)` auto-`Source()` via definitions hook |
| TD-FL-018 | R2 | `ScanChunkFluidFrontier` / `EnqueueFluidFrontierAt` (ReflowScan-style) |
| TD-FL-019 | R2 | Restored `LiquidRenewable` for water; transform creates sources from 2 neighbors |
| TD-FL-020 | audit | `ARCHITECTURE.md` flow-level opaque↔fluid section updated |
| TD-FL-023 | R2 | Luanti-style `TransformFluidCell` in `UFluidSpreadSystem` |
| TD-FL-024 | R3 | `fluid_queue_integration_test` via `UFluidUpdateSet` + budget 128 |
| TD-FL-026 | worldgen-pit-placement | Solid hotbar blocks replace liquid cells (`IsPlaceableForSolidBlock` in `CanPlaceClassic` + `AddObject`) |
| TD-FL-028 | waterlogging | Permeable decor waterlogging (`IsFluidPermeable`, `fluid_data` without block replace; seal/spread/mesh) | `IsFluidPermeable`, `CanReceiveFluid` / `ShouldReplaceBlockWithFluid`, `SealFluidPermeableDecorInChunk`, GreedyMesher waterlogged pass, `fluid_permeable_decor_test` |
| TD-FL-025 | gameplay-flood | `FloodWetPockets` on `DelBlockAt` + shared seal; submerged-air physics; mesh fluid↔fluid culling | `UFluidSpreadSystem::FloodWetPockets*`, `fluid_gameplay_fixes_test` |
| TD-FL-029 | render | Per-column below-surface fog (variant A): `UFluidSurfaceMap` + shader `surfaceYAt`/`fluidIndexAt`; removed 20 cm pre-submerge band |
| TD-FL-030 | waterlogging | Explicit `FluidKind` in `fluid_data` (bits 4–7); placement/worldgen/flood write kind; legacy `kind=0` infer + sea-band fallback | `FluidCellState`, `ApplyFluidFill`, `ResolveFluidBlockId`, `fluid_placement_liquid_decor_test` |
| TD-FL-031 | render | Remove `PrimaryLiquidBlockId`; GreedyMesher uses per-cell `ResolveFluidBlockIdForMesh` | `GreedyMesher.cpp` |
| TD-FL-032 | phase-1-tuning | Centralize fluid/fog/worldgen magic numbers in `URuntimeTuning` + config.json | `RuntimeTuning.h`, `config.json.example` |
| TD-FL-021 | phase-8-docs | Manual liquids + fog QA moved to dedicated runbook | [`QA_FLUIDS_2026.md`](QA_FLUIDS_2026.md) |
| TD-FL-033 | perf-remesh | Fluid spread uses `MarkBlockChunkDirtyFromPhysics` (budgeted/async remesh); player placement keeps sync `MarkBlockChunkDirty` | `WorldBlockPhysicsService`, `WorldFluidFacade` |

## Phase tracker

| Phase | Status | Notes |
|-------|--------|-------|
| F0–F6 | done | Initial flow-level refactor |
| R1 | done | Full-cube mesh + simple culling restored |
| R2 | done | Transform sim + ReflowScan + Source placement |
| R3 | done | Integration test, telemetry stub, docs |
| R4 | backlog | Level mesh + sloped surfaces |

## TD-FL-034 — below-surface fog: симптомы и откат fix 2026-07-06

### Симптомы (in-game, ветка `arch_refactor2`, до и после неудачного fix)

| # | Сценарий | Наблюдение |
|---|----------|------------|
| A | Суша рядом с лужами / мелкой водой | Тёмные участки земли у кромки воды (per-column tint без полного underwater fog) |
| B | Взгляд с берега на море | Под водой видны **более тёмные** пятна на дне (рядом со светлыми участками песка) |
| C | Погружение (глаза на границе surface) | На определённой глубине, когда глаз **частично** под водой: кадр(ы) без полного тумана; участки дна сначала **тёмно-синие**, затем обычный подводный цвет |
| D | Дно моря | Часть блоков песка **светлее** соседних (не как на берегу, но заметная «пятнистость») — **не исправлено** |

Связано с TD-FL-029 (variant A): per-column `UFluidSurfaceMap` + `uBelowSurfaceFog` в `fshader_greedy.glsl`. Manual QA: FOG-01, FOG-03, FOG-06 в [`QA_FLUIDS_2026.md`](QA_FLUIDS_2026.md).

### Неудачная попытка fix (откачена 2026-07-06)

Цель: включить pre-submerge tint (`BelowSurfaceFogStrength = 1.0` при `!cameraInFluid`), закрыть дыры в surface map, выровнять `depthBelow` по верху блока.

| Изменение | Файл | Намерение |
|-----------|------|-----------|
| `BelowSurfaceFogStrength(columnFog, cameraInFluid)` → 1.0 при wading | `FluidUnderwaterFogLogic.h` | REG-01: tint до погружения |
| Fallback `FindFluidColumnSurfaceAt` при пустом slice | `FluidSurfaceMap.cpp` | REG-03: нет колонок без `fi` |
| `depthBelow = sy - (blockIndexY + 1)` вместо `sy - vWorldPos.y` | `fshader_greedy.glsl`, GLES | Ровный tint на дне |
| Тесты + FOG-07 | `FluidSurface*Test`, `QA_FLUIDS_2026.md` | Gate |

**Результат после fix:** регресс — тёмные пятна на **суше** у луж (A), тёмные пятна **дна** при взгляде с берега (B), при нырянии вспышка тёмно-синего дна (C); светлые пятна песка (D) и момент без тумана на границе погружения (C) **остались**.

**Откат:** `git checkout` на перечисленные fluid/fog/shader/test файлы; код возвращён к состоянию до fix.

### Направления для следующей попытки (не реализовано)

1. **Не смешивать** full-strength column tint на суше с distance fog — отдельная политика для shore band vs open ocean vs submerged.
2. **Диагностика** per-column: `surfaceY`, `bottomBlockY`, `inFluidSpan`, `uBelowSurfaceFog` на светлых vs тёмных блоках (одинаковая глубина).
3. **Partial submerge:** ранний сигнал из капсулы / eye band, не только `eye.y < surfaceY` колонки глаз (см. [`UNDERWATER_FOG_TRANSITION.md`](UNDERWATER_FOG_TRANSITION.md) §6).
4. Избегать «силового» `BelowSurfaceFogStrength = 1.0` над сушей — вероятная причина тёмных кромок у луж.

## Related docs

- [FLUID_ARCHITECTURE.md](FLUID_ARCHITECTURE.md)
- [UNDERWATER_FOG_TRANSITION.md](UNDERWATER_FOG_TRANSITION.md) — анализ подводного тумана при погружении, план variant A (TD-FL-029)
- [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md)

## Execution progress (2026-07-07)

- Packet `P0.2` baseline refresh committed (`15bbb00`): метрики startup/frame/cache закреплены в `REMEDIATION_BASELINE_METRICS.md`.
- Packet `R1` icon cache diagnostics (`57ef029`): versioned manifest + PNG read/write failure counters для наблюдаемости regressions.
- Packet `R2` targeted persistent cache invalidation (`17de9ca`): адресная инвалидация `block/creature/skin` снижает риск stale визуалов при runtime catalog refresh.
- `TD-FL-034` code closed (`f2e8a26`): v2 shore policy (`BelowSurfaceFogStrengthV2`, `IsShallowFluidSpan`, `IsPartialSubmerge`) behind `render.below_surface_fog_v2` (default **off**); GLES stencil in wave 0 (`fd04741`). Prior rollout (`dc75582`) откачен (`33c21c9`) — не повторять без A/B manual gate.
- **Manual sign-off (2026-07-07):** desktop v1 (`below_surface_fog_v2: false`) — FOG-01/03/04/06 PASS; symptoms A–C PASS; **D FAIL → wont-fix**. v2 A/B not run. Android AND-17 FAIL (surface film) — GLES follow-up open.

### TD-FL-034 manual symptom sign-off (2026-07-07)

Config: v1 only (`render.below_surface_fog_v2` **false**). v2 column not exercised.

| Symptom | Scenario | v1 (flag off) | v2 (flag on) | Resolution |
|---------|----------|---------------|--------------|------------|
| A | Суша у луж 1×1 / 2×2 | [X] PASS | — | **closed** |
| B | Взгляд с берега на дно моря | [X] PASS | — | **closed** |
| C | Погружение, глаза на границе surface | [X] PASS | — | **closed** |
| D | Пятнистость песка на дне моря | [X] FAIL | — | **wont-fix** (pre-existing) |

- Tester: manual (desktop)
- Build/commit: `arch_refactor3` @ `f2e8a26`
- Date: 2026-07-07
- Ship policy: `below_surface_fog_v2` default **false**; enable v2 only after GLES AND-17 + optional v2 A/B
