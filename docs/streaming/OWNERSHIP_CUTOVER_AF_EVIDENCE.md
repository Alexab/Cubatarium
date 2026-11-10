# Ownership cutover AF evidence (fog ON)

Date: 2026-09-21  
Commits: `f1a594a1` SeamVisibility; `5eb9de26` LightConverge; `f95826dc` SeamDebt+Hitch  
SoT regress: `perf_20260921-161139_28636.jsonl`  
Black-clean anchors: `27beca1c`, `dd7871ab`  
Gates: [SYSRESET_OPERATOR_AF_GATES.md](SYSRESET_OPERATOR_AF_GATES.md)  
**AF ≠ manual — CLOSED only after operator eye.**

## AF suite

| Run | perf | scorecard |
|---|---|---|
| cold | `bin/logs/perf_20260921-165729_23124.jsonl` | `ownership_cold_score.json` |
| warm | `bin/logs/perf_20260921-165942_1528.jsonl` | `ownership_warm_score.json` |
| dive | `bin/logs/perf_20260921-170142_6444.jsonl` | `ownership_dive_score.json` |

## Gate matrix vs 161139

| Gate | 161139 | cold | warm | dive | Verdict |
|---|---|---|---|---|---|
| west | COVERED | COVERED | COVERED | COVERED | PASS |
| unfinished max | 67 | **25** | **25** | **19** | PASS (↓) |
| SoftDefer empty owned max | 200 | **0** | 44 | 86 | cold PASS; warm/dive ↓ |
| PreferKick | ≡0 | max **1** | ≡0 | ≡0 | partial (cold flicker) |
| VB fly med | 48 | 29 | 44.5 | 29 | trend ↓ (AF≠manual) |
| FD stalled med | 11 | 0.5 | 3 | 0 | ↓ |
| dark_face_stale max | 103 | 89 | **1618** | **1140** | warm/dive OPEN regress |
| eye stale_visual mid | — | 0 | 3 | 0 | warm OPEN |
| opaque_cull_skipped | 1 | 1 | 1 | 1 | hitch C KEEP |
| wall_ms max | 157 | **301** | 294 | 219 | hitch OPEN (AF) |
| kick max | 1.6 | 7.3 | 13.7 | **52** | dive OPEN |
| merge_green | false | false | false | false | OPEN (manual) |

## Wins

- **Sky / unfinished:** SoftDefer≠Unknown + FaceDebt census — unfinished 67→≤25; SoftDefer owned burst 200→0 (cold).
- LightConverge: PreferKick max 1 on cold (was ≡0 forever); focus admit KEEP.
- Hitch C cull_skip KEEP; west/eye cold PASS.

## Still OPEN

- dark_face_stale spike warm/dive (Unlit emit may surface lit-stale faces — watch manual).
- wall_ms early/AF hitch still high; SoftDefer deadline skip not enough alone.
- PreferKick still mostly 0; ForceDirtyStuck remains live escape.
- merge_green blocked until **manual** eye vs 161139 / black-clean anchors.

## Operator note

Build: `f95826dc`. Manual SoT path World_164 west. Confirm: no sky-through faces, no late FullyDark blacks, UI lag. AF unfinished win must not greenwash operator.
