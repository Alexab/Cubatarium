# Android GPU backlog (A0–A4)

Desktop D1 lands first. Android stays CPU-bound by default
(`AllowAndroidGpu=false` in `DetectRenderBackendCaps` / factory Select).

| Phase | Goal | Caps / entry |
|-------|------|--------------|
| A0 | Capability audit on devices | Real `DetectRenderBackendCaps` probe |
| A1 | PreferGpu fluid column scan (GLES compute) | `HasCompute` + `HasSsbo`; fork `GpuFluidColumnScan` → `#version 310 es` |
| A2 | Hybrid mesher (GPU extract, batched submit, no MDI) | `UAndroidGpuGreedyMesher` : `IUChunkMesher` |
| A3 | Transparent sort keys + existing GLES single-pass | Reuse D1.2 keys + `DrawGlesTransparentSinglePass` |
| A4 | Opt-in full Android GPU bundle | Device allowlist; `AllowAndroidGpu=true` |

Gate stubs: `AG0`…`AG4` in `tools/flight_sim_phase_gate.py` (no Desktop CB thresholds).

Factory contract: without `AllowAndroidGpu`, Android + compute still selects CPU
mesher/cull/staging — covered by `render_backend_factory_test`.
