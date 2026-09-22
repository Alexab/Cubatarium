# A21 acceptance matrix (DoD §4) — residual update 2026-09-22

## Residual R0–R5 landing

| Step | Status | Notes |
|---|---|---|
| R0 evidence 114954 | DONE | `A21_RESIDUAL_BLACK_114954.md`, end-gate tool |
| R1 classify | DONE | LegalDark requires light revs match |
| R2 admit/PreferKick | DONE | equal-rev FD no longer silent-continue; DirtyAdmit reserve; AF cold `124620` |
| R3 cutover | DONE | `ChunkDemandCutoverEnabled` default ON; `CUBA_DEMAND_CUTOVER=0` rollback |
| R4 provenance | DONE | meshed light stamped on publish candidate |
| R5 eye/DoD | DONE (honest) | scorecards + eye matrix; `operator_visual=UNTESTED`; end-gate FAIL |

## AF after residual

| Metric | 114954 manual | R2 cold `124620` | R5 warm `125120` |
|---|---:|---:|---:|
| mid VB | 59 | 49 | 48 |
| mid FD stalled | 16 | **3** | **0** |
| tail admit_end | 0 | **4** | **4** |
| tail VB / debt | 47 | ≈87 | **83** |
| prefer_kick_n | 0 | 0 | 0 (Dirty path) |
| end_gate | FAIL | FAIL | FAIL |
| input adequacy | n/a | PASS | PASS |
| dual-lane | n/a | PASS (cold) | PASS (warm) |
| merge_green | false | false | false |

## Visual (§4.1)

| Gate | Status |
|---|---|
| Operator eye | **UNTESTED** — see `A21_OPERATOR_EYE_MATRIX.md` |
| Material / reference mesher | UNTESTED |
| Scenarios AF proxy | PARTIAL (adequacy PASS; eye-proxy FAIL) |

## Perf / convergence (§4.2)

| Gate | Status |
|---|---|
| End-of-flight black gate | FAIL (`tools/a21_residual_end_gate.py`) |
| TTR / soak / 60 FPS | UNTESTED |
| Stop orphan pending | PARTIAL — job_trace tail still `admitted` / rev=0 |

## Honesty

`operator_visual=UNTESTED` ⇒ `merge_green=false`. AF ≠ CLOSED.  
Mid stalled and DirtyAdmit headroom improved; residual black census at cruise end remains open.
