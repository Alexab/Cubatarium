# Remediation test matrix

> Baseline for audit remediation (b9adeab..aef3399). Owner phase = primary gate when touching the module.

## Physics / fluids

| Target | Module | Owner phase | Smoke CI |
|--------|--------|-------------|----------|
| `fluid_state_pack_test` | FluidCellState pack/unpack | 1 | nightly |
| `fluid_chunk_io_test` | Chunk fluid IO | 1 | nightly |
| `fluid_spread_level_test` | Transform sim levels | 2 | nightly |
| `fluid_mesh_faces_test` | GreedyMesher culling | 4 | nightly |
| `fluid_surface_slice_test` | Column surface slice | 4 | nightly |
| `underwater_fog_column_test` | Underwater fog column | 4 | nightly |
| `fluid_queue_integration_test` | Fluid update queue | 2 | nightly |
| `fluid_sim_mesh_integration_test` | Sim → remesh | 2 | smoke |
| `fluid_placement_liquid_decor_test` | Placement + kind | 1, 7 | nightly |
| `fluid_permeable_decor_test` | Waterlogging | 1, 7 | smoke |
| `fluid_gameplay_fixes_test` | Dig flood + physics | 2 | nightly |
| `fluid_worldgen_seal_test` | Worldgen seal | 2, 3 | smoke |
| `fluid_worldgen_chunk_test` | Chunk worldgen fluids | 3 | nightly |
| `fluid_placement_test` | Classic placement | 1 | nightly |
| `fluid_tuning_defaults_test` | Fluid tuning constants | 1 | smoke |
| `fluid_block_resolver_test` | IUFluidBlockResolver | 1, 4 | smoke |
| `fluid_flood_service_test` | UFluidFloodService | 2 | smoke |
| `fluid_fill_policy_test` | UFluidFillPolicy | 2 | smoke |
| `fluid_kind_resolver_test` | Kind inference | 2, 7 | smoke |
| `fluid_surface_map_logic_test` | Fog CPU logic v3 | 4 | smoke |
| `underwater_waterline_test` | Sky waterline NDC | 4 | smoke |
| `fluid_permeable_block_flag_test` | Block JSON flag | 7 | smoke |
| `fluid_kind_preset_test` | Block preset FluidKind | 7 | smoke |
| `liquid_viscosity_gate_test` | Lava viscosity | 2 | nightly |
| `liquid_non_renewable_flow_test` | Non-renewable flow | 2 | nightly |
| `liquid_flow_scenarios_test` | Flow scenarios | 2 | nightly |
| `liquid_queue_priority_test` | Queue priority | 0 | smoke |
| `liquid_queue_backpressure_test` | Queue backpressure | 0 | smoke |
| `physics_integration_test` | Physics integration | 2 | smoke |
| `physics_budget_guard_test` | Budget limits | 0 | smoke |
| `material_reaction_rules_test` | Water/lava reaction | 7 | smoke |
| `decor_mesh_integration_test` | Decor mesh | 4 | smoke |

## Worldgen / objects

| Target | Module | Owner phase | Smoke CI |
|--------|--------|-------------|----------|
| `worldgen_vegetation_placement_test` | ObjectUtil placement | 0, 3 | smoke |
| `worldgen_fluid_vegetation_pipeline_test` | Seal → prune E2E | 3 | smoke |
| `worldgen_hills_vegetation_gate_test` | Hills topSolid gate | 3 | smoke |
| `worldgen_biome_features_test` | Biome feature weights vs pack | 3 | smoke |
| `worldgen_structure_slope_gate_test` | Structure slope gate | 3 | smoke |
| `worldgen_scatter_test` | Scatter placement | 3 | smoke |
| `object_placement_mode_test` | JSON placement mode | 1, 3 | smoke |

## Thread safety / streaming stress

| Target | Module | Owner phase | Smoke CI |
|--------|--------|-------------|----------|
| `chunk_generation_registry_test` | Concurrent Bump/Current registry | 0 | smoke |
| `linux-ninja-tsan` preset | TSAN build (Linux Clang/GCC) | 0 | manual |
| create → save → enter → walk | Streaming after quiesce | 0 | manual |

### Concurrency checklist (async path changes)

Any PR touching `ChunkLoadScheduler`, `WorldCooperativeOps`, `ChunkMeshCache`,
`AsyncChunkIO`, or `QuiesceBackgroundWork` must pass:

1. `chunk_generation_registry_test`
2. `cmake --preset linux-ninja-tsan` + `ninja chunk_generation_registry_test` (TSAN clean)
3. Manual stress (~2–5 min): new world → enter → walk beyond create patch → save → continue → exit

```bash
cmake --preset linux-ninja-tsan
cmake --build build/desktop-linux-tsan --target chunk_generation_registry_test
./build/desktop-linux-tsan/chunk_generation_registry_test
```

## Creatures / AI

| Target | Module | Owner phase | Smoke CI |
|--------|--------|-------------|----------|
| `creature_activity_steering_test` | Steering | 5 | smoke |
| `navigation_pathfinder_test` | A* pathfinding | 5 | smoke |
| `creature_gltf_loader_test` | glTF loader | 5 | nightly |
| `creature_bone_skeleton_loader_test` | Bone skeleton | 5 | nightly |
| `creature_skinned_draw_test` | Skinned UBO draw | 5 | smoke |
| `wander_interval_test` | Wander interval JSON | 5 | smoke |

## World / render / app

| Target | Module | Owner phase | Smoke CI |
|--------|--------|-------------|----------|
| `chunk_load_priority_test` | Chunk streaming | 0 | smoke |
| `world_mesh_service_test` | Mesh service | 6 | smoke |
| `block_placement_raycast_test` | Raycast placement | 1 | nightly |
| `movement_integration_test` | Movement | 0 | smoke |
| `deterministic_replay_test` | Replay harness | 0 | smoke |

## Gate commands (remediation)

```bash
python tools/audit_style.py
python tools/audit_clang_format.py
python tools/audit/check_include_rules.py
cmake --build build/desktop-msvc --config Release --target <target>
./build/desktop-msvc/Release/<target>.exe
```

See [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md) for full fluid regression list.
