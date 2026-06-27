# Performance — Cubatarium

This document describes **implemented** runtime optimizations in the current codebase. For streaming/mesh backlog work see [docs/TECH_DEBT_CHUNK_STREAMING.md](docs/TECH_DEBT_CHUNK_STREAMING.md).

## Render path

| Feature | Config flag | Module |
|---------|-------------|--------|
| Greedy meshing (merged quads) | `render.greedy_meshing` | `GreedyMesher`, `ChunkMeshCache` |
| Face quads (world-space UV) | `render.face_quads` | `GreedyMeshBatch` |
| Frustum culling | `render.frustum_culling` | `GeometryEngine`, `Frustum` |
| Batch cache (instanced legacy path) | `render.batch_cache` | `GeometryEngine` |
| Async mesh build | `render.async_meshing` | `UAsyncMeshBuilder` |
| Transparent 4-pass pipeline | (always when greedy) | `GreedyTransparentPipeline` |

Legacy instanced cubes (`greedy_meshing: false`) remain for bisect/debug; fluids require greedy meshing.

## World streaming

| Feature | Config / API | Notes |
|---------|--------------|-------|
| Chunk streaming | `UChunkStreamer` | Ring gate, priority load |
| Async chunk I/O | `ProceduralSettings.AsyncChunkIo` | Background save/load, main-thread commit |
| Async chunk generation | `ProceduralSettings.AsyncChunkGeneration` | Defaults off; see tech debt |
| Load scheduler | `UChunkLoadScheduler` | Sync collision ring |
| Movement diagnostics | `movement_diagnostics.json` | Gen/mesh/io breakdown (`movement_diagnostics.v2`) |

## Profiling bisect

1. HUD: `ui.show_performance: true` in `config.json`.
2. Toggle one flag at a time under `render` and `procedural`.
3. Export: `worlds/World_NNN/movement_diagnostics.json`.
4. CI: `python tools/smoke_worldgen_metrics.py --metrics-json <path>`.

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
