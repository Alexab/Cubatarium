# Mesh R1.5 → SHIP — phase 3 audit (2026-09-03)

**Коммит:** phase 3 R3.6–R3.8 (narrow hole-pressure, schedule honesty, playable FPS push)  
**Ветка:** `perf_opt17`  
**Gate-of-record (phase 2 end):** `perf_20260903-085143_23864.jsonl` → `manual_20260903-085143_analyze.json`  
**Gate-of-record (post phase-3):** *pending manual 3 min re-run*

---

## Вердикт

| Уровень | Статус | Комментарий |
| --- | --- | --- |
| **SHIP** | **NO-GO** | post-phase-3 manual gate-of-record не прогнан |
| **R3.6 diet unlock** | **LANDED** | `focus_dirty` убран из hole-pressure; scan/phase shed decoupled; MESH-R30 ≥15 FPS |
| **R3.7 schedule/finish** | **LANDED** | post-prune dirty_fm; Pass2 skip telem; true schedule_ok med; rim PreferKick |
| **R3.8 playable FPS** | **LANDED** | emerge prep accounted; input-first ShedFar on prev wall>130 |
| **R4.3 verify** | **PARTIAL** | harness + enter guard; manual gate pending |

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

## Interim targets

| Метрика | `085143` | Interim (R3.8) | **SHIP** |
| --- | ---: | ---: | ---: |
| wall_fly / FPS | 377 / 2.7 | ≤66 / ≥15 | ≤33 / ≥30 |
| stream_ms | 208 | ≤90 | ≤50 |
| holes | 80% | ≤60% | ≤10% |
| diet | 49% | ≥40% | ≥70% |
| mismatch | 49% | ≤10% | ≤5% |
| fm_finish | 0 | >0 | >0 |

---

## Next step

1. User: manual fly 3 min → analyze + MESH-R26 / R30 / SHIP-joint
2. Trio autofly regression reports under `mesh_phase3_trio_*`
3. Update this audit with GO/NO-GO
