# FluidSurfaceMap hitch: budget + scroll threshold

## Goal

Cut `fluid_map_cpu_ms` spikes (400–800 ms, `full_rebuild=1`) from manual
`201036` / `192304` without async rewrite.

## 1. Wall-aware chunk budget

`ChunkUpdateBudget` / `FluidDirtyBudget` in `FluidSurfaceMap.cpp`:

| Condition | baseline | burst | near dirty cap |
|-----------|----------|-------|----------------|
| `last_wall_ms ≤ 40` | 8 | 16→24→**32** | 9 |
| `last_wall_ms > 40` | **4** | **8** | **4** |

Pass `world.GetLastMovementFrameMs()` into `UFluidSurfaceMap::Update`.

Near ring still prioritized (sort); just fewer chunks per hitch frame.

## 2. Scroll threshold

`FluidSurfaceWindowMoveThreshold` default **16 → 32** blocks
(`RuntimeTuning` / config `render.underwater_fog.surface_window_move_threshold`).

Fewer scroll strip rebuilds when walking across chunk boundaries.

## Out of scope

Map radius < RD, async slices, dry-chunk skip, scan_up/down wire.

## Validate

Manual walk near water: spike max `fluid_map_cpu` ↓; `full_rebuild` less sticky;
underfeet fog still ok within a few frames. Unit tests for threshold 32.
