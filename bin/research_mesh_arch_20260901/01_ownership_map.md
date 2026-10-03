# Ownership map (post M4)

| Stage | Owner |
| --- | --- |
| Capture store | `UMeshCaptureStore` + `UMeshCaptureWorker` drain |
| Schedule / Dirty | `UChunkMeshCache::RebuildDirtyChunksWithStats` |
| FirstMesh admission | `UColumnFlowExecutor` + `ColumnFlowMeshOwnership` guard |
| GPU kick/finish | `UChunkMeshCache::ProcessPendingGpuMeshes` |
| Pool upload | `UGreedyVertexPool` per-frame unsync budget |
| Draw | `GeometryEngine` MDI path |

Parallel `MarkDirty` from SoftDefer empty ownership blocked when ColumnFlow owns FirstMesh (`BlockParallelMarkDirtyForColumnFlow`).
