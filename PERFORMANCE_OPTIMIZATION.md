# Performance — Cubatarium

This document describes **implemented** runtime optimizations in the current codebase. For streaming/mesh backlog work see [docs/TECH_DEBT_CHUNK_STREAMING.md](docs/TECH_DEBT_CHUNK_STREAMING.md). For Wall vs Sim FPS interpretation see [docs/PHYSICS_SLO.md](docs/PHYSICS_SLO.md).

## Render path

| Feature | Config flag | Module |
|---------|-------------|--------|
| Greedy meshing (merged quads) | `render.greedy_meshing` | `GreedyMesher`, `ChunkMeshCache` |
| Face quads (world-space UV) | `render.face_quads` | `GreedyMeshBatch` |
| Frustum culling | `render.frustum_culling` | `GeometryEngine`, `Frustum` |
| Batch cache (instanced legacy path) | `render.batch_cache` | `GeometryEngine` |
| Async mesh build | `render.async_meshing` | `UAsyncMeshBuilder` |
| Transparent 4-pass pipeline | (always when greedy) | `GreedyTransparentPipeline` |
| VSync / present | `render.vsync` (default **false** → SwapInterval 0) | `WindowManager` |
| MSAA | `render.msaa_samples` (default **0**) | window create |

Legacy instanced cubes (`greedy_meshing: false`) remain for bisect/debug; fluids require greedy meshing.

## Automatic performance logging

InGame frames write:

- glog: `[Perf] kind=period|spike wall_ms=… sim_ms=… swap_wait_ms=… unaccounted_ms=… prepare_frame_ms=… post_scene_ms=… gui_overlay_ms=… residual_ms=…`
- JSONL: `bin/logs/perf_<YYYYMMDD-HHMMSS>_<pid>.jsonl`

Interval: `ui.perf_log_interval_sec` (default 2). Spikes (`wall_ms > 100`) log immediately. No world save required.

**Wall ≪ Sim** → inspect `swap_wait_ms`; if near zero, use `prepare_frame_ms` / `post_scene_ms` / `gui_overlay_ms` / `app_update_ms` / `residual_ms` from the same JSONL line.

## World streaming

| Feature | Config / API | Notes |
|---------|--------------|-------|
| Chunk streaming | `UChunkStreamer` | Ring gate, priority load |
| Async chunk I/O | `ProceduralSettings.AsyncChunkIo` | Background save/load, main-thread commit |
| Async chunk generation | `ProceduralSettings.AsyncChunkGeneration` | Async gen on by default in current configs |
| Load scheduler | `UChunkLoadScheduler` | Sync collision ring |
| Movement diagnostics | `movement_diagnostics.json` | Gen/mesh/io breakdown (`movement_diagnostics.v2`) |

## Profiling bisect

1. HUD: `ui.show_performance: true` (default off; Settings «Show debug overlay» or F10) — compare Wall / Sim / Swap.
2. Read latest `bin/logs/perf_*.jsonl` after play (or glog `[Perf]` lines).
3. If Wall ≪ Sim: toggle `render.vsync`, keep `msaa_samples: 0`, check GPU load (weather drops to Fast when swap_wait high).
4. If Wall ≈ Sim: toggle `render.async_meshing` / procedural budgets; export `worlds/World_NNN/movement_diagnostics.json`.
5. CI: `python tools/smoke_worldgen_metrics.py --metrics-json <path>`.

## Smoke / regression

```powershell
./scripts/doctor-windows.ps1
python tools/integration_test_worldgen.py --exe bin/Cubatarium.exe --cwd bin
python tools/audit/orchestrate.py --phase scan
```

## Deferred (not yet default)

- GPU instancing for cross vegetation (TD-CS-014)
- Persistent GPU VBO pooling (TD-CS-016)
- Full incremental resource-pack atlas rebuild (TD-002)

See [docs/TECH_DEBT_*.md](docs/) for the full backlog.
