# QA Fluids 2026

Manual QA checklist for fluids and underwater fog rollout (Phase 8 closure).
Source checklist: `docs/PHYSICS_ROLLOUT.md` ("Manual QA" and "Liquids" sections).

## Automated coverage links

- Core fluid integration: [`src/Test/FluidSimMeshIntegrationTest.cpp`](../src/Test/FluidSimMeshIntegrationTest.cpp), [`src/Test/FluidQueueIntegrationTest.cpp`](../src/Test/FluidQueueIntegrationTest.cpp), [`src/Test/PhysicsIntegrationTest.cpp`](../src/Test/PhysicsIntegrationTest.cpp), [`src/Test/LiquidFlowScenariosTest.cpp`](../src/Test/LiquidFlowScenariosTest.cpp)
- Worldgen and shoreline gates: [`src/Test/FluidWorldGenSealTest.cpp`](../src/Test/FluidWorldGenSealTest.cpp), [`src/Test/FluidWorldGenChunkTest.cpp`](../src/Test/FluidWorldGenChunkTest.cpp), [`src/Test/WorldGenFluidVegetationPipelineTest.cpp`](../src/Test/WorldGenFluidVegetationPipelineTest.cpp), [`tools/integration_test_worldgen.py`](../tools/integration_test_worldgen.py)
- Rendering and fog sentinels: [`src/Test/FluidMeshFacesTest.cpp`](../src/Test/FluidMeshFacesTest.cpp), [`src/Test/FluidSurfaceSliceTest.cpp`](../src/Test/FluidSurfaceSliceTest.cpp), [`src/Test/FluidSurfaceMapLogicTest.cpp`](../src/Test/FluidSurfaceMapLogicTest.cpp), [`src/Test/UnderwaterFogColumnTest.cpp`](../src/Test/UnderwaterFogColumnTest.cpp)
- Placement and interaction: [`src/Test/BlockPlacementRaycastTest.cpp`](../src/Test/BlockPlacementRaycastTest.cpp), [`src/Test/FluidPlacementTest.cpp`](../src/Test/FluidPlacementTest.cpp)

## Automated run (2026-07-05, Release MSVC)

| Test | Result |
|------|--------|
| `fluid_sim_mesh_integration_test` | OK |
| `fluid_queue_integration_test` | OK |
| `physics_integration_test` | OK |
| `liquid_flow_scenarios_test` | OK |
| `fluid_worldgen_seal_test` | OK |
| `fluid_worldgen_chunk_test` | OK |
| `underwater_fog_column_test` | OK |
| `fluid_mesh_faces_test` | OK |
| `fluid_surface_slice_test` | OK |
| `fluid_surface_map_logic_test` | OK |
| `fluid_placement_test` | OK |
| `fluid_chunk_io_test` | OK |
| `worldgen_scatter_test` | OK |
| `worldgen_fluid_vegetation_pipeline_test` | OK |
| `worldgen_hills_vegetation_gate_test` | OK |
| `navigation_pathfinder_test` | OK |

`python tools/audit_style.py`: 0 violations.

## Manual QA - underwater fog

| ID | Scenario | PASS | FAIL | Notes | Automated reference |
|----|----------|------|------|-------|---------------------|
| FOG-01 | Open ocean: wade in; seafloor tint appears before `eye.y < surface`; full underwater fog right after submerge | [X] | [ ] | Краткий flash без тумана под водой при входе — см. комментарий ниже | `UnderwaterFogColumnTest`, `FluidSurfaceMapLogicTest` |
| FOG-02 | Shallow water, standing, level view; no full-screen underwater fog | [X] | [ ] | | `UnderwaterFogColumnTest` |
| FOG-03 | Shallow water, look down; tint only on seafloor blocks below local surface | [ ] | [ ] | | `FluidSurfaceSliceTest`, `FluidSurfaceMapLogicTest` |
| FOG-04 | Lake above/below sea level uses per-column local surface (not sea-level constant) | [ ] | [ ] | На мелководье смешанный air/water view — см. комментарий | `FluidSurfaceMapLogicTest` |
| FOG-05 | Lava pool on shore uses lava-colored below-surface tint | [ ] | [ ] | | `UnderwaterFogColumnTest` |
| FOG-06 | Run along loaded/unloaded chunk edge; no bright tint artifacts outside map (`-1000` sentinel) | [ ] | [ ] | | `FluidSurfaceSliceTest`, `UnderwaterFogColumnTest` |

**FOG-01 / FOG-04 (tester notes):** На мелководье можно встать так, что частично видно дно и воздух (голова чуть выше воды); блоки у ног без тумана. При небольшом приседании — уровень воды, подводный мир с туманом и одновременно воздух над водой, но воздушный мир тоже с подводным туманом. При погружении (FOG-01) блоки непосредственно под водой на мгновение видны без тумана — вероятная проблема полосы перехода (follow-up, не блокер automated gate).

## Manual QA - liquids

| ID | Scenario | PASS | FAIL | Notes | Automated reference |
|----|----------|------|------|-------|---------------------|
| LIQ-01 | Wide worldgen ravine (7x7): look down from rim, target appears in center, click places block | [ ] | [ ] | Нет оврагов в тестовом мире | `BlockPlacementRaycastTest`, `FluidPlacementTest` (partial) |
| LIQ-02 | Water-filled worldgen pit: target floor and place stone in water cell | [ ] | [ ] | Нет подходящей ямы | `FluidPlacementTest` |
| LIQ-03 | 1x1 pit: classic `hit+normal`, source on floor, fluid fills center, source remains `Level=0` | [X] | [ ] | | `FluidPlacementTest`, `LiquidFlowScenariosTest` |
| LIQ-04 | 2x2 pit + one water source: four cells water, source cell stable | [X] | [ ] | | `LiquidFlowScenariosTest`, `PhysicsIntegrationTest` |
| LIQ-05 | Lava 2x2: source does not move (no block ping-pong) | [X] | [ ] | | `LiquidFlowScenariosTest`, `PhysicsIntegrationTest` |
| LIQ-06 | Shore: break block, fill completes within about 1 second in 7-block radius | [X] | [ ] | | `PhysicsIntegrationTest`, `LiquidFlowScenariosTest` |
| LIQ-07 | Water block on flat ground: top + 4 side faces render as semi-transparent shell | [X] | [ ] | | `FluidMeshFacesTest`, `FluidSimMeshIntegrationTest` |
| LIQ-08 | Underwater/shore slope: no seafloor bleed-through and no co-planar flicker | [X] | [ ] | | `FluidMeshFacesTest`, `UnderwaterFogColumnTest` (partial) |
| LIQ-09 | Opaque occlusion through fluids: log occludes lava behind water on same pixel | [ ] | [ ] | | `FluidSimMeshIntegrationTest` (partial) |
| LIQ-10 | Pit connected to ocean: transparent water, bottom visible | [X] | [ ] | | `FluidWorldGenSealTest`, `FluidWorldGenChunkTest` |
| LIQ-11 | Old worlds load and ocean remains stable | [X] | [ ] | | `FluidChunkIoTest`, `PhysicsIntegrationTest` |

## Sign-off

- Tester: manual partial (in-game); automated gate by agent
- Build/commit: `arch_refactor` @ `a350d9f` + remediation DoD (uncommitted)
- Date: 2026-07-05
- Result: [X] Automated PASS [ ] Manual PASS [X] Manual partial — fog transition band (FOG-01/04) open; LIQ-01/02 N/A (terrain)
