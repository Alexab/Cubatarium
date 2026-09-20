# Sysreset v3 audit — SoT manual 202031

Date: 2026-09-20  
Manual SoT: `bin/logs/perf_20260920-202031_6988.jsonl`  
Scorecard: `bin/suite_reports/g1_a10_relight/manual_202031_score.json`  
Prior: Sysreset v2 (`SYSRESET_V2_AF_EVIDENCE.md`).

## Operator symptoms (new + tails)

| Class | Metric / observation | vs gate |
|---|---|---|
| X-ray single faces (heal over time) | unfinished max 12; stale_visual 6\|8 | OPEN |
| Hitch (control) | emerge max 90–137; kick/finish ~114 (p36); spikes max 5 | OPEN |
| prior_lit_hold | med ~599, age max 90 | OPEN count |
| cold blink / merge_green | blink ~0.14; merge false | OPEN |
| VB / west / flip / dual | improved vs 191112 | KEEP-ish |

## X-ray causal chain

neighbor Unknown/SoftDefer-hidden → `NeighborHidesFace` suppresses faces on A →
x-ray until B drawable → BecameKnown remesh A. Writers: SoftDefer empty,
PriorLit hold, Accept Retain (OOM/material/I3t). UV = `!IsColumnRenderReady`.

## Hitch classes (not one metric)

| Class | Signature | Root |
|---|---|---|
| A Kick mid-op | emerge≈kick≈finish ~114 | Kick under soft budget |
| B Emerge residual | emerge 90–137, kick≪1 | Consume/ColumnFlow over emerge_cap≈2 |
| C Render | render 94–178 | cull/draw — follow-on |

Prep max~11 — not root. PreferKick≡0 → Dirty flood amplifies B.

## v3 locked path

FaceDebt owning + Accept (geom/material Dirty only) + BecameKnown on load +
emerge ledger + mid-Kick gate + PreferKick bounded + PriorLit count converge.
No SoftDefer-for-holes / remesh floors / fog VB latch / FullyDark peer remesh.
