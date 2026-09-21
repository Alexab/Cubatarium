# Sysreset v4 audit — SoT manual 101105

Date: 2026-09-21  
Manual SoT: `bin/logs/perf_20260921-101105_14028.jsonl`  
Scorecard: `bin/suite_reports/g1_a10_relight/manual_101105_score.json`  
Prior: Sysreset v3 (`SYSRESET_V3_AF_EVIDENCE.md`).

## Operator symptoms

| Class | Metric / observation | vs gate |
|---|---|---|
| Sky-through faces (holes=0) | unfinished med 48–63; SoftDefer→overlay→Unknown suppress | OPEN |
| FullyDark / VB plateau | VB fly 44.5; mid_fd_stalled 11–19 | OPEN |
| PreferKick≡0 + Dirty FM flood | prefer_kick max 0; dirty_fm~85; dirty_dropped~40k | OPEN |
| Hitch class C | render 79–200; opaque cull~195 / transparent~106 | OPEN |
| Mesh kick/emerge / prior_lit | kick≪1; emerge fly~5; prior_lit~8 | KEEP (v3 wins) |

## Sky causal chain

B SoftDefer/!drawable → overlay `missingNeighborFaces` →
`GetNeighborLoadState` forces Unknown → `NeighborHidesFace` suppresses emit on A →
sky through seam until BecameKnown remesh A. Not VisualHoles; not FullyDark blacks.

## PreferKick chicken-egg

PreferKick needs `has_publish_progress`; progress only from MarkDirty;
`skip_already_dirty` carved PreferKick out → with dirty_fm≫schedule_ok kick never runs.

## Hitch classes

| Class | Signature | Root |
|---|---|---|
| A/B mesh | closed on 101105 fly | v3 ledger/mid-Kick |
| C Render | cull ~195 / transparent ~106 | GeometryEngine — independent of FD drain |

## v4 locked path

FaceDebt on overlay-missing + BecameKnown remesh (cap4) + PreferKick progress from GPU +
skip_already_dirty PreferKick + Dirty admission + opaque cull/transparent budget.
No SoftDefer-for-holes / remesh floors / fog VB latch / Unknown void-emit reopen.
