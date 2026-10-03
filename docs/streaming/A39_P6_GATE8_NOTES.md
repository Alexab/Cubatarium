# A39 P0b / P6 — Gate 8 matrix notes

## Clean SHA

A39 code is uncommitted on top of `c0785daf`. AF while dirty → `dirty_diff_hash` non-empty → **not** merge_green claimable per A31 P6 matrix.

After commit of A39: re-run with empty dirty hash:
```bat
set CUBA_ALLOW_DIRTY_AF=
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --visible
```

## Attribution

| Signal | Use |
|---|---|
| `near_focus_holes` periods_gt0 | Gate 8 mesh holes |
| `holes_rate` / unfinished hole_key | **not** Gate 8 |
| `defect_class_primary` / ClassifyChunkDefect | VB primary class |
| `eye_proxy` mid_corridor | secondary only |
| `operator_visual` | human PASS required on same exe |

## Eye protocol

1. Same Release `bin/Cubatarium.exe` as AF manifest
2. World_164, product-174657 route + west far if claiming merge_green
3. Record PASS/FAIL; UNTESTED ≠ PASS

Ring stays OFF until eye+holes PASS.
