# A36 S0 — Clean evidence baseline (post-A35)

Date: 2026-09-23  
HEAD: resolve with `git rev-parse --short HEAD` on **clean** tree (`dirty_diff_hash=clean`).

## Class samples (not Gate 8 PASS)

| Session | Role | Key |
|---|---|---|
| `bin/logs/perf_20260923-185333_32664.jsonl` | Manual post-A35 healthy enter | SourceMismatch=0; opaque med 50.5; unfinished med 62; vb med 49; nfh periods>0 = 0; shutdown OK |
| `bin/logs/perf_20260923-171410_17444.jsonl` | Pre-A35 hang class | SourceMismatch≈49819; MeshWarmup timeout; opaque 18–21 |
| `bin/logs/perf_20260923-170746_27720.jsonl` | Pre-A35 empty class | SourceMismatch≈30765; opaque→0 |
| `bin/suite_reports/a35_realign/cold.json` | Visible AF cold | empty/enter PASS; dual-lane/eye/A24 FAIL |

## Required AF matrix (Gate 1)

| Run | Scenario | Flags |
|---|---|---|
| cold | `product-174657` | `--visible`, clean tree |
| warm | `product-174657` | `--visible --warmup-sec 20` + `CUBA_WARM_PROTOCOL` + `CUBA_FLIGHT_WARM=1` (stamps period `warm`) |
| far | `product-174657-far` | `--visible`; harness sets `CUBA_FLIGHT_MOVE_SPEED_SCALE=12` so travel ≥8192; checkpoints 0 / 2^13 / … |

Binding: `flight_sim_run` fails product family when `dirty_diff_hash != clean` (unless `CUBA_ALLOW_DIRTY_AF=1`).

A37: KickUnfinished OFF unless `CUBA_KICK_UNFINISHED=1`; unfinished uses `AdmitUnfinishedVisualDemand`.

## Baseline metrics to record

- `near_focus_holes` periods>0 (whole-route)
- `unfinished_visual` med/max
- `visible_black_focus_n` med
- `post_stop_*`, `demand_stop_converged`
- `pub_reject_source_mismatch` (must stay 0 — R0 regress)
- fluid_map / wall p50/p95/max
- `run_manifest.manifest_required_empty` must be empty for close

## Far vs west-272

`focus_cx` −10..7 ≈ 272 blocks — **not** far. Use `product-174657-far` for A31-04 distance stress.
