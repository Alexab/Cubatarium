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

## Baseline refresh (2026-07-07)

| Metric | Owner | Baseline capture | Target gate | Notes |
|--------|-------|------------------|-------------|-------|
| Android startup p95 (cold start) | app/android | pending | no ANR-like dialog, p95 improvement vs 2026-07-07 baseline | Measure on low/mid/high devices |
| Android inventory controls overlap rate | gui/android | pending | 0 repros on smoke matrix | Cover 16:9, 20:9, 4:3 profiles |
| Android Back flow correctness | app/input | pending | 100% pass on 3-state flow | Inventory -> close, InGame -> menu, Menu -> exit confirm |
| Android joystick stuck incidents | input/android | pending | 0 stuck incidents in 10x multi-touch runs | Include release outside joystick area |
| Icon cache hit/miss/store | gui/cache | pending | hit ratio up after warmup, no UI hitch spikes | Track persistent PNG cache lifecycle |
| Frame time p95 (streaming stress) | render/world | pending | no regression vs current baseline | Use in-game performance HUD + diagnostics export |
