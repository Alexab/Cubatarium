# 06 — Architecture options (post iteration A→C)

## Selected: V4 slice (ColumnFlow + job graph)

- `ColumnJobGraph.h` stages
- Per-column `ColumnJobStage` in ColumnFlowExecutor
- `ColumnVisualSnapshot` for per-epoch scan (header)

## Deferred

- Full deprecate Admit/Recover/Refresh zoo
- Dirty as pure job projection in ChunkMeshCache
