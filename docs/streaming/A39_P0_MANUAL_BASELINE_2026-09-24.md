# A39 P0a — manual baseline 20260924-092417

Date: 2026-09-24  
Parent: [A38_CONFORMANCE](A38_CONFORMANCE_2026-09-23.md), plan A39 algorithm deepplan

## Evidence (live manual, not AF)

| Artifact | Path |
|---|---|
| Perf | `bin/logs/perf_20260924-092417_33428.jsonl` |
| EnterLit | `bin/logs/enter_lit_20260924-092448.jsonl` |
| Score | `bin/suite_reports/a38/manual_092417.json` (`pass=false`, 16/39) |

No `FlightSim` in INFO — operator flight. West route **UNTESTED** (`cx_min=−2`).

## Key metrics (FAIL baseline for A39)

| Metric | Value |
|---|---|
| unfinished_visual | 81 (end ~104–106) |
| demand_stop_converged | false |
| unsat geom/light/face/cov med | 198 / 47 / 15 / 8 |
| chunk_meshed_unlit_med | 33 |
| visible_black_focus_n | 61 (end 104) |
| holes_rate (hole_key=unfinished) | 0.83 |
| near_focus_holes med | ~0 |
| defect_class_primary | LightStale (2) |
| Ring / Kick | OFF |

## Invalid for Gate 8

- A38 AF matrix under `bin/suite_reports/a38/{cold,warm,far}*` — `git_sha=b404b9dc` (A37) + dirty_diff ≠ clean `c0785daf`
- `eye_proxy` / unfinished `holes_rate` ≠ mesh-hole Gate 8
- Clean-SHA AF deferred to **P0b after P1–P3** (not pre-fix on light+1 HEAD)

## A39 code targets

P1 ban light+1; P2 Admit lifecycle; P3 UnknownPeer; P4 fluid install; P5 Cross expected; P6 attribution + clean matrix + eye.
