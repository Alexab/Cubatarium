# Physics pipeline SLO

Operational targets for the modular physics rollout (`Primitive` / `Standard` / `Advanced`).

## Scope

These SLOs cover:

- physics scheduler step time (`physics_step_ms`)
- block and liquid queue depth
- visual remesh / collision rebuild backlog
- collision readiness wait time
- deferred and dropped update ratio under load

Diagnostics are exported via:

- `movement_diagnostics.json` (`physics_*` fields)
- `PhysicsTelemetry` counters on `UWorld`

## MVP thresholds (single-player desktop)

| Metric | Target (p95) | Alert (p99) | Notes |
|--------|----------------|-------------|--------|
| `physics_step_ms` | <= 2.0 ms | <= 5.0 ms | movement + block + drain rebuild queues |
| `physics_block_queue_depth` | <= 256 | <= 1024 | after steady-state gameplay |
| `physics_liquid_queue_depth` | <= 128 | <= 512 | with `enable_fluids=true` |
| `physics_visual_remesh_backlog` | <= 32 | <= 128 | physics-driven remesh only |
| `physics_collision_rebuild_backlog` | <= 32 | <= 128 | broadphase cache rebuild |
| `collision_ready_wait_ms` | <= 16 ms | <= 50 ms | near streaming boundaries |
| deferred+dropped ratio | <= 5% | <= 15% | under scripted stress |

## Rollout stages

1. **Primitive** — scheduler on, falling/fluids off (legacy parity).
2. **Standard observe (default)** — `profile=standard`, block events + collision
   optimizations on; falling/fluids in **shadow mode** (candidates only).
3. **Standard active** — set `falling_shadow_mode=false` and/or
   `liquid_shadow_mode=false` in limited worlds.
4. **Advanced** — full block/fluid budgets; monitor queue backlogs daily.

## Rollback order (incident)

1. Set `physics.profile` to `primitive`.
2. Disable `enable_falling`, `enable_fluids`, `enable_block_events`.
3. Disable `enable_collision_broadphase` and `enable_collision_readiness_gate` if movement regressions appear.
4. Reduce per-tick budgets (`block_events_per_tick_max`, `visual_remesh_per_tick_max`) before touching render distance.

## Verification

PR-gated CI runs:

- `deterministic_replay_test`
- `physics_budget_guard_test`
- `liquid_queue_backpressure_test`
- `collision_readiness_gate_test`
- `movement_chunk_boundary_test`
- `falling_blocks_stability_test`

Local smoke:

```powershell
cmake --build build/desktop-msvc --config Release --target deterministic_replay_test physics_budget_guard_test
./build/desktop-msvc/Release/deterministic_replay_test.exe
./build/desktop-msvc/Release/physics_budget_guard_test.exe
```

## Alerting (manual MVP)

Inspect `movement_diagnostics.json` after reproducing a hitch:

- rising `physics_visual_remesh_backlog` with flat `mesh_rebuild_ms` → collision/visual queue starvation
- rising `physics_dropped_updates` with low FPS → reduce spawn rate or lower liquid radius
- `fall_through_suspected=true` with `collision_readiness_gate` enabled → increase `collision_safety_radius_chunks` temporarily
