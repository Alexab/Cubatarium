# Mesh R1.5 → SHIP — research index (2026-09-03)

| Field | Value |
| --- | --- |
| Date | 2026-09-03 |
| HEAD | `899f7a81` (phase 3 R3.6–R3.8) |
| Branch | `perf_opt17` |
| Status | **R3.6–R3.8 LANDED · R4.3 harness DONE · manual gate pending · SHIP NO-GO** |

## SoT logs

| ID | Report | Perf jsonl | Role |
| --- | --- | --- | --- |
| Manual-085143 | `bin/suite_reports/manual_20260903-085143_analyze.json` | `perf_20260903-085143_23864.jsonl` | gate-of-record pre phase-3 |
| Manual-phase3-post | *pending* | *pending* | gate-of-record post R3.6–R3.8 |
| Phase3-enter | `bin/suite_reports/mesh_phase3_fz_cold_enter.json` | — | fz-cold-enter after phase-3 (**GO**) |
| Phase3-trio-replay | `bin/suite_reports/mesh_phase3_trio_replay.json` | — | autofly replay (**MESH-R26 GO**) |
| Phase3-trio-heavy | `bin/suite_reports/mesh_phase3_trio_fly_heavy.json` | — | autofly fly-heavy |
| Phase3-trio-fzlong | `bin/suite_reports/mesh_phase3_trio_fz_long.json` | — | autofly fz-manual-long (best wall) |

## Phase gates (post phase-3 autofly)

| Gate | Criteria | Latest |
| --- | --- | --- |
| MESH-R15-capture | retry>0, schedule_ok≥2 | **GO** (`085143`) |
| MESH-R26-completion | fm_finish>0 | **GO** (trio replay `fm_finish=1`) |
| MESH-R30-fps | wall≤66, stream≤90, fps≥15 | **NO-GO** (best fz-long wall 159 / 6.3 FPS) |
| MESH-SHIP-joint | diet≥40%, holes≤30%, fm>0, mismatch≤10%, fps≥15 | **NO-GO** (all trio) |
| MESH-parity-manual | holes ≤55% | **NO-GO** (`085143` 80%; pending post manual) |
| fz-cold-enter | enter_unfinished_max≤10 | **GO** (enter_max=3) |

## Verdict one-liner

Phase 3 code **LANDED**. Autofly: R26 first GO (replay), enter guard GO, stream on fz-long ≤90, but playable FPS / joint still NO-GO. SHIP blocked on **manual 3 min** gate-of-record.
