# Remediation baseline metrics

> Recorded at remediation phase 0.2 (2026-07-04). Updated after gaps follow-up (2026-07-05).

## God-class LOC (source lines, approximate)

| File | Baseline LOC | Current LOC (2026-07-05) | Target after remediation | Status |
|------|--------------|--------------------------|--------------------------|--------|
| `src/World/Physics/FluidSpreadSystem.cpp` | 1293 | **183** | coordinator < 400 | **met** |
| `src/Render/Engine/GeometryEngine.cpp` | 2151 | **2012** | −300 via fog pass extract | partial (−139) |
| `src/World/Core/World.cpp` | 2100 | **~2180** | facade slices | partial (fluid facade extracted) |

## Gaps follow-up (2026-07-05)

| Item | Result |
|------|--------|
| P0 fog transition | Per-column submerged fog via `FluidUnderwaterFogLogic`; no full-screen fog when map ready |
| CI remediation tests | All 13 plan targets in smoke CI |
| `URuntimeTuning` + config | `physics.fluid_tuning`, `render.underwater_fog`, `procedural.tuning.hills_vegetation_height_norm_max` |
| TD-FL-032 | Closed — runtime tuning wired |
| `UWorldFluidFacade` | Extracted from `World.cpp` |
| QA bridge | `PHYSICS_ROLLOUT.md` ↔ `QA_FLUIDS_2026.md` |

## Open audit items (relevant)

- TD-AUD-010: UWorld god-class (fluid facade slice done; further extractions backlog)
- TD-AUD-012: GeometryEngine coupling (fog pass extracted; LOC partial)
- TD-AUD-026/027: partial

See [TECH_DEBT_AUDIT.md](TECH_DEBT_AUDIT.md).
