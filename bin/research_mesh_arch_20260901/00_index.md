# Mesh R1.5 → SHIP debt closure — research index (2026-09-02)

| Field | Value |
| --- | --- |
| Date | 2026-09-02 |
| HEAD | `18cb5899` + R2.6–R4.1 debt closure |
| Branch | `perf_opt17` |
| Status | **R1.5 GO · R2.6–R4.1 LANDED · SHIP NO-GO** |

## SoT logs

| ID | Report | Perf jsonl | Role |
| --- | --- | --- | --- |
| Manual-R15 | `bin/suite_reports/manual_20260902-173028_analyze.json` | `perf_20260902-173028_25184.jsonl` | gate-of-record (pre-fix, ~3 min manual) |
| Debt-replay | `bin/suite_reports/mesh_debt_trio_replay.json` | `perf_20260902-181922_30324.jsonl` | trio replay-manual (pre teardown fix) |
| Debt-fly-heavy | `bin/suite_reports/mesh_debt_trio_fly_heavy.json` | `perf_20260902-190322_36808.jsonl` | trio fly-heavy |
| Debt-fz-long | `bin/suite_reports/mesh_debt_trio_fz_long.json` | — | trio fz-manual-long |
| Enter-guard | `bin/suite_reports/mesh_debt_fz_cold_enter.json` | `perf_20260902-181413_27212.jsonl` | fz-cold-enter regression |

## Forensics raw (R2.6)

| File | Role |
| --- | --- |
| `raw/completion_chain_173028.txt` | FM completion chain manual |
| `raw/wall_waterfall_173028.txt` | FPS/wall waterfall manual |
| `raw/completion_chain_memo.md` | dominant stall summary |

## Phase gates

| Gate | Criteria | Latest |
| --- | --- | --- |
| MESH-R15-capture | retry>0, schedule_ok≥2 | **GO** (fly-heavy) |
| MESH-R26-completion | fm_to_gpu_finish>0 | **GO** (fly-heavy) |
| MESH-R30-fps | wall_fly≤120, stream≤90, fps≥8 | **NO-GO** (wall 130, fps 7.7) |
| MESH-parity-manual | holes ≤ manual×1.15+5pp | **NO-GO** (84% vs cap 55%) |
| fz-cold-enter | enter convergence | PASS |

## Verdict one-liner

Debt closure **LANDED** (ColumnJobGraph, completion chain, harness, frame budget interim). SHIP blocked: holes, witness diet, post-fix manual gate-of-record pending.
