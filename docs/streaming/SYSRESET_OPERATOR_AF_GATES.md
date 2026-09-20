# Sysreset operator + AF gates (v3 vs SoT 202031)

Controls vs manual `bin/logs/perf_20260920-202031_6988.jsonl` and v2 AF baselines.
Doc: [SYSRESET_V3_AUDIT_202031.md](SYSRESET_V3_AUDIT_202031.md).

## AF suite (fog ON)

```bash
set CUBA_FLIGHT_FOG_ON=1
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --warmup-sec 20
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive
python -X utf8 tools/n01_v21_scorecard.py <perf.jsonl> -o bin/suite_reports/g1_a10_relight/<label>_score.json --label <label>
```

## Scorecard gates (v3)

| Gate | 202031 / v2 bad | Pass |
|---|---|---|
| west_route_coverage | COVERED | COVERED (cx≤−3) |
| VB fly med | 42 / ≤35 AF | ≤35 |
| mid_fd_stalled | 19 manual | ≤5 |
| unfinished_visual max | 12 | ≤4 |
| prior_lit_hold fly med | ~599 | ≤400 or Δ↓≥20% vs 202031 |
| prior_lit_hold_age_max | 90 | ≤90 KEEP |
| flip / dual | 0\|0 | KEEP 0\|0 |
| dive void_near med | ≤500 v2 | ≤500 |
| mesh_emerge_ms max | 90–137 | ≤40 |
| mesh_gpu_kick_ms max (kick_n>0) | ~114 | ≤20 |
| spikes max | 5 | ≤2 |
| transparent_cmd_reorder mid | 0–0.5 | ≤0.5 |
| cold blink | 0.14 | ≤0.3 or non-blocking if warm+dive PASS |

## Operator SoT

| Class | Check |
|---|---|
| x-ray faces | single-block wall holes drain; unfinished≤4 |
| hitch | no control stutter from emerge/kick spikes |
| wrong-tex / walls / blacks | KEEP v2 improvements |

Artifacts: `bin/suite_reports/g1_a10_relight/sysreset_v3_*`.
