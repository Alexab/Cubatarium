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

## Gap После Era 11

- Preview mesh при `PendingLight` всё ещё возможен (R5).
- Draw не гейтится `RenderReady`.
- R15–R18 подтверждены логами `perf_165208…181020`; откат к `2cb85f3c` обязателен перед V2.

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

## Практический Вывод Для Cubatarium

Наиболее полезные заимствования:

1. Строгий `RenderReady` контракт.
2. Commit-time skylight seed для загруженного соседнего ring.
3. Отдельная метрика `unfinished_visual` как источник правды для gates.
4. Постепенное схлопывание watchdog zoo в единый column scheduler.
