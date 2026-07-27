# GPU phase execution (G0→GA land, P* completion)

Desktop GPU after streaming H→CB. Dual-stack: Desktop MDI+GPU progressive;
Android/GLES stays `cpu_greedy` + `cpu_staging` + `cpu_frustum`
(no MDI/compute required).

## Workflow (each phase — mandatory)

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/flight_sim_run.py --world World_164 --teleport-cruise --seconds 130 `
  --fly-stop --fly-phase-sec 45 --stop-phase-sec 60 --idle-sec 8 `
  --phase-id <id> --report bin/phase_<id>.json
python tools/phase_run_record.py --phase <id> --report bin/phase_<id>.json --note "..."
# Analyze metrics in report + perf jsonl; fix regress; re-run if needed
python tools/flight_sim_phase_gate.py --phase-id F2 --report bin/phase_<id>.json
python tools/flight_sim_phase_gate.py --phase-id <P*|G*> --report bin/phase_<id>.json
# GO → git commit (no commit on NO-GO)
```

Unit tests for P* tails (no GL):

```powershell
cmake --build build/desktop-msvc --config Release --parallel 8 `
  --target gpu_skylight_merge_test fluid_surface_pack_reuse_test
./build/desktop-msvc/Release/gpu_skylight_merge_test.exe
./build/desktop-msvc/Release/fluid_surface_pack_reuse_test.exe
# or: pwsh tools/run_gpu_tail_unit_tests.ps1
```

Loop: **code → build → unit tests → autofly → analyze → fix → gates → auto-commit**.
Spike-only C/CB variance with F2 GO → one re-run; do not reopen SoftDefer.

Streaming regress baseline: `bin/phase_cb_pack.json` (spike≤200, wall≤37,
dirty≤450, cold≤3, sticky=0).

## Ladder G0→GA (landed)

| Phase | Goal | Gate id |
|-------|------|---------|
| G0–GA | Bind + wiring + MVP compute tails | G0…GA |

## Completion P* (best-practice)

| Phase | Goal | Gate id |
|-------|------|---------|
| P0 | Docs + P* gates + telemetry hooks | P0 |
| P2 | Cull → compact → MDI (no sync mask→flat-ref) | P2 |
| P3 | Single pool upload (no MapBucket double-copy) | P3 |
| P7 | Fluid PreferGpu dirty-only on Desktop | P7 |
| P6 | Skylight seed apply into lightmap | P6 |
| P5 | GPU face extract → pool VBO | P5 |
| PA | Full F2+P*+GA sign-off | PA |

Order: `P0 → P2 → P3 → P7 → P6 → P5 → PA`.

## Anti-patterns

- Sync `GetBufferSubData` visibility/mesh as “GPU done”
- GL on async mesh/relight workers
- Mid-session mesher/store swap
- Preview mesh before LitReady
- `CullRevision` forcing full geometry Refresh
- MapBucket stage + pool Upload double-copy
- Commit on NO-GO / skip autofly on P2–P7
- MDI/compute required on Android

## Status (honest)

Sign-off report: `bin/phase_P_tails.json` — **F2 + P2 + P3 + P5 + P6 + P7 + PA = GO**
(wall_no_holes≈35.7, sticky=0, pending≈5, fd_end≈209, cull_indirect=1).

Prior baseline: `bin/phase_P2f2.json` (wall_no_holes≈33).

| Phase | Landed | Notes |
|-------|--------|-------|
| P0 | Yes | Docs / gates / telemetry |
| P2 | Yes+ | 1:1 `IndirectCmdsBuffer` + compute compact → `instanceCount`; MultiDraw from GPU table; vis readback **only** on `DrawElementsBaseVertex` fallback (`SyncCompactVisToCpu`) |
| P3 | Yes | Single pool `Allocate` upload |
| P7 | Yes+ | PreferGpu + pack-hash reuse (full slice skip + tops reuse); `fluid_surface_pack_reuse_test` |
| P6 | Yes+ | Sync `RelightChunkCoords` GPU seed (cap 1/batch); **async apply** merges block light via `MergeBlockLightKeepingGpuSky`; `gpu_skylight_merge_test` |
| P5 | Yes+ | Worker defers eligible opaque → main `TryExtractOpaqueToBatches`; deferred + hot path use CPU extract + `MergeOpaqueQuadsStrict` (no mask readback; legacy behind `CUBATARIUM_GPU_MASK_READBACK=1`) |
| PA | Yes | Formal F2+P*+GA on `phase_P_tails` |
| D1 | Yes | Caps/factory split; opaque no mask readback; transparent keys; sky seed CPU parity; force Full on Desktop GPU; `bin/phase_D1.json` F2/D1a–D1d/PA GO |

## D1 ladder

| Phase | Goal | Gate id |
|-------|------|---------|
| D1.0 | Caps probe + mask_readback telemetry | F2+PA |
| D1.5a | Factory-driven fluid bind; `AllowAndroidGpu=false` | factory test + PA |
| D1.1 | Opaque greedy merge, `gpu_mask_readback_med==0` | D1a |
| D1.2 | Transparent sort via AABB/cullSphere keys | D1b |
| D1.3 | Sky seed no readback + blocklight flood hook | D1c |
| D1.4 | Force Full lighting on Desktop GPU stack | D1d |
| D1.6 | Android GPU A0–A4 backlog (opt-in) | AG0…AG4 stubs |
| D1.7 | Docs + PA sign-off | PA |

Order: `D1.0 → D1.5a → D1.1 → D1.2 → D1.3 → D1.4 → D1.7` (+ Android backlog).

```powershell
# D1 units
pwsh tools/run_gpu_tail_unit_tests.ps1
cmake --build build/desktop-msvc --config Release --target render_backend_factory_test
./build/desktop-msvc/Release/render_backend_factory_test.exe

python tools/flight_sim_run.py --world World_164 --teleport-cruise --seconds 130 `
  --fly-stop --fly-phase-sec 45 --stop-phase-sec 60 --idle-sec 8 `
  --phase-id D1d --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id F2 --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id D1a --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id D1b --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id D1c --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id D1d --report bin/phase_D1.json
python tools/flight_sim_phase_gate.py --phase-id PA --report bin/phase_D1.json
```

### Remaining tails (post-P2f2)

| Tail | Fix | Test / gate |
|------|-----|-------------|
| P2 vis sync on hot path | Lazy `SyncCompactVisToCpu` only in fallback draw | F2 wall/sticky; no unit (needs GL) |
| D1.1 mask readback | Hot path CPU extract+merge; env legacy readback | `gpu_mask_readback_med==0` / D1a |
| P6 async skylight + block merge | `include_skylight` + `MergeBlockLightKeepingGpuSky` | `gpu_skylight_merge_test` |
| P7 full slice cache | `has_slice` + pack-hash hit returns cached slice | `fluid_surface_pack_reuse_test` |

Desktop: `gpu_greedy` / `mdi_vertex_pool` / `gpu_frustum` + PreferGpu fluid;
`backend_lighting_mode` is `gpu_full`/`full` (never `flat` on GPU stack).
