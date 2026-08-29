# 08 — Decision memo (flight perf)

## FP0 outcome — DONE

- Research pack `bin/research_flight_20260829/` created.
- FP0–FP5 gates in `flight_sim_phase_gate.py`; cruise metrics in analyzer.

## FP1–FP5 implementation — LANDED

| Phase | Commit | Key change |
| --- | --- | --- |
| FP1 | `38b20e79` | witness pin 120f, damp retarget unf>5, enter blocks capture retarget |
| FP2 | `7ef76f6a` | FM floor moving holes, emerge budget min 4 under visual holes |
| FP3 | (in 7ef76f6a) | `ShouldProtectRemeshUnderTicketedVbCruise` |
| FP4 | `8cd36477` | `ColumnJobGraph.h` stage model |
| FP5 | `b5907e73` | stream phase backpressure in `StreamingPressure` |

## Autofly verification (2026-08-29)

| Run | FP-enter | FP1 | FP2 highlights |
| --- | --- | --- | --- |
| `fz-cold-enter` | **PASS** (unfinished=4, ring=2) | capture_retarget med **3** (was 12) | enter non-regress OK |
| `fz-manual-plateau` | — | — | schedule_ok **4**, unfinished **1**, holes_rate **0.55** (was 1.0) |

**Guardrail:** FP-enter PASS — enter-load fix preserved.

**Remaining:** relight_completed spike still 0 on cold-enter cruise; miss_stuck 26s; stream_ms gate needs segment fix; VB 59→target 25.

## SHIP status

**PARTIAL** — architecture slice landed; iterate relight apply + stream budget before FP5 DoD.
