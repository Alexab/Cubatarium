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
- G1–GA landed on `opt_3d` (`g_ladder_land`): Desktop backends
  `gpu_greedy` / `mdi_vertex_pool` / `gpu_frustum`; `gpu_cull_ms` med≈0.09;
  `gpu_draw_cmds` med≈8; **F2 GO**; G1–G7+GA **GO**. CB spike/wall may still
  show variance vs `cb_pack` — streaming reference unchanged; do not reopen
  SoftDefer. Android: factory keeps CPU mesher/cull + staging (tested).
