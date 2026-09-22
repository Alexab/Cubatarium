# A21 acceptance matrix (DoD §4)

## Phase landing (code + docs)

| Phase | Landed | Notes |
|---|---|---|
| P0 | YES | run manifest, schema v2, job trace, input adequacy, repro, fixtures |
| P1 | YES | cull/shell/credit/material/fluid identity + PreferKick |
| P2 | YES | demand store, coalesce, FaceDebt peer_gen, reconcile, cutover **flag OFF** |
| P3 | YES | ArtifactManifest + shared validator + publication epochs |
| P4 | YES | LightValidity, BC doc, SeamCoverageManifest, light reference compare helpers |
| P5 | YES | op inventory, work slots shared, critical unit cost gate |
| P6 | YES | versioned fluid summary, incomplete-tile age, map scroll strips |
| P7 | YES | RingReadinessBudget Evaluate wired; flag **OFF**; fog≠hole-close |
| P8 | PROFILE-GATED | options documented; no default-path opts without before/after AF |

## Visual (§4.1)

| Gate | Criterion | Status |
|---|---|---|
| Material ID | 0 wrong IDs in agreed area | UNTESTED (needs ID buffer) |
| Opaque surface | no unexplained disappearance | UNTESTED (needs reference mesher) |
| Light vs reference | cave zero OK; stale halo reject | PARTIAL (helpers + LightValidity); full world ref UNTESTED |
| Debug buffers | chunk/artifact/material/depth/light gen | PARTIAL |
| Scenarios | cold/warm, 180° turn, stop, dive, edits… | AF proxy PARTIAL |
| Operator eye | manual west cruise | UNTESTED |

PASS proxy never replaces PASS visual gate.

## Perf / convergence (§4.2)

| Gate | Criterion | Status |
|---|---|---|
| 60 FPS profile | P95≤16.7, P99≤33.3 steady; >100ms only load/reset | UNTESTED (product profile) |
| TTR SLA hypothesis | 95%≤1s, max≤3s on reference scene after P0 | UNTESTED hypothesis |
| Soak | ≥15 min + ≥5 cold/warm replays | UNTESTED |
| Memory | credit owns payload until last consumer | PARTIAL (P1 snapshot credit) |
| Stop | no orphan pending / infinite Retain | UNTESTED |

## Honesty

`operator_visual=UNTESTED` ⇒ `merge_green=false`. AF ≠ CLOSED.
Any driver/backend, manual picture, or missing reference remains UNTESTED.
Cutover / RingReadiness / P8 second-order opts stay flag-gated until evidence.
