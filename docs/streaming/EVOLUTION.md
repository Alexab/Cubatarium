# Evolution Of Streaming Visualization

## Executive Summary

История Cubatarium по визуализации streaming делится на три независимых трека:
`mesh`, `lighting`, `streaming`. Каждый трек по отдельности улучшался, но
конечный контракт между ними долго оставался неявным. Из-за этого проект
несколько раз приходил к одной и той же поломке в новой форме: либо
пропадал mesh, либо mesh был уже виден, но оставался тёмным, либо `PendingLight`
застревал после пролёта.

Ключевой исторический вывод: большинство регрессий не были «проблемой одной
очереди», а следствием разрыва между состояниями `column ready for work` и
`column safe to draw`.

## Три трека

| Трек | До мая 2026 | Июнь 2026 | Июль 2026 |
|------|-------------|-----------|-----------|
| `mesh` | sync `RebuildBlockMesh()` | async mesh, dirty revisions | mesh-on-commit, soft-defer, starvation heuristics |
| `light` | single `vLight` MVP | нет полноценного streaming-contract | lightmap, async relight, `PendingLightBeforeMesh` |
| `streaming` | sync gen / JSON | scheduler, async IO, diagnostics | column FSM, wedge, pressure, watchdogs |

## Эра 0. Baseline До Streaming

- Полный или spawn-патч строится синхронно через `RebuildBlockMesh()`.
- Граничные артефакты чинятся через 3x3 `MarkDirty` на соседние колонки.
- Масштабирование ограничено: модель работает для стартовой области, но
  плохо переносится на реальный stream world.

Опорный документ: `docs/ARCHITECTURE.md`.

## Эра 1. Май 2026: Terrain-Aware Streaming

Ключевые коммиты:

- `445ec535` — terrain-aware streaming и remesh новых чанков.
- `6a9af2c4` — уменьшение hitch при движении, первые sync collision fixes.

Что изменилось:

- Мир стал подгружаться колонками вокруг игрока.
- Возникла первая большая развилка: данные уже можно закоммитить в мир, но
  mesh и свет ещё не обязаны быть готовы в тот же момент.

Главный урок: commit terrain column и visual readiness нельзя считать одним
событием.

## Эра 2. Июнь 2026: Phases A-E

Это период, закрытый в `docs/TECH_DEBT_CHUNK_STREAMING.md`.

Ключевые коммиты:

- `fbbad823` — `UChunkLoadScheduler`.
- `4c7f2f02` — `UAsyncMeshBuilder`.
- `5ccf7e4b` — modular `.cchunk` IO.
- `41f7b837` — generation tokens.
- `38bdc7b2` — `movement_diagnostics.v2`.

Что улучшили:

- Разделили gen / mesh / IO.
- Появились per-frame budgets и нормальная диагностика.
- Убрали часть синхронных стопоров на load/unload.

Что не решили:

- Свет ещё не входил в render-contract.
- Для игрока «chunk committed» и «chunk visually ready» по-прежнему выглядели
  как одно и то же, хотя в коде это уже не так.

## Эра 3. Июнь 23-27: Horizon And Warmup

Ключевые коммиты:

- `0ced6dd6` — fog / gradient sky.
- `11159631`, `7fbd3ca8` — mesh warmup, fog/cull alignment.

Что улучшили:

- Смягчили видимую границу мира.
- Согласовали cull horizon, fog horizon и загрузку.
- Улучшили enter-game warmup.

Урок: визуально скрыть край мира можно, но это не заменяет готовность самих
колонок.

## Эра 4. Июль 1-3: Facades И Ring Gate

Ключевые коммиты:

- `92eb51ed` — `UWorldMeshService`, `UWorldStreaming`.
- `f38a9a11` — ring gate.

Что улучшили:

- Разбили монолит `World.cpp`.
- Появился более формальный streaming фасад.
- Ring gate стал terrain-oriented, а не light-oriented.

Урок: архитектурный рефакторинг упростил кодовую базу, но не решил инвариант
`draw => lit`.

## Эра 5. Июль 8-10: Lightmap Revolution

Ключевые коммиты:

- `ee121c81` — MVP voxel lighting.
- `cf4146cf` — packed lightmap storage.
- `17abd24c` — sampling lightmap in mesh.
- `0733a374` — async streamed column relight.
- `e528db7c` — lighting-aware fog.

