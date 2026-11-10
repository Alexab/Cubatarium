# Sysreset v5 audit — SoT manual 112357

Date: 2026-09-21  
Manual regress: `bin/logs/perf_20260921-112357_29496.jsonl`  
Scorecard: `bin/suite_reports/g1_a10_relight/manual_112357_score.json`  
Prior: Sysreset v4 (`SYSRESET_V4_AF_EVIDENCE.md`, commit `eca9289c`).  
Baseline SoT: `perf_20260921-101105_14028.jsonl`.

## Operator symptoms (112357 vs 101105)

| Class | 101105 | 112357 | Verdict |
|---|---|---|---|
| VB fly med | 44.5 | 44.5 | flat |
| VB stalled fly | 7.5 | **12.5** | REGRESS blacks |
| unfinished med/max | 48/63 | **61/79** | REGRESS |
| PreferKick | ≡0 | ≡0 | still dead |
| near_focus_holes | ≈0 | ≈0 | sky ≠ VisualHoles |
| softdefer_empty max | 93 | **211** | REGRESS |
| hitch C / cull skip | — | skip≡1 | KEEP (83a7b8dd) |
| kick / emerge / prior_lit | KEEP | KEEP | KEEP |

## Root cause (v4 eca9289c)

1. **Focus Dirty admit drop** in `ExecuteLitApplyPlan` — when admit exhausted, focus `horiz≤4` dropped same as hinterland; comment “PreferKick owns pending GPU” false at PreferKick≡0 → no Dirty → no pending → PreferKick never starts → FullyDark stalled↑.
2. **FaceDebt without Dirty** — overlay Missing sets FaceDebt census / unfinished, but already-drawable peers never remesh (BecameKnown only on FirstDrawable).
3. **NotePublishProgress** on MarkRelit even when 0 Dirty admitted — progress greenwash.

## KEEP (do not revert)

- FaceDebt mask API / overlay writers  
- `NoteGpuPipelineProgress` on Kick/Finish  
- PreferKick without publish-progress gate + skip_already_dirty PreferKick (idea)  
- Unknown always-hide  
- hitch C opaque deadline defer + transparent resort skip  

## Anti-goals

SoftDefer-for-holes; remesh floors; fog VB latch; Unknown void-emit reopen; PreferKick without pending GPU; FaceDebt on light Accept; hitch C churn; R06 remesh flood.

## v5 locked path

Focus admit bypass (`horiz≤4` always Dirty) + FD `dirty∧!pending∧stall≥8` ForceDirty + FaceDebt→Dirty on already-known peer (cap4) + progress honesty.
