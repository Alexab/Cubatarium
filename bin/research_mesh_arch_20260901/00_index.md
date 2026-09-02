# Mesh R1.5 → SHIP debt closure — research index (2026-09-02)

| Field | Value |
| --- | --- |
| Date | 2026-09-02 |
| HEAD | `0b40bcb5` + R3.2–R3.5 phase 2 |
| Branch | `perf_opt17` |
| Status | **R3.2–R3.5 LANDED · R4.2 manual pending · SHIP NO-GO** |

## SoT logs

| ID | Report | Perf jsonl | Role |
| --- | --- | --- | --- |
| Manual-R15 | `bin/suite_reports/manual_20260902-173028_analyze.json` | `perf_20260902-173028_25184.jsonl` | gate-of-record (pre-fix, ~3 min manual) |
| Manual-phase2-pre | `bin/suite_reports/manual_20260902-201637_analyze.json` | `perf_20260902-201637_23188.jsonl` | gate-of-record (post-R3.0, pre phase-2) |
| Manual-phase2-post | *pending* | *pending* | gate-of-record (post R3.2–R3.5) |
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
| MESH-R26-completion | fm_to_gpu_finish>0 | **pending** (post phase-2 manual) |
| MESH-R30-fps | wall_fly≤200, stream≤120, fps≥5 (interim) | **pending** |
| MESH-SHIP-joint | diet≥40%, holes≤30%, fm>0, mismatch≤10% | **pending** |
| MESH-parity-manual | holes ≤ manual×1.15+5pp | **NO-GO** (`201637` holes 93%) |
| fz-cold-enter | enter convergence | PASS (pre phase-2) |

## Verdict one-liner

Phase 2 **LANDED** (capture retry, rim hole-pressure SoT, witness SLA, FPS interim gates). SHIP blocked until post-fix manual gate-of-record + trio regression.
