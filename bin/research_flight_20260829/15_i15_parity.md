# I15 parity table (debt + perf)

Gate of record: FP-manual, manual no-teleport World_164 ≥3 min.

| Metric | B0 `081522` | B1 `150840` | B2 `194104` | B3 `075308` | I15 target |
|--------|-------------|-------------|-------------|-------------|------------|
| `wall_ms_fly_med` | ~110 | 110 | 109 | 97 | ≤90 |
| `stream_ms` | ~83 | 83 | 84 | 84 | ≤75 |
| `prep_refresh_pressure_ms` | ~39 | 39 | 44 | 40 | ≤30 |
| `prep_gap_honest_med` | n/a | n/a | n/a | ~26* | ≤12 |
| `mesh_emerge_ms` | 15 | 15 | 36 | 39 | ≤25 |
| `mesh_emerge_prep_other_ms` | 9 | 9 | 32 | 35 | ≤12 |
| `emerge_prep_other_share` | ~0.6 | ~0.6 | ~0.9 | ~0.9 | ≤0.35 |
| `relight_drain_ms` | ~1.7 | 1.7 | 1.9 | 1.9 | ≤2.5 |
| `post_stop_visible_black_max` | 97 | 95 | 89 | 97 | ≤50 step / ≤20 stretch |
| `relight_drain_near_zero_while_vb_sec` | 0 | 2 | 22 | 22 | ≤10 |
| `chain_stall_sec` | TBD | TBD | TBD | TBD | ≤15 |
| `opaque_idle_churn_max` | ~190 | 260 | 450 | 447 | ≤120 |
| `miss_stuck_max_run_sec` | 84 | 110 | 192 | 216 | ≤60 |
| `visual_holes_telemetry_mismatch_frames` | — | — | — | 451 | −50% |
| `EH_blink` | — | 0.038 | 0.011 | 0.028 | ≤0.05 |
| `schedule_ok_zero_rate` | ~18% | 18% | 59% | 28% | ≤28% |

\* B3 gap before I15-I exclusive accounting was ~38 ms tagged; honest gap subtracts ring/vb_raw.

## Bisect order

1. `0+G` — waterfall / analyze forensics
2. `I` — RefreshStreamingPressure gap fix + cadence
3. `H` — emerge prep_other accounting + SoftDefer diet
4. `A` — relight chain throughput
5. `B` — stop VB tail drain
6. `C+D` — churn + miss SLA
7. `E+F+J` — full FP-manual

## Commands

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium miss_first_mesh_class_test
python tools/flight_sim_analyze.py bin/logs/perf_<ts>.jsonl --manual-idle --report bin/suite_reports/manual_<ts>_analyze.json
python tools/flight_sim_phase_gate.py --phase-id FP-manual --report bin/suite_reports/manual_<ts>_analyze.json
python bin/research_flight_20260829/scripts/stream_waterfall.py bin/logs/perf_20260831-075308_14752.jsonl bin/logs/perf_<ts>.jsonl
```
