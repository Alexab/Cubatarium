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

## Observability

- In-game debug overlay: `Phys profile`, queue depths, broadphase counters, collision readiness wait
- `movement_diagnostics.json`: `physics_*` fields including purge and broadphase telemetry
- Nightly workflow: `.github/workflows/physics-nightly.yml`
