# Mesh R1.5 → SHIP — phase 3 audit (2026-09-03)

**Коммит:** `899f7a81` — `perf(stream): Mesh SHIP phase 3 diet unlock and playable FPS bar`  
**Ветка:** `perf_opt17`  
**Gate-of-record (phase 2 end):** `perf_20260903-085143_23864.jsonl` → `manual_20260903-085143_analyze.json`  
**Gate-of-record (post phase-3):** *pending manual 3 min re-run*

---

## Вердикт

| Уровень | Статус | Комментарий |
| --- | --- | --- |
| **SHIP** | **NO-GO** | playable FPS / joint не закрыты; manual gate-of-record не прогнан |
| **R3.6 diet unlock** | **LANDED** | `focus_dirty` убран из hole-pressure; scan/phase shed decoupled; MESH-R30 ≥15 FPS |
| **R3.7 schedule/finish** | **LANDED** | post-prune dirty_fm; Pass2 skip telem; true schedule_ok med; rim PreferKick |
| **R3.8 playable FPS** | **LANDED** | emerge prep accounted; input-first ShedFar on prev wall>130 |
| **R4.3 verify** | **HARNESS DONE** | enter GO; trio done; joint NO-GO; **manual pending** |

---

## Manual `085143` (pre phase-3 baseline)

| Метрика | `201637` | `085143` |
| --- | ---: | ---: |
| wall_fly / FPS | 370 / 2.7 | **377 / 2.65** |
| stream / render share | — | **57% / 12%** |
| holes | 93% | **80%** |
| mismatch / diet | 52% / 52% | **49% / 49%** |
| fm_finish | 0 | **0** |
| rim_perf_diet nz | — | **0%** |
| rim_hole_pressure nz | — | **54%** (ложный latch от focus_dirty) |

**FPS note:** `effective_fps_fly = 1000/wall_fly` — корректный main-thread FPS. Render ~46 ms; stream+emerge ~85% wall. Input CPU ≈0; «тормоза управления» = 1 poll/frame при wall≈370 ms.

---

## Phase 3 sprint summary

### R3.6 — Stream diet unlock
- `ShouldComputeRimHolePressure`: только unfinished / column_no_mesh (не focus_dirty)
- Decouple `force_full_unfinished` / `cruise_scan_fast` / phase-over from heal pressure
- Far-rim emerge floor + wall-based IngressDebt ShedFar
- Analyze: `wall_render_share`, `input_ms_fly_med`
- MESH-R30 interim: wall≤66, stream≤90, fps≥15; MESH-SHIP-joint +fps≥15

### R3.7 — Schedule honesty + FM finish
- Post-prune live `LastDirtyFmN` before schedule
- Pass2 Inflight/PendingGpu RemoveAt → skip counters
- True `cruise_schedule_ok_med` (zeros); `cruise_schedule_ok_when_positive_med`
- PreferKick + ConsumeGpu floor on rim miss without visual_holes
- HoleDrain exit when pressure stale (no unfinished/clnm)

### R3.8 — Playable FPS push
- emerge prep_other accounts pending/setup/dirty/sticky/column_flow
- Input-first: prev wall>130 + far rim → ShedFar before stream; emerge cap floor

---

## R4.3 harness results (autofly + enter)

### fz-cold-enter — **GO**

| Метрика | Значение |
| --- | ---: |
| enter_unfinished_max | **3** (≤10) |
| holes | 17% |
| wall / FPS | 713 / 1.4 |
| fm_finish | 0 |
| diet / mismatch | 6% / 6% |

### Trio autofly

| Report | holes | wall / FPS | stream | fm_finish | diet | mismatch | miss_stuck |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| replay | 34% | 453 / 2.2 | 268 | **1.0** | 4% | 4% | 44s |
| fly-heavy | 62% | 459 / 2.2 | 289 | 0 | 3% | 3% | 22s |
| fz-long | 84% | **159 / 6.3** | **57** | 0 | 0.7% | 0.7% | 30s |

### Phase gates on trio

| Gate | Best evidence | Result |
| --- | --- | --- |
| MESH-R26-completion | replay (`fm_finish=1`, schedule_ok=7) | **GO** |
| MESH-R30-fps | fz-long (stream OK; wall 159 / fps 6.3) | **NO-GO** |
| MESH-SHIP-joint | all trio | **NO-GO** (diet≪40%, fps≪15; holes/fm vary) |

**Чтение:** R3.7 дал первый autofly `fm_finish>0` (replay). fz-long снизил stream до ≤90 и wall ~159 (~6 FPS), но до playable (≥15) далеко. Witness diet на autofly схлопнулся (~1–4%) вместе с mismatch — joint diet≥40% не выполняется. Dominant schedule blocker на cruise: `empty_fm_queue`.

---

## Interim targets

| Метрика | `085143` | Interim (R3.8) | Autofly best | **SHIP** |
| --- | ---: | ---: | ---: | ---: |
| wall_fly / FPS | 377 / 2.7 | ≤66 / ≥15 | 159 / 6.3 (fz-long) | ≤33 / ≥30 |
| stream_ms | 208 | ≤90 | **57** (fz-long) | ≤50 |
| holes | 80% | ≤60% | 34% (replay) | ≤10% |
| diet | 49% | ≥40% | 4% (replay) | ≥70% |
| mismatch | 49% | ≤10% | **4%** (replay) | ≤5% |
| fm_finish | 0 | >0 | **1** (replay) | >0 |

---

## Next step

1. **User:** manual fly ~3 min → `tools/flight_sim_analyze.py` на новый `perf_*.jsonl` → MESH-R26 / R30 / SHIP-joint / parity
2. Если wall всё ещё ≫66: следующий sprint по stream/emerge budget (не camera sub-step)
3. Разобрать почему autofly diet≪40% при низком mismatch (predicate vs latch semantics)
