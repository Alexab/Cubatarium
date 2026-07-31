# Best Practices Comparison

## Главный Инвариант

В mature voxel engines mesh sampling и light propagation синхронизированы так,
чтобы рендер видел только итоговое состояние света. Preview без готового света
либо не рисуется вовсе, либо рисуется специальным нейтральным placeholder.

Для Cubatarium это означает: `GreedyCache` сам по себе не должен считаться
достаточным критерием «визуально готово».

## Сравнение

| Практика | Industry | Cubatarium | Gap |
|----------|----------|------------|-----|
| Light before first visible mesh | Minecraft-like clients, Veloren | ранее разрешался preview mesh при `PendingLight` | критический |
| Skylight source seeding from top / neighbor context | Starlight, Minecraft lighting optimizations | relight обычно стартовал после commit как отдельный шаг | высокий |
| Do not propagate into not-yet-ready chunks | Starlight | trail columns могли долго жить в backlog | высокий |
| Dirty-only rebuild with bounded budget | common chunk managers | есть, но смешивался с starvation heuristics | средний |
| Separate visibility contract from render contract | typical production engines | `visual_holes` vs unfinished/black sticky; gap закрывается V2a | критический |
| Single owner for column lifecycle | explicit job graphs | ownership размазан (Admit/Recover/Refresh/Drain) — V2b | высокий |
| Explicit memory budgets + fill% telemetry | UE streaming pools, vertex pools | throughput-caps есть; byte-budget / fill% — Era 12 / `MEMORY_BUDGET.md` | высокий |
| Bounded result queues (drop-oldest + requeue) | job graphs with backpressure | Completed mesh/relight grow-only → rings | высокий |
| Grow-only GPU buffers with Reserve/Max | common GPU upload pools | `GreedyVertexPool` grow-only; Reserve/Max — Era 12 | средний |

## Gap После V2–V5 migration (arch/streaming-v2-v4, 2026-07-28)

Architecture landed (R0–R7): SeedDecision fail→PendingLight, ColumnFlowExecutor,
FOV/keep visual SLA (not terrain PendingLight gate), lighting seed factory
Cpu≠Gpu, idle Capture progress, CollectAll removed. Evidence: `edge_R1`/`R2`/`R3`
`run_outcome=success`.

Era13 architecture DoD (026–030): **done** — Hide⇒Ticket via ColumnFlow
Contains, AllowUnlitFirstMesh SoT SoftDefer, FocusPressure≠hole, Capture floor
on UnfinishedVisual, FocusIngress Stage SLA. Remaining gate DoD (not architecture):

- ARCH_D3 `wall_ms_med≤30` (lit remesh clamp; evidence autofly).
- F2/C/CB residuals on edge; TD-ARCH-011 blue_screen; TD-ARCH-015 worker Capture backlog.

Research alignment: Luanti/Minetest chunk job ownership, UE streaming memory
budgets, Qt RHI capability backends, 0fps-style lighting-before-mesh — mapped to
V3 seed, V4 executor, V5 visual SLA, E4 factory (see TD-ARCH closed table).

## Gap После Era 11 (historical)

- Preview mesh при `PendingLight` всё ещё возможен (R5).
- Draw не гейтится `RenderReady`.
- R15–R18 подтверждены логами `perf_165208…181020`; откат к `2cb85f3c` обязателен перед V2.

## Memory (Era 12)

Industry: UE-style memory budgets, vertex pooling, toroidal/chunk pools, overflow
policies (drop reproducible work; never drop sole world state). Cubatarium: см.
[`MEMORY_BUDGET.md`](MEMORY_BUDGET.md) — Soft/Budget Mb, Completed rings,
Dirty/Pending soft-caps, GPU Reserve/Max, `MemoryBudgetController`, chunk free-list.

Правило overflow: drop oldest/farthest **только** если результат можно
пересоздать (remesh/relight); gen/IO — block admit; cold PendingLight — не erase.

## Что Совпадает С Industry

- Async mesh rebuild с validation по revision.
- Budgeted scheduler для rebuild и IO.
- 3x3 remesh на колонных границах.
- Отдельный perf harness для flight/load regression.

## Что Не Совпадало

### 1. Preview Mesh До Готового Света

Это главная причина «чёрной земли без дыр».

С точки зрения пользователя проблема выглядит как «мир не достроен», но в
телеметрии долгое время это не считалось hole, потому что mesh действительно
существовал.

### 2. Свет Как Отдельный Долг После Commit

Если relight запускается уже после того, как колонка попала в visual ring,
система начинает жить в режиме долга:

- колонка уже нужна игроку;
- колонка уже может претендовать на draw;
- итоговый light state ещё не готов.

### 3. Несколько Recovery-Путей На Одну И Ту Же Колонку

В industry-практиках чаще используется единая job ownership model. В
Cubatarium recover/admit/promote paths постепенно эволюционировали отдельно,
из-за чего стало сложнее предсказать судьбу конкретной колонки.

## Источники

- [0fps: Meshing in a Minecraft Game](https://0fps.net/2012/06/30/meshing-in-minecraft/)
- [PaperMC Starlight Technical Details](https://github.com/PaperMC/Starlight/blob/fabric/TECHNICAL_DETAILS.md)
- [Let's Make a Voxel Engine: Chunk Management](https://sites.google.com/site/letsmakeavoxelengine/home/chunk-management)
- [Gamedev StackExchange: voxel lighting with occlusion](https://gamedev.stackexchange.com/questions/19207/how-can-i-implement-voxel-based-lighting-with-occlusion-in-a-minecraft-style-gam)

## Gap После manual_1752 / Era 13 (2026-07-29)

| Практика | Industry | Cubatarium после Era13 closeout | Gap |
|----------|----------|--------------------------------|-----|
| Hide ⇒ guaranteed repair ticket | Job graph + TTL/requeue | ColumnFlow Contains + sticky enqueue TickDerived | **closed** TD-ARCH-026 |
| Throughput floor when FOV unfinished | Raise async/apply floor | AllowUnlitFirstMesh + schedule floors | **closed** TD-ARCH-027 |
| Single ColumnRenderable SoT | One stage flag | GetColumnRenderableState + FocusPressure split | **closed** TD-ARCH-028/030 |
| FirstMesh queue ≠ Remesh thrash | Separate priorities | FirstMesh tickets + lit remesh clamp | **closed** TD-ARCH-029 |
| SoftDefer with Capture floor | Light debt must progress | Capture on UV\|missing; FocusIngress Stage SLA | **closed** TD-ARCH-030 |
| Frontier rim first-mesh latency | Stage SLA | FocusIngress unfinished/stale-dark | **partial** TD-ARCH-033 |
| GPU mesher end-to-end | Resident GPU mesh | Hybrid extract + packed path; cost ≠ readiness | средний (cost track) |

## Практический Вывод Для Cubatarium

Наиболее полезные заимствования:

1. Строгий `RenderReady` / `ColumnRenderable` контракт.
2. Commit-time skylight seed для загруженного соседнего ring.
3. Отдельная метрика `unfinished_visual` как источник правды для gates.
4. Постепенное схлопывание watchdog zoo в единый column scheduler.
5. **Hide⇒RepairTicket** — никогда не прятать геометрию без job в ColumnFlow.
6. **Async throughput floor** при unfinished FOV; cap только Immediate/sync.
7. **FirstMesh ≠ Remesh** в dirty admission.
