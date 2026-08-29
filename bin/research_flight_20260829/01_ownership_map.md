# 01 — Ownership map (flight perf iteration A→C)

| Subsystem | Owner | Consumers |
| --- | --- | --- |
| FM dirty enqueue | MarkRelitInstall, ColumnFlow FirstMesh | ChunkMeshCache FirstMeshQ |
| Witness pin | WorldStreaming SoftDeferCapturePin | ColumnFlow RequestPromoteRelight |
| Admission mode | MeshWorkAdmission | ChunkEmergeCoordinator, ChunkMeshCache |
| Relight FIFO | WorldPersistence | DrainRelightQueues |
| VB consume | ColumnFlow TickDerived | RelightThenMesh tickets |

## MarkDirty ownership (FP-C1 lint)

Allowed direct MarkDirty paths: `tools/lint_mark_dirty_ownership.py`.