Что улучшили:

- Перешли от грубого `vLight` к lightmap-подходу.
- Вынесли relight в отдельный pipeline.
- Привязали свет к greedy mesh.

Новая проблема:

- Стало возможно получить корректно загруженную колонку с уже собранным mesh,
  но без итогового streamed relight.

## Эра 6. Июль 10-11: Load Freeze И TD-LIGHT

Ключевые темы:

- `TD-LIGHT-001` — bulk emissive relight после create/load.
- `TD-CS-022/023/024` — freeze на 48% / 80%, async relight during load.
- `TD-CS-021` — hitch при движении остаётся частично открытым.

Что улучшили:

- Load path стал более строго упорядоченным.
- Mesh warmup перестал стартовать слишком рано.

Урок: порядок `load -> relight -> mesh warmup` был исправлен на загрузке, но
не на обычном runtime streaming при движении.

## Эра 7. Июль 12-13: World_Gen4 Crisis

Ключевые коммиты:

- `a6da4875` — stable fix-forward baseline.
- `d4ab59dd` — mesh-on-commit + per-chunk revision.
- `87a91c8b` — seam neighbor remesh.
- `a42ee80a` — keep old mesh during rebuild.

Что улучшили:

- Ревизии mesh стали более строгими.
- Отказались от хрупкого incremental merge.
- Уменьшили мигание при async rebuild.

Урок: ревизии решили часть race condition, но сами по себе не создают
визуальный SLA.

## Эра 8. Июль 14-18: Column FSM

Ключевые коммиты:

- `92c0b946` — `ColumnEmergeState`.
- `2104326d` — light-before-mesh near player.
- `03f98416` — underfeet unblock.
- `556bc3d0` — ahead-fill at cruise.
- `a9539b34` — pressure admission.

Что улучшили:

- Появилась явная FSM:
  `Empty -> VoxelsReady -> Lighting -> LitReady -> Meshing -> RenderReady`.
- Введён `PendingLightBeforeMesh`.
- Появился `forward wedge`, чтобы ускорять фронт движения.

Что сломалось концептуально:

- `soft-defer` и `forward wedge` образовали обходные пути вокруг строгой FSM.
- Часть колонок получала preview mesh до готового света.
- Trail-колонки оставались в `PendingLight` без гарантированного mesh ingress.

## Эра 9. Июль 20: Flight-Sim Era

Ключевые коммиты:

- `310073d0` — flight-sim harness.
- `2b8c7f8b` — inflight exclusion.
- `4ffe1d98` — never-drop queues.
- `5e7adb19` — empty=missing remesh.
- `907119ac` — revert `5e7adb19`.

Что улучшили:

- Появилась воспроизводимая автоматическая регрессия.
- Система стала измеряться не только по FPS, но и по дыркам на маршруте.

Что стало главным предупреждением:

- Глобальная трактовка empty mesh как hole привела к регрессии и была откатана.

Урок: recovery-механизм не должен ломать семантику «пустой, но корректный»
срез или всю колонку.

## Эра 10. Июль 21: Idle Drain И Dark Telemetry

Ключевые коммиты:

- `f06acade` — idle underfeet remesh.
- `c0d95c04` — sticky holes без starvation Dirty.
- `52900d3a` — ghost `InFlight` cleanup и dark telemetry.

Что улучшили:

- `PendingLight` перестал полностью «прятаться» за `visual_holes=0`.
- Harness научился видеть тёмные чанки и plateau по `pending_light_focus`.

Что осталось:

- На stop still возможна связка: `mesh есть`, `visual_holes=0`, `pending_light`
  не уходит к нулю, картинка тёмная.

## Эра 11. Июль 21 вечер: Regress Window И Откат К Last-Good

Коммиты после `2cb85f3c` (align unfinished_visual) дали полный регресс:

| Commit | Claimed | Evidence |
|--------|---------|----------|
| `2cb85f3c` | unfinished = render-ready | `perf_165208`: sticky=0, pending→0, not_ready≈25 (lit-but-dirty) |
| `9c51dae2` | idle stale remesh | not_ready=73, pending stall |
| `675635a3` | plateau sync-relight | wall↑, spike 620ms |
| `9f5de9eb` | full-band sync remesh | wall 172→420 |
| `fba28746` | unified async idle drain | `perf_181020`: not_ready=90, sticky=32, pending=53, dirty→705 |

