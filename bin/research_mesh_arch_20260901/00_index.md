# Mesh R1.5 → SHIP — research index (2026-09-04)

| Field | Value |
| --- | --- |
| Date | 2026-09-04 |
| HEAD | `868715bf` (phase 4 R4.1–R4.3) |
| Branch | `perf_opt18` |
| Status | **R4.1–R4.3 LANDED · harness DONE · manual gate pending · SHIP NO-GO** |

## SoT logs

| ID | Report | Perf jsonl | Role |
| --- | --- | --- | --- |
| Manual-202455 | `bin/suite_reports/manual_20260903-202455_analyze.json` | `perf_20260903-202455_21316.jsonl` | gate-of-record pre phase-4 |
| Manual-phase4-post | *pending* | *pending* | gate-of-record post R4.1–R4.3 |
| Phase4-enter | `bin/suite_reports/mesh_phase4_fz_cold_enter.json` | `perf_20260904-101632_33740.jsonl` | fz-cold-enter (**GO**, enter_max=4) |
| Phase4-trio-replay | `bin/suite_reports/mesh_phase4_trio_replay.json` | — | autofly replay |
| Phase4-trio-heavy | `bin/suite_reports/mesh_phase4_trio_fly_heavy.json` | — | autofly fly-heavy |
| Phase4-trio-fzlong | `bin/suite_reports/mesh_phase4_trio_fz_long.json` | — | autofly fz-manual-long (best wall) |

## Phase gates (post phase-4 autofly)

| Gate | Criteria | Latest |
| --- | --- | --- |
| MESH-R15-capture | retry>0, schedule_ok≥2 | **GO** (schedule_ok med 6–8.5) |
| MESH-R26-completion | fm_finish>0 | **NO-GO** (autofly 0) |
| MESH-R30-fps | wall≤66, stream≤90, fps≥15 | **NO-GO** (best fz-long wall 111 / 9.0 FPS; stream **28**) |
| MESH-SHIP-joint | diet≥40%, holes≤30%, fm>0, mismatch≤10%, fps≥15 | **NO-GO** |
| MESH-parity-manual | holes ≤55% | **pending** (pre: `202455` 81%) |
| fz-cold-enter | enter_unfinished_max≤10 | **GO** (enter_max=4) |

## Verdict one-liner

Phase 4 code **LANDED**. O(1) stale + SyncFocusRing R≤2: Sync med≈0.45 ms, fz-long stream≤90 and wall≈111 (~9 FPS). Protect-ring ShedRim + SoftDefer→Dirty landed; autofly `fm_finish` still 0. SHIP blocked on **manual 3 min** gate-of-record.
