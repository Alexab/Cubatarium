# 02 — Baseline delta: P13 eye-best → HEAD (P17)

**Дата:** 2026-08-26  
**Baseline:** `9f502dd50d81a0e07858268eef59fdfb8bc0196f` (FZ27-P13)  
**HEAD:** `1f3f767554c1b1fa5313279baf0f50136eb8173d` (FZ27-P17)  
**Метод:** `git diff` / `git show` (без binary A/B)

## Diff --stat (scoped)

`git diff --stat 9f502dd5..HEAD -- src/World/Streaming src/Render/Mesh src/World/Core src/World/Diagnostics/FramePerfMonitor.cpp`

| Файл | Δ |
| --- | --- |
| `ChunkMeshCache.cpp` | miss_undrawn hold |
| `FramePerfMonitor.cpp` | SoftDefer site counters |
| `AntiFlickerPolicy.h` | MaxAge after retarget, decay |
| `ChunkEmergeCoordinator.cpp` | cruise pin / owned FM / stand age |
| `ColumnFlowExecutor.cpp` | moving F3/F4 repair |
| `MeshWorkAdmission.h` | mh≤4, stand VB heal signal |
| `OceanCruisePolicy.h` | moving no_ticket floors |
| `SoftDeferEmptyPolicy.h` | U1/U2 predicates |
| `WorldStreaming.cpp` | site telem + VB cadence |
| **Итого** | **~9 files, +126 / −13** |

P14 (`07f6d745`) полностью откатан `02f537ce` → в net-diff к P13 его нет.

## Commit list

| SHA | Subject | Зачем |
| --- | --- | --- |
| `07f6d745` | FZ27-P14 | F1–F6 frontier flicker — **FAIL** |
| `02f537ce` | Revert P14 | SoftDefer thrash / MaxAge ratchet / F5 deadband |
| `4b7ec652` | FZ27-P15 | Site A/B telem; MaxAge **decay**; moving F3/F4 only |
| `ea854043` | FZ27-P16 | nh≤1 pin cruise; owned FM nh≤2 |
| `1f3f7675` | FZ27-P17 | miss_undrawn hold; mh≤4; VB cadence; stand owned FM |

---

## What P13 had that still looks best

1. **`ShouldProtectLitSettleRemesh`** — remesh floor≥2 при stale dark + holes.
2. **Stale keep_h** — StarveRemeshKeepHoriz с stale_keep 2/3.
3. **FIFO protect prune exemption** — stale FullyDark в protect horiz не выкидывается из Dirty.
4. **Capture / PL wins не трогались.**

Constraint: не расширять Capture / MaxAge / P13 remesh protect без доказанного root-cause.

## What post-P13 cured discard

Root (`100413`): `MarkDirtyPriority` Forgot when `!Drawable` + inflight + `!soft_undrawn` → discard ~0.47/spike.

**P17 A:** `miss_undrawn=true` → hold supersede. Post: discard/spike **0.002–0.035**, stale late **0**.

## What failed to heal VB/unf stand

| Фактор | Роль |
| --- | --- |
| P14 F1/F5 | FAIL SoftDefer — reverted |
| P15b F3/F4 | moving-only — не stand VB |
| P16 | SoftDefer KEEP; sticky miss не снят |
| P17 B VB cadence | stream tax; не drain VB |
| P17 A | discard cured; VB/unf FAIL |

Residual HEAD: SoftDefer KEEP + discard cured; **VB/unf never-heal** на long stand; stream_ms≈41.

---

## KEEP vs SUSPECT

### KEEP
- P13 remesh protect + stale keep_h + FIFO protect prune
- P15a Site A/B observe-only
- P15c MaxAge decay (no ratchet)
- SoftDefer standstill gates
- P17 miss_undrawn hold
- P17 mh≤4 + stand age>300 owned FM

### SUSPECT / DO NOT RE-LAND
- P14 F5 publish deadband in control
- P14 F1 MaxAge ratchet
- P14 F2 count-gating ingress
- Capture++ / fifo++ / MarkMissing storm without consumer ready
- «Усиление» P13 remesh protect как ответ на VB stand

### CONDITIONAL
- P15b moving F3/F4 — KEEP cruise; not stand VB
- P17 VB cadence — KEEP stream; not VB cure
- `ShouldPreferStandVbHealOwnership` — signal only

---

## Per-phase

### P14 — FAIL → full revert
F1 ratchet + F5 deadband → SoftDefer thrash на NE standstill. Repro: no-teleport `fz-ne-frontier-stand`.

### P15 — SoftDefer observe + moving repair
Site split + MaxAge decay; moving F3/F4 only. Manual `221516`: SoftDefer OK; sticky miss open.

### P16 — underfeet / FirstMesh ownership
U1 nh≤1 pin in cruise; U2 owned FM nh≤2. SoftDefer KEEP; miss_sticky still 1.0 → PARTIAL FAIL → P17 mh≤4.

### P17 — discard / mid-rim / VB cadence
A miss_undrawn; B stand VB cadence; C mh≤4 + stand age FM. **PARTIAL PASS:** discard/stale cured; VB/unf/stream open.

## References
- `docs/streaming/COLUMN_PIPELINE_EXPERIMENT_LOG.md` §§ P14–P17
- `bin/research_srbr_20260826/00_index.md`
- SoT: `170807`, `221516`, `100413`, `104722`, `112418`
