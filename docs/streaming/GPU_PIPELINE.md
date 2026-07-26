# GPU pipeline contracts (Cubatarium)

Init-time binding of mesher / mesh GPU store / cull / fluid backends.
No mid-session CPU↔GPU fallback for the same contract.

## Tiers

| Tier | Meaning |
|------|---------|
| **G0–GA** | Ladder land: Desktop bind + hybrid MVP compute |
| **P\*** | Best-practice completion: cull→MDI without sync flat-ref; single upload; light/fluid/mesh on GPU hot path |
| **D1** | Follow-up: full GPU-driven (no CPU flat refs), greedy merge, blocklight flood |

## Interfaces

| Interface | CPU | GPU (Desktop) |
|-----------|-----|---------------|
| `IUChunkMesher` | `UCpuGreedyMesher` | `UGpuGreedyMesher` |
| `IUMeshGpuStore` | `UCpuStagingGpuStore` | `UMdiVertexPoolStore` |
| `IUChunkCull` | `UCpuFrustumCull` | `UGpuFrustumCull` |
| `IULightingPipeline` | Full / Flat | `UGpuFullLightingPipeline` |
| `IUFluidSurfaceProvider` | `UCpuFluidSurfaceMap` | `UGpuFluidSurfaceMap` |

Factory: `URenderBackendFactory::BindOnce` / `Select`. Lighting via
`ULightingPipelineFactory`.

## Init matrix

| Platform | Bound stack |
|----------|-------------|
| Desktop | `gpu_greedy` + `mdi_vertex_pool` + `gpu_frustum` + GpuFull + Gpu fluid |
| Desktop force-cpu | CPU mesher + staging + CPU cull |
| Android / GLES | CPU mesher + staging + CPU cull + CpuFull + Cpu fluid |

## Best-practice rules (P*)

1. No sync visibility/mesh readback on cruise hot path.
2. No GL on job-pool mesh/relight workers.
3. `CullRevision` must not force full geometry `RefreshPassRefs`.
4. One write path into pool VBO/EBO.
5. SoftDefer / LitReady unchanged.
6. Transparent/Cross may stay CPU until D1.

## Telemetry

`backend_mesher/store/cull/fluid`, `gpu_draw_cmds`, `gpu_cull_ms`,
`vertex_pool_fill`, `gpu_cull_indirect`, `gpu_mesh_vbo_dispatch`,
`gpu_light_seed_apply`, `gpu_fluid_scan_on` in `perf_*.jsonl`.

Analyze emits `*_med` / `backend_*_mode` for gates in
`flight_sim_phase_gate.py`.

## Phase execution

[`GPU_PHASE_EXECUTION.md`](GPU_PHASE_EXECUTION.md) — mandatory
autofly → analyze → fix → GO → auto-commit per phase.

## Tests

- `edit_mesh_remesh_policy_test`, `render_backend_factory_test`,
  `mesh_gpu_store_mdi_test`
- `gpu_greedy_face_extract_test`, `gpu_skylight_column_seed_test`,
  `gpu_fluid_column_scan_test`
