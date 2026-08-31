# I17: Ingress throughput + blink forensics (post I16/I17-A rollback)

**База:** commit `b9bd7d2f` (I15-B4) — gate-of-record manual `perf_20260831-144853`  
**Откат:** I16-F (park/damp) и I17-A (silent stale) — **не коммитить**, оба провалились  
**Gate of record:** FP-manual PASS, ручной no-teleport World_164 ≥3 мин

---

## Что доказали три пролёта 31.08

| Пролёт | Коммит/патч | `wall_ms` cruise | `stream_ms` | `prep_gap` | `EH_blink` | `unfinished` med | Вердикт |
|--------|-------------|----------------|-------------|------------|------------|-------------------|---------|
| `144853` | `b9bd7d2f` | **137** | **95** | **40** | **0.018 (2)** | **1** | **лучший SoT** |
| `152250` | I16-F | 179 | 100 | 41 | 0.013 | 3 | длинные дыры, throttle |
| `164830` | I17-A | 270 | 212 | 92 | 0.044 (4) | 2 | FPS×2 хуже, blink хуже |

### Анти-паттерны (закрыты)

| Подход | Почему нет |
|--------|------------|
| I16-F park RAA + witness damp | дросселирует witness/remesh → `unfinished`↑, `schedule_ok`↓ |
| I17-A silent stale + drawable hold | `mesh_replace_hole_avoided=0` на всём пролёте — **path не активируется**; FPS-регресс от pressure spiral, не от патча |

### Реальный диагноз мигания (`144853` forensics)

```
witness retarget (rim nh≤4)
  → revision bump
  → in-flight GPU stale (RemeshObsoleteTracked)
  → reschedule (mesh_apply_stale burst i=27–30)
  → 1–2 кадра inconsistent drawable → blink
```

Мигание — **симптом медленного ingress + revision churn**, не отдельный баг damp/park.

Узкие места (общие):

| Узкое место | `144853` | `164830` (спираль) | Доля stream |
|-------------|----------|---------------------|-------------|
| `prep_refresh_pressure_ms` | 44 | **94** | ~46% |
| `prep_refresh_gap_ms` | 40 | **92** | untagged gap |
| `mesh_emerge_ms` | 16 | 36 | ~16% |
| `schedule_ok` med | 2 (часто 0) | 4 | FM starvation |
| `markrelit→gpu_finish` med | **0** | **0** | chain обрыв |
| `visible_black_stalled` med | 6 | **22** | VB backlog |

---

## Принципы I17

1. **Ускорять pipeline**, не маскировать симптомы (no damp / no park / no silent discard без forensics).
2. **Один коммит = одна гипотеза** + ручной пролёт vs `144853`.
3. **Blink gate вторичен** — первичен throughput: `stream_ms ≤95`, `prep_gap ≤40`, `wall_ms ≤140`.
4. Flicker fix только после **доказанного path** (телеметрия в момент blink).

---

## Bisect order

```
P0 forensics → P1 pressure diet → P2 FM/GPU chain → P3 VB stall → P4 blink bind (если останется)
```

---

## P0 — Blink forensics pack (без изменения поведения)

**Цель:** поймать реальный path мигания, не гадать.

| ID | Задача | Файлы | Gate |
|----|--------|-------|------|
| P0-1 | Period snapshot при `unfinished_visual` 0→N: `miss_cx/cz`, `miss_horiz`, `mesh_apply_stale_delta`, `mesh_discarded_late_delta`, `softdefer_witness_horiz`, `focus_cx/cz` | `FramePerfMonitor.cpp` | script выводит top-5 blink windows |
| P0-2 | Script `blink_window_audit.py`: diff соседних периодов, correlate stale/revision/witness | `bin/research_flight_20260829/scripts/` | отчёт на `144853` |
| P0-3 | Waterfall `144853` vs `164830`: кто раздувает `prep_refresh_pressure` | `stream_waterfall.py` | top-3 untagged subtimers |

**Exit:** документирован blink path (≥1 из: stale apply, discarded_late, witness hop, VB pop).

---

## P1 — RefreshStreamingPressure diet (главный FPS lever)

**Гипотеза:** `prep_refresh_gap` 40→92ms — причина FPS×2 в `164830`; I12 diet не закрыт на I15 baseline.

| ID | Задача | Файлы | Метрика |
|----|--------|-------|---------|
| P1-1 | Incremental `unfinished_visual` reuse — не full ring resync каждый кадр на cruise | `WorldStreaming.cpp` RefreshStreamingPressure | `prep_refresh_unfinished_ms` med ≤5 |
| P1-2 | VB raw scan cadence: throttle `prep_refresh_vb_raw` когда `vb_stalled` plateau | `WorldStreaming.cpp`, policy header | `prep_refresh_gap` ≤35 |
| P1-3 | `ring_resync` только при `focus_cx/cz` jump или `keep_cols` change | `WorldStreaming.cpp` | `prep_refresh_ring_resync_ms` med ≤2 |
| P1-4 | Witness latch diet share ≥0.55 без роста `miss_stuck` | I12 policies audit vs I15 | `witness_latch_diet_share` |

**Gate P1:** vs `144853`: `stream_ms ≤100`, `prep_gap ≤42`, `wall_ms ≤145`. Blink не хуже.

