# Mesh R1.5 → SHIP — research index (2026-09-03)

| Field | Value |
| --- | --- |
| Date | 2026-09-03 |
| HEAD | phase 3 R3.6–R3.8 (post `0f453d79` baseline) |
| Branch | `perf_opt17` |
| Status | **R3.6–R3.8 LANDED · R4.3 manual pending · SHIP NO-GO** |

## SoT logs

| ID | Report | Perf jsonl | Role |
| --- | --- | --- | --- |
| Manual-085143 | `bin/suite_reports/manual_20260903-085143_analyze.json` | `perf_20260903-085143_23864.jsonl` | gate-of-record pre phase-3 |
| Manual-phase3-post | *pending* | *pending* | gate-of-record post R3.6–R3.8 |
| Phase2-trio-* | `bin/suite_reports/mesh_phase2_trio_*.json` | — | phase-2 trio baseline |
| Phase3-enter | `bin/suite_reports/mesh_phase3_fz_cold_enter.json` | — | fz-cold-enter after phase-3 |

## Phase gates

| Gate | Criteria | Latest |
| --- | --- | --- |
| MESH-R15-capture | retry>0, schedule_ok≥2 | **GO** (`085143`) |
| MESH-R26-completion | fm_finish>0 | **NO-GO** (`085143`) |
| MESH-R30-fps | wall≤66, stream≤90, fps≥15 | **pending** post-fix manual |
| MESH-SHIP-joint | diet≥40%, holes≤30%, fm>0, mismatch≤10%, fps≥15 | **pending** |
| MESH-parity-manual | holes ≤55% | **NO-GO** (`085143` 80%) |
| fz-cold-enter | enter convergence | run after build |

## Verdict one-liner

Phase 3 **LANDED** (narrow hole-pressure, schedule honesty, input-first shed, playable FPS bar ≥15). SHIP blocked until post-fix manual gate-of-record.
