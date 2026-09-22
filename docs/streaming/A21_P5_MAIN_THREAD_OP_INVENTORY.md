# A21 P5 main-thread indivisible op inventory

| Op | Location | Notes |
|---|---|---|
| Capture | MeshCaptureStore::CaptureAndStore | snapshot bytes credit |
| IncrementalShell refresh | MeshCaptureStore::RefreshIncrementalShell | must transfer credit (P1.3) |
| Light install | MarkRelitInstall / LitApply | stage timestamps via JobStageTrace |
| Emerge/generation commit | ChunkEmergeCoordinator | |
| Upload | GreedyGpuPublication | per-pass transaction |
| Transparent sort | GeometryEngine | order key required (P1.1) |
| Fluid surface | FluidSurfaceMap / ColumnSlice | P6 async target |
| Retirement | Vertex pool fence | allocation generation |

Critical unit after deadline must be measured-bounded quantum (`FrameDeadline`).
Work slots: extend beyond AsyncMeshBuilder alone.
