# QA Fluids 2026

Manual QA checklist for fluids and underwater fog rollout (Phase 8 closure).
Source checklist: `docs/PHYSICS_ROLLOUT.md` ("Manual QA" and "Liquids" sections).

## Automated coverage links

- Core fluid integration: [`src/Test/FluidSimMeshIntegrationTest.cpp`](../src/Test/FluidSimMeshIntegrationTest.cpp), [`src/Test/FluidQueueIntegrationTest.cpp`](../src/Test/FluidQueueIntegrationTest.cpp), [`src/Test/PhysicsIntegrationTest.cpp`](../src/Test/PhysicsIntegrationTest.cpp), [`src/Test/LiquidFlowScenariosTest.cpp`](../src/Test/LiquidFlowScenariosTest.cpp)
- Worldgen and shoreline gates: [`src/Test/FluidWorldGenSealTest.cpp`](../src/Test/FluidWorldGenSealTest.cpp), [`src/Test/FluidWorldGenChunkTest.cpp`](../src/Test/FluidWorldGenChunkTest.cpp), [`src/Test/WorldGenFluidVegetationPipelineTest.cpp`](../src/Test/WorldGenFluidVegetationPipelineTest.cpp), [`tools/integration_test_worldgen.py`](../tools/integration_test_worldgen.py)
- Rendering and fog sentinels: [`src/Test/FluidMeshFacesTest.cpp`](../src/Test/FluidMeshFacesTest.cpp), [`src/Test/FluidSurfaceSliceTest.cpp`](../src/Test/FluidSurfaceSliceTest.cpp), [`src/Test/FluidSurfaceMapLogicTest.cpp`](../src/Test/FluidSurfaceMapLogicTest.cpp), [`src/Test/UnderwaterFogColumnTest.cpp`](../src/Test/UnderwaterFogColumnTest.cpp)
- Placement and interaction: [`src/Test/BlockPlacementRaycastTest.cpp`](../src/Test/BlockPlacementRaycastTest.cpp), [`src/Test/FluidPlacementTest.cpp`](../src/Test/FluidPlacementTest.cpp)

## Manual QA - underwater fog

| ID | Scenario | PASS | FAIL | Notes | Automated reference |
|----|----------|------|------|-------|---------------------|
| FOG-01 | Open ocean: wade in; seafloor tint appears before `eye.y < surface`; full underwater fog right after submerge | [ ] | [ ] | | `UnderwaterFogColumnTest`, `FluidSurfaceMapLogicTest` |
| FOG-02 | Shallow water, standing, level view; no full-screen underwater fog | [ ] | [ ] | | `UnderwaterFogColumnTest` |
| FOG-03 | Shallow water, look down; tint only on seafloor blocks below local surface | [ ] | [ ] | | `FluidSurfaceSliceTest`, `FluidSurfaceMapLogicTest` |
| FOG-04 | Lake above/below sea level uses per-column local surface (not sea-level constant) | [ ] | [ ] | | `FluidSurfaceMapLogicTest` |
| FOG-05 | Lava pool on shore uses lava-colored below-surface tint | [ ] | [ ] | | `UnderwaterFogColumnTest` |
| FOG-06 | Run along loaded/unloaded chunk edge; no bright tint artifacts outside map (`-1000` sentinel) | [ ] | [ ] | | `FluidSurfaceSliceTest`, `UnderwaterFogColumnTest` |

## Manual QA - liquids

| ID | Scenario | PASS | FAIL | Notes | Automated reference |
|----|----------|------|------|-------|---------------------|
| LIQ-01 | Wide worldgen ravine (7x7): look down from rim, target appears in center, click places block | [ ] | [ ] | | `BlockPlacementRaycastTest`, `FluidPlacementTest` (partial) |
| LIQ-02 | Water-filled worldgen pit: target floor and place stone in water cell | [ ] | [ ] | | `FluidPlacementTest` |
| LIQ-03 | 1x1 pit: classic `hit+normal`, source on floor, fluid fills center, source remains `Level=0` | [ ] | [ ] | | `FluidPlacementTest`, `LiquidFlowScenariosTest` |
| LIQ-04 | 2x2 pit + one water source: four cells water, source cell stable | [ ] | [ ] | | `LiquidFlowScenariosTest`, `PhysicsIntegrationTest` |
| LIQ-05 | Lava 2x2: source does not move (no block ping-pong) | [ ] | [ ] | | `LiquidFlowScenariosTest`, `PhysicsIntegrationTest` |
| LIQ-06 | Shore: break block, fill completes within about 1 second in 7-block radius | [ ] | [ ] | | `PhysicsIntegrationTest`, `LiquidFlowScenariosTest` |
| LIQ-07 | Water block on flat ground: top + 4 side faces render as semi-transparent shell | [ ] | [ ] | | `FluidMeshFacesTest`, `FluidSimMeshIntegrationTest` |
| LIQ-08 | Underwater/shore slope: no seafloor bleed-through and no co-planar flicker | [ ] | [ ] | | `FluidMeshFacesTest`, `UnderwaterFogColumnTest` (partial) |
| LIQ-09 | Opaque occlusion through fluids: log occludes lava behind water on same pixel | [ ] | [ ] | | `FluidSimMeshIntegrationTest` (partial) |
| LIQ-10 | Pit connected to ocean: transparent water, bottom visible | [ ] | [ ] | | `FluidWorldGenSealTest`, `FluidWorldGenChunkTest` |
| LIQ-11 | Old worlds load and ocean remains stable | [ ] | [ ] | | `FluidChunkIoTest`, `PhysicsIntegrationTest` |

## Sign-off

- Tester:
- Build/commit:
- Date:
- Result: [ ] PASS [ ] FAIL
