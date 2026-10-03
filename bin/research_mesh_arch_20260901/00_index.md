# Mesh R1.5 → SHIP — research index (2026-09-04)

| Field | Value |
| --- | --- |
| Date | 2026-09-04 |
| HEAD | `4513ddf8` (phase 4.6 R4.6.1–R4.6.2) |
| Branch | `perf_opt18` |
| Status | **R4.6.1–R4.6.2 LANDED · enter GO · pin/mismatch heal on autofly · manual gate pending · SHIP NO-GO** |

## SoT logs

| ID | Report | Perf jsonl | Role |
| --- | --- | --- | --- |
| Manual-135644 | `bin/suite_reports/manual_20260904-135644_analyze.json` | `perf_20260904-135644_11364.jsonl` | gate-of-record post R4.5 / pre R4.6 |
| Manual-113457 | `bin/suite_reports/manual_20260904-113457_analyze.json` | `perf_20260904-113457_32744.jsonl` | post phase-4 |
| Manual-202455 | `bin/suite_reports/manual_20260903-202455_analyze.json` | `perf_20260903-202455_21316.jsonl` | gate-of-record pre phase-4 |
| Manual-phase46-post | *pending* | *pending* | gate-of-record post R4.6 |
| Phase46-enter | `bin/suite_reports/mesh_phase46_fz_cold_enter.json` | `perf_20260904-153228_20540.jsonl` | fz-cold-enter (**GO**, enter_max=9) |
| Phase46-fzlong | `bin/suite_reports/mesh_phase46_fz_long.json` | `perf_20260904-153806_14636.jsonl` | fz-manual-long (wall_fly≈123) |
| Phase46-trio-heavy | `bin/suite_reports/mesh_phase46_trio_fly_heavy.json` | `perf_20260904-155027_33264.jsonl` | fly-heavy (fm_finish≈8) |
| Phase45-enter | `bin/suite_reports/mesh_phase45_fz_cold_enter.json` | `perf_20260904-132318_12276.jsonl` | prior enter GO |
| Phase45-trio-heavy | `bin/suite_reports/mesh_phase45_trio_fly_heavy.json` | `perf_20260904-133026_30784.jsonl` | prior fly-heavy |

## Phase gates (post phase-4.6 autofly)

| Gate | Criteria | Latest |
| --- | --- | --- |
| MESH-R15-capture | retry>0, schedule_ok≥2 | **GO** (schedule_ok med 6–9) |
| MESH-R26-completion | fm_finish>0 | **GO** (fly-heavy finish≈8) |
| MESH-R30-fps | wall≤66, stream≤90, fps≥15 | **NO-GO** (fz-long wall≈123; heavy≈414) |
| MESH-SHIP-joint | diet≥40%, holes≤30%, fm>0, mismatch≤10%, fps≥15 | **NO-GO** |
| MESH-parity-manual | holes ≤55% | **pending** (135644: 53% GO holes) |
| fz-cold-enter | enter_unfinished_max≤10 | **GO** (enter_max=9) |
| capture retarget blocked | ≤0.3 | **GO** (enter 0.22 / heavy 0.19; long 0.31) |
| mismatch | ≤15% | **GO** (0.0) |

## Verdict one-liner

Phase 4.6 code **LANDED** (TerrainCompleteCache on ring probes + SoftDefer pin hard-expire + rim hole SoT). Enter **GO**; fz-long wall_fly≈123 vs manual 319; fly-heavy fm_finish>0. SHIP still NO-GO — needs manual gate-of-record vs `135644`.
