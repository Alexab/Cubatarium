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

Loop: **code → build → autofly → analyze → fix → gates → auto-commit**.
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

## Status

- G0–GA ladder landed; P* completion closed (`P0→P2→P3→P7→P6→P5→PA`).
- Desktop: `gpu_greedy` / `mdi_vertex_pool` / `gpu_frustum` + PreferGpu fluid;
  opaque cull via instanceCount (no CullRevision geometry refresh).
- D1 backlog: full GPU-driven (no CPU flat refs), transparent GPU sort,
  greedy merge, blocklight flood, SSBO→VBO without mask readback.
