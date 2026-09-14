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

## Wall-diet track (audit-first after 154921) — 2026-09-14

Plan: `.cursor/plans/west_cruise_wall_diet.plan.md`. Tip at freeze: `b521eea0`.
Gate: `product-174657` no-teleport west (not land-cruise / not yaw 90).

| Step | Commit | Cold report | Note |
|---|---|---|---|
| A0a | `ce453505` | — | Q9/Phase57 fail-closed; CI `cursor_audit_impl*` |
| A0b | `0e9c3642` | `wall_diet_a0_cold.json` | K3/M3 remesh demand |
| A1 | `58f65ffe` | `wall_diet_a1_cold.json` | atomic chunk-pass publish |
| A2 | `4d638637` | `wall_diet_a2_cold.json` | frustum row extraction |
| A3 | `f122ea11` | `wall_diet_a3_cold.json` | structural CooldownKey |
| A4 | `b521eea0` | `wall_diet_a4_{cold,warm}.json` | prep_sched_* + lazy spawn ring |
| A5 | *(reverted)* | — | stream SoT diet stop-lined (VB/stale); no land |
| A6 | docs + `wall_diet_a6_*` | closeout | ctest streaming 26/26 |

Baseline: [wall_diet_baseline.md](wall_diet_baseline.md). Audit:
`docs/streaming/CURRENT_STATE_AUDIT_2026-09-14.md`.

A4 evidence: `prep_schedule_policy_ms` med already ~0 on autofly; dominant residual is
`streamer_update_ms` / `async_io_ms`. Aggressive missing/pending SoT reuse in
`UpdateStreaming`/`TickAsync` raised VB/stale above dual-lane class → reverted.

Do **not** claim G1 CLOSED / Q9 complete / oracle done.
