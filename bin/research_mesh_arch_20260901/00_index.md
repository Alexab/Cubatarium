# Mesh R1.5 → SHIP — research index (2026-09-04)

| Field | Value |
| --- | --- |
| Date | 2026-09-04 |
| HEAD | `cacb73b4` (phase 4.5 R4.5.1–R4.5.2) |
| Branch | `perf_opt18` |
| Status | **R4.5.1–R4.5.2 LANDED · enter GO · autofly still stream-bound · manual gate pending · SHIP NO-GO** |

## SoT logs

| ID | Report | Perf jsonl | Role |
| --- | --- | --- | --- |
| Manual-202455 | `bin/suite_reports/manual_20260903-202455_analyze.json` | `perf_20260903-202455_21316.jsonl` | gate-of-record pre phase-4 |
| Manual-113457 | `bin/suite_reports/manual_20260904-113457_analyze.json` | `perf_20260904-113457_32744.jsonl` | post phase-4 (emerge win, holes regress) |
| Manual-phase45-post | *pending* | *pending* | gate-of-record post R4.5 |
| Phase45-enter | `bin/suite_reports/mesh_phase45_fz_cold_enter.json` | `perf_20260904-132318_12276.jsonl` | fz-cold-enter (**GO**, enter_max=2) |
| Phase45-trio-heavy | `bin/suite_reports/mesh_phase45_trio_fly_heavy.json` | `perf_20260904-133026_30784.jsonl` | autofly fly-heavy |
| Phase4-enter | `bin/suite_reports/mesh_phase4_fz_cold_enter.json` | `perf_20260904-101632_33740.jsonl` | prior enter GO |
| Phase4-trio-fzlong | `bin/suite_reports/mesh_phase4_trio_fz_long.json` | — | prior best wall |

## Phase gates (post phase-4.5 autofly)

| Gate | Criteria | Latest |
| --- | --- | --- |
| MESH-R15-capture | retry>0, schedule_ok≥2 | **GO** (cruise_schedule_ok_med 7) |
| MESH-R26-completion | fm_finish>0 | **NO-GO** (autofly 0) |
| MESH-R30-fps | wall≤66, stream≤90, fps≥15 | **NO-GO** (fly-heavy wall≈329 / stream≈215) |
| MESH-SHIP-joint | diet≥40%, holes≤30%, fm>0, mismatch≤10%, fps≥15 | **NO-GO** |
| MESH-parity-manual | holes ≤55% | **pending** (113457: 97%) |
| fz-cold-enter | enter_unfinished_max≤10 | **GO** (enter_max=2) |

## Verdict one-liner

Phase 4.5 code **LANDED** (refresh honesty + terrain-complete reuse + near-miss FM finish). Enter guard **GO**. Autofly still stream-bound; `camera_complete_ms≈0` but residual `prep_refresh_gap` still large — needs manual gate-of-record vs `113457`/`202455`.
