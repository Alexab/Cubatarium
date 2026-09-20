# Sysreset operator + AF gates (SoT 143831)

Controls vs `bin/logs/perf_20260920-143831_55228.jsonl`.

## AF suite (fog ON)

```bash
set CUBA_FLIGHT_FOG_ON=1
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive
python -X utf8 tools/n01_v21_scorecard.py <perf.jsonl> -o bin/suite_reports/g1_a10_relight/<label>_score.json --label <label>
```

Bisect RelightReplace OFF: `CUBA_RELIGHT_REPLACE_OWNER=0`.

## Scorecard gates (phase 1+)

| Gate | 143831 bad | Pass |
|---|---|---|
| west_route_coverage | — | COVERED (cx≤−3) |
| VB fly med\|max | 45\|52 | med≤35, max≤45 |
| unfinished_visual max | 12 | ≤4 |
| column_loaded_no_mesh max | 12 | ≤4 |
| prior_lit_hold fly med | 531 | ≤400 or Δ↓ vs phase baseline |
| prior_lit_hold_age_max | — | finite; converges (not mono plateau) |
| stale_accepted_refresh max | 5 | ≤2 |
| flip / dual | 0\|0 | KEEP 0\|0 |
| fog_rd thrash % | ~7 | ≤10; no margin 28↔132 |
| dark_face_void fly med (dive) | 826 | ≤500 |
| MarkRelit invoked when repair≥8 | often 0 | >0 on stand slice |

## Operator SoT (manual)

| Class | Check |
|---|---|
| wrong-tex | no blink on west rim under fog ON |
| holes | unfinished+nlm drain; no SoftDefer-empty spoof |
| dive walls | no distant underwater closing walls |
| blacks | VB/FD repair drains; MarkRelit>0 when repair≥8 |

Artifacts: `bin/suite_reports/g1_a10_relight/sysreset_*`.
