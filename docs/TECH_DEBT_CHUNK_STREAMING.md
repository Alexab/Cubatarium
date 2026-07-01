# Tech debt: Chunk streaming & performance

> Review at end of each phase (A–E). Close items when implemented or explicitly wont-fix.

## Profiling bisect

1. Enable HUD: `ui.show_performance: true` in `config.json`.
2. Watch `Gen`, `Mesh`, `Flat`, `Dirty`, `Cache` lines while flying through forest / after load.
3. Toggle flags one at a time in `render`: `async_meshing`, `greedy_meshing`, `frustum_culling`, `distance_fog`, `altitude_adaptive_fog`.
4. Toggle `procedural.async_chunk_generation`, `procedural.async_chunk_io`, `max_chunk_commits_per_frame`, `max_load_ops_per_frame`.
5. Export: `worlds/World_NNN/movement_diagnostics.json` (schema `movement_diagnostics.v2`).
6. CI: `python tools/smoke_worldgen_metrics.py --metrics-json <path>`.

**Async defaults (2026-06):** `ProceduralSettings.AsyncChunkGeneration` and `AsyncChunkIo` default **true** in code and `bin/config.json`. TD-CS-011/012 doc drift corrected; headless worldgen CLI still forces sync generation for determinism.

**Ring gate (TD-CS-017):** `procedural.ring_gate_enabled` in `config.json` (default **false**). Applied on streamer creation in `UWorldStreaming::EnsureStreamer` and when settings refresh via `RefreshStreamerSettings` → `UChunkStreamer::SetRingGateEnabled`.

**MarkBlockChunkDirty contract (TD-AUD-013):** when `BlockRegistry != nullptr` (normal gameplay), block edits call `MeshCache.RebuildChunkImmediate` on the chunk + neighbors; during headless load before registry init, `MarkDirty` defers rebuild to the frame budget / async mesh path.

**In-game validation (async mesh):** with `ui.show_performance: true`, fly through loaded terrain; toggle `render.async_meshing` in `config.json` and compare `mesh_rebuild_ms` / hitch lines in HUD. Export `movement_diagnostics.v2` before/after — each sample includes `async_meshing_enabled` for bisect correlation.

**Cross vegetation (TD-CS-014):** cross blocks store per-chunk centers in `ChunkMeshCache`; flat merge builds `CrossInstanceBatch[]`; `GeometryEngine` draws via `UCrossGpuBackend` (one `glDrawElementsInstanced` per block type).

**Perf regression fix (2026-07):** `RebuildDirtyChunks` no longer marks flat batches dirty every frame; GPU backends use `glBufferSubData` when buffer capacity suffices (no per-frame orphan); render uses single `PrepareGreedyDraw` per frame.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-CS-010 | 2026-06 | ~~Async meshing defaults off; needs in-game validation~~ | Default **on** (`RenderSettings.AsyncMeshing`); bisect in profiling section; `movement_diagnostics.v2` exports `async_meshing_enabled` | closed |
| TD-CS-011 | 2026-06 | ~~Async chunk generation defaults off~~ | Defaults **on** since 2026-06 (`ProceduralSettings`, `config.json`) | closed |
| TD-CS-012 | 2026-06 | ~~Async chunk I/O defaults off~~ | Defaults **on** since 2026-06 | closed |
| TD-CS-014 | 2026-06 | ~~GPU instancing for Cross vegetation~~ | `UCrossGpuBackend` + `CrossInstanceBatch`; one instanced draw per block type | closed |
| TD-CS-016 | 2026-06 | Persistent GPU VBO / vertex pooling | Nick McDonald-style pool; large refactor | backlog |
| TD-CS-017 | 2026-06 | Ring gate tuning / configurable disable | `procedural.ring_gate_enabled` in config | closed |
| TD-CS-018 | 2026-06 | Incremental frustum-only greedy cull without full flat merge | camera-chunk skip + `LastVisibleChunks` cache; full incremental cull deferred | partial |
| TD-CS-019 | 2026-06 | Sky horizon fog uses fixed band; true radial sky fog optional | horizon blend shipped | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-CS-001 | 2026-06 | `MarkDirty` bumps revision only when chunk newly enters dirty set |
| TD-CS-002 | 2026-06 | Deferred `AccumulateDirtyColumn` + chunk-slice `MarkDirtyColumn` iteration |
| TD-CS-003 | 2026-06 | `MovementDiagnostics` exposes gen/mesh/io/dirty breakdown |
| TD-CS-004 | 2026-06 | `UAsyncMeshBuilder` + `RenderSettings.AsyncMeshing` |
| TD-CS-005 | 2026-06 | `UChunkLoadScheduler` + sync collision ring (`forceSync`) |
| TD-CS-006 | 2026-06 | `UAsyncChunkIO` background save/load with main-thread commit |
| TD-CS-007 | 2026-06 | Cave carve early-continue + chunk-level cave density gate in populator |
| TD-CS-008 | 2026-06 | `MeshCache` removed from `WorldGenContext`; dirty on commit/streamer |
| TD-CS-009 | 2026-06 | `UChunkGenerationRegistry` + token validation on async results |
| TD-CS-013 | 2026-06 | Smoke reads `movement_diagnostics.v2` with flat/count/backlog budgets |
| TD-CS-015 | 2026-06 | Cooperative cancel in `PipelineChunkPopulator` via `shouldCancel` token check |
| TD-CS-014 | 2026-07 | `UCrossGpuBackend` retained instance VBO + `glDrawElementsInstanced` per block type |
| TD-CS-010 | 2026-07 | Async meshing default on; bisect documented; diagnostics export `async_meshing_enabled` |
| TD-CS-020 | 2026-06 | Dedupe mesh dirty on async chunk commit (`ColumnMeshDirty` only) |

## Phase tracker

| Phase | Status | Last commit |
|-------|--------|-------------|
| Setup | done | docs: add chunk streaming tech debt tracker |
| A | done | feat(diag): add gen/mesh/io timing breakdown |
| B | done | feat(mesh): integrate async mesh builder |
| C | done | feat(streaming): wire scheduler into chunk streamer |
| D | done | feat(io): async chunk save on unload |
| E | done | perf(mesh): skip empty sky layers in greedy mesher |
| Metrics | done | test: extend smoke_worldgen_metrics with latency budgets |
| Perf2 | done | perf/streaming: mesh warmup, priority, altitude fog, cross cutout |
