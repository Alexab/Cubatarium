# GPU pipeline contracts (Cubatarium)

Init-time binding of mesher / mesh GPU store / cull / fluid backends.
No mid-session CPU↔GPU fallback for the same contract.

## Tiers

| Tier | Meaning |
|------|---------|
| **G0–GA** | Ladder land: Desktop bind + hybrid MVP compute |
| **P\*** | Best-practice completion: cull→MDI without sync flat-ref; single upload; light/fluid/mesh on GPU hot path |
| **D1** | Caps-driven Desktop completion: no mask readback, greedy merge, transparent cullSphere keys, force Full lighting |
| **GPF6** | Android GPU-by-default (probe + allowlist); user opt-out via `android_gpu_enabled` |

## Interfaces

| Interface | CPU | GPU (Desktop) | GPU (Android effective) |
|-----------|-----|---------------|-------------------------|
| `IUChunkMesher` | `UCpuGreedyMesher` | `UGpuGreedyMesher` | `UAndroidGpuGreedyMesher` |
| `IUMeshGpuStore` | `UCpuStagingGpuStore` | `UMdiVertexPoolStore` | `UCpuStagingGpuStore` |
| `IUChunkCull` | `UCpuFrustumCull` | `UGpuFrustumCull` | `UCpuFrustumCull` |
| `IULightingPipeline` | Full / Flat | `UGpuFullLightingPipeline` | `UFullLightingPipeline` |
| `IUFluidSurfaceProvider` | `UCpuFluidSurfaceMap` | `UGpuFluidSurfaceMap` | `UGpuFluidSurfaceMap` |

Factory: `DetectRenderBackendCaps` → `ProbeOpenGLRenderBackendCaps` →
`ApplyAndroidGpuPolicy` → `URenderBackendFactory::BindOnce` / `Select`
(+ `CreateFluidSurfaceProvider(caps)`). Lighting via `ULightingPipelineFactory`.

Android: `AllowAndroidGpu` is **true by default** when probe + allowlist pass;
set `render.android_gpu_enabled=false` to opt out (restart world).

## Init matrix

| Platform | Bound stack |
|----------|-------------|
| Desktop | `gpu_greedy` + `mdi_vertex_pool` + `gpu_frustum` + GpuFull + Gpu fluid |
| Desktop force-cpu | CPU mesher + staging + CPU cull |
| Android / GLES (opt-out or probe fail) | CPU mesher + staging + CPU cull + CpuFull + Cpu fluid |
| Android + effective GPU | `android_gpu_hybrid` + staging + CPU cull + CpuFull + Gpu fluid |

## Best-practice rules (P* / D1 / GPF6)

1. No sync visibility/mesh mask readback on cruise hot path (`gpu_mask_readback_med==0`).
2. No GL on job-pool mesh/relight workers.
3. `CullRevision` must not force full geometry `RefreshPassRefs`.
4. One write path into pool VBO/EBO.
5. SoftDefer / LitReady unchanged.
6. Transparent sort keys from AABB/cullSphere; GLES keeps single-pass draw (`PreferSinglePassTransparent`).
7. Desktop GPU stack never binds `UFlatLightingPipeline` (Performance preset → Full).

## Telemetry

`backend_mesher/store/cull/fluid/lighting_mode`, `gpu_draw_cmds`, `gpu_cull_ms`,
`vertex_pool_fill`, `gpu_cull_indirect`, `gpu_mesh_vbo_dispatch`,
`gpu_light_seed_apply`, `gpu_mask_readback`, `gpu_blocklight_flood`,
`gpu_fluid_scan_on`, `caps_probe_completed`, `caps_has_compute`,
`android_gpu_user_pref`, `android_gpu_effective`, `android_gpu_deny_reason`,
`gl_version`, `gl_renderer`
in `perf_*.jsonl`.

Analyze emits `*_med` / `backend_*_mode` for gates in
`flight_sim_phase_gate.py` (D1a–D1d, AG0–AG4, F2/PA).

## Phase execution

[`GPU_PHASE_EXECUTION.md`](GPU_PHASE_EXECUTION.md) — mandatory
autofly → analyze → fix → GO → auto-commit per phase.

Android backlog: [`ANDROID_GPU_BACKLOG.md`](ANDROID_GPU_BACKLOG.md).
Execution backlog: [`GPU_FULL_BRANCH_BACKLOG.md`](GPU_FULL_BRANCH_BACKLOG.md).

## Tests

- `edit_mesh_remesh_policy_test`, `render_backend_factory_test`,
  `android_gpu_policy_test`, `mesh_gpu_store_mdi_test`
- `gpu_greedy_face_extract_test` (incl. `MergeOpaqueQuadsStrict`),
  `gpu_skylight_column_seed_test`, `gpu_fluid_column_scan_test`,
  `gpu_skylight_merge_test`, `fluid_surface_pack_reuse_test`
