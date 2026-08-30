# I10 parity: manual vs replay-manual autofly

Gate of record: **manual no-teleport** World_164 ≥2 min.

## Baseline (manual I9 `085951` vs autofly `230219`)

| Metric | Manual `085951` | Autofly `230219` | I10 target |
|--------|-----------------|------------------|------------|
| `holes_rate` | 0.94 | 0.15 | ≤0.55 |
| `stream_ms` med | 76 | 176 | ≤120 |
| `prep_refresh` med | 37 | ~81 | ≤25 |
| `schedule_ok` med | 1 | 0 | ≥3 |
| `dirty_fm` med | 2 | 0 | >0 |
| `miss_stuck` max | 106s | 40s | ≤30s |
| `post_stop_VB` max | 76 | 4 | ≤10 |
| `chunks_traveled` | 10 | 4 | ≥5 |

## Compare protocol

```powershell
python tools/flight_sim_analyze.py bin/logs/perf_<manual>.jsonl --manual-idle --report bin/suite_reports/manual_<ts>_analyze.json
python tools/flight_sim_phase_gate.py --phase-id FP-manual --report bin/suite_reports/manual_<ts>_analyze.json
python bin/scripts/budget_reality.py bin/logs/perf_<manual>.jsonl
python bin/research_flight_20260829/scripts/vb_stop_drain_audit.py bin/logs/perf_<manual>.jsonl
python bin/research_flight_20260829/scripts/miss_stuck_forensics.py bin/logs/perf_<manual>.jsonl
```

## FP-manual hard gates

| Gate | Threshold |
|------|-----------|
| `holes_rate` | ≤0.55 |
| `cruise_schedule_ok_med` | ≥3 |
| `miss_stuck_max_run_sec` | ≤30 |
| `post_stop_visible_black_max` | ≤10 |
| `chunks_traveled` | ≥5 |
