# Era30 Ocean Autofly Parity Research (H0)

SoT manual: `bin/logs/perf_20260810-104841_30332.jsonl`  
SoT autofly smoke: `bin/logs/perf_20260810-113512_16520.jsonl`  
Parity JSON: `bin/iter_reports/era30_ocean_parity.json`

## Root-cause matrix (baseline pre-fix)

| # | Factor | Evidence (manual / autofly) | Mitigation |
|---|--------|----------------------------|------------|
| H1 | Teleport cold start | unfinished_idle 32 vs 16; enter debt lower on autofly | `ocean-cruise-enter` (no teleport, idle≥45s) |
| H2 | Wall pressure | wall_fly_med 103 vs 41 (ratio ~0.40) | `ocean-cruise-stress` (sprint + fly 90s) |
| H3 | Residency gap | chunk_count_end 1040 vs 848 | stress + enter path |
| H4 | Void gate blind on smoke | fly_void_near_max 1328 vs **0** | parity gate; stress must void≥400 |
| H5 | frontier_pressure | manual fly_frontier_pressure_frac **0.42**; smoke 0 | P1 void-only `IsFrontierPressure` |
| H6 | False green smoke | OCEAN_CRUISE GO with holes 8% vs manual 79% | OCEAN_CRUISE_STRESS + OCEAN_MANUAL DoD |

## Harness scenarios

- `ocean-cruise` — smoke (teleport, idle 8)
- `ocean-cruise-enter` — full enter path
- `ocean-cruise-stress` — resume + sprint + fly 90
- `ocean-cruise-short` — idle 3 stop-debt snapshot

## Gates

- **OCEAN_CRUISE** — smoke (soft: relight_drain, enter_app, fly_visible_black_max)
- **OCEAN_CRUISE_STRESS** — parity regression (void≥400, holes≥40%)
- **OCEAN_MANUAL** — DoD on manual analyze (CLOSED only when GO + eye)

## Code (Era30 P0–P6)

- `OceanCruisePolicy.h` — predicates TD-066
- `IsFrontierPressure` — void/VB without gen/async gate
- Relight drain without miss; void columns keep RelightThenMesh
- fluid_map cruise throttle; Capture retarget damp; enter_app hard cap 200ms
