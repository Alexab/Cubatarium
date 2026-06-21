# Tech debt: Chunk streaming & performance

> Review at end of each phase (A–E). Close items when implemented or explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-CS-010 | 2026-06 | Async meshing defaults off; needs in-game validation | Enable via `Render.AsyncMeshing` after manual fly-through | backlog |
| TD-CS-011 | 2026-06 | Async chunk generation defaults off | Enable via `ProceduralSettings.AsyncChunkGeneration` | backlog |
| TD-CS-012 | 2026-06 | Async chunk I/O defaults off | Enable via `ProceduralSettings.AsyncChunkIo` | backlog |
| TD-CS-013 | 2026-06 | CI smoke uses synthetic latency budgets only | Hook real MovementDiagnostics export | backlog |

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