Урок: competing recovery (sync remesh, Refresh flood, async-only без throughput)
чинит один симптом и раздувает соседний. Last-good для системного V2→V3 —
`2cb85f3c`. Harness anti-hang (`7c487c83`): preflight kill, process-timeout,
report-before-shutdown, `PrepareForShutdownFast`.

## Эра 12. Июль 22: Memory Crisis И Budget Control

Ключевые коммиты:

- `0cb92063` — mesh Completed `DrainAll`; нет orphan RAM после ForgetInflight.
- `b1f8924c` — RemeshAfterApply / standing remesh latch.
- `02b9868d` — fluid `HasChunk` (не `GetOrCreate` в missing).
- `8bbc3139` — memory telemetry (`rss_mb` / `private_mb` / `chunk_count`).
- `152cb5df` — bound light BFS к existing chunks; cap idle Capture; remesh thrash clamp.

Что улучшили:

- Place lit block больше не раздувает Private Bytes через unbounded skylight/blocklight BFS.
- Fluid simulation не тянет missing chunks в resident set.
- Completed drain не оставляет orphan mesh payloads в RAM.

Что осталось / следующий шаг:

- Нет byte-budget и fill%-телеметрии очередей; Completed/Dirty/FIFO grow-only.
- См. [`MEMORY_BUDGET.md`](MEMORY_BUDGET.md): ring Completed, soft-caps Dirty/Pending,
  GPU Reserve/Max, `MemoryBudgetController`, chunk free-list.

Урок: throughput-caps (pipeline ×N, pressure G/Y/R) не заменяют byte-budget;
unbounded BFS/queues дают 10–25 GB high-water при «нормальном» FPS.

## Каталог Решений R1-R24

| ID | Решение | Когда | Почему не стало финалом |
|----|---------|-------|--------------------------|
| `R1` | sync full-world mesh | baseline | не масштабируется |
| `R2` | async mesh без light gate | июнь | unlit mesh |
| `R3` | ring gate by light | июль | idle empty pockets |
| `R4` | strict pending-light gating | июль | trail starvation |
| `R5` | soft-defer first-mesh | июль | dark preview |
| `R6` | forward wedge only | июль | trail debt |
| `R7` | full near-focus idle admit | июль | dirty flood |
| `R8` | `StarveRemeshForHoles` | июль | блокирует recovery |
| `R9` | global empty=missing | 20.07 | reverted |
| `R10` | sync idle relight flood | июль | spikes 1-2 s |
| `R11` | never-drop queues | 20.07 | dirty plateau |
| `R12` | watchdog zoo | июль | complexity / ghost state |
| `R13` | pressure G/Y/R | июль | holes != light debt |
| `R14` | ghost promote fix | 21.07 | partial fix only |
| `R15` | idle Refresh / stale remesh flood | 21.07 | dirty↑, mesh thrash |
| `R16` | plateau sync-relight | 21.07 | wall spikes, not_ready↑ |
| `R17` | full-band sync remesh | 21.07 | wall 172→420 |
| `R18` | unified async drain без ownership | 21.07 | sticky=32, not_ready=90 |
| `R19` | Completed DrainAll / no Forget orphan | 22.07 | нет byte-budget |
| `R20` | RemeshAfterApply / remesh latch | 22.07 | standing thrash при async↑ |
| `R21` | fluid HasChunk (no GetOrCreate) | 22.07 | только fluid path |
| `R22` | rss/private/chunk telemetry | 22.07 | observe-only |
| `R23` | bound light BFS + idle Capture cap | 22.07 | нет Soft/Budget controller |
| `R24` | MemoryBudget (rings/caps/controller) | 22.07 | внедряется; gates TBD |

## Матрица Проблем И Уроков

| Симптом | Историческая причина | Урок |
|---------|----------------------|------|
| Дыры | missing mesh starvation | first-mesh важнее remesh |
| Чёрные чанки | preview до relight | draw contract должен включать light |
| plateau `pending_light_focus` | ghost `InFlight` | нужен единый owner очереди |
| `dirty` > 500 | recovery flood | recover должен быть bounded |
| autofly != manual | headless oversync | отдельные gates по wall time |

