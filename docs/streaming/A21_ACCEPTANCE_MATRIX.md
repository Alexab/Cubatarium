# A21 acceptance matrix (DoD §4)

## Visual (§4.1)

| Gate | Criterion | Status |
|---|---|---|
| Material ID | 0 wrong IDs in agreed area | UNTESTED (needs ID buffer) |
| Opaque surface | no unexplained disappearance | UNTESTED (needs reference mesher) |
| Debug buffers | chunk/artifact/material/depth/light gen | PARTIAL |
| Scenarios | cold/warm, 180° turn, stop, dive, edits… | AF proxy PARTIAL |
| Operator eye | manual west cruise | UNTESTED |

PASS proxy never replaces PASS visual gate.

## Perf / convergence (§4.2)

| Gate | Criterion |
|---|---|
| 60 FPS profile | P95≤16.7, P99≤33.3 steady; >100ms only load/reset |
| TTR SLA hypothesis | 95%≤1s, max≤3s on reference scene after P0 |
| Soak | ≥15 min + ≥5 cold/warm replays |
| Memory | credit owns payload until last consumer |
| Stop | no orphan pending / infinite Retain |

## Honesty

`operator_visual=UNTESTED` ⇒ `merge_green=false`. AF ≠ CLOSED.
