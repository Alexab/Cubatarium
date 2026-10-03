# Mesh R1.5 → SHIP — phase 4.6 audit (2026-09-04)

**Ветка:** `perf_opt18`  
**База phase-4.5:** `4513ddf8` / manual `135644` (wall≈319 / stream≈173 / holes≈53%)  
**Gate-of-record (post phase-4.6):** *pending manual 3 min vs `135644`*

---

## Вердикт

| Уровень | Статус | Комментарий |
| --- | --- | --- |
| **SHIP** | **NO-GO** | playable FPS / joint / manual gate-of-record не закрыты |
| **R4.6.1 ring complete-cache + Facing cadence** | **LANDED** | Facing/Unfinished/SoT → `IsTerrainColumnCompleteFast`; Facing rim cadence 12f; period-avg `prep_refresh_*` |
| **R4.6.2 stop-rim pin rotate + SoT** | **LANDED code** | hard-expire > pinned_still; rim_hole_pressure on focus_missing mh≤4; stream_ingress_ops; PreferKick FocusMissing |
| **R4.6.3 verify** | **HARNESS DONE** | enter GO; fz-long wall_fly≈123; fly-heavy fm_finish>0; **manual pending** |

---

## Baseline → phase-4.6 autofly

| Метрика | `135644` | enter 4.6 | fz-long 4.6 | fly-heavy 4.6 | R4.6.2 target |
| --- | ---: | ---: | ---: | ---: | ---: |
| wall_fly / FPS | 319 / 3.1 | 386 | **123** | 414 | ≤150 |
| stream (phase) | 173 | ~291 | **41** | ~334 | ≤80 |
| emerge | 95 | ~71 | **7** | ~69 | ≤45 |
| holes | 53% | 35% | 87% | 70% | ≤40 |
| capture retarget blocked | **1.0** | **0.22** | 0.31 | **0.19** | ≤0.3 |
| mismatch rate | 45% | **0.0** | **0.0** | **0.0** | ≤15 |
| schedule_ok med | 1 | 9 | **8** | **6** | ≥2 |
| fm_finish med | 0 | 0 | 0 | **8.0** | >0 |
| enter_unfinished_max | — | **9** | — | — | ≤10 |

---

## Phase 4.6 sprint summary

### R4.6.1 — Ring-walk complete cache + Refresh honesty
- Spike bisect `135644`: facing/unfinished med ~7ms when gap>20 (camera_complete≈0 confirmed)
- `UWorld::IsTerrainColumnCompleteFast` → Streamer `IsTerrainChunkCompleteCached` (const+mutable cache)
- Replaced raw complete in Facing / UnfinishedCheap / GetColumnRenderableState horiz≤1
- Facing cadence under rim miss / hole pressure (reuse ≤12f); stand clear denser
- FramePerf period: average (+ max track) `prep_refresh_*` (was last-frame only)

### R4.6.2 — Stop-rim heal
- `ShouldExtendWitnessPinHold` / `ShouldBlockWitnessCaptureRetarget`: age≥48 wins over pinned_still
- Stop ShedRim: `pin_T` capped at hard expire; PreferKick+Dirty on hard-expire hop
- `ShouldComputeRimHolePressure(..., focus_missing)` for mh∈[3,4]
- `cruise_scan_fast` blocked for focus_missing mh∈[2,4]
- PreferKick / 2nd Consume when FocusMissing even if VisualHoles=0
- `StreamIngressOps = loads + async_queued`; analyze mismatch excludes rim SoT
- stop_vb: skip budget inflate when miss+watches

### Enter / trio
- `mesh_phase46_fz_cold_enter.json`: **enter_unfinished_max=9 ≤10 → GO**; retarget_blocked≈0.22; mismatch=0
- `mesh_phase46_fz_long.json`: wall_fly≈**123**, stream≈41, emerge≈7, schedule_ok=8, retarget_blocked≈0.31, holes 87%
- `mesh_phase46_trio_fly_heavy.json`: fm_finish_med≈**8**, schedule_ok=6, retarget_blocked≈0.19, mismatch=0; wall_fly still 414 (short heavy)

### Phase gates

| Gate | Best evidence | Result |
| --- | --- | --- |
| fz-cold-enter | enter_max=9 | **GO** |
| MESH-R26-completion | fly-heavy fm_finish≈8 | **GO** (autofly) |
| MESH-R30-fps | fz-long wall 123 / heavy 414 | **NO-GO** (SHIP≤66) |
| MESH-SHIP-joint | autofly | **NO-GO** |

**Чтение:** R4.6 закрыл sticky pin (1.0→≤0.31) и rim mismatch→0; fz-long показывает сильный wall/stream win vs `135644`, но holes на long autofly ещё высоки. Нужен **manual 3 min** vs `135644`.

---

## Next step

1. **User:** manual fly ~3 min → analyze → gate-of-record vs `135644`/`113457`
2. Если manual gap_explained&lt;70%: residual untimed Refresh after cache
