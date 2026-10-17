# 06 — Architectural options

Дата: 2026-08-26  
Связь с `docs/streaming/ARCHITECTURE_OPTIONS.md` (V1–V4) и evidence H1–H10.

## Decision matrix

| Criterion | A Minimal | B Medium | C Deep |
| --- | --- | --- | --- |
| Effort | S (days) | M (1–2 wk) | L (3+ wk) |
| Risk | Low | Med | High |
| Hits H1 ghost Dirty | **Yes** | Yes | Yes |
| Hits H3 ticketed VB | Partial | Yes | Yes |
| Hits H4 stream | Partial | Yes | Yes |
| SoftDefer KEEP safe | Yes | Yes | Must preserve |
| Aligns to ARCHOPTS | V1 patch | V2+scan | V4 unified |
| Recommended now | **Yes P0** | Follow-up | Later |

---

## Variant A — Minimal (policy consolidation + ghost Dirty / miss ownership)

**Суть:** не трогать SoftDefer/Capture/P13 remesh protect. Убрать Dirty !HasChunk re-admit; гарантировать schedule для sticky miss nh≤0–1 HasChunk; приоритет ticketed unlit remesh на focus.

**Files (expected):**
- `ChunkMeshCache.cpp` — don’t MarkDirty / requeue coords without HasChunk; prune ghost Dirty bulk
- `ChunkEmergeCoordinator.cpp` / `SoftDeferEmptyPolicy.h` — owned FM only if HasChunk
- `MeshWorkAdmission.h` — stand HoleDrain prefer miss nh≤1 HasChunk over remesh far
- Optionally MarkRelit force remesh for ticketed FullyDark focus when skip_already_dirty

**KEEP / NEVER:**
- KEEP SoftDefer SiteB/floor, P13 remesh protect, miss_undrawn hold, MaxAge decay
- NEVER Capture++, fifo++, MarkMissing storm, F5 deadband

**Proof plan:**
- Manual ≥180s land stand keep≥160 (route like 112418 or World_164 west stand)
- Autofly **no-teleport only:** `fz-ne-frontier-stand` SoftDefer KEEP; `fz-manual-plateau` regression
- Gates: discard≤0.05; SoftDefer KEEP; stand VB late ≤70; unf late ≤40; stream_ms ≤30; miss_frac ↓≥50%; skip_orphan med ≤10

**Expected effect on 112418:** unf/VB heal trend; stream↓ via less dirty_gpu churn.

---

## Variant B — Medium (single-source per-epoch scan cache + cost telem)

**Суть:** один authority scan cache на frame epoch для VB / Unfinished / SoftDefer empty candidates; расширить telem reason codes (ghost Dirty count, miss age, ticketed VB age).

**Files:**
- `WorldStreaming.cpp` / `ColumnFlowExecutor.cpp` — read `VisibleBlackFocusSample` only; ban live recount
- `ChunkEmergeCoordinator.cpp` — SoftDefer empty scan shares memo
- `FramePerfMonitor.cpp` — `dirty_ghost_n`, `miss_sticky_age`, `ticketed_vb_age`

**KEEP:** Variant A fixes first; SoftDefer KEEP.

**Proof:** stream_ms↓ on stand; H7 closed; RCA clarity for next packages.

**Expected:** clearer RCA + modest stream cut; not sufficient alone for miss heal.

---

## Variant C — Deep (unified work graph)

**Суть:** ARCHOPTS V4 — один Column job graph (Gen→Seed/Relight→Mesh→RenderReady) с reason-coded stages; Dirty dual-Q становится projection scheduler’а, не parallel owner.

**Files:** ColumnFlowExecutor + Scheduler redesign; MeshCache Dirty becomes mirror; MarkRelit install as stage transition.

**Risks:** high; easy to reintroduce P14 SoftDefer thrash / P8 MarkMissing.

**When:** only after A+B close self-heal (#6) and budget (#7) on manual SoT.

---

## Recommendation

1. **Ship research → implement Variant A as Package P0.**  
2. Variant B as Package P1 after A green.  
3. Variant C deferred; keep as north-star aligned with ARCHOPTS V4.
