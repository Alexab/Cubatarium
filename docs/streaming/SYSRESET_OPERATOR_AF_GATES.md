# Sysreset operator + AF gates (v4 vs SoT 101105)

Controls vs manual `bin/logs/perf_20260921-101105_14028.jsonl`.
Docs: [SYSRESET_V4_AUDIT_101105.md](SYSRESET_V4_AUDIT_101105.md), prior v3 gates still KEEP where noted.

## AF suite (fog ON)

```bash
set CUBA_FLIGHT_FOG_ON=1
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --warmup-sec 20
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive
python -X utf8 tools/n01_v21_scorecard.py <perf.jsonl> -o bin/suite_reports/g1_a10_relight/<label>_score.json --label <label>
```

## Scorecard gates (v4)

| Gate | 101105 bad | Pass |
|---|---|---|
| west_route_coverage | COVERED | COVERED (cx≤−3) |
| VB fly med | 44.5 | ≤35 |
| mid_fd_stalled | 11–19 | ≤5 |
| unfinished_visual max | 63 | ≤4 |
| PreferKick stand/fly | ≡0 | >0 when pending GPU |
| dirty_fm med | ~85 | ≪90 / Δ↓≥30% |
| prior_lit_hold fly med | ~8 | ≤400 KEEP |
| flip / dual | 0\|0 | KEEP 0\|0 |
| mesh_gpu_kick_ms max | ≪1 | ≤20 KEEP |
| mesh_emerge fly max | ~5 | ≤40 |
| render_total fly max | 79–200 | ≤80 |
| spikes max | 2 | ≤2 |
| transparent_cmd_reorder mid | 0–1 | ≤0.5 |
| operator sky-through | OPEN | CLOSED |

## Operator SoT

| Class | Check |
|---|---|
| sky-through faces | SoftDefer→overlay→Unknown seams remesh; unfinished≤4 |
| FullyDark / VB | PreferKick drain; mid_fd≤5; VB≤35 |
| hitch C | opaque cull / transparent under deadline; no control stutter |

Artifacts: `bin/suite_reports/g1_a10_relight/sysreset_v4_*`.
