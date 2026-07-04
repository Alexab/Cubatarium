# Fluid architecture (flow-level model)

> Minecraft/Luanti-style source + flowing fluids with discrete levels 0–7.
> Related: [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md), [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md), [ARCHITECTURE.md](ARCHITECTURE.md).

## Glossary

| Term | Meaning |
|------|---------|
| **source** | Infinite fluid at level 0; block never removed on spread |
| **flowing** | Finite fluid at level 1–7 (1 = thickest, 7 = thinnest) |
| **falling** | Flowing fluid with `Falling` bit; spreads downward first |
| **level** | 0 = source; 1–7 = flowing depth |
| **wake** | Enqueue neighbors after a fluid change |
| **frontier** | Active liquid/air boundary cells (`FluidReflowScan`) |

## FluidCellState

Packed in one byte per chunk cell (`fluid_data[i]`):

```
bits 0–2: Level (0–7)
bit  3  : Falling (1 = vertical stream)
bits 4–7: FluidKind (0=infer/legacy, 1=water, 2=lava, …)
```

Dry permeable decor: `block_id` = grass/reeds, `fluid_data=0`. Wet (waterlogged): same `block_id`, non-zero `fluid_data` with explicit `FluidKind` when written by placement, worldgen seal, or flood.

## Transform rules (`UFluidTransformSim::TickBlock`)

Per queued cell (Luanti `transformLiquidsLocal`-style rebalance). `UFluidSpreadSystem` is a thin coordinator delegating to `UFluidFloodService`, `UFluidFillPolicy`, `UFluidTransformSim`, and `UFluidBlockResolver`.

1. **Down first:** floodable below → `Flowing(1, falling=true)`; source stays.
2. **Renewable source:** ≥2 horizontal sources → `Source()` at cell.
3. **Adjacent source (not below):** `Flowing(1)`.
4. **Else:** max level from neighbors (upper drop boost +4, horizontal `nb+1` only if `sideBelow` blocked).
5. **Range cutoff:** level `< min_survive` or `> FluidMaxLevel` → remove block.
6. **Viscosity:** lava steps level by `LiquidViscosity` per tick.

Placement: hotbar `AddObject` uses `ApplyFluidFill` + explicit `FluidKind`; `SetBlock(liquid)` → `Source()` with kind from block preset.

## Waterlogging (permeable decor)

| Policy | `block_id` | `fluid_data` | Spread / seal |
|--------|------------|--------------|---------------|
| AIR | air | source/flow + kind | `SetBlock(fluid)` + `SetFluidState` |
| **Permeable** (cross/cutout, occupancy &lt; 1) | e.g. `tall_grass` | flow/source + **explicit kind** | **only** `SetFluidState`; block preserved |
| **Floodable** (legacy) | replaced | source/flow + kind | `SetBlock` + `SetFluidState` |
| Solid | stone | — | blocks fluid |

`IsFluidPermeable` is derived from render style + occupancy (no JSON flag yet). Mesh: GreedyMesher waterlogged pass uses **per-cell** `ResolveFluidBlockIdForMesh` (no global default water). Worldgen: `SealFluidPocketsInChunk` + `SealFluidPermeableDecorInChunk` write `FluidKind::Water` on decor in sea columns and coastal stacks (sea…sea+8).

## Gameplay flood (`FloodWetPockets`)

Shared BFS (up to 8 passes) used by worldgen seal and `DelBlockAt`:

| Context | AIR fill | Permeable decor | Fluid id |
|---------|----------|-----------------|----------|
| Worldgen `SealFluidPocketsInChunk` | `SetBlock(water)` + `Source()` | `SetFluidState` only | fixed water |
| Gameplay `DelBlockAt` | `SetBlock(water)` + `Flowing(1)` | `SetFluidState` only | water if any water neighbor, else other liquid |

Break-site flood (`FloodBreakSiteFromWetNeighbors`): one hop only — fills the broken cell (if wet-adjacent) plus each wet neighbor’s four horizontal and downward spill targets (max ~30 cells). No BFS/radius scan. Worldgen still uses `FloodWetPocketsInBox` (8 passes). Simulation uses Luanti transform only (no active horizontal push).

## Material reactions (water + lava)

`UMaterialReactionRules::TryWaterMeetsLava`: AIR cell with water and lava neighbors → **stone** (not lava). Shore “lava” sightings are usually worldgen `TryPlaceLavaPool` (Hills, above sea) or `ResolveFluidKind` filling air next to exposed lava; gameplay flood prefers water when both touch.

## Render (phase R1 — current)

- **Full-block cubes** for all fluid cells (`FluidCellHeight` returns 1.0).
- **Culling:** fluid→opaque hidden; opaque→fluid shown; fluid→air shown; fluid↔fluid same id hidden; **fluid↔different fluid** face kept.
- **Shell transparency:** four-pass `GreedyTransparentPipeline` on full faces; opaque depth snapshot after solid/cutout pass (`UOpaqueDepthCapture`) rejects transparent color/fuzzy fragments behind opaque geometry on the same screen pixel.
- Level-based mesh deferred to phase R4 (see [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md)).

## Face visibility matrix (R1)

| Neighbor | Fluid face drawn? |
|----------|-------------------|
| AIR | yes |
| FLUID same id | no |
| FLUID different id | yes |
| SOLID opaque | no (terrain face kept) |
| SOLID toward fluid | yes |

## Frontier queue (`FluidReflowScan`)

- `EnqueueFluidFrontierAt` on block removal / placement wake.
- `ScanChunkFluidFrontier` on chunk commit — column scan, chunk-edge neighbors.
- `UFluidUpdateSet` budget: `fluid_blocks_per_tick_max` (default 128).

## Migration (binary chunk)

- **v1:** block runs only; on load liquids get `Pack(Source())`
- **v2:** after runs, `uint32 fluid_byte_count` + `fluid_data[4096]`

## Deprecated

- `ULiquidSimulationSystem`, `ULiquidUpdateQueue` (move/copy model)
- Incremental one-neighbor-only spread without rebalance

## Acceptance criteria

See [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md) liquids section. Automated: `fluid_mesh_faces_test`, `fluid_queue_integration_test`, `fluid_placement_liquid_decor_test`, `fluid_permeable_decor_test`.

## Related docs

- [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md) — deferred work tracker
