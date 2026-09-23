# A31 P6 — acceptance matrix (no-teleport)

Use after clean rebuild. Do **not** edit historical A29/A30 dirty scorecards as baseline.

## Commands

```bat
REM cold
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --visible
REM warm
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --warmup-sec 20 --visible
REM dive
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive --visible
REM far (longer no-teleport)
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-far --visible
```

Optional: set `CUBA_GL_CAPS`, `CUBA_GPU_DRIVER`, `CUBA_RESOLUTION`, `CUBA_LIGHT_DISTANCE` for manifest completeness.

## Required report fields

- `run_manifest.git_sha`, `dirty_diff_hash` (`clean` for acceptance), `exe_hash`
- `a24_safety_stop_line.near_focus_holes_periods_gt0` == 0 (whole route)
- `a24_safety_stop_line_pass` and `post_stop_convergence_pass`
- `a31_progress_snapshot` present
- `west_route_coverage.far_flight` / `far_distance_blocks` for far scenario
- `eye_proxy_stop_line_pass` is **secondary** — must not override whole-route holes FAIL
- `operator_visual` — manual PASS required for merge_green; UNTESTED ≠ PASS

## Pass criteria (product)

| Gate | Pass |
|---|---|
| Whole-route mesh holes | periods_gt0 == 0 |
| Post-stop | all listed post_stop_* true |
| Dirty-drop | delta median ≤ 800 (not correctness alone) |
| Far | far scenario reaches documented distance or records honest UNTESTED |
| Eye | human PASS on same build |
| Ring | stays OFF |

## Progress compare

Diff `a31_progress_snapshot` across PR builds; regressing holes/post_stop/fluid hitch vs baseline → stop expanding recovery heuristics.
