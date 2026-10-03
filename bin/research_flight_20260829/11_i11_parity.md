# I11 parity: completion drain + lit settle

Gate of record: **manual no-teleport** World_164 ≥2 min.

## Baseline (manual I9 `085951` / I10 `100455` / I11 target)

| Metric | Manual I9 `085951` | Manual I10 `100455` | I11 target |
|--------|---------------------|---------------------|------------|
| `holes_rate` | 0.94 | **1.00** | ≤0.55 |
| `stream_ms` med | 76 | **76** | ≤120 |
| `schedule_ok` med | 1 | **5** | ≥3 |
| `dirty_fm` med | 2 | **4** | >0 |
| `mode3` share | 89% | **81%** | ≤60% |
| `miss_stuck` max | 106s | **88s** | ≤30s |
| `post_stop_VB` max | 76 | **32** | ≤10 |
| `post_stop_VB_no_ticket` | 44 | **0** | ≤0 |
| `relight_completed` med | 0 | **0** | ≥1 |
| `fifo_dropped` | 26 | **0** | ≤10 |
| `blink_rate` | ~0 | **0** | — |

## I11 code phases

| Phase | Focus |
|-------|-------|
| I11-A | Miss completion gate, `MissCompletionStuckFrames`, nh≤4 drain, forensics v2 |
| I11-B | Relight chain telem, apply→drain coupling, FIFO trim guard |
| I11-C | HoleDrain clnm exit, rim remesh protect, classifier v3 |
| I11-D | VB stalled→mesh_drain, unified `StopVbDrainFrames`, vb audit v2 |
| I11-E | Underfeet band scan, unfinished ring reuse |
| I11-F | FP-manual gate protocol |

## Compare protocol

```powershell
python tools/flight_sim_analyze.py bin/logs/perf_<manual>.jsonl --manual-idle --report bin/suite_reports/manual_<ts>_analyze.json
python tools/flight_sim_phase_gate.py --phase-id FP-manual --report bin/suite_reports/manual_<ts>_analyze.json
python bin/scripts/budget_reality.py bin/logs/perf_<manual>.jsonl
python bin/research_flight_20260829/scripts/miss_stuck_forensics.py bin/logs/perf_<manual>.jsonl
python bin/research_flight_20260829/scripts/vb_stop_drain_audit.py bin/logs/perf_<manual>.jsonl
```

## FP-manual hard gates

| Gate | Threshold |
|------|-----------|
| `holes_rate` | ≤0.55 |
| `cruise_schedule_ok_med` | ≥3 |
| `miss_stuck_max_run_sec` | ≤30 |
| `post_stop_visible_black_max` | ≤10 |
| `chunks_traveled` | ≥5 |
