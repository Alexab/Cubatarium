# 02 — Baseline long SoT (pending autofly)

| Log | Role | Status |
| --- | --- | --- |
| `perf_20260829-081522_22660.jsonl` | manual short pre-fix | baseline |
| `perf_20260829-142846_30244.jsonl` | manual short post-opt14 | regression check |
| `fz-manual-long` | gate-of-record ≥270s | **pending run** |

Run: `python tools/flight_sim_run.py --world World_164 --scenario fz-manual-long --build --phase-id FP0`
