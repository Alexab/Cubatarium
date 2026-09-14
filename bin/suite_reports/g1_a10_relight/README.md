# G1 A10 RelightReplace suite reports

Product gate proxy: `--scenario product-174657` (west yaw 180, no-teleport).

- Diff: [autofly_vs_manual_diff.md](autofly_vs_manual_diff.md)
- Route: `tools/manual_flight_world164_product_174657.json`
- North `--replay-manual` yaw 90 = smoke only (see `g1_progress/`)

## Dual-lane schedule (A10/A11) — 2026-09-14

Contract: fixed remesh-snapshot→FirstMesh order (no focus-miss reorder);
`ComputeDualLaneSchedule` lane quotas; telem `schedule_lane_starve_reason`.
Second Finish kept.

| Run | StaleVL fly med | VB fly med | unlit max | dirty_remesh | adequacy / note |
|---|---:|---:|---:|---:|---|
| Anchor cold `125123` | 80 | 80.5 | 10 | 52 | PASS |
| P3 cold `133817` | 94.5 | 94 | 25 | 74.5 | PASS |
| S1 cold | 83 | 84 | 14 | 51 | PASS |
| S3 cold | 84.5 | 84.5 | 12 | 54.5 | PASS |
| Anchor warm `125331` | 83.5 | 83.5 | 19 | 57 | PASS |
| S3 warm r2 | 75 | 75 | 16 | 50 | PASS |
| Manual `122212` | 77 | 78.5 | 11 | 54 | pre-P3 eye |
| Manual `134914` | 87 | 84 | 39 | 79 | P3 black regress |
| **Manual `154921`** | **75** | **77** | **13** | **50** | **eye PASS** (closes 134914) |

Reports: `dual_lane_s1_cold.json`, `dual_lane_s3_*.json`, `dual_lane_s3_gates.json`,
`dual_lane_s4_manual_154921.json`.

**Manual `154921` / enter `154948`:** corridor `(7,3)→(−3,3)`, visually OK
(no edits). Wall med ~71 ms (~14 FPS) — same class as `122212` (~78 ms);
not a dual-lane FPS regression. Do **not** claim G1 product CLOSED vs 141350.

## Prior evidence

- `perf_20260914-122212_41064.jsonl` + `enter_lit_20260914-122244.jsonl`
- P3 regress: `134914` / enter `134941`
- prior: `100645` / enter `100713`

proxy_v3: `hold_space=False`, `--min-alt-above-sea 0`, `--cruise-eye-y 56`.
Product G1 still OPEN (VB/stuck FAIL vs 141350).
