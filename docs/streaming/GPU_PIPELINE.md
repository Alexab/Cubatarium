# GPU pipeline contracts (Cubatarium)

Init-time binding of mesher / mesh GPU store / cull / fluid backends.
No mid-session CPU↔GPU fallback for the same contract.

## Interfaces

| Interface | CPU | GPU |
|-----------|-----|-----|
| `IUChunkMesher` | `UCpuGreedyMesher` | `UGpuGreedyMesher` (parity wrap; compute later) |
| `IUMeshGpuStore` | `UCpuStagingGpuStore` | `UMdiVertexPoolStore` |
| `IUChunkCull` | `UCpuFrustumCull` | `UGpuFrustumCull` (delegates CPU rebuild) |
| `IULightingPipeline` | Full / Flat (`UCpuFullLightingPipeline` alias) | `UGpuFullLightingPipeline` (delegate; factory not switched yet) |
| `IUFluidSurfaceProvider` | `UCpuFluidSurfaceMap` | `UGpuFluidSurfaceMap` (CPU map until compute scan) |

Factory: `URenderBackendFactory::BindOnce` / `Select` — second BindOnce rejected.

## Init matrix

| Platform | Caps | Bound stack |
|----------|------|-------------|
| Desktop | MDI | CPU mesher + `mdi_vertex_pool` + GPU cull wrapper |
| Desktop | force-cpu | CPU mesher + staging + CPU cull |
| Android / GLES | | CPU mesher + staging + CPU cull |

## Edit remesh

`EvaluateEditMeshRemesh` (`EditMeshRemeshPolicy`):
- Default: Immediate cap 9 for face/light ring hybrid async.
- `PreferGpuStorePatch`: only center Immediate; ring → Dirty (upload/MDI path).

`UGeometryEngine::EnsureRenderBackendsBound` sets
`WorldMeshService::SetPreferGpuStorePatch(store->SupportsMultiDrawIndirect())`
so desktop MDI bind automatically enables the lighter edit remesh policy.

## Vertex pool (TD-CS-016)

`UGreedyVertexPool`: grow-only GL buffers + free-list `Free`/`Allocate` reuse.

Desktop MDI draw (`UMdiVertexPoolStore`): local indices stay 0-based per batch;
`baseVertex` = `vboByteOffset / sizeof(GreedyMeshVertex)`. Opaque/cutout refs
are sorted by `blockId` before pool refresh so same-texture batches are
contiguous. Attribs bind at pool origin once; one
`glMultiDrawElementsIndirect` per texture group. Telemetry `gpu_draw_cmds`
counts API submits (MultiDraw or DrawElements), not per-batch indirect cmds.

## Telemetry

`backend_mesher`, `backend_store`, `backend_cull`, `gpu_draw_cmds`,
`gpu_cull_ms`, `vertex_pool_fill` in `perf_*.jsonl`.

## Tests

- `edit_mesh_remesh_policy_test`
- `render_backend_factory_test`
- `mesh_gpu_store_mdi_test`

## Phase ladder

Execution, autofly gates, and dual-stack Android rules:
[`GPU_PHASE_EXECUTION.md`](GPU_PHASE_EXECUTION.md). Analyze emits
`backend_*_mode`, `backend_store_mdi`, `gpu_draw_cmds_med`, `gpu_cull_ms_med`,
`vertex_pool_fill_med` for G0–GA gates in `flight_sim_phase_gate.py`.
