# Mesh R1.5 → SHIP — phase 4.5 audit (2026-09-04)

**Ветка:** `perf_opt18`  
**База phase-4:** `868715bf` / manual `113457` (wall≈291 / stream≈180 / holes≈97%)  
**Gate-of-record (post phase-4.5):** *pending manual 3 min*

---

## Вердикт

| Уровень | Статус | Комментарий |
| --- | --- | --- |
| **SHIP** | **NO-GO** | playable FPS / joint / manual gate-of-record не закрыты |
| **R4.5.1 refresh honesty + terrain-complete** | **LANDED** | gap double-count fix; camera-complete cache/reuse; sparse miss-pin; schedule memo; Body/Camera timers |
| **R4.5.2 near-miss FM finish** | **LANDED code** | PreferKick/2nd Consume mh≤4; orphan watch hygiene; ShedRim capture floor≥2; near SoT ignore lone dark-face; SoftDeferHeld→Dirty nh≤2 |
| **R4.5.3 verify** | **HARNESS PARTIAL** | enter GO; fly-heavy done; **manual pending**; fm_finish still 0 on autofly |

---

## Baseline → phase-4.5 autofly

| Метрика | `202455` | `113457` | fz-cold-enter 4.5 | fly-heavy 4.5 | R4.5.1 target |
| --- | ---: | ---: | ---: | ---: | ---: |
| wall_fly / FPS | 329 / 3.0 | 291 / 3.4 | 398 / — | 329 / — | ≤200 |
| stream | 181 | 180 | 211 | 215 | ≤90 |
| emerge | 103 | 42 | — | 135 | ≤40 |
| prep_refresh / gap_explained | 78 / ~41% | 84 / ~21% | — / ~3%* | 95 / ~2%* | ≤35 / ≥70% |
| camera_complete_ms | (in gap) | (in gap) | **≈0** | **≈0** | ≤2 |
| holes | 81% | **97%** | 46% | 51% | ≤85% |
| fm_finish | 0 | 0 | 0 | 0 | >0 (R4.5.2) |
| enter_unfinished_max | — | — | **2** | 0 | ≤10 |

\*Autofly: `camera_complete` уже O(1); residual gap всё ещё ≈pressure — следующий бисект (не terrain-complete).

---

## Phase 4.5 sprint summary

### R4.5.1 — Refresh honesty + terrain complete
- `RingResyncMs`: только sticky full-walk (убран `+= unfinished`)
- Same-frame `LastCameraTerrainComplete*` + public `IsTerrainChunkCompleteCached` (+ ProcedurallyGenerated fast-path)
- Miss pin: sparse solid step-4; `FindNearest` только при `run_miss_probe`
- Schedule HasMissing: MissingMemo reuse (xz/radius)
- Unfinished cadence: mh∈[1,2] && !underfeet reuse 1–2f
- Named: `PrepRefreshCameraCompleteMs`, `PrepRefreshBodyMs`; analyze `gap_explained`

### R4.5.2 — Near-miss FM finish
- PreferKick + 2nd Consume: `mh ∈ [0,4]`
- `ApplyMeshResult` early-return: abandon FM watch; age≥16 orphan → MarkDirtyPriority
- MemoryBudget: visual_holes + ShedRim/mh≤4 → `capture_hard_cap` floor **2**
- `GetColumnRenderableState` nh≤2: stale = revision only (ignore lone `GpuHasDarkFace`)
- SoftDeferHeld→Dirty nh≤2 even if `dirty_fm>0`; telem `softdefer_held_age_max`

### Enter guard
- `mesh_phase45_fz_cold_enter.json`: **enter_unfinished_max=2 ≤10 → GO**

### Autofly (partial trio)

| Report | holes | wall_fly | stream | emerge | fm_finish | schedule_ok med |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| fz-cold-enter | 46% | 398 | 211 | — | 0 | — |
| fly-heavy | 51% | 329 | 215 | 135 | 0 | **7** |

### Phase gates

| Gate | Best evidence | Result |
| --- | --- | --- |
| fz-cold-enter | enter_max=2 | **GO** |
| MESH-R26-completion | autofly fm_finish | **NO-GO** (0) |
| MESH-R30-fps | fly-heavy wall 329 | **NO-GO** |
| MESH-SHIP-joint | autofly | **NO-GO** |

**Чтение:** R4.5 убрал ложный gap double-count и сделал camera-complete бесплатным; stream wall на short autofly ещё не сдвинулся. Finish-path код для nh≤2 landed; нужен **manual 3 min** vs `113457`.

---

## Next step

1. **User:** manual fly ~3 min → analyze → обновить gate-of-record vs `113457`/`202455`
2. Если gap_explained&lt;70% на manual: бисект residual Refresh untimed (не terrain-complete)
3. Camera sub-step по-прежнему запрещён пока wall_med&gt;66
