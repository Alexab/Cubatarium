# Android GPU backlog (A0–A4) — GPF6

Desktop D1/GPF0–GPF5 lands first. Android uses **GPU-by-default** when
probe + allowlist succeed; users may **opt out** via
`render.android_gpu_enabled=false` (Settings → Graphics on Android).

| Phase | Goal | Caps / entry |
|-------|------|--------------|
| A0 | Capability audit + policy | `ProbeOpenGLRenderBackendCaps` + `ApplyAndroidGpuPolicy` |
| A1 | Fluid column scan (GLES compute) | `HasCompute` + `HasSsbo` + `AllowAndroidGpu` |
| A2 | Hybrid mesher (defer extract + staging) | `UAndroidGpuGreedyMesher` : `IUChunkMesher` |
| A3 | Transparent sort keys + single-pass | Caps `PreferSinglePassTransparent` |
| A4 | Production rollout | Allowlist + UI toggle (default ON) |

Gates: `AG0`…`AG4` in `tools/flight_sim_phase_gate.py`.

Runner: `python tools/android_gpu_phase_run.py --phase-id AGx --report bin/phase_AGx.json [--skip-device] [--commit]`

Factory: without `AllowAndroidGpu`, Android selects CPU mesher/cull/staging —
covered by `render_backend_factory_test` + `android_gpu_policy_test`.

Allowlist: `assets/config/android_gpu.json` (also under Android APK assets).