## Legacy / Мёртвые Пути

- `OnIsColumnPendingLight` (= `!IsColumnVisualReadyForRing`) влияет на
  PrefetchKeepShell / MemoryBudget SLA; **не** на terrain `RingPrerequisitesMet`.
- `RecoverStickyBlackFocusSync` удалён (R2); sticky → `ColumnFlowExecutor` RemeshSeam.
- `RebuildChunkLegacy` остаётся sync fallback path.

## Anti-Patterns

- Глобальный `empty => missing`.
- `CancelAsyncInFlightKeepDirty` + `MarkDirty all focus`.
- Sync relight flood на idle.
- Render-поведение, которое зависит от эвристики, а не от `RenderReady`.
- Drop Completed без Dirty/relight requeue; erase PendingLight у cold hole.
- Unbounded light BFS вне `HasChunk`; fluid `GetOrCreate` в missing.
- Expand буферов при Soft/Hard memory pressure; `glBufferData` orphan каждый кадр.
- Hide sticky/stale-dark **без** ColumnFlow repair ticket.
- Cap `mesh_schedule` при holes (async underfeed).
- Fog / draw-hide как замена FOV throughput.

## Эра 13. Июль 29: Root Cause Freeze + Hide⇒Ticket

Артефакты: `manual_latest_1645`, `manual_qual_fix3`, `manual_latest_1752`,
`edge_qual_fix3`. Executive: [`ROOT_CAUSE_2026-07.md`](ROOT_CAUSE_2026-07.md).

Что показал 1752:

- `post_load_ring_not_ready=37` не падает за idle → spawn burst недостаточен
  (TD-ARCH-021 reopen → partial).
- `holes_rate=1.0`, Dirty≈300, MeshAsync≈2 → underfeed + SoftDefer.
- Hide sticky/dark (022) улучшил stop в qual_fix3, ухудшил cruise (025).
- GPU packed (Phase3) на ветке; mesher всё ещё hybrid на hot path.

Дорожная карта исправления (не zoo-knobs):

- D1: Hide⇒Ticket, async floor, spawn idle catch-up, stop sticky drain.
- D2: ColumnRenderable SoT, FirstMesh/Remesh dirty class, ColumnFlow-only,
  SoftDefer Capture floor.
- D3: harness gates = architecture DoD; merge develop только после PASS.

TD-ARCH-026..030 открыты в `TECH_DEBT_CHUNK_STREAMING.md`.

## Эра 14. Авг 2026: Frame nest + wall-gated heal deadlock

Артефакты: `manual_latest_151212`, `manual_latest_131827`, `manual_latest_110751`.
Executive: [`ROOT_CAUSE_2026-08.md`](ROOT_CAUSE_2026-08.md),
[`ERA14_POSTMORTEM.md`](ERA14_POSTMORTEM.md).

Что показал 151212:

- wall med ~200 / max ~957; phys med ~172 при `physics_block≈0` → nest.
- miss_cy 0–3 sticky ~38s на land exit; underfeet OK.
- `stand_rim_dirty_n` рос, но `stale_repair_wave_n=0`, `stand_rim_imm_n=0`
  (gates wall≤40/50).

Post-Era13 кластеры (1f52bdd5..): MeshWorkAdmission / F0 sync=0 / idle I1–I7 /
rim witness / DigSeam / stand heal — частичный hold, wall и land tops не закрыты.

Дорожная карта Era14 V4:

- Phase 1: `TickWorldStreamingPhase` вне `RunLegacyPhysicsFrame` (TD-040).
- Phase 2: DesiredStage; kill calm Imm / wall enqueue (TD-041..043).
- Phase 3: commit seed + remesh-on-lit (TD-044..045).
- Phase 4: worker Capture / trim knobs (TD-046..047).
- Phase 5: PREMERGE + ARCH_D3 DoD (TD-048).

### Матрица Проблем (Era14 addendum)

| Симптом | Историческая причина | Урок |
|---------|----------------------|------|
| phys 170+ без collision | stream/emerge inside DoMovement | frame contract first |
| Imm/wave never fire | heal gated on last_frame_ms | enqueue ≠ sync cost |
| Invisible chunk tops | miss sticky + Dirty backlog | FirstMesh floor + land gates |
