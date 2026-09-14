# G1 A10 RelightReplace suite reports

Product gate proxy: `--scenario product-174657` (west yaw 180, no-teleport).

- Diff: [autofly_vs_manual_diff.md](autofly_vs_manual_diff.md)
- Route: `tools/manual_flight_world164_product_174657.json`
- North `--replay-manual` yaw 90 = smoke only (see `g1_progress/`)

## Dual-lane schedule (A10/A11) — 2026-09-14

Contract: fixed remesh-snapshot→FirstMesh order (no focus-miss reorder);
`ComputeDualLaneSchedule` lane quotas; telem `schedule_lane_starve_reason`.
Second Finish kept.

| Run | StaleVL fly med | VB fly med | unlit max | dirty_remesh | adequacy |
|---|---:|---:|---:|---:|---|
| Anchor cold `125123` | 80 | 80.5 | 10 | 52 | PASS |
| P3 cold `133817` | 94.5 | 94 | 25 | 74.5 | PASS |
| S1 cold | 83 | 84 | 14 | 51 | PASS |
| S3 cold | 84.5 | 84.5 | 12 | 54.5 | PASS |
| Anchor warm `125331` | 83.5 | 83.5 | 19 | 57 | PASS |
| S3 warm r2 | 75 | 75 | 16 | 50 | PASS |

Reports: `dual_lane_s1_cold.json`, `dual_lane_s3_cold.json`,
`dual_lane_s3_warm.json` (=r2), `dual_lane_s3_gates.json`.

**Manual eye (operator):** west `(7,3)→(−3,3)` eye-level; compare to
`122212` (good) / `134914` (P3 black regress). Pass if StaleVL/VB/unlit and
subjective black chunks ≤ `122212` class. Do **not** claim G1 product CLOSED
vs 141350 from this alone.

## Prior evidence

Latest west manual evidence (E3 FAIL/freeze, not CLOSED):

- `perf_20260914-122212_41064.jsonl` + `enter_lit_20260914-122244.jsonl`
  - corridor `(7,3)→(-3,3)`, VB all/fly med `78/78.5`, `focus_missing` med `1`,
    `miss_stuck` max `311`, StaleVL ~`77`, `gpu_kick` fly med `0`,
    `gpu_kick_post_drain_n` max `1` (does not clear stuck)
- P3 regress: `134914` / enter `134941` (black chunks returned)
- prior: `perf_20260914-100645_45256.jsonl` + `enter_lit_20260914-100713.jsonl`

proxy_v3: `hold_space=False`, `--min-alt-above-sea 0`, `--cruise-eye-y 56`.
Product G1 still OPEN (VB/stuck FAIL vs 141350).
