# 02 — Baseline long SoT (perf_opt15)

| Log | Role | Status |
| --- | --- | --- |
| `perf_20260829-081522_22660.jsonl` | manual short pre-fix | baseline |
| `perf_20260829-142846_30244.jsonl` | manual short post-opt14 | regression check |
| `perf_20260829-150511_20456.jsonl` | **fz-manual-long** autofly ≥650s | **gate-of-record** |

Report: `bin/suite_reports/20260829-150507_fz-manual-long.json`

## Summary (20260829-150511)

| Metric | Value | Gate |
| --- | --- | --- |
| periods / steady | 313 / 303 | — |
| chunks_traveled | 116 | FP0 OK |
| holes_rate | 0.74 | FAIL (≤0.10) |
| wall_ms_med | 110 | FAIL (≤25) |
| schedule_ok_med | 4 | OK |
| capture_retarget_med | 4 | FAIL (plateau target 0) |
| relight_apply_final_med | 0 | FAIL (FP1) |
| dominant_schedule_blocker | `empty_fm_queue` | — |
| miss_stuck_max_run_sec | 26 | FAIL (≤4) |
| post_load_ring_idle_max | 1 | OK |
| stop_recovery | OK (gates_stop 9/12) | partial |
| blue_screen_suspect | 1.0 | investigate |

**FP0:** GO (`chunks_traveled≥3`). **Full analyze pass:** false (cruise holes + wall + miss_stuck).

Run: `python tools/flight_sim_run.py --world World_164 --scenario fz-manual-long --build --phase-id FP0`
