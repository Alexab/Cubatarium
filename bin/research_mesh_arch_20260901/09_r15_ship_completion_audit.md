# Mesh R1.5 → SHIP — phase 4 audit (2026-09-04)

**Ветка:** `perf_opt18`  
**База phase-3:** `6f01bbe7` / manual `202455` (wall≈329 / FPS≈3)  
**Gate-of-record (post phase-4):** *pending manual 3 min*

---

## Вердикт

| Уровень | Статус | Комментарий |
| --- | --- | --- |
| **SHIP** | **NO-GO** | playable FPS / joint / manual gate-of-record не закрыты |
| **R4.1 O(1) stale + Sync R≤2** | **LANDED** | column SoT без voxel walk; SyncFocusRing med≈0.5 ms; emerge↓ |
| **R4.2 dual unfinished + cadence** | **LANDED** | post-load reuse unfinished keys; cruise unfinished defer |
| **R4.3 protect-ring FM finish** | **LANDED code** | ShedRim protect; SoftDeferHeld→Dirty; PreferKick+2nd Consume |
| **R4.4 verify** | **HARNESS DONE** | enter GO; trio done; **manual pending**; fm_finish still 0 on autofly |

---

## Baseline `202455` → phase-4 autofly

| Метрика | `202455` manual | fz-cold-enter | trio fz-long | R4 target |
| --- | ---: | ---: | ---: | ---: |
| wall_fly / FPS | 329 / 3.0 | 314 / 3.2 | **111 / 9.0** | ≤66 / ≥15 |
| stream | 181 | 155 | **28** | ≤90 (R4.2) |
| emerge | 103 | **42** | **5.3** | ≤40 (R4.1) |
| prep_other (abs) | ~77 | med≈14 | — | ≤15 |
| SyncFocusRing | ~O(R²×512) | med **0.45 ms** | — | O(1) lookups |
| holes | 81% | 33% | 67% | ≤60% (R4.3) |
| fm_finish | 0 | 0 | 0 | >0 |
| miss_stuck | 132s | 28s | **28s** | ≤60 |

---

## Phase 4 sprint summary

### R4.1 — Kill O(R²×voxel)
- `GetColumnRenderableState`: `IsMeshLightStaleGpu` / `IsMeshLightStale` via `FillLitApplyMeshProbe`
- `SyncFocusRingRadiusUnderDebt`: R≤2 under ShedFar / phase_over
- Prep early-exit on exact ShedFar + running wall>12
- Named: `PrepSyncFocusRingMs`, `PrepRecoverMs` → FramePerf JSON

### R4.2 — Dual unfinished + cadence
- `CountPostLoadRingNotReady`: reuse unfinished_keys Chebyshev≤4 on cruise
- Cruise unfinished defer when diet + mh≥3 + reuse_age<6
- `PrepRefreshHasMissingMs` on UpdateStreaming HasMissing trio

### R4.3 — Protect-ring FM finish
- `IsProtectRingFocusMiss` → ShedRim (VB latch + EvaluateIngressDebt + wall bump)
- MemoryBudget `capture_hard_cap=1` only exact ShedFar **and** mh>4
- SoftDeferHeld age≥4 + dirty_fm==0 + horiz≤4 → MarkDirtyPriority
- PreferKick only if hole pending/queued; SoftDeferHeld backup MarkDirty
- Second `ConsumeGpuApplyBacklog` after Rebuild for watches noted this frame
- PrefetchAhead allowed under ShedRim (only ShedFar sheds)

### Enter guard
- `mesh_phase4_fz_cold_enter.json`: **enter_unfinished_max=4 ≤10 → GO**

### Trio autofly

| Report | holes | wall_fly / FPS | stream | emerge | fm_finish | diet | mismatch | miss_stuck |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| replay | 72% | 407 / 2.5 | 253 | 68 | 0 | 6.8% | 6.8% | 44s |
| fly-heavy | 25% | 398 / 2.5 | 263 | 69 | 0 | 2.6% | 2.6% | 22s |
| fz-long | 67% | **111 / 9.0** | **28** | **5.3** | 0 | 0.8% | 0.8% | **28s** |

### Phase gates

| Gate | Best evidence | Result |
| --- | --- | --- |
| fz-cold-enter | enter_max=4 | **GO** |
| MESH-R26-completion | autofly fm_finish | **NO-GO** (0) |
| MESH-R30-fps | fz-long wall 111 / fps 9 | **NO-GO** (need ≤66 / ≥15) |
| MESH-SHIP-joint | all trio | **NO-GO** |

**Чтение:** алгоритмический FPS-рычаг сработал на calm long cruise (stream 28, emerge 5, wall≈111). Short replay/fly-heavy всё ещё stream-bound (~250 ms). Protect-ring ownership в коде, но autofly `fm_finish` ещё 0 — нужен manual gate-of-record.

---

## Next step

1. **User:** manual fly ~3 min → analyze → обновить gate-of-record
2. Если fm_finish=0 на manual: добить SoftDeferHeld witness / empty_fm ownership
3. Camera sub-step по-прежнему запрещён пока wall_med>66
