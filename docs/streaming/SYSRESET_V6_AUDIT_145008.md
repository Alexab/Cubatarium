# Sysreset v6 audit — SoT manual 145008

Date: 2026-09-21  
Manual regress: `bin/logs/perf_20260921-145008_25236.jsonl`  
Scorecard: `bin/suite_reports/g1_a10_relight/manual_145008_score.json`  
Prior: Sysreset v5 (`SYSRESET_V5_AF_EVIDENCE.md`, commits `e305f72f` / `a168fef7`).  
Earlier SoT: `perf_20260921-112357_29496.jsonl` (v4 regress), `perf_20260921-101105_14028.jsonl`.

## Operator symptoms (145008 vs 112357 / v5 AF)

| Class | 112357 | 145008 | v5 AF cold | Verdict |
|---|---|---|---|---|
| VB fly med | 44.5 | **45** | 22 | OPEN (AF greenwash ≠ manual) |
| VB stalled fly | 12.5 | 6 | ≤2 | partial |
| unfinished max | 79 | **64–70** | ~71 | OPEN (sky-through) |
| PreferKick | ≡0 | ≡0 | ≡0 | OPEN |
| near_focus_holes | ≈0 | ≈0 | ≈0 | sky ≠ VisualHoles |
| **stale_geom mid med** | ~0 | **8** (max 15) | — | **NEW REGRESS** |
| **dark_face_stale max** | ≈0 | **498** | — | **NEW REGRESS (black block faces)** |
| hitch C / cull skip | KEEP | KEEP | KEEP | KEEP |

## Root cause (v5 FaceDebt remesh + geom-stale Accept)

1. **FaceDebt already-known remesh** (`MarkDirtyPriority` cap4) + focus admit → Dirty/FM flood.
2. Remesh bakes while SoftDefer peer still empty / light incomplete → geom-stale GPU/CPU finish.
3. **Geom-stale Accept Retain** keeps / publishes dark quads (`hasFullyDarkFace`) over prior lit without a light-fresh remesh → operator «чёрные грани отдельных блоков».
4. AF cold VB↓ did **not** mirror on manual 145008 — AF≠manual honesty required.

## KEEP (do not revert)

- Focus admit bypass MarkRelit (`horiz≤4` always Dirty)
- FD `dirty∧!pending∧stall≥8` ForceDirty
- `NoteGpuPipelineProgress` / PreferKick without publish-progress gate idea
- Unknown always-hide
- hitch C opaque deadline + transparent resort skip
- FaceDebt mask API / overlay writers / BecameKnown coalesce

## Anti-goals

SoftDefer-for-holes; remesh floors; fog VB latch; Unknown void-emit; PreferKick without pending GPU; FaceDebt on light Accept (v3 D4); hitch C churn; Claim CLOSED by AF-only VB; full rollback of focus admit.

## v6 locked path

1. **Stale-dark reject** — geom-stale Accept Retain forbidden when new mesh dark + prior lit; prior KEEP + one light-fresh RemeshAfterApply/DirtyPriority.
2. **FaceDebt remesh discipline** — SoftDefer peer → debt census only (no Dirty); remesh cap **2**/frame.
3. **AF honesty** — SoT = **145008**; gates include `stale_visual` mid med ≤1 and `dark_face_stale_near` ↓≪498; merge_green only after **manual** eye.
