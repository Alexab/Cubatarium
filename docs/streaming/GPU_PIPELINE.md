# GPU pipeline contracts (Cubatarium)

Init-time binding of mesher / mesh GPU store / cull / fluid backends.
No mid-session CPU↔GPU fallback for the same contract.

## Interfaces

| Interface | CPU | GPU |
|-----------|-----|-----|
| `IUChunkMesher` | `UCpuGreedyMesher` | `UGpuGreedyMesher` — opaque-solid face-mask compute + CPU greedy fallback |
| `IUMeshGpuStore` | `UCpuStagingGpuStore` | `UMdiVertexPoolStore` — live `MapBucket` + pool `glMapBufferRange` |
| `IUChunkCull` | `UCpuFrustumCull` | `UGpuFrustumCull` — SSBO spheres + compute visibility → flat refs |
| `IULightingPipeline` | Full / Flat | `UGpuFullLightingPipeline` — skylight column-seed compute warm; Full BFS still CPU |
| `IUFluidSurfaceProvider` | `UCpuFluidSurfaceMap` | `UGpuFluidSurfaceMap` — compute column top-Y scan when prefer-GPU |

Factory: `URenderBackendFactory::BindOnce` / `Select` — second BindOnce rejected.
Lighting: `ULightingPipelineFactory` binds GpuFull on Desktop.

## Init matrix

| Platform | Caps | Bound stack |
|----------|------|-------------|
| Desktop | MDI + compute | `gpu_greedy` + `mdi_vertex_pool` + `gpu_frustum` + GpuFull light + Gpu fluid provider |
| Desktop | force-cpu | CPU mesher + staging + CPU cull |
| Android / GLES | | CPU mesher + staging + CPU cull + CpuFull light + Cpu fluid |

## Edit remesh

`EvaluateEditMeshRemesh` (`EditMeshRemeshPolicy`):
- Default: Immediate cap 9 for face/light ring hybrid async.
- `PreferGpuStorePatch`: only center Immediate; ring → Dirty (upload/MDI path).

`UGeometryEngine::EnsureRenderBackendsBound` sets
`WorldMeshService::SetPreferGpuStorePatch(store->SupportsMultiDrawIndirect())`
so desktop MDI bind automatically enables the lighter edit remesh policy.

## Vertex pool (TD-CS-016)

`UGreedyVertexPool`: grow-only GL buffers + free-list `Free`/`Allocate` reuse;
Desktop uploads via `glMapBufferRange` (SubData fallback).

Desktop MDI draw (`UMdiVertexPoolStore`): local indices stay 0-based per batch;
`baseVertex` = `vboByteOffset / sizeof(GreedyMeshVertex)`. Opaque/cutout refs
are sorted by `blockId` before pool refresh so same-texture batches are
contiguous. `RefreshPassRefs` stages concatenated verts through `MapBucket`.
Telemetry `gpu_draw_cmds` counts API submits (MultiDraw or DrawElements).

## Compute MVP notes

- **G2 cull:** SSBO spheres + compute visibility for ≤384 entries; larger rings
  use CPU Delegate (sync readback not viable at full ring size yet).
- **G5 mesher:** padded occupancy face-mask extract on main GL thread only
  (async workers stay CPU greedy); opaque-solid-only chunks.
- **G6 light:** one-shot skylight column-seed compute warm; Full BFS remains CPU.
- **G7 fluid:** compute column scan available via `PreferGpuFluidColumnScan`
  (off by default on hot path); CPU `FindFluidColumnSurfaceAt` default.

## Telemetry

`backend_mesher`, `backend_store`, `backend_cull`, `gpu_draw_cmds`,
`gpu_cull_ms`, `vertex_pool_fill` in `perf_*.jsonl`.

## Tests

- `edit_mesh_remesh_policy_test`
- `render_backend_factory_test`
- `mesh_gpu_store_mdi_test`
- `gpu_greedy_face_extract_test`
- `gpu_skylight_column_seed_test`
- `gpu_fluid_column_scan_test`

## Phase ladder

Execution, autofly gates, and dual-stack Android rules:
[`GPU_PHASE_EXECUTION.md`](GPU_PHASE_EXECUTION.md). Analyze emits
`backend_*_mode`, `backend_store_mdi`, `gpu_draw_cmds_med`, `gpu_cull_ms_med`,
`vertex_pool_fill_med` for G0–GA gates in `flight_sim_phase_gate.py`.
