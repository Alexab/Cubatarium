# 04 — Hotspot matrix H1–H10 + orphan forensics + cost waterfall

Дата: 2026-08-26 · HEAD `1f3f7675` · SoT `112418`  
Скрипт: `scripts/orphan_miss_cost.py` → `raw/orphan_miss_cost.txt`

---

## P2b — Orphan / miss forensics (112418 stand n=5094)

### Sticky miss
| Coord (cx,cy,cz,nh) | Count |
| --- | ---: |
| **(-2, 0, 4, 0)** | 2519 |
| (-1, 0, 5, 1) | 1747 |
| (-3, 1, 4, 1) | 692 |

Focus stand = **(-2, 4)**. Miss `(-2,0,4)` nh=0 = **колонка под игроком, cy=0**.  
`miss_frac=1.000` на всём stand. `heal_deferred_for_miss=1` sticky.

### skip_orphan semantics (code)
В `ChunkMeshCache.cpp` schedule loop:
```cpp
if (!world.GetChunkManager().HasChunk(*it)) {
  ++LastMeshDirtyScheduleSkipOrphanN;
  return Dirty.RemoveAt(it);  // always RemoveAt
}
```
**Orphan = Dirty entry без resident chunk**, не «FirstMesh отклонён на живом чанке».

### LATE stand (last 200)
| Metric | med |
| --- | ---: |
| skip_orphan | **54** |
| schedule_ok | **6** |
| dirty_fm_n | **64.5** |
| dirty_n | **99** |
| unfinished / no_mesh | **71** |
| VB focus | **107** |
| other skips (softdefer/locked/pipeline) | **0** |

early/mid/late: orphan≈53–54, unf 66.5→71, VB=107 flat, dirty_n≈99 — **нет heal во времени**.

### pending_cols frozen
mid == end:
`(-7,0..8), (-6,9), (-5,9), (-4,9)` — западный обод **никогда** не render-ready.

### Вердикт форенсики
1. Primary: **Dirty ghost thrash** — ~54 !HasChunk RemoveAt/frame при ok≈6 → FM backlog не drain’ится.
2. Sticky underfeet miss nh=0 живёт параллельно ticketed VB/unlit (~62).
3. Discard path закрыт (P17): discard/spike≈0.002 — проблема **не** Forget storm.

---

## P2c — Cost waterfall (LATE stand med)

```text
wall_ms                   135.5
  world_streaming_phase   116.5
  mesh_emerge_ms           74.8   (внутри phase / overlaps)
  stream_ms                42.4
    mesh_dirty_tick        27.0
      mesh_dirty_gpu       19.9
      mesh_dirty_schedule   4.1
    relight_drain           9.5
    softdefer_empty_scan    0.3   ← не root FPS
  scene_ms                 13.1
```

**Вывод:** FPS stand = dirty_tick (GPU apply thrash) + emerge + relight; SoftDefer scan ≪1ms → **H4 CONFIRMED, SoftDefer scan not wall**.

Cruise (0:229) stream med≈29 vs stand≈41 — деградация на stop.

---

## Hotspot hypotheses

### H1: Sticky miss nh≤1 / orphan-skip FirstMesh
- **Status: CONFIRMED (refined)**
- Symptom: 112418 stand miss_frac=1, unf≈70, VB=107
- Code: `ChunkMeshCache.cpp` `!HasChunk` → SkipOrphanN + RemoveAt
- Evidence: orphan med=54, ok=6, fm=64.5; 196/200 late frames orphan≥50
- Nuance: orphan = **ghost Dirty**, не reject loaded underfeet. Sticky miss `(-2,0,4)` — отдельный ownership/unlit path, но ghost thrash голодает schedule.
- Fix implication: stop re-Dirty unloaded coords; ensure underfeet FM only on HasChunk; drain ghost Dirty without re-admit.
- Risk if wrong: low — counter is unambiguous.

### H2: Dirty thrash without discard
- **Status: CONFIRMED**
- Evidence: dirty_n≈99 plateau early→late; dirty_touch~112; discard≈0; remesh~48 + fm~64
- Fix: coalesce Dirty / stop re-touch of ghost+unlit churn; ownership heal not more MarkDirty.

