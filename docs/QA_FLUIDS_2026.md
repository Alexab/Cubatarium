# QA Fluids 2026

Manual QA checklist for fluids and underwater fog rollout (Phase 8 closure).
Source checklist: [`docs/PHYSICS_ROLLOUT.md`](PHYSICS_ROLLOUT.md) (automated↔manual bridge).

## Automated coverage links

- Core fluid integration: [`FluidSimMeshIntegrationTest.cpp`](../src/Test/FluidSimMeshIntegrationTest.cpp), [`FluidQueueIntegrationTest.cpp`](../src/Test/FluidQueueIntegrationTest.cpp), [`FluidStablePuddleTest.cpp`](../src/Test/FluidStablePuddleTest.cpp), [`PhysicsIntegrationTest.cpp`](../src/Test/PhysicsIntegrationTest.cpp), [`LiquidFlowScenariosTest.cpp`](../src/Test/LiquidFlowScenariosTest.cpp)
- Worldgen and shoreline gates: [`FluidWorldGenSealTest.cpp`](../src/Test/FluidWorldGenSealTest.cpp), [`FluidWorldGenChunkTest.cpp`](../src/Test/FluidWorldGenChunkTest.cpp), [`WorldGenFluidVegetationPipelineTest.cpp`](../src/Test/WorldGenFluidVegetationPipelineTest.cpp)
- Rendering and fog: [`FluidMeshFacesTest.cpp`](../src/Test/FluidMeshFacesTest.cpp), [`FluidSurfaceSliceTest.cpp`](../src/Test/FluidSurfaceSliceTest.cpp), [`FluidSurfaceMapLogicTest.cpp`](../src/Test/FluidSurfaceMapLogicTest.cpp), [`UnderwaterFogColumnTest.cpp`](../src/Test/UnderwaterFogColumnTest.cpp), [`FluidUnderwaterFogLogic.h`](../src/Render/Engine/FluidUnderwaterFogLogic.h)
- Placement: [`BlockPlacementRaycastTest.cpp`](../src/Test/BlockPlacementRaycastTest.cpp) (LIQ-01/02 scenarios), [`FluidPlacementTest.cpp`](../src/Test/FluidPlacementTest.cpp)

## Automated run (2026-07-07, `arch_refactor3` @ `f2e8a26`)

| Check | Result |
|-------|--------|
| `python tools/audit_style.py` | **0 violations** |
| `validate_gltf_creature.py --skinned-only` | **33/33 OK** |
| `test_gltf_skinned_bind_pose.py` | **33/33 passed** |
| `fluid_surface_map_logic_test` (v2 policy tests) | _run in CI/desktop build_ |
| Prior remediation smoke (2026-07-05 MSVC) | green — see table below |

Prior remediation smoke targets (2026-07-05): `fluid_tuning_defaults_test`, `fluid_block_resolver_test`, `fluid_flood_service_test`, `fluid_fill_policy_test`, `fluid_kind_resolver_test`, `object_placement_mode_test`.

## P0 fog fix (per-column submerged fog)

Submerged rendering now uses **per-column below-surface fog** when the surface map is ready instead of full-screen `uFogEnabled`, fixing air blocks above water receiving underwater fog (FOG-04). Global fog remains only as fallback when the map is unavailable. See [`FluidUnderwaterFogLogic.h`](../src/Render/Engine/FluidUnderwaterFogLogic.h).

## TD-FL-034 v2 shore policy (2026-07-07)

Feature flag: `render.below_surface_fog_v2` in `config.json` (default **false**). When **true**, `BelowSurfaceFogStrengthV2` applies shallow-span / partial-submerge / open-ocean policies (see [`TECH_DEBT_FLUIDS.md`](TECH_DEBT_FLUIDS.md) symptoms A–D).

**Manual A/B procedure:**

1. Creative world with ocean + shore puddles (1×1, 2×2 water on land).
2. Run FOG-01, FOG-03, FOG-04, FOG-06 with `below_surface_fog_v2: false`.
3. Set `below_surface_fog_v2: true`, restart, repeat same scenarios.
4. Fill symptom table A–D in `TECH_DEBT_FLUIDS.md` execution progress.
5. Android: AND-17 (sea surface film) on GLES — separate from v2 flag.

## Manual QA - underwater fog

| ID | Scenario | PASS | FAIL | Notes | Automated reference |
|----|----------|------|------|-------|---------------------|
| FOG-01 | Open ocean wade-in; tint before submerge; fog after submerge | [X] | [ ] | v1 PASS (symptoms A–C); see TECH_DEBT_FLUIDS | `FluidSurfaceMapLogicTest`, `UnderwaterFogColumnTest` |
| FOG-02 | Shallow water, standing, level view; no full-screen fog | [X] | [ ] | | `UnderwaterFogColumnTest` |
| FOG-03 | Shallow water, look down; tint on seafloor below local surface | [X] | [ ] | v1 PASS | `FluidSurfaceSliceTest` |
| FOG-04 | Lake uses per-column surface, not sea-level constant | [X] | [ ] | v1 PASS | `FluidSurfaceMapLogicTest` |
| FOG-05 | Lava pool lava-colored below-surface tint | [ ] | [ ] | Not tested this run | `UnderwaterFogColumnTest` |
| FOG-06 | Chunk edge; no artifacts outside sentinel | [X] | [ ] | v1 PASS | `FluidSurfaceSliceTest` |

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
| LIQ-12 | Land puddle: LiqQ→0, no oscillation after spread | [ ] | [ ] | F3: LiqQ/BlockQ drain without reload | `FluidStablePuddleTest`, `FluidQueueDrainIntegrationTest` |

## Sign-off

- Tester: manual run (desktop, v1 `below_surface_fog_v2: false`)
- Build/commit: `arch_refactor3` @ `f2e8a26`
- Date: 2026-07-07
- Config shipped: `below_surface_fog_v2` default **false** (v2 A/B not run; v1 sufficient for A–C)
- Result: [X] Automated PASS (2026-07-07 style + glTF) [ ] Manual PASS [X] Manual partial
- DoD blockers (plan): FOG-01/03/04/06 PASS on v1; symptom **D** wont-fix; Android AND-17 PASS (`18b81e0`)
- Notes: desktop v1 sign-off complete; Android fluids verified post-GLES single-pass
