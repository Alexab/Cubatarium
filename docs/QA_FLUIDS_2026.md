# QA Fluids 2026

Manual QA checklist for fluids and underwater fog rollout (Phase 8 closure).
Source checklist: [`docs/PHYSICS_ROLLOUT.md`](PHYSICS_ROLLOUT.md) (automated↔manual bridge).

## Automated coverage links

- Core fluid integration: [`FluidSimMeshIntegrationTest.cpp`](../src/Test/FluidSimMeshIntegrationTest.cpp), [`FluidQueueIntegrationTest.cpp`](../src/Test/FluidQueueIntegrationTest.cpp), [`PhysicsIntegrationTest.cpp`](../src/Test/PhysicsIntegrationTest.cpp), [`LiquidFlowScenariosTest.cpp`](../src/Test/LiquidFlowScenariosTest.cpp)
- Worldgen and shoreline gates: [`FluidWorldGenSealTest.cpp`](../src/Test/FluidWorldGenSealTest.cpp), [`FluidWorldGenChunkTest.cpp`](../src/Test/FluidWorldGenChunkTest.cpp), [`WorldGenFluidVegetationPipelineTest.cpp`](../src/Test/WorldGenFluidVegetationPipelineTest.cpp)
- Rendering and fog: [`FluidMeshFacesTest.cpp`](../src/Test/FluidMeshFacesTest.cpp), [`FluidSurfaceSliceTest.cpp`](../src/Test/FluidSurfaceSliceTest.cpp), [`FluidSurfaceMapLogicTest.cpp`](../src/Test/FluidSurfaceMapLogicTest.cpp), [`UnderwaterFogColumnTest.cpp`](../src/Test/UnderwaterFogColumnTest.cpp), [`FluidUnderwaterFogLogic.h`](../src/Render/Engine/FluidUnderwaterFogLogic.h)
- Placement: [`BlockPlacementRaycastTest.cpp`](../src/Test/BlockPlacementRaycastTest.cpp) (LIQ-01/02 scenarios), [`FluidPlacementTest.cpp`](../src/Test/FluidPlacementTest.cpp)

## Automated run (2026-07-05, Release MSVC)

All remediation smoke targets green, including `fluid_tuning_defaults_test`, `fluid_block_resolver_test`, `fluid_flood_service_test`, `fluid_fill_policy_test`, `fluid_kind_resolver_test`, `object_placement_mode_test`.

`python tools/audit_style.py`: 0 violations.

## P0 fog fix (per-column submerged fog)

Submerged rendering now uses **per-column below-surface fog** when the surface map is ready instead of full-screen `uFogEnabled`, fixing air blocks above water receiving underwater fog (FOG-04). Global fog remains only as fallback when the map is unavailable. See [`FluidUnderwaterFogLogic.h`](../src/Render/Engine/FluidUnderwaterFogLogic.h).

## Manual QA - underwater fog

| ID | Scenario | PASS | FAIL | Notes | Automated reference |
|----|----------|------|------|-------|---------------------|
| FOG-01 | Open ocean wade-in; tint before submerge; fog after submerge | [ ] | [ ] | Re-test after P0 per-column submerged fog | `FluidSurfaceMapLogicTest`, `UnderwaterFogColumnTest` |
| FOG-02 | Shallow water, standing, level view; no full-screen fog | [X] | [ ] | | `UnderwaterFogColumnTest` |
| FOG-03 | Shallow water, look down; tint on seafloor below local surface | [ ] | [ ] | Manual in-game | `FluidSurfaceSliceTest` |
| FOG-04 | Lake uses per-column surface, not sea-level constant | [ ] | [ ] | Re-test after P0 fix | `FluidSurfaceMapLogicTest` |
| FOG-05 | Lava pool lava-colored below-surface tint | [ ] | [ ] | Manual in-game | `UnderwaterFogColumnTest` |
| FOG-06 | Chunk edge; no artifacts outside sentinel | [ ] | [ ] | Manual in-game | `FluidSurfaceSliceTest` |

## Manual QA - liquids

| ID | Scenario | PASS | FAIL | Notes | Automated reference |
|----|----------|------|------|-------|---------------------|
| LIQ-01 | 7×7 ravine rim placement | [X] | [ ] | Automated: T5/T7 in `block_placement_raycast_test` | `BlockPlacementRaycastTest` |
| LIQ-02 | Water-filled pit placement | [X] | [ ] | Automated: T6 in `block_placement_raycast_test` | `BlockPlacementRaycastTest` |
| LIQ-03 | 1×1 pit classic placement | [X] | [ ] | | `FluidPlacementTest` |
| LIQ-04 | 2×2 pit + source stable | [X] | [ ] | | `LiquidFlowScenariosTest` |
| LIQ-05 | Lava 2×2 no ping-pong | [X] | [ ] | | `LiquidFlowScenariosTest` |
| LIQ-06 | Shore break fill ~1s | [X] | [ ] | | `PhysicsIntegrationTest` |
| LIQ-07 | Water shell faces | [X] | [ ] | | `FluidMeshFacesTest` |
| LIQ-08 | Shore slope no bleed-through | [X] | [ ] | | `FluidMeshFacesTest` |
| LIQ-09 | Opaque log occludes lava behind water | [X] | [ ] | Automated: column barrier test (mesh partial) | `FluidSimMeshIntegrationTest` |
| LIQ-10 | Ocean pit transparency | [X] | [ ] | | `FluidWorldGenSealTest` |
| LIQ-11 | Old worlds stable | [X] | [ ] | | `FluidChunkIoTest` |

## Sign-off

- Tester: automated gate complete; manual FOG-03/05/06 + FOG-01/04 re-test pending in-game
- Build/commit: `arch_refactor` (remediation gaps follow-up, uncommitted)
- Date: 2026-07-05
- Result: [X] Automated PASS [ ] Manual PASS [X] Manual partial — FOG-03/05/06 need in-game pass; FOG-01/04 need re-test after P0
