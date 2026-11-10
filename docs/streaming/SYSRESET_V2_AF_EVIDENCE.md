# Sysreset v2 AF evidence (fog ON)

Date: 2026-09-20  
Vs SoT manual `191112` / regress `143831`.

## Runs

| Label | Perf | Scorecard |
|---|---|---|
| cold | `perf_20260920-195643_50324.jsonl` | `sysreset_v2_cold_195643_score.json` |
| dive | `perf_20260920-195847_9484.jsonl` | `sysreset_v2_dive_195847_score.json` |
| warm | `perf_20260920-200105_36896.jsonl` | `sysreset_v2_warm_200105_score.json` |

## Vs 191112

| Gate | 191112 | v2 AF | Verdict |
|---|---|---|---|
| west | COVERED | COVERED | KEEP |
| VB fly med (dual-lane) | 46 | 35 / 30 / 24 | PASS ≤35 |
| mid_fd_stalled | 24.5 | 0 | PASS |
| flip fly max | 6 | cold 0 / dive 2 | PARTIAL |
| dual | 0 | 0 | KEEP |
| dive void_near med | 806\|1271 | **244** | PASS ≤500 |
| transparent_cmd_reorder mid | ≡1 | 0 / 0 / 0.5 | PASS |
| unfinished max | 16 | 13 / 9 | OPEN ≤4 |
| prior_lit age | plateau 690 | age max **90** (TTL) | PARTIAL |
| PreferKick vs schedule | PreferKick sole | PreferKick≡0 schedule≤4 | CLOSED wire |

## Units

`mesh_publish_contract_test`, `mesh_neighbor_policy_test`, `miss_first_mesh_class_test` PASS.

## Remaining OPEN

unfinished drain; prior_lit hold count; cold eye blink rate; operator eye SoT.
No new SoftDefer-for-holes / remesh floors / fog VB latch.
