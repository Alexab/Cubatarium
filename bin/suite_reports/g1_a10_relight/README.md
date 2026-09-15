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

Plan: `.cursor/plans/west_cruise_wall_diet.plan.md`. Tip at freeze: `7a25581b`.
Gate: `product-174657` no-teleport west (not land-cruise / not yaw 90).

| Step | Commit | Cold report | Note |
|---|---|---|---|
| A0a | `ce453505` | — | Q9/Phase57 fail-closed; CI `cursor_audit_impl*` |
| A0b | `0e9c3642` | `wall_diet_a0_cold.json` | K3/M3 remesh demand |
| A1 | `58f65ffe` | `wall_diet_a1_cold.json` | atomic chunk-pass — **REOPEN** (B4 eye FAIL) |
| A2 | `4d638637` | `wall_diet_a2_cold.json` | frustum row extraction — **KEEP** |
| A3 | `f122ea11` | `wall_diet_a3_cold.json` | structural CooldownKey — **KEEP** |
| A4 | `b521eea0` | `wall_diet_a4_{cold,warm}.json` | prep_sched_* OK; lazy ring **REOPEN→cache** |
| A5 | *(deferred)* | — | stream SoT diet stop-lined (VB/stale); no land |
| A6 | docs + `wall_diet_a6_*` | closeout | autofly only; manual eye later FAIL |

Baseline: [wall_diet_baseline.md](wall_diet_baseline.md). Audit:
`docs/streaming/CURRENT_STATE_AUDIT_2026-09-14.md`.

A4 evidence: `prep_schedule_policy_ms` med already ~0 on autofly; dominant residual is
`streamer_update_ms` / `async_io_ms`. Aggressive missing/pending SoT reuse in
`UpdateStreaming`/`TickAsync` raised VB/stale above dual-lane class → reverted.

**Autofly gap:** runs were **without `--visible`** (hidden GLFW). Merge used
`adequacy_pass` (needs VB≥40), not dual-lane upper stop-line / operator eye.

Do **not** claim G1 CLOSED / Q9 complete / oracle done.

## Follow-on: N01 rework + mid-black (after tip eye FAIL)

Plan: `.cursor/plans/n01_rework_mid-black.plan.md`.

| Anchor | Role |
|---|---|
| Manual `154921` | historical eye PASS |
| Tip manual `200927` | wall-diet tip eye FAIL (holes/swap/blink) |
| Bisect B1 `202741` | A2 OFF worse |
| Bisect B2 `203535` | A1 OFF partial improve |
| Bisect B3 `204326` | eager ring — blinks gone; water end blacks |
| Bisect B4 `205048` | A1 ON again = full FAIL |
| Confirm `205626` | A1 OFF+eager: only sticky mid blacks (stalled) |
| Manual `085208` | post-N01 v2 eye FAIL (B4 + sticky mid worse); v2.1 baseline |
| Manual `102527` | post-v2.1 eye FAIL thrash (swap/blink/per-block); holes counters≈0 |
| Manual `121131` | **post-epoch thrash regress** (stale mid 21.5 vs 102527=8); per-block/wrong tex |
| Autofly `095545` | pre-epoch postfix; fly stale_med=2 **missed** mid=17 (N08 gap) |

Reports: `bisect_b1_result.md`, `bisect_b2_result.md`, `bisect_b3_result.md`,
`bisect_b4_result.md`, `bisect_b3b_205626_result.md`,
`n01_thrash_manual_{102527,121131}.json`, `thrash_autopsy_121131.md`.

**Interim eye-safe (pre N01 v2):** A2+A3 ON, A1 OFF, eager spawn ring, A4 telem ON.

**N01 epoch-split → narrow any_fresh (C1):** group-commit KEEP; mesh/cull/sort only when
`PendingGeometryDirty.empty()`; `publicationVersion++` **only on `any_fresh`**
(not untouched reshuffle). Autopsy: [thrash_autopsy_121131.md](thrash_autopsy_121131.md).
T2 mid MarkRelit **frozen** until thrash ≤102527.

Scorecard: `python tools/n01_v21_scorecard.py bin/logs/perf_*.jsonl -o ...`
(includes eye_proxy). Self-test: `python tools/test_eye_proxy_stop_line.py`.

**Four merge signals (N08):** `--visible` + `adequacy_pass` + dual-lane
(VB/StaleVL≤84.5, unlit, mid stalled≤5) + **eye_proxy mid-corridor**
(`focus_cx∈[2,5]`: stale_visual med≤6, Δmed≤1.5, holes blink≤0.05, reorder≤1,
`publication_incomplete_material` mid med≤5,
plus `stale_visual_without_hole_counters` when holes=0 but stale mid>6)
+ manual west eye.
Do **not** merge on adequacy alone. Do **not** treat `near_focus_holes=0` as
no per-block defects. G1 / pixel oracle / Q9 remain **OPEN**.

## N01 retain-storm (full-cache dirty publish) — SoT `134038`

Plan: N01 retain storm (incomplete guard on frustum-filtered refs).

| Anchor | Role |
|---|---|
| Manual `134038` | retain storm SoT (`publication_overload_retain_n` mid≈40, `pass_mesh_rev_lag_max`≈535); C1 OK (`pubver_changed_without_fresh`=0) |
| Fix | Dirty `RefreshPassRefs` expands upload inputs to full `GreedyCache` pass materials; incomplete predicate = `cache⊆upload` (not `resident⊆upload`) |
| Telem | `publication_incomplete_material_n` / `publication_oom_retain_n`; overload = sum |
| KEEP | C1 any_fresh pubVer; T2 MarkRelit **freeze**; N08 mid-corridor; G1 **OPEN** |

Acceptance: incomplete mid ≪40 (eye_proxy ≤5); lag↓; manual west per-block/wrong tex better than `134038`.
T2 mid stalled≤5 — **not** this track.

Cold autofly post-fix `145051` (`n01_retain_af_cold.json`):
`publication_incomplete_material_mid_med=0`, `oom_mid=0` (gate PASS);
`pass_mesh_rev_lag_max` mid still ~459 (not zero — other dirty classes remain);
eye_proxy still FAIL on stale_visual mid≈19.5 / blink; dual-lane FAIL (VB/stale/stalled).
G1 **OPEN**. Manual west eye still required.

## Post-N01 eye thrash (stamp → T2) — SoT `160234` / autofly `173946`

Plan: post-N01 eye thrash → T2 mid black (do not edit plan SoT during quiet exec).

| Anchor | Role |
|---|---|
| Manual `160234` | N01 retain closed (incomplete/oom mid=0); residual Δstale=3 eye FAIL; stalled~54 |
| P0 | reason split `mesh_apply_stale_{geom,light,catalog,stamp_invalid}` — light dominant mid |
| P1-b | accept light-stale apply; Priority only undrawn holes |
| P2 | accept geom-stale when drawable; blink=holes-only; mid stale≤8 (=102527 thrash class) |
| Autofly `173946` | eye_proxy **PASS** (mid stale=7, Δ=0, blink=0, incomplete=0); adequacy PASS |
| T2 / N04 | **still FREEZE** until P3 ticket remesh (mid stalled≪5) |

Reports: `post_n01_p0_af_*`, `post_n01_p1*_af_*`, `post_n01_p2_final_af_{cold,score}.json`.
Self-test: `102527` eye_proxy PASS under thrash class; `095545`/`121131` still FAIL.

**Merge:** adequacy ∧ dual_lane (stalled≤5 after P3) ∧ eye_proxy ∧ operator west eye.
Do **not** claim G1 CLOSED. T2 MarkRelit retune stays frozen until P3 green.
