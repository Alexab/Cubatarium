# Phase Execution Report — Streaming Open Tasks (2026-07-22)

## Резюме

Последовательно выполнены открытые фазы плана `IMPLEMENTATION_PLAN.md` с отдельными
ветками, коммитами и autofly `--teleport-cruise`. Лучший комбинированный прогон —
**`final_combined`** на ветке `streaming/phase-4-unified` @ `ba98cfb9`.

| Метрика | Baseline | P0-v2 | F2-v1 | **final_combined** | F2 gate |
|---------|----------|-------|-------|-------------------|---------|
| sticky | 9 | **0** | 0 | **0** | ≤0 ✓ |
| nr_end | 90 | 28 | 37 | **25** | ≤36 ✓ |
| nr_Δ stop | +54 | −36 | −35 | **−62** | falling ✓ |
| fd_end | 604 | 371 | 403 | **358** | ≤280 ✗ |
| fd_Δ stop | +208 | +25 | +30 | **−59** | falling ✓ |
| pending_med | 2.5 | **0** | 3.5 | 3.5 | ≤5 ✓ |
| ahead | 41 | **13** | 13 | **13** | — |
| wall_med | 169 | **75** | 83 | **76** | — |
| async_stuck | 24s | 30s | 38s | **22s** | ≤10s ✗ |
| cold_relight | — | — | — | **12s** | new metric |

**Итог:** sticky и not_ready восстановлены; lit-but-dirty (`fd_end`) всё ещё выше
порога 280, но на финальном прогоне **fd падает** (−59 за stop). Gate F2 не закрыт
из‑за `fd_end` и `stop_wall_med` / `async_stuck`.

---

## Ветки и коммиты

| Ветка | Назначение | Ключевые коммиты |
|-------|------------|------------------|
| `streaming/phase-tracking` | учёт прогонов | `59d5fd21` docs + `phase_run_record.py` |
| `streaming/phase-p0` | P0 frontier ingress | `75392e0a` P0-v2, `b119887d` CMake scrollbar fix |
| `streaming/phase-f2` | F2 idle remesh | `ae8de29e` heavy_dirty caps, `8a5dabae` F2-v2 + unified helper |
| `streaming/phase-5-harness` | harness parity | `ba98cfb9` `cold_relight_holes_sec` |
| `streaming/phase-4-unified` | финальная сборка | = phase-5 + `PromoteFrontierHoleIngress` |

Полный лог: `docs/streaming/phase_runs.jsonl`, таблица: `PHASE_EXECUTION.md`.

---

## Фаза P0 — Frontier hole stall (A)

### Изменения (P0-v2)

- **`WorldStreaming.cpp`:** при движении + missing mesh + pending + `mesh_async<8`
  поднимается `bg_budget` (32–40); усилен re-promote при пустом relight FIFO.
- **`ChunkEmergeCoordinator.cpp`:** promote relight каждый тик при cold mesh pool.

### Плюсы

- sticky 9→0, nr_end 90→28, nr_Δ +54→−36.
- pending_med→0 на P0-v2.
- wall_med 169→75 (меньше hitch на teleport-cruise).

### Минусы

- `holes_rate` ~54% (артефакт teleport-cruise, не регрессия stop).
- `cold_relight_holes_sec` всё ещё ~12s на финале — ingress phase A не закрыт полностью.
- `fd_end` остаётся >280.

### Anti-patterns соблюдены

- Нет dark preview (кроме underfeet V2a).
- Нет sync relight flood / MarkDirty flood.

---

## Фаза F2 — Idle remesh plateau (B)

### Изменения

- **F2-v1:** ранний порог `idle_remesh_debt` (nr>12, fd>20) + heavy_dirty caps.
- **F2-v2:** откат раннего порога (регресс fd 371→403); оставлены caps при `focus_dirty>280`.

### Плюсы

- `suppress_seam_for_sticky_catchup` (из P0-v1) + heavy_dirty → **final**: fd_Δ −59, nr_Δ −62.
- `ahead` 41→13.

### Минусы

- Раннее включение `idle_remesh_debt` ухудшило fd без выигрыша по nr.
- `async_stuck` 22–38s — pipeline saturation на fly всё ещё высокая.
- F2 gate: `fd_end≤280` не достигнут (358).

---

## Фаза 5 — Harness parity

- Добавлена метрика **`cold_relight_holes_sec`**: сегменты с
  `holes>0 ∧ pending>0 ∧ mesh_async<4 ∧ relight_drain≈0`.
- `phase_run_record.py` записывает `cold_relight_sec`.
- Критерий P0 из плана теперь измерим в autofly (текущее: 12s > целевых 2–3s).

---

## Фаза 4 — Unified Column Flow (минимальный шаг)

- Вынесен helper **`PromoteFrontierHoleIngress`** в `ChunkEmergeCoordinator.cpp`
  (единая точка для P0 cruise ingress). Полный unified scheduler — отложен.

---

## Сборка

- Исправлен **LNK2001** `GuiScrollbarController` — cpp был только в test target
  (`b119887d`). Параллельные агенты ломали линковку; повторные сборки после fix
  проходят стабильно.

---

## Рекомендации для дальнейшей работы

### Merge (приоритет)

1. **Рекомендуется merge `streaming/phase-4-unified` → `perf`** — лучший баланс
   sticky/nr/fd на autofly.
2. Отдельно можно cherry-pick `b119887d` (CMake) если merge отложен.

### P0 (продолжить)

- Снизить `cold_relight_holes_sec` с 12s до ≤3s: поднять relight при
  `holes∧pending∧async<4` без sync flood (например, dedicated cruise promote path
  в `DrainRelightQueues` budget, не только `bg_budget`).
- Manual replay `perf_20260722-102559` через `--replay-manual` для валидации phase A.

### F2 (продолжить)

- Держать порог `idle_remesh_debt` 15/24; тюнить только **heavy_dirty** ветку.
- Исследовать почему `fd_end` стартует ~400+: возможно outside Dirty до stop;
  рассмотреть focus-only dirty sweep при teleport landing.
- Gate: довести `fd_end≤280` и `async_stuck≤10s`.

### Harness

- Добавить gate на `cold_relight_holes_sec≤3` в `flight_sim_phase_gate.py`.
- Сравнить `--replay-manual` vs teleport-cruise на одном phase-id.

### Не делать (подтверждено прогонами)

- Ранний `idle_remesh_debt` (12/20) — регресс fd.
- `CancelAsyncInFlightKeepDirty` на idle remesh.
- Full `idle_remesh_debt` при sticky>0.
- Sync `RebuildChunkImmediate` в force_hole при pending (sticky регресс).

---

## Артефакты

| Файл | Описание |
|------|----------|
| `bin/phase_baseline_3589c59f.json` | baseline до P0-v2 |
| `bin/phase_P0_v2.json` | P0-v2 |
| `bin/phase_F2_v1.json` | F2-v1 (частичный регресс fd) |
| `bin/phase_final_combined.json` | лучший комбинированный прогон |
| `docs/streaming/phase_runs.jsonl` | машинный лог всех прогонов |