### H3: Ticketed VB (VB≈107, no_ticket≈6) = unlit_hidden / PL focus
- **Status: CONFIRMED**
- Evidence: VB late=107, no_ticket≈6–7; unlit_hidden≈62 ≈ pending_light_focus≈62; black_sticky=0
- Fix: ticketed consume / MarkRelit→lit remesh for focus unlit; not more no_ticket repair.

### H4: stream_ms≈41 = dirty+emerge, not SoftDefer scan
- **Status: CONFIRMED**
- Evidence: softdefer_scan med=0.30; dirty_tick=27; emerge=75; stream=42
- Fix: do **not** optimize SoftDefer disk scan for stand FPS.

### H5: P17 VB cadence never arms (raw never stable)
- **Status: PARTIAL / REJECTED as primary**
- Code: full scan while `vb_focus_stable_frames < 8` OR cd; stable when raw==prev_raw
- Evidence: VB=107 flat forever → raw stable after 8 frames **should** arm cadence 4. prep_unfinished_full_n≈0. stream still 41 via dirty/emerge.
- Fix: cadence may be working; wall not from VB scan. Don’t chase cadence further for FPS.

### H6: Producer>consumer (Capture/floor alive, apply low)
- **Status: PARTIAL**
- Evidence: SoftDefer floor/spike≈1.97; capture_bg=3; apply path busy (dirty_gpu≈20) but unlit sticky; completed discard=0
- Consumer exists but **wrong work** (ghost Dirty / remesh churn) vs underfeet+ticketed unlit.
- Fix: consumer priority toward miss nh=0 + ticketed unlit, not Capture++.

### H7: Duplicate VisibleBlack/Unfinished scans inflate stream
- **Status: PARTIAL (code risk) / REJECTED as stand primary**
- Code: RefreshStreamingPressure + possible ColumnFlow recount; Unfinished Near on idle
- Evidence: softdefer_scan small; dirty dominates stream. Duplicate scans real tech-debt but not 41ms.
- Fix: Variant B scan cache — medium priority.

### H8: Trim PendingLight / FIFO farthest O(N²)
- **Status: REJECTED as stand root**
- Code: `TrimFarRelightFifoFarthest` while+full scan per drop = O(N·drops) potential
- Evidence on 112418 stand: PL drop≈0; SoftDefer KEEP; stream dominated by dirty_gpu
- Fix: algorithmic later; not P0 for never-heal.

### H9: SoftDefer SiteB not root
- **Status: CONFIRMED (not root)**
- Evidence: SiteB≈0.07, floor≈1.97, stuck≤5, keep=169 — SoftDefer KEEP PASS on CUR and 104722

### H10: P13 eye-better due to remesh floor draining FullyDark without orphan stall
- **Status: PARTIAL CONFIRMED**
- Evidence: P13 `170807` stand stream≈19.5, unf≈27, VB≈95, discard low; CUR stream≈41, unf≈70, VB≈107, orphan≈54
- P13 remesh protect KEEP in HEAD; post-P13 added FM steal / ownership paths that leave ghost Dirty + ticketed unlit plateau
- Fix: restore **consumer effectiveness** of P13-era remesh drain without re-landing P14 SoftDefer thrash

---

## Summary verdict table

| ID | Verdict | Primary for 112418? |
| --- | --- | --- |
| H1 | CONFIRMED (ghost Dirty orphan) | **YES primary** |
| H2 | CONFIRMED | YES (coupled) |
| H3 | CONFIRMED | **YES secondary (ticketed VB)** |
| H4 | CONFIRMED | YES (cost) |
| H5 | REJECTED as primary | no |
| H6 | PARTIAL | supporting |
| H7 | PARTIAL / not primary | no |
| H8 | REJECTED as stand root | no |
| H9 | CONFIRMED not-root | SoftDefer cleared |
| H10 | PARTIAL | baseline narrative |

**Closed ≥8/10 with verdicts.** H1 and H3 required: both CONFIRMED.

## Root-cause (research)

Long land stand never-heal = **(A)** Dirty queue flooded with !HasChunk ghosts (skip_orphan≈54) starving real FirstMesh (ok≈6) **+ (B)** ticketed black = unlit_hidden/PL≈62 never consumed, sticky miss nh=0 underfeet. SoftDefer and discard are green. P13 looked better because unf/VB/stream were lower before ghost/ticketed plateau locked in.
