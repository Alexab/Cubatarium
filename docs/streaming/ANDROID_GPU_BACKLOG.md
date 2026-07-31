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

### Lighting seed (E4 / TD-ARCH-013)

`PreferGpuLightingSeed` is **false** on Android/GLES → commit seed uses
`CpuLightingSeedBackend` with the same streaming contracts as desktop CPU.
Desktop Gpu seed = `ApplyGpuSkylightSeedToChunk` (not full Relight).

**TD-ARCH-013b (backlog):** real GLES compute skylight seed when
`AllowAndroidGpu && HasCompute` — until desktop F2/C/CB GO and host autofly
stable. CPU seed contracts remain the Android path (TD-ARCH-013 closed).
Smoke (device optional): `python tools/android_gpu_phase_run.py --phase-id AG0 --skip-device`

Allowlist: `assets/config/android_gpu.json` (synced into APK as `config/` via
`syncAssets` + AssetExtractor whitelist).

A2 hybrid path: GLES face-mask SSBO (`shaders/gles/compute/face_mask_extract.comp`)
→ decode → `MergeOpaqueQuadsStrict` on pack/main thread; CPU extract fallback.
Desktop `UGpuGreedyMesher` stays desktop-only (caps Platform check).

Device runner: without `--skip-device`, `android_gpu_phase_run.py` does
`adb install` + launch smoke (+ logcat). Full AG1–AG4 cruise still needs an
on-device flight_sim harness (desktop cruise covers F2/AG0 probe metrics).
