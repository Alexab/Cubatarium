# A21 residual blacks — manual `114954`

Date: 2026-09-22  
Canon flight: `bin/logs/perf_20260922-114954_30808.jsonl`  
Scorecards: `bin/suite_reports/g1_a10_relight/a21_residual_*_score.json`  
End-gate: `bin/suite_reports/g1_a10_relight/a21_residual_114954_end_gate.json`  
`operator_visual=UNTESTED` (eye FAIL reported by operator) ⇒ `merge_green=false`.

## Mid-corridor comparison

| Flight | VB med | FD census med | FD stalled med | legal_dark med | mid_stalled_gate |
|---|---:|---:|---:|---:|---|
| `114954` (manual residual) | 59 | 60 | 16 | 0 | FAIL |
| `092307` (AF cold same day) | 34 | 34 | 0 | 0 | PASS* |
| `183133` (anchor) | 60.5 | 61 | 16.5 | 0 | FAIL |

\*092307 mid stalled low but VB still ~34; symptom_reproduction not merge criterion.

## End-gate `114954` (last 3 periods)

| period | VB | debt−legal | admit_end | dirty_dropped | prefer_kick | fm_enqueue |
|---:|---:|---:|---:|---:|---:|---:|
| 19 | 79 | 79 | 2 | 12452 | 0 | 0 |
| 20 | 50 | 50 | 0 | 12995 | 0 | 2 |
| 21 | 47 | 47 | 0 | 13512 | 0 | 0 |

`end_gate_pass=false`: debt persists; FD without admit/kick on tail.

## Job-trace tail

64 `job_trace` events; tail stuck at `stage=admitted` with `desired_rev=0` / `published_rev=0` on repeated coords (`(-5,2,0)`, `(1,3,6)`, `(-1,3,1)`). No publish/retire progress in ring buffer dump.

## Hypotheses (ordered)

| ID | Claim | Evidence |
|---|---|---|
| H1 | DirtyAdmit starvation outside focus | `dirty_dropped`→13k, `dirty_admit_budget_end=0` mid-cruise |
| H2 | PreferKick dead in prod | `mark_relit_prefer_kick_n=0` all periods |
| H3 | Equal-rev / AlreadySatisfied skips remesh while FullyDark drawable remains | planner LightValidity + MarkRelit `AlreadySatisfied` skip; legal_dark=0 while debt≈VB |
| H4 | Column FaceDebt dual-path (cutover OFF) | `ChunkDemandCutoverEnabled=false` |

## Plan mapping

R1 classify → H3 telemetry; R2 admit+kick → H1/H2/H3; R3 cutover → H4; R4 provenance; R5 eye/DoD.

## Post-R2 AF cold (`perf_20260922-124620_1120.jsonl`)

- mid FD stalled 16 → **3**; DirtyAdmit tail **4** (was 0).
- input adequacy PASS; dual-lane PASS; eye-proxy FAIL (holes blink).
- end_gate still FAIL (tail VB≈87); `prefer_kick_n` stays 0 (schedules MarkDirty, not PreferKick GPU list).
- `operator_visual=UNTESTED`; `merge_green=false`.

## Post-R3/R4 AF warm (`perf_20260922-125120_2288.jsonl`) — R5 scorecard

| period | VB | debt−legal | admit_end | dirty_dropped | prefer_kick | fm_enqueue |
|---:|---:|---:|---:|---:|---:|---:|
| 42 | 83 | 83 | 4 | 29351 | 0 | 2 |
| 43 | 83 | 83 | 4 | 29351 | 0 | 0 |
| 44 | 83 | 83 | 4 | 29351 | 0 | 0 |

- mid FD stalled **0**; DirtyAdmit tail **4** (H1 partial: no longer admit=0).
- input adequacy PASS; dual-lane **warm PASS**; eye-proxy FAIL; end_gate FAIL.
- job_trace tail still `stage=admitted` with rev=0 — no stop convergence to 0 orphans.
- Eye matrix: `A21_OPERATOR_EYE_MATRIX.md`. **CLOSED withheld** until operator eye.
