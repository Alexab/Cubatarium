# Fluid architecture (flow-level model)

> Minecraft/DwarfCorp-style source + flowing fluids with discrete levels 0–7.
> Related: [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md), [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md), [ARCHITECTURE.md](ARCHITECTURE.md).

## Glossary

| Term | Meaning |
|------|---------|
| **source** | Infinite fluid at level 0; block never removed on spread |
| **flowing** | Finite fluid at level 1–7 |
| **falling** | Flowing fluid that spreads only downward (vertical stream) |
| **level** | 0 = source; 1 = full block height; 7 = thinnest layer |
| **wake** | Enqueue neighbors after a fluid change |
| **frontier** | Active set of liquid cells scheduled for spread ticks |

## FluidCellState

Packed in one byte per chunk cell (`fluid_data[i]`):

```
bits 0–2: Level (0–7)
bit  3  : Falling (1 = spread down only)
bits 4–7: Reserved
```

**Invariants:**

- `Level == 0` only when `BlockId` is a liquid in the same cell
- Non-liquid block → fluid byte must be 0
- `SetFluidState` only on liquid cells (debug assert)

## Spread rules (water)

Tick rate: 1 spread attempt / 5 physics ticks (configurable via `FluidSpreadPeriodTicks`).

Pseudocode per tick (one direction per block per tick, MC-style):

```
if not liquid: return
if not ShouldProcessFluidTick(tick, pos, period=5): return
state = GetFluidState(pos)

// Down first
if CanAccept(below) and below is air/floodable:
  SetBlock(below, id)
  SetFluidState(below, Flowing(1, falling=true))
  record change; source stays at pos
  return

// Horizontal (only if not falling)
if state.Falling: return
for side in 4 horizontal neighbors:
  new_level = state.Level + 1
  if new_level > FluidMaxLevel: continue
  if neighbor not air: continue
  if neighbor fluid level <= new_level: continue
  SetBlock(side, id)
  SetFluidState(side, Flowing(new_level))
  record change; source stays
  return after first success
```

## Spread rules (lava)

- `FluidMaxLevel = 3`
- `FluidSpreadPeriodTicks = 30`
- Same algorithm; source remains stable in pits

## Render height table

| Level | Mesh Y extent (fraction of block) |
|-------|-----------------------------------|
| 0 (source) | 1.0 |
| 1 | 7/8 |
| 2 | 6/8 |
| … | … |
| 7 | 1/8 |
| falling | 1.0 |

Formula: `height = 1.0f - level * (1.0f / 8.0f)`; falling → 1.0f.

## Face visibility matrix

| Neighbor | Fluid face drawn? |
|----------|-------------------|
| AIR | yes |
| FLUID same id, neighbor level ≥ self | no (hidden) |
| FLUID same id, neighbor level < self | yes (step) |
| SOLID opaque, enclosed basin | yes (truncated side) |
| SOLID opaque, open cliff | hide if source at cliff edge |

**Enclosed basin:** all 4 horizontal neighbors are solid or fluid → draw fluid sides against solid (pit visibility).

## Migration (binary chunk)

- **v1:** block runs only; on load liquids get `Pack(Source())`
- **v2:** after runs, `uint32 fluid_byte_count` + `fluid_data[4096]`

## Deprecated

- `BlockedReturnCells`, move/copy whole block semantics
- `ULiquidSimulationSystem`, `ULiquidUpdateQueue` (replaced by `UFluidSpreadSystem`, `UFluidUpdateSet`)
- `liquid_renewable` JSON field (ignored; use source model + `FluidMaxLevel` presets)

## Acceptance criteria

- Water/lava place in 1×1 pit (new and old with stone floor)
- 2×2 + 1 source water → 4 cells ≤ 2 s; source Level=0 at place position
- Lava 2×2: source does not move; no block position ping-pong
- Shore: break block → fill ≤ 1 s within 7 blocks
- Lava pit: top + 1–4 side faces to air; remesh ≤ 2 frames
- Old worlds load; ocean stable

## Related docs

- [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md) — deferred work tracker
