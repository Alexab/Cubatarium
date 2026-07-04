# Physics rollout (Stage C)

Operational guide for enabling Standard/Advanced physics in production worlds.

## Active defaults (`config.json.example`)

- `profile`: `standard`
- `falling_shadow_mode`: `false` (falling blocks apply world changes)
- `liquid_shadow_mode`: `false` (liquids simulate)
- `enable_collision_broadphase`: `true`
- `enable_collision_readiness_gate`: `true`

## Rollback order (incident)

1. Set `falling_shadow_mode`: `true`
2. Set `liquid_shadow_mode`: `true`
3. Set `physics.profile`: `primitive`

If movement regressions persist:

4. Disable `enable_collision_broadphase` and `enable_collision_readiness_gate`
5. Lower per-tick budgets (`block_events_per_tick_max`, `liquid_events_per_tick_max`)

## Manual QA checklist

- [ ] Sand cascade 10 blocks — no duplication or voids
- [ ] Break block at chunk boundary — neighbor chunks remesh and collision rebuild
- [ ] Ocean edge fill without oscillation (renewable water stable)
- [ ] Fast run into unloaded chunks — no fall-through (`CollReady` gate holds)
- [ ] Large horizontal move with `enable_collision_dda` — no tunneling

### Liquids (level-based model, 2026-07)

#### Automated shore gates

C++ (physics-nightly / smoke): `fluid_sim_mesh_integration_test`, `fluid_worldgen_seal_test`, `fluid_worldgen_chunk_test`, `fluid_mesh_faces_test`, `fluid_queue_integration_test`, `liquid_flow_scenarios_test`, `physics_integration_test`.

Python (worldgen smoke): `shore_air_gaps_max: 0` in `tools/worldgen_baseline.json` via `integration_test_worldgen.py`.

Automated gates: `block_placement_raycast_test`, `fluid_placement_test`, `fluid_mesh_faces_test`, `underwater_fog_column_test`, `fluid_surface_slice_test`, `fluid_sim_mesh_integration_test`, `fluid_queue_integration_test`, `fluid_worldgen_seal_test`, `fluid_worldgen_chunk_test`, `liquid_flow_scenarios_test`, `physics_integration_test`. Python CI: `shore_air_gaps_max` in `tools/worldgen_baseline.json` via `integration_test_worldgen.py`.

Manual QA — below-surface / underwater fog (variant A, TD-FL-029):

- [ ] Open ocean: wade in — seafloor tint before `eye.y < surface`; full underwater fog immediately after submerge
- [ ] Shallow water, standing, level view — no full-screen underwater fog
- [ ] Shallow water, look down — tint on seafloor blocks below local surface (per-column)
- [ ] Lake above/below SeaLevel — correct surface from column scan, not sea-level constant
- [ ] Lava pool on shore — lava-colored below-surface tint
- [ ] Run along loaded/unloaded chunk edge — no bright tint artifacts outside map (sentinel `-1000`)

Manual QA (required before closing TD-FL-021):

- [ ] Wide worldgen ravine (7×7): look down from rim → green target in center; click places block
- [ ] Water-filled worldgen pit: aim at floor → green in water cell; stone replaces water
- [ ] 1×1 pit: aim at pit floor (classic `hit+normal`); source on floor face; fluid spreads to fill center; source `Level=0` at placement
- [ ] 2×2 pit + one water source — four cells water; source cell stable
- [ ] Lava 2×2: source does not move (no block ping-pong)
- [ ] Shore: break block — fill within ~1 s in 7-block radius
- [ ] Water block on flat ground: top + 4 sides, semi-transparent shell
- [ ] Underwater / shore slope: no seafloor bleed-through, no co-planar flicker
- [ ] **Opaque occlusion through fluids:** `(x,y+1)=water`, `(x,y)=tree_log`, `(x,y,z-1)=lava` (lava on stump height behind column) — without water lava hidden by log; with water on log, lava must **not** show through water on pixels where opaque log occludes
- [ ] Pit connected to ocean: transparent water, bottom visible
- [ ] Old worlds load; ocean stable

See [FLUID_ARCHITECTURE.md](FLUID_ARCHITECTURE.md) and [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md).

### Liquids (legacy checklist)
- [ ] Break one block in sea floor above a cave — water fills the gap and drains downward
- [ ] Break a block in a water wall — neighbors refill the gap within a few seconds
- [ ] Place one lava block on flat stone — lava spreads slowly without ping-pong; volume does not duplicate
- [ ] Lava over air with stone sides — falls down without breaking adjacent blocks
- [ ] Watch `physics_visual_remesh_backlog` in debug overlay during spread — backlog should drain, edge textures should appear within ~2 frames

## Observability

- In-game debug overlay: `Phys profile`, queue depths, broadphase counters, collision readiness wait
- `movement_diagnostics.json`: `physics_*` fields including purge and broadphase telemetry
- Nightly workflow: `.github/workflows/physics-nightly.yml`
