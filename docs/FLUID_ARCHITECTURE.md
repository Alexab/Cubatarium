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
bits 4–7: Reserved
```

## Transform rules (`UFluidSpreadSystem::TickBlock`)

Per queued cell (Luanti `transformLiquidsLocal`-style rebalance):

1. **Down first:** floodable below → `Flowing(1, falling=true)`; source stays.
2. **Renewable source:** ≥2 horizontal sources → `Source()` at cell.
3. **Adjacent source (not below):** `Flowing(1)`.
4. **Else:** max level from neighbors (upper drop boost +4, horizontal `nb+1` only if `sideBelow` blocked).
5. **Range cutoff:** level `< min_survive` or `> FluidMaxLevel` → remove block.
6. **Viscosity:** lava steps level by `LiquidViscosity` per tick.

Placement: `SetBlock(liquid)` → `Source()`; player `AddObject` uses same path.

## Render (phase R1 — current)

- **Full-block cubes** for all fluid cells (`FluidCellHeight` returns 1.0).
- **Culling:** fluid→opaque hidden; opaque→fluid shown; fluid→air shown; fluid↔fluid hidden.
- **Shell transparency:** four-pass `GreedyTransparentPipeline` on full faces.
- Level-based mesh deferred to phase R4 (see [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md)).

## Face visibility matrix (R1)

| Neighbor | Fluid face drawn? |
|----------|-------------------|
| AIR | yes |
| FLUID same id | no |
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

See [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md) liquids section. Automated: `fluid_mesh_faces_test`, `liquid_flow_scenarios_test`, `fluid_queue_integration_test`.

## Related docs

- [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md) — deferred work tracker
