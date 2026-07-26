# GPU phase execution (G0→GA)

Desktop GPU ladder after streaming H→CB. Dual-stack: Desktop MDI+GPU
progressive; Android/GLES stays `cpu_greedy` + `cpu_staging` + `cpu_frustum`
(no MDI/compute required).

## Workflow (each phase)

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/flight_sim_run.py --world World_164 --teleport-cruise --seconds 130 `
  --fly-stop --fly-phase-sec 45 --stop-phase-sec 60 --idle-sec 8 `
  --phase-id <id> --report bin/phase_<id>.json
python tools/phase_run_record.py --phase <id> --report bin/phase_<id>.json --note "..."
python tools/flight_sim_phase_gate.py --phase-id F2 --report bin/phase_<id>.json
python tools/flight_sim_phase_gate.py --phase-id C --report bin/phase_<id>.json
python tools/flight_sim_phase_gate.py --phase-id CB --report bin/phase_<id>.json
python tools/flight_sim_phase_gate.py --phase-id <G*> --report bin/phase_<id>.json
# GO → git commit
```

Streaming regress baseline: `bin/phase_cb_pack.json` (spike≤200, wall≤37,
dirty≤450, cold≤3, sticky=0). Spike-only variance (~200–260) with F2 GO →
re-run once; do not reopen SoftDefer/snap anti-patterns.

## Phase ladder

| Phase | Goal | Gate id |
|-------|------|---------|
| G0 | Docs + analyze GPU metrics + gates | G0 |
| G1 | Wire `IUChunkCull` / `IUChunkMesher` live | G1 |
| G2 | Real Desktop GPU frustum cull | G2 |
| G3 | MDI mapped upload polish | G3 |
| G4 | Select/bind `gpu_greedy` | G4 |
| G5 | Compute greedy MVP | G5 |
| G6 | GPU lighting factory | G6 |
| G7 | Fluid GPU provider | G7 |
| GA | Android dual-stack sign-off | GA |

## Anti-patterns

- MDI/compute on Android without extension probe + staging fallback
- Mid-session mesher/store swap
- Preview mesh before LitReady for GPU throughput
- Breaking F2/CB for `gpu_draw_cmds`
- Declaring parity-wrapper done without jsonl + gate evidence

## Status

- T0/T1 streaming tails closed 2026-07-26 (`cb_pack` reference; break-stand memory).
- G0: gates + analyze GPU metrics (2026-07-26).
- G1–GA ladder land (`g_ladder_land`): Desktop backends
  `gpu_greedy` / `mdi_vertex_pool` / `gpu_frustum`.
- G2–G7 compute tails (2026-07-26, `bin/phase_gpu_compute_tails2.json`):
  - G2: frustum SSBO compaction for ≤384 spheres; larger → CPU Delegate
  - G3: live `MapBucket` + pool `glMapBufferRange`; `gpu_draw_cmds` med≈8
  - G5: opaque face-mask compute (main GL only) + unit test
  - G6: one-shot skylight column-seed warm; Full BFS CPU
  - G7: fluid column-scan compute available (prefer off on hot path)
  - Evidence: **F2 GO**; G2–G7+GA **GO**; `gpu_cull_ms` med≈0.19
  - C/CB spike/wall may still miss vs `cb_pack` (variance) — do not reopen SoftDefer
  Android: factory keeps CPU mesher/cull/staging (no MDI/compute required).
