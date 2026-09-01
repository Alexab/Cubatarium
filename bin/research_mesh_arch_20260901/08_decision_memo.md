# Decision memo — mesh architecture H0–M5

**Status: NO-GO** — код и harness landed (`f62041bc`); perf gates fail on holes/FM queue.

## Autofly results (2026-09-01, no-teleport)

| Run | chunks | wall_fly_med | emerge | holes | blocker | M2 snap |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| replay-manual | 11 | 44.3 | 47.4 | 100% | empty_fm_queue | 0.0 |
| fly-heavy (H1) | 32 | 92.0 | 58.0 | 100% | empty_fm_queue | 0.0 |
| fz-manual-long | — | 83.1 | — | 100% | empty_fm_queue | 0.0 | 125 |
| manual 124859 | — | 277.8 | 122.3 | 95.2% | unknown | — |

`parity_within_2x`: **PASS** (wall/stream/emerge ratios within 2× manual).

## Gate matrix (replay-manual)

| Gate | Result | Notes |
| --- | --- | --- |
| MESH-H0-baseline | GO | chunks_traveled ≥ 3 |
| MESH-M0-waterfall | GO | drain med > 0; kick/finish present |
| MESH-M1-capture | NO-GO | emerge 47ms, holes 100% |
| MESH-M2-worker | GO | main mesh_snapshot_ms = 0 |
| MESH-M3-gpu | GO | pool_unsync ≤ 50 |
| MESH-M4-ownership | NO-GO | holes 100%, diet share 0% |
| MESH-parity-manual | NO-GO | holes 100% > 55% cap |

## GO/NO-GO by phase

| Phase | Verdict | Rationale |
| --- | --- | --- |
| H0 | GO | baseline trio captured |
| H1 | GO | harness + parity metrics; holes gap persists |
| M0 | GO | waterfall live; dominant stage = drain |
| M1 | NO-GO | empty_fm_queue majority; store diet not closing FM starvation |
| M2 | PARTIAL | worker enabled; M2c main Capture fallback remains |
| M3 | PARTIAL | pool batch only; M3-1..M3-3 not shipped |
| M4 | PARTIAL | single-path guard; duplicate MarkDirty owners remain |
| M5 | NOT VERIFIED | no pending_light gate run |
| M6 | DEFERRED | per plan |

## Next actions

1. FM queue refill: `fm_dirty_enqueue` / `dirty_fm_med` → manual parity (M1-4)
2. M2c: remove hot-path `TakeOrRefresh` fallback after worker soak
3. Holes telemetry: `unfinished_visual` stuck at 12 — align autofly hole class with manual
4. Research pack gaps: `04_hotspot_matrix.md`, `05_industry_scorecard.md`, `07_roadmap.md`, `raw/`

## Waivers

None.
