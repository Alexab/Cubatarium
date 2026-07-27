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
| P5 | Yes+ | Worker defers eligible opaque → main `TryExtractOpaqueToBatches`; deferred path uses **CPU extract** (no mask readback). Hot path mask readback interim (D1) |
| PA | Yes | Formal F2+P*+GA on `phase_P2f2` — re-run after tail land |
| D1 | Backlog | No CPU flat refs / transparent GPU sort / greedy merge / blocklight flood / SSBO→VBO without mask readback |

### Remaining tails (post-P2f2)

| Tail | Fix | Test / gate |
|------|-----|-------------|
| P2 vis sync on hot path | Lazy `SyncCompactVisToCpu` only in fallback draw | F2 wall/sticky; no unit (needs GL) |
| P5 mask readback on non-deferred extract | `deferred_no_gpu_readback` → `ExtractOpaqueFacesCpu` | `gpu_greedy_face_extract_test` (CPU ref) |
| P6 async skylight + block merge | `include_skylight` + `MergeBlockLightKeepingGpuSky` in `DrainAsyncRelightResults` | `gpu_skylight_merge_test` |
| P7 full slice cache | `has_slice` + pack-hash hit returns cached slice | `fluid_surface_pack_reuse_test` |

Desktop: `gpu_greedy` / `mdi_vertex_pool` / `gpu_frustum` + PreferGpu fluid.
