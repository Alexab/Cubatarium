# Ownership cutover audit — SoT manual 161139

Date: 2026-09-21  
Manual regress: `bin/logs/perf_20260921-161139_28636.jsonl`  
Prior: Sysreset v6 (`SYSRESET_V6_AF_EVIDENCE.md`).

## Operator black-clean anchors (not luck)

| SHA | Era | Role |
|---|---|---|
| `27beca1c` | AUDIT16 «почти OK» | operator no black chunks |
| `dd7871ab` | **end v3** AF close | PreferKick required `has_publish_progress`; Dirty admit direct |

Blacks return at **`eca9289c` (v4)**: PreferKick without progress + PreferKick→`NotePublishProgress` + dry-admit drop focus Dirty.

## Symptoms 161139 vs anchors

| Class | black-clean | 161139 | Root |
|---|---|---|---|
| Sky-through block faces | residual OPEN | unfinished 56/67, SoftDefer max 200 | SoftDefer→overlay→Unknown hide |
| FullyDark chunk blacks | **clean** | VB mid/late spike, PreferKick≡0 | v4 PreferKick chicken-egg |
| Black block faces | — | dark max 103 (was 498) | v6 KEEP |
| UI hitch | — | wall early 153, cull_skip 1 | SoftDefer scan + hitch C leftover |

## Four owners

| Owner | Contract | Must not |
|---|---|---|
| **SeamVisibility** | Unknown only if unloaded; SoftDefer→Unlit emit | SoftDefer→overlay Unknown |
| **LightConverge** | v3 PreferKick∧progress; KEEP v5 focus admit + ForceDirtyStuck | PreferKick=progress alone |
| **SeamDebt** | FaceDebt census only; BecameKnown after peer ready | MarkDirtyPriority from FaceDebt mask |
| **HitchBudget** | one FrameDeadline leftover | parallel caps fighting hitch C |

## KEEP

Unknown true-unload; hitch C; D4 no FaceDebt on light Accept; v6 dark geom-stale reject;
v5 focus admit bypass; R06 sticky-only; stop amortize U-A+K-B.

## Anti-goals

SoftDefer-for-holes; fog VB latch; remesh floors; PreferKick without Dirty progress;
FaceDebt on light Accept; hitch C churn; AF-only CLOSED; hard-reset to `27beca1c`.

## Wire order

Refresh → emerge admit (light) → bake → MeshPublish → seam observe → hitch leftover → cull.

Phase C before D (light path before removing FaceDebt Dirty pump).