---

## P2 — Rim ingress FM→GPU chain (закрыть дыры быстрее)

**Гипотеза:** `fm_dirty_to_gpu_finish_med=0` — mesh на rim не доходит до GPU finish; revision успевает устареть → blink.

| ID | Задача | Файлы | Метрика |
|----|--------|-------|---------|
| P2-1 | FM floor nh≤4 на cruise moving: `schedule_ok med ≥3` | `ChunkEmergeCoordinator.cpp`, `MeshWorkAdmission.h` | `dirty_fm med ≥5` |
| P2-2 | GPU finish carve-out: underfeet+rim nh≤4 bias Finish over Kick | `ChunkMeshCache.cpp` ProcessPendingGpuMeshes | `gpu_finish_n > 0` на ingress |
| P2-3 | `markrelit→fm_dirty→gpu_finish` chain telemetry + SLA kick при stall 8f | `WorldStreaming.cpp`, `ChunkEmergeCoordinator.cpp` | `fm_dirty_to_gpu_finish_n > 0` |
| P2-4 | Coalesce revision bump: не bump пока `pending_gpu_applies>0` на coord (rim only) | `ChunkMeshCache.cpp` BumpChunkMeshRevision | `mesh_apply_stale_delta` bursts ≤2 |

**Gate P2:** `unfinished_visual med ≤1`, `EH_blink ≤0.02`, `mesh_emerge_ms ≤25`.

---

## P3 — VB stall drain (стоп-спираль `164830`)

**Гипотеза:** `visible_black_stalled` 6→22 запускает pressure spiral (`heal_on_hot` 84s).

| ID | Задача | Файлы | Метрика |
|----|--------|-------|---------|
| P3-1 | Ticketed VB consume при `vb_stalled` без `pending_light_focus` | `RelightFifoPolicy.h`, `WorldPersistence.cpp` | `vb_progress_without_dark_clear_sec ≤15` |
| P3-2 | Stop VB budget — продолжение I15-B4, tail ≤50 | уже в B4 | `post_stop_visible_black_max ≤60` step |
| P3-3 | `relight_drain_near_zero_while_vb_sec ≤10` | chain drain I15-A | soft gate |

**Gate P3:** нет спирали `prep_gap >60` на cruise; `wall_ms` не выше `144853`+15%.

---

## P4 — Blink bind fix (только после P0 path proof)

**Не начинать до P0 exit.** Варианты зависят от forensics:

| Path (из P0) | Fix | Не делать |
|--------------|-----|-----------|
| `RemeshObsoleteTracked` + drawable | double-buffer bind: commit new GPU **before** free old slot (`ShouldPublishCpuBatchesBeforeFreeGpu` pattern) | park RAA / skip MarkDirty |
| `mesh_discarded_late` | hold supersede under rim drawable in-flight | witness damp blanket |
| witness hop alone | pin SLA I14b уже есть — усилить **только** при `fm_schedule_starved` bypass | block retarget при `unfinished>0` |
| VB pop | ticketed consume P3 | throttle emerge |

---

## Parity table (цели после I17)

| Метрика | `144853` (SoT) | I17 target |
|---------|----------------|------------|
| `wall_ms_fly_med` | 123 | ≤120 |
| `stream_ms` | 95 | ≤90 |
| `prep_refresh_gap_ms` | 39 | ≤35 |
| `mesh_emerge_ms` | 16 | ≤20 |
| `unfinished_visual` med | 1 | ≤1 |
| `EH_blink` | 0.018 | ≤0.02 |
| `mesh_apply_stale` bursts/пролёт | 4 | ≤2 |
| `schedule_ok` med | 2 | ≥3 |
| `post_stop_visible_black_max` | 94 | ≤60 step |
| `miss_stuck_max_run_sec` | 224 | ≤90 step |

---

## Коммит-протокол

```
P0-forensics     → docs + telem only
P1-1..P1-4       → отдельные коммиты, каждый с manual vs 144853
P2-1..P2-4       → после P1 gate
P3-*             → parallel если P1 stable
P4-*             → только с P0 blink path doc
```

Сообщения: `perf(stream): I17-P1-1 unfinished incremental reuse`

---

## Команды верификации

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium miss_first_mesh_class_test
.\build\desktop-msvc\Release\miss_first_mesh_class_test.exe

# gate of record
python tools/flight_sim_analyze.py bin/logs/perf_<ts>.jsonl --manual-idle --report bin/suite_reports/manual_<ts>_analyze.json
python tools/flight_sim_phase_gate.py --phase-id FP-manual --report bin/suite_reports/manual_<ts>_analyze.json

# delta vs SoT
python bin/research_flight_20260829/scripts/stream_waterfall.py bin/logs/perf_20260831-144853_32564.jsonl bin/logs/perf_<ts>.jsonl
python bin/research_flight_20260829/scripts/blink_window_audit.py bin/logs/perf_<ts>.jsonl --baseline bin/logs/perf_20260831-144853_32564.jsonl
```

---

## Следующий шаг

**P0-2 + P1-1** — forensics script + первый throughput fix (`unfinished` incremental reuse).  
Не трогать flicker policy flags до P0 exit.
