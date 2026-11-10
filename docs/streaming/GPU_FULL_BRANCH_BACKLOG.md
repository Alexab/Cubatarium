# GPU Full Branch Backlog (Desktop-first, Android opt-in)

Цель: довести текущий D1 до полноценной GPU-ветки без CPU-assist в desktop hot-path, с повторяемым циклом `build -> unit -> autofly -> analyze -> gate -> commit`.

## Правила выполнения

- Коммит только при `GO` нужной фазы.
- При `NO-GO`:
  - один rerun допустим только при spike-only шуме;
  - иначе фикс и повтор полного цикла.
- Для каждого phase-report хранить `bin/phase_<id>.json`.
- Обязательные базовые проверки на каждом этапе: `F2` + phase gate.

## Базовые команды (шаблон)

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
pwsh tools/run_gpu_tail_unit_tests.ps1

python tools/flight_sim_run.py --world World_164 --teleport-cruise --seconds 130 `
  --fly-stop --fly-phase-sec 45 --stop-phase-sec 60 --idle-sec 8 `
  --phase-id <PHASE_ID> --report bin/phase_<PHASE_ID>.json

python tools/flight_sim_phase_gate.py --phase-id F2 --report bin/phase_<PHASE_ID>.json
python tools/flight_sim_phase_gate.py --phase-id <PHASE_ID> --report bin/phase_<PHASE_ID>.json
```

## Метрики, которые проверяем всегда

- `wall_ms_no_holes_med`
- `post_stop_pending_med`
- `cold_relight_holes_sec`
- `post_stop_black_sticky_max`
- `chunks_traveled`

## Фазы реализации

### GPF0 — Baseline freeze и измеримость

**Задачи**
- Зафиксировать baseline report: `bin/phase_GPF0.json`.
- Убедиться, что в analyze/gates доступны:
  - `gpu_mask_readback_med`
  - `gpu_blocklight_flood_med`
  - `backend_lighting_mode` (`backend_lighting_full`, `backend_lighting_flat`)
- Добавить (если нет) метрики:
  - `gpu_transparent_sort_gpu_med`
  - `gpu_opaque_emit_gpu_med`
  - `gpu_fluid_readback_med`
  - `gpu_light_readback_med`

**GO**
- `F2=GO`, `D1d=GO`, `PA=GO` на baseline.

**Коммит**
- `gpu(full): baseline metrics and probes`

---

### GPF1 — Opaque full GPU emit (главный хвост)

**Задачи**
- Перейти от CPU extract/merge к GPU-цепочке:
  - mask pass
  - greedy-rect pass
  - vertex/index emit pass
- Убрать CPU decode/merge из desktop hot-path.
- Оставить debug parity readback только под флагом.

**Тесты**
- `gpu_greedy_face_extract_test`
- новый parity test для GPU emit (counts/area/material/light parity)

**GO (gate D2a)**
- `gpu_mask_readback_med == 0`
- `gpu_opaque_emit_gpu_med > 0`
- `wall_ms_no_holes_med <= 45` (целевой <= 38)
- `vertex_pool_fill_med <= 0.85`

**Коммит**
- `gpu(full): opaque GPU greedy emit without CPU decode`

---

### GPF2 — Transparent GPU order-table

**Задачи**
- Вынести сортировку transparent refs в GPU backend:
  - build keys
  - GPU sort (bitonic/radix)
  - order indirection table
- Сохранить draw-technique:
  - desktop stencil multi-pass
  - GLES single-pass

**Тесты**
- parity порядка по фиксированной камере
- стабильность tie-break/минимизация flicker

**GO (gate D2b)**
- `gpu_transparent_sort_gpu_med > 0`
- `wall_ms_no_holes_med <= 45`
- `post_stop_black_sticky_max == 0`

**Коммит**
- `gpu(full): transparent GPU order table`

---

### GPF3 — Fluid scan без sync readback

**Задачи**
- Убрать sync `GetBufferSubData` из fluid scan hot-path.
- Перейти на GPU-consumed буфер/таблицу (или async staging без stall).

**Тесты**
- `gpu_fluid_column_scan_test`
- `fluid_surface_pack_reuse_test`

**GO (gate D2c)**
- `gpu_fluid_readback_med == 0`
- `gpu_fluid_scan_on_med >= 0.5`
- `wall_ms_no_holes_med <= 45`

**Коммит**
- `gpu(full): fluid scan path without sync readback`

---

### GPF4 — Lighting compute path (skylight + blocklight)

**Задачи**
- Реализовать compute blocklight flood.
- Убрать sync full-volume readback из lighting hot flow.
- Сохранить LitReady/SoftDefer контракты.

**Тесты**
- `chunk_lighting_propagation_test`
- `chunk_bulk_blocklight_test`
- `gpu_skylight_merge_test`

**GO (gate D2d)**
- `gpu_light_readback_med == 0`
- `cold_relight_holes_sec <= 3`
- `post_stop_pending_med <= 5`
- `backend_lighting_full == 1`
- `backend_lighting_flat == 0`

**Коммит**
- `gpu(full): compute lighting flood without sync readback`

---

### GPF5 — Desktop hot-path cleanup (CPU-assist tails)

**Задачи**
- Свести CPU fallback в desktop hot-path к аварийному/debug-only.
- Явно мониторить fallback activation rate.
- Подчистить remaining sync-readback fallback branches.

**GO (gate D2e)**
- `gpu_fallback_rate == 0`
- `gpu_cull_indirect_med >= 0.5`
- `gpu_draw_cmds_med <= 15`
- `wall_ms_no_holes_med <= 45`

**Коммит**
- `gpu(full): minimize cpu assist in desktop hot path`

---

### GPF6 — Android GPU A0-A4 (opt-in / GPU-by-default)

**Status:** Landed (GPU-by-default when probe+allowlist; user opt-out).

**Задачи**
- A0 capability audit + `ApplyAndroidGpuPolicy`.
- A1 fluid compute GLES path (`ComputeProgram` + `#version 310 es`).
- A2 hybrid mesher (`UAndroidGpuGreedyMesher` + staging, CPU cull).
- A3 transparent keys + caps-driven GLES single-pass.
- A4 production rollout (`android_gpu_enabled` default true + allowlist + UI).

**GO**
- `AG0` on desktop (probe telemetry); `AG1..AG4` on capable device.
- стабильность + отсутствие регрессий fallback CPU path (opt-out).

**Коммиты**
- `gpu(android A0)..A4` (or combined `gpu(android A0-A4)` when landed together).

## Шаблон авто-коммита после GO

```powershell
git add <changed_files>
git commit -m "gpu(full): <phase short title>"
```

Рекомендуемый порядок коммитов:
1. `GPF0`
2. `GPF1`
3. `GPF2`
4. `GPF3`
5. `GPF4`
6. `GPF5`
7. `GPF6` (A0..A4 отдельно)

## Checklist перед merge full GPU

- Все фазы `GPF0..GPF5` в `GO`.
- `PA=GO` после финальной фазы.
- `gpu_mask_readback_med == 0`
- `gpu_fluid_readback_med == 0`
- `gpu_light_readback_med == 0`
- `backend_lighting_flat == 0` при `backend_mesher_gpu == 1`
- Доки обновлены:
  - `GPU_PHASE_EXECUTION.md`
  - `GPU_PIPELINE.md`
  - `PREMERGE_CHECKLIST.md`
  - этот backlog-файл
