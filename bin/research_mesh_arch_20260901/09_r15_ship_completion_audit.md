# Mesh R1.5 → SHIP — debt closure audit (2026-09-02)

**Коммит:** `18cb5899` + R2.6–R4.1 debt closure (ColumnJobGraph SoT, completion chain, witness guard, frame budget)  
**Ветка:** `perf_opt17`  
**Gate-of-record manual (pre-fix):** `perf_20260902-173028_25184.jsonl` → `manual_20260902-173028_analyze.json`

---

## Вердикт

| Уровень | Статус | Комментарий |
| --- | --- | --- |
| **SHIP** | **NO-GO** | holes 84%, witness diet &lt;2%, manual gate-of-record не перепрогнан |
| **R1.5 capture loop** | **GO** | retry 83–101, schedule_ok 6–8 |
| **R2.6 forensics** | **GO** | completion stall classifier, wall waterfall, phase gates wired |
| **R2.7–R2.9 код** | **LANDED** | ColumnJobGraph SoT, dark-reject routing, M4 lint, store diet |
| **R3.0 frame budget** | **INTERIM** | fz-long wall_fly 130ms (~7.7 FPS) vs manual 330ms (~3 FPS) |
| **R3.1 witness** | **PARTIAL** | stage guard wired; diet 1–15% autofly vs цель 40% |
| **R2.8 completion** | **INTERIM** | `fm_dirty_to_gpu_finish_med=1` на fly-heavy; 0 на fz-long |
| **R4.1 harness** | **GO** | trio + gates прогнаны; replay teardown/travel fix landed |

---

## Trio autofly (post debt-closure, Release)

| Метрика | replay-manual* | fly-heavy | fz-manual-long |
| --- | ---: | ---: | ---: |
| `process_rc` / outcome | 1 / crash† | 0 / success | 0 / success |
| `wall_ms_fly_med` | 558 | 929 | **130** |
| `effective_fps_fly` | 1.8 | 1.1 | **7.7** |
| `stream_ms` med | 360 | 537 | **53** |
| `mesh_emerge_ms` | 143 | 198 | 77 |
| `holes_rate` | 0% | 0% | **84%** |
| `witness_latch_diet_share` | 15% | 9% | 1.3% |
| `fm_dirty_to_gpu_finish_med` | 0 | **1** | 0 |
| `cruise_schedule_ok_med` | 9 | 8 | 6 |
| `mesh_schedule_retry_max` | 103 | 83 | 101 |

\* replay-manual: pre-fix run; `chunks_traveled=2` → harness exit 1; 38 min scene hang на stop.  
† Исправлено: fly-phase 90s, skip render on stop, `harness_fail` classifier.

**Отчёты:** `bin/suite_reports/mesh_debt_trio_{replay,fly_heavy,fz_long}.json`

---

## Phase gates (2026-09-02)

| Gate | Профиль | Результат |
| --- | --- | --- |
| MESH-R15-capture | fly-heavy | **GO** |
| MESH-R26-completion | fly-heavy | **GO** (`fm_finish>0`) |
| MESH-R30-fps | fz-long | **NO-GO** (wall 130&gt;120, fps 7.7&lt;8) |
| MESH-parity-manual | fz-long vs `173028` | **NO-GO** (holes 84%&gt;55%) |
| fz-cold-enter | enter guard | PASS (`enter_unfinished_max=1`) |

---

## Sprint summary

### R2.6 — Forensics
- `wall_waterfall_audit.py`, `dominant_completion_stall`, `dominant_wall_stage`, `effective_fps_fly`
- Raw: `raw/completion_chain_173028.txt`, `raw/wall_waterfall_173028.txt`

### R2.7 — ColumnJobGraph SoT
- `SyncColumnJobStageFromWorld`, `column_job_graph_stage_test` PASS

### R2.8 — FM→GPU completion
- Dark reject → `PendingLight`; fake finish telem removed; GPU budget reserve; isolated-hole refill

### R3.1 — M4 + witness
- Witness retarget blocked on `Meshing|GpuPending`; swap grace 30f; `m4_ownership_lint` PASS

### R3.0 — Frame budget
- Witness diet threshold, stream phase cap 120ms, cruise fast-path under budget pressure
- fz-long: wall_fly **130ms** (↓60% vs manual `173028`)

### R2.9 — Store diet
- Store hit telem; M2c sync only when saturated; `capture_incremental_test` PASS

### R4.1 — SHIP verify
- Trio sequential; parity gate wired; replay teardown fix (travel + skip final render)

---

## SHIP gates vs факт

| Метрика | Manual `173028` | Interim (fz-long) | SHIP |
| --- | ---: | ---: | ---: |
| wall_fly / FPS | 330ms / ~3 | 130ms / ~7.7 | ≤33 / ≥30 |
| stream_ms | 199 | 53 | ≤90 |
| holes_rate | 70% | 84% | ≤10% |
| witness_diet | 24% | 1.3% | ≥70% |
| fm_to_gpu_finish | 0 | 0 (long) / 1 (heavy) | >0 |
| mesh_emerge_ms | 78 | 77 | ≤25 |

---

## Оставшиеся действия

1. **Ручной fly 3 min** на актуальном `bin/Cubatarium.exe` — gate-of-record для SHIP.
2. **Witness diet** — autofly 1–15% vs цель 40%; stage guard не восстанавливает diet alone.
3. **holes_rate** на fz-long (84%) — completion + diet trade-off на длинном маршруте.
4. **Перепрогнать replay-manual** после teardown fix (ожидается `process_rc=0`).

---

## Unit / lint

- `column_job_graph_stage_test` — PASS  
- `mesh_schedule_retry_test` — PASS  
- `m4_ownership_lint` — PASS  
- `capture_incremental_test` — PASS  
