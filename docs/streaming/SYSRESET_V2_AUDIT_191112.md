# Sysreset v2 audit — SoT manual 191112

Date: 2026-09-20  
Operator SoT: `bin/logs/perf_20260920-191112_44384.jsonl`  
Scorecard: `bin/suite_reports/g1_a10_relight/manual_191112_score.json`  
Prior plan: systemic_streaming_reset after `27beca1c` / SoT `143831`.

## Operator symptoms (191112)

| Class | Metric | Value | vs exit |
|---|---|---|---|
| Blacks | VB fly med\|max | 46\|60 | FAIL (≤35\|45) |
| Blacks | mid_fd_stalled | 24.5 | FAIL |
| Empty | unfinished max | 16 | FAIL (≤4) |
| Wrong-tex | flip fly max | 6 | FAIL (≡0) |
| Wrong-tex | prior_lit_hold | 647\|690 | FAIL (plateau) |
| Walls | dark_face_void fly | 806\|1271 | FAIL (≤500) |
| UW flicker | transparent_cmd_reorder | ≡1 | OPEN |
| Route | west | COVERED | KEEP |
| Dual | dual | ≡0 | KEEP |

## Landed vs intent (v1)

| Intent | HEAD | Status |
|---|---|---|
| Freeze + bisect env | `aac4ec8e` | CLOSED |
| R06 sticky-only peer remesh | SeaSeamRemeshPolicy | CLOSED policy; walls OPEN (other root) |
| Fog VB≥15 latch off | FogPullInPolicy | CLOSED |
| I3t PriorLit TTL 90 | MeshLitGate | PARTIAL — TTL releases bad Replace |
| Live∩Free=0 | MeshPublishContract | CLOSED |
| Accept (geom,light,material) | **not wired** in writer | OPEN |
| ColumnVisualState owning FSM | void helpers only | OPEN |
| PreferKick with publish progress | parameter void; PreferKick=FD+pending | OPEN |
| Seam backup MarkRelit≡0 | reverted `79b7aea0` | OPEN deadlock class |
| Liquid Unknown hide | solids OK; liquid void-emit remains | OPEN |
| NeighborBecameKnown seam | absent | OPEN |

## Retract (overclaim)

AUDIT16 Sysreset 2026-09-20 rows claiming PreferKick-without-progress **CLOSED** and
MeshPublishContract Accept **CLOSED** are retracted. Only Live∩Free + policy
neutralize (R06/fog/TTL helper) remain. Operator gates remain OPEN until v2.

## v2 locked path

Owning ColumnVisualState store + real MeshPublishContract Accept + Unknown
liquid hide + NeighborBecameKnown seam + transparent order stabilize.
No SoftDefer-for-holes / remesh floors / fog VB latch / FullyDark peer remesh.
