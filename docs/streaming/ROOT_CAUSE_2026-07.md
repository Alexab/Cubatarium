# Root Cause: почему фиксы визуализации не кончаются (2026-07)

## Одна фраза

GPU draw/store/cull уже ускорены; боль — **контракт готовности колонки**:
`commit → seed/relight → first-mesh → GPU admit → draw-gate` размыт между SoftDefer,
Dirty backlog и hide-without-repair.

## Доказательство (manual_1752)

| Симптом | Метрика | Root |
|---------|---------|------|
| Неполное кольцо при старте | `post_load_ring_not_ready=37` idle | Spawn warmup/burst не добивает Visual RD |
| Не догоняет в полёте | `holes_rate=1.0`, `dirty≈300`, `mesh_async≈2` | SoftDefer + underfed async + cap schedule |
| Чёрные грани на выходе | `dark_face_near≈761`, sticky peak 3 | Hide sticky/dark без гарантированного remesh ticket |

Логи: `bin/logs/perf_20260729-175223_27816.jsonl`,
`bin/iter_reports/timeline/manual_latest_1752.json`.

## Почему локальные фиксы возвращают ту же боль

1. **Три трека** (mesh / light / streaming) с разными бюджетами — фикс одного слоя ломает другой.
2. **Hide без ticket** (`IsColumnRenderReady` sticky/stale-dark r>1) → unfinished навсегда (TD-ARCH-025).
3. **Cap `mesh_schedule≤6`** при holes → async голодает при Dirty сотнях (TD-ARCH-024 trade-off).
4. **Fog / draw-hide** маскируют unfinished, не чинят throughput.
5. **GPU packed/MDI** ускоряет emit — не SoftDefer и не cold relight.

## Целевой контракт (D1–D2)

1. `Draw ⇔ ColumnRenderable` — один SoT.
2. **Hide ⇒ RepairTicket** через `ColumnFlowExecutor` (RemeshSeam / RelightThenMesh).
3. **Async throughput floor** при `unfinished_fov>0` (не cap schedule).
4. **FirstMesh ≠ Remesh** в Dirty.
5. SoftDefer только с **Capture/relight floor**.

## Ветки

`develop` → `opt_3d` (GPU hybrid) → `arch/streaming-v2-v4` (V2–V5 + packed).
Merge в develop — после gate DoD PASS.

## Связанные документы

- [`ARCHITECTURE_OPTIONS.md`](ARCHITECTURE_OPTIONS.md) — Era 13
- [`EVOLUTION.md`](EVOLUTION.md) — Era 13
- [`BEST_PRACTICES.md`](BEST_PRACTICES.md) — Hide⇒Ticket gap
- [`../TECH_DEBT_CHUNK_STREAMING.md`](../TECH_DEBT_CHUNK_STREAMING.md) — TD-ARCH-021, 025–030
