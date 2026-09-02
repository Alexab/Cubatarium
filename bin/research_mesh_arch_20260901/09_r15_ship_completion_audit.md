# Mesh R1.5 → SHIP — phase 2 audit (2026-09-02)

**Коммит:** `0b40bcb5` + R3.2–R3.5 phase 2 (capture retry, hole-pressure SoT, witness SLA, FPS interim)  
**Ветка:** `perf_opt17`  
**Gate-of-record (pre phase-2):** `perf_20260902-201637_23188.jsonl` → `manual_20260902-201637_analyze.json`  
**Gate-of-record (post phase-2):** *pending manual 3 min re-run*

---

## Вердикт

| Уровень | Статус | Комментарий |
| --- | --- | --- |
| **SHIP** | **NO-GO** | post-fix manual gate-of-record не прогнан |
| **R3.2 capture retry** | **LANDED** | backlog-proportional retry, drain-before-re-enqueue, retry-first admission |
| **R3.3 hole-pressure SoT** | **LANDED** | `rim_hole_pressure` / `rim_perf_diet` split; admission carve frozen under pressure |
| **R3.4 witness SLA** | **LANDED** | GpuPending-only retarget block; Meshing SLA kick; stage sync before admission |
| **R3.5 FPS interim** | **LANDED** | stream phase cap gated on `!rim_hole_pressure`; MESH-R30 interim wall≤200/fps≥5 |
| **R4.2 verify** | **PENDING** | trio + joint gate wired; manual gate-of-record awaits user fly |

---

## Manual `201637` (pre phase-2 baseline)

| Метрика | `173028` | `201637` |
| --- | ---: | ---: |
| witness_diet | 24% | **52%** |
| holes_rate | 70% | **93%** |
| fm_to_gpu_finish | 0 | **0** |
| dominant_stall | gpu_not_ready | **capture_pending** |
| wall_fly / FPS | 330ms / 3.0 | 370ms / 2.7 |
| miss_stuck | 166s | 162s |

**Корневые причины (addressed in phase 2):**
1. `PendingCaptureReady` backlog >> retry budget → `fm_finish=0` (R3.2)
2. Diet и hole-blindness на одном `!visual_holes` при mh≥3 (R3.3)

---

## Phase 2 sprint summary

### R3.2 — Capture retry throughput
- `ComputeCaptureRetryBudget()` — backlog-proportional end-of-tick retry
- Drain-before-re-enqueue on `PendingCaptureSet_` store miss
- `ShouldDeferNewCaptureEnqueue()` — retry-first admission
- Classifier: `capture_pending` по `mesh_pending_capture_ready_n > schedule_ok_n`
- Raw: `raw/completion_chain_201637.txt`

### R3.3 — Diet / Hole SoT split
- `ShouldComputeRimHolePressure()` — mh≥3 + incremental unfinished/dirty/clnm signals
- `ShouldExitRimPerfDiet()` — exit on hole pressure, stale reuse, rising focus debt
- Admission: `holes` OR=`rim_hole_pressure`; carve-out disabled under pressure
- `rim_plateau_close` skip only when `!rim_hole_pressure`
- Telem: `rim_hole_pressure`, `rim_perf_diet` in jsonl

### R3.4 — Witness SLA + stage order
- Retarget block: `GpuPending && !drawable` only (not Meshing)
- `ShouldKickMissWitnessOnMeshingSla()` — release pin after 3600f Meshing
- `SyncFocusRingColumnJobStages()` before early admission
- Hole enqueue escalation: Meshing + miss_age>240 → PromoteRelight + FirstMesh

### R3.5 — FPS interim
- `stream_phase_over_budget` only when `!rim_hole_pressure && !underfeet`
- `MESH-R30-fps` interim gate: wall≤200, stream≤120, fps≥5

### R4.2 — SHIP verify (harness)
- `MESH-SHIP-joint` gate: diet≥40%, holes≤30%, fm_finish>0, mismatch≤10%
- fz-cold-enter regression guard on each sprint
- **Manual 3 min gate-of-record:** user action required

---

## Interim targets (R3.5) vs SHIP

| Метрика | `201637` | Interim (R3.5) | **SHIP** |
| --- | ---: | ---: | ---: |
| wall_fly / FPS | 370ms / 2.7 | ≤200 / ≥5 | ≤33 / ≥30 |
| stream_ms | 246 | ≤120 | ≤90 |
| holes_rate | 93% | ≤60% | ≤10% |
| witness_diet | 52% | ≥40% | ≥70% |
| fm_to_gpu_finish | 0 | >0 | >0 |
| mismatch_rate | 52% | ≤15% | ≤5% |
| miss_stuck | 162s | ≤60s | ≤30s |

---

## Next step

1. User: manual fly 3 min → `perf_*_analyze.json`
2. Run phase gates: `MESH-R26-completion`, `MESH-R30-fps`, `MESH-SHIP-joint`, `MESH-parity-manual`
3. Trio autofly regression (replay, fly-heavy, fz-long)
4. Update this audit with GO/NO-GO verdict
