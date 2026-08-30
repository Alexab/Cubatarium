# I9 parity: manual vs replay-manual autofly

Gate of record remains **manual no-teleport** World_164 ≥2 min.

Autofly `--replay-manual` is a regression proxy only. I8-full showed:
- `chunks_traveled` 4 vs manual 9
- Different save focus corridor

## Compare protocol

```powershell
python tools/flight_sim_analyze.py bin/logs/perf_<manual>.jsonl --manual-idle --report bin/suite_reports/manual_<ts>_analyze.json
python tools/flight_sim_run.py --replay-manual --phase-id I9-parity --report bin/suite_reports/I9_autofly_analyze.json
python bin/research_flight_20260829/scripts/refresh_pressure_audit.py bin/logs/perf_<manual>.jsonl bin/logs/perf_<autofly>.jsonl --labels manual autofly
```

## Key deltas to track

| Metric | Manual target | Autofly acceptable delta |
|--------|---------------|--------------------------|
| holes_rate | ≤0.55 | same order |
| stream_ms | ≤120 | +20% max |
| prep_refresh_pressure_ms | ≤25 | identifies same hotspot |
| schedule_ok | ≥3 | may be lower on autofly |
| chunks_traveled | ≥5 | ≥4 proxy |
