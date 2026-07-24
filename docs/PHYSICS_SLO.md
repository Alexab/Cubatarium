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
- In-game HUD (`ui.show_performance: true`, default off; Settings / F10): **Wall FPS** (full frame interval), **Sim FPS** (physics + view + draw), **Swap** ms, **Phys/Move/Block/Drain** ms breakdown
- Automatic InGame perf log: glog `[Perf]` lines + `bin/logs/perf_<timestamp>_<pid>.jsonl` (no world save required; interval `ui.perf_log_interval_sec`, default 2s)

## HUD metrics (F10 debug overlay)

| Line | Meaning |
|------|---------|
| Wall FPS | `1 / wall_frame_ms` — real frame pacing (includes input, swap, VSync/present wait) |
| Sim FPS | `1 / (physics_step_ms + view_ms + scene_ms)` — measured simulation + CPU draw-submit |
| Swap | Wall-clock ms spent in `glfwSwapBuffers` (GPU/present stall) |
| Phys | Total `DoMovement` time (movement + block physics + drain queues) |
| Move / Block / Drain | Per-phase breakdown from `PhysicsTelemetry` |
| steps | Block/drain ticks this frame (always 1; fixed multi-step removed 2026-07) |
| LiqQ | Liquid queue depth — rising under load indicates spread backlog |

**Wall ≪ Sim** (e.g. Wall ~5 FPS, Sim 60–90): first check `swap_wait_ms`. If swap is ~0 and `unaccounted_ms` dominates, read the phase breakdown in the same JSONL line: `input_ms`, `app_update_ms`, `world_extra_ms`, `prepare_frame_ms` (FluidSurfaceMap + sky + clear), `post_scene_ms` (weather/HUD after cubes), `gui_overlay_ms`, `residual_ms`. Config: `render.vsync` (default false → `glfwSwapInterval(0)`), `render.msaa_samples` (default 0).

If Wall ≈ Sim and both are low, the bottleneck is CPU sim/streaming — compare with `physics_step_ms` / GenQ in `movement_diagnostics.v2`.

## Threading evaluation (2026-07)

Block/fluid physics remains **main-thread** (Luanti-style queue + per-tick budget). Async workers (`UJobThreadPool`) already cover mesh build and chunk I/O only.

A future worker path would follow `UAsyncMeshBuilder`: immutable chunk snapshot → worker proposes fluid deltas → main thread commits to `BlockWorld`. **Not implemented** — deferred until `physics_step_ms` p95 exceeds SLO after remesh-path fix (TD-FL-033) and queue optimizations.

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

| Subsystem | Stage | Config |
|-----------|-------|--------|
| Code fallback (no `physics` section) | Primitive | `World.h` default |
| Example config | Standard | `config.json.example` `profile: standard` |
| Falling | active (example) | `falling_shadow_mode: false` |
| Liquids | active (example) | `liquid_shadow_mode: false` |
| Rollout stage | **C** | see `docs/PHYSICS_ROLLOUT.md` |

1. **Primitive** — scheduler on, falling/fluids off (legacy parity).
2. **Standard observe (default)** — `profile=standard`, block events + collision
   optimizations on; falling/fluids in **shadow mode** (candidates only).
3. **Standard active** — `falling_shadow_mode=false` and `liquid_shadow_mode=false`
   (see `docs/PHYSICS_ROLLOUT.md` for QA checklist).
4. **Advanced** — full block/fluid budgets + material rules; monitor queue backlogs daily.

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
- `chunk_rebuild_queue_test`
- `physics_profile_parse_test`
- `block_update_queue_purge_test`
- `liquid_queue_priority_test`
- `physics_integration_test`

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
