# Audit 2026-09-16 S9 closure matrix

Source audit: commit `7a658525` / `docs/streaming/CURRENT_STATE_AUDIT_2026-09-16.md`.

## Absolute gates (must be green)

| Gate | Owner step | Status |
|---|---|---|
| live/free allocation overlap = 0 | S1 `publication_audit` | code + CTest |
| empty Replace / Remove last material | S2 | code + CTest |
| reorder bumps table identity | S2 | code + CTest |
| packed excluded when MDI resident | S2 GeometryEngine | code |
| eye-proxy absolute holes + missing fields | S0 | GateRepro |
| `operator_visual=None` ↛ merge_green | S0 scorecard | code |
| west route `cx≤−3` or UNTESTED | S0 | metrics + autofly COVERED |
| per-frame GPU kick/finish timers | S3 | code |
| source light stamp on publish | S4 | code |
| per-column stalled remesh bookkeeping | S5 | code |
| draw-oracle age not reset on unrelated lit | S5 | CTest |
| snapshot credit lifetime with Store | S6 | code |
| spawn-ring cache world epoch + focus.y | S7 | code |
| no independent 4ms GPU floors | S7 | code |
| cull stats buffer-update barrier | S8 | code |

## Product acceptance (still OPEN without manual eye)

- Gate of record: `product-174657` **no-teleport** west.
- Autofly adequacy ≠ CLOSED.
- Manual operator west: mid blacks + texture-swap vs SoT `074859` / `123828`.
- Proposed SLA profile (not measured achievement): 16.7 ms or 33.3 ms frame;
  streaming slice ~5 ms; edit→visible p95≤100 / max≤250.

### West route unblock (land-eye follow)

Sticky once-per-run `CruiseEyeY`/`terrain+12` pinned Y at spawn and stuck
autofly at `focus_cx≈2` for years of suite history (`focus_west_delta_cx=5`).
`AppRunner` now continuously follows column `FindHighestSolidY+12`.
`product-174657` default fly = **55s** (reaches `cx≤−3` without overshoot to −20).
Eye-proxy mid band is spatial `focus_cx∈[2,5]` across all periods (not temporal mid_third).

### Cold / warm after west fix (`194126` / `194421`)

| Signal | Cold `audit16_west55_cold` | Warm `audit16_west55_warm` |
|---|---|---|
| incomplete mid | **0** KEEP | **0** KEEP |
| eye_proxy | PASS (mid COVERED, stale 5, blink 0) | PASS (mid COVERED, stale 5, blink 0) |
| west_route_coverage | **COVERED** (cx_min=−9) | **COVERED** (cx_min=−10) |
| adequacy | PASS | PASS |
| dual-lane / mid stalled | FAIL (stalled~54) — G1 OPEN | FAIL (stalled~58) — G1 OPEN |
| merge_green | **false** (`operator_visual` UNTESTED) | **false** |
| operator west eye | **UNTESTED** (manual required) | **UNTESTED** |

Reports: `bin/suite_reports/g1_a10_relight/audit16_west55_{cold,warm}.json`  
Scores: `bin/suite_reports/g1_a10_relight/audit16_west55_{cold,warm}_score.json`

Cold×3 / warm×3 soak and operator eye remain follow-on before product CLOSED.
Absolute ownership/gates above are landed in code+CTest; west autofly route is COVERED.

## Plan completeness (S0–S9 vs audit plan)

| Stage | Absolute / gate intent | Status |
|---|---|---|
| S0 | CTest publication_audit, fail-closed eye, west field, CI branch, N04 freeze | **done** |
| S1 | retain untouched, pool ledger, overlap tests | **done** |
| S2 | empty Replace/Remove, packed exclusion, RepresentationSwitch callback | **done** (full typed `PublicationDelta` enum / single registry writer — deferred) |
| S3 | per-frame GPU timers, fail-closed eye, analytic oracle MVP | **done** (full rename of misnamed counters — deferred) |
| S4 | source light stamp, boundary ADR | **done** (full immutable overlay rewrite — deferred) |
| S5 | per-column stalled, FullyDark diagnostic-only, draw-oracle age | **done** |
| S6 | snapshot credit lifetime with Store | **done** (shared concurrency budget — partial / existing FrameDeadline) |
| S7 | spawn-ring world epoch, drop extra 4ms floors | **done** (full readiness projection+deadline ledger — partial) |
| S8 | cull stats barrier + pass/frame ids | **done** (frustum parity fixtures soak — deferred) |
| S9 | absolute gates + west COVERED cold/warm evidence | **partial**: west COVERED + cold/warm pair; cold×3/warm×3 soak + manual eye still open |

## KEEP

N01 incomplete=0 class, LegalDark rollback, frustum N02, cooldown N06,
I3t hold-prior (temporary until Replace/Remove convergence proven in field).

## FREEZE

N04 census FullyDark remesh caps / wrong-tex gates on misnamed `pass_mdi_stale_*`.
