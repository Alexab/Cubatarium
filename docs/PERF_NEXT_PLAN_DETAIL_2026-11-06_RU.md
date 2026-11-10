# Детальный план доработок PERF-0.29…0.33

Дата: 2026-11-06. Развёртка `docs/PERF_NEXT_PLAN_2026-11-06_RU.md` по состоянию `develop` после `b2e950e6f` (PERF-0.28).
Дополнено картой кода: для каждого этапа указаны точки входа, гипотезы, witness-процедуры, шаги, тесты и критерии приёмки.

## 1. Зафиксированная база (эталон после PERF-0.28)

Маршрут `World_223`, cruise `(-550,110)`, полёт с остановкой, 83 с, `--metrics`, без trace:

| Метрика | Значение | Комментарий |
|---|---|---|
| `face_debt` | 0 | цикл FaceDebtCallback ↔ PriorityChunkEmergeRepair разорван |
| `dirty` | 0 | |
| `ready` | 63 | |
| `missing_mesh` | 27 | resident missing meshes — объект PERF-0.29 |
| `miss_end` | 1 | финальный miss witness — объект PERF-0.29 |
| `visual_holes` | 0 | реальных дыр нет |
| `demand_stop_converged` | false | объект PERF-0.30 |

Последние witness-запуски: `bin/suite_reports/perf_0_28/first_drawable_edge/logs/*.jsonl` (без trace),
`bin/suite_reports/perf_0_28/orphaned_missing_trace/logs/*.jsonl`. Числа ревизий сравнивать только между trace-запусками.

## 2. Карта кода (якоря для всех этапов)

### Метрики и классификация

- `FocusMissingMesh` (0/1, near-probe): `src/World/Streaming/WorldStreaming.cpp:3128`; probe/witness capture — `WorldStreaming.cpp:800-880` (`missing_near_probe_*`, memo-hit).
- Census классов колонок `FocusVisualMissingMeshN` (счётчик вида «missing_mesh=27»): `src/World/Streaming/WorldStreaming.cpp:2897`.
- Классификация `FocusColumnVisualClass`: `ClassifyFocusColumnVisual` — `src/World/Core/World.cpp:3861-3960`; перевод `MissingMesh`→`FaceDebt` через `ShouldClassifyMissingMeshAsFaceDebt(face_debt, stable_face_debt_only)` (`World.cpp:3921-3930`).
- `ColumnRenderableState::BlockReason::MissingMesh`: `src/World/Core/World.cpp:3662, 3851-3858` (частичная колонка может быть `draw_ok=true` с reason=MissingMesh).
- Подсчёт непогашенных: `CountUnfinishedVisualNear` — `World.cpp:3993`, `CountUnfinishedVisualByFacing` — `World.cpp:6544`, `CountPostLoadRingNotReady` — `World.cpp:11892` (кэш `UnfinishedVisualCache`, радиус enter R=4 vs streaming R).
- `miss_end` в анализаторе: `tools/flight_sim_analyze.py:714-716` (последнее значение `focus_missing_mesh` в периоде), `miss_end_stop` — `:781-785`; gates: `tools/flight_sim_phase_gate.py:362-363` (`miss_end<=0`, `miss_end_stop<=0`).

### Сходимость demand (PERF-0.30)

- `UChunkRenderDemandStore::StopConverged` — `src/World/Streaming/ChunkRenderDemand.cpp:1045-1104`. Возврат `false`:
  1) orphan: `has_active_attempt && active_stage==Created && last_progress_ms<=0`;
  2) `face_debt_mask != 0`;
  3) `!CoverageSatisfied(rec, 0)`;
  4) infinite Retain (`retained_awaiting_successor` без нового desire или без живого attempt);
  5) `published_geom_rev/light_rev != desired_*` без живого attempt либо со стальном stall (`kStallFailMs`).
- Публикация в телеметрию: `src/World/Core/World.cpp:3087-3089` (`DemandStopConverged`, `ColumnEmergeDemandStopMs`).
- Identity полей demand: `src/World/Streaming/ChunkRenderDemand.h:85-121` — `desired/published_geom_rev`, `desired/published_light_rev`, `desired/published_coverage_gen`, `overlay_repair_peer_coverage_gen[6]`, `face_debt_mask`. `DemandCoverageGenToPublish` — `ChunkRenderDemand.h:336-345`.
- Имена stage ремонтов: `src/World/Diagnostics/JobStageTrace.cpp:1615` (`priority_chunk_emerge_repair`), `:1619` (`priority_relit_install_repair`), `:1627` (`priority_light_dependency_repair`).
- Overlay repair target light rev: `src/World/Streaming/ChunkEmergeCoordinator.cpp:1293-1389`.
- Repair admission / caps: `ChunkEmergeCoordinator.cpp:5916-5917` (`repair_adm.max_schedule`), `:6141-6216` (`bounded.max_schedule`, `available_schedule_cap`).

### Frontier-задержка (PERF-0.31)

- Готовые диагностические крюки (PERF-0.11): `first_mesh_frontier_trace`, `focus_terrain_incomplete_trace`, bounded ring `sample_kind=20` (256 записей), CPU-only режим `CUBA_VISUAL_BLACK_TRACE_CPU_ONLY=1` — отключает framebuffer probes, сохраняя CPU trace.
- Известные числа PERF-0.11: dirty age median 26 кадров / p95 64 / max 94; `frontier_scheduled_this_tick` median 6; насыщение `max_schedule` в 232 выборках, `first_mesh_cap` в 57; capture reserve иногда исчерпан; TerrainIncomplete — в основном внешнее trailing ring h=5.
- Релейный soft cap relight FIFO по `miss_end_or_stuck`: `src/World/Streaming/RelightFifoPolicy.h:1630-1632`.
- Приоритеты near-FOV: `src/World/Streaming/NearFovWorkPriority.h`.

### Замена mesh при редактировании (PERF-0.32)

- Free-first обработка при CPU replace: `src/Render/Mesh/ChunkMeshCache.cpp:8540-8600` и `:12143` — `CpuReplaceFreeFirstWouldHole(had_gpu_drawable, new_cpu_drawable)` (регресс-счётчик `MeshReplaceHoleAvoided`), grace hold `HasWitnessSwapGraceAt`, затем `GpuPipeline->FreeChunk`.
- Intentional empty residency (0-quad, `GpuResident=true, GpuSlotIndex=-1, GpuQuadCount=0`): `ChunkMeshCache.cpp:8541-8551, 8600+` — «match Immediate / GPU 0-quad ready (HasMeshSatisfying)».
- Атомарные boundary-швы (уже сделано, b7939d858): `fix(mesh): publish boundary edit seams atomically`.
- PERF-0.28 edge: `ShouldNotifyFirstDrawableCoverage` — `src/Render/Mesh/MeshNeighborPolicy.h:60-65`; вызовы `ChunkMeshCache.cpp:6101-6107` (`CommitGpuMeshResult`) и `:8613-8619` (`ApplyMeshResult`).

### Тесты

- `src/Test/MissFirstMeshClassTest.cpp` — классы miss/urgency/admission (`IsMissFirstMeshClass`, `IsNearFocusMissUrgent`, `ComputeMeshWorkAdmission`, soft-defer empty).
- `src/Test/ChunkRenderDemandTest.cpp` — `StopConverged`: `:249-279` (stop после publish/face clear), `:494-514` (N1 drain, infinite Retain).
- `src/Test/MeshNeighborPolicyTest.cpp` — покрыто PERF-0.28 (first drawable coverage edge).
- Профильные demand/relight тесты — контрольный набор из базового плана.

## 3. PERF-0.29 — классифицировать оставшиеся missing_mesh

**Цель:** разложить 27 resident missing meshes по классам: реальные незамешированные resident solid slices / намеренные zero-quad meshes, ошибочно попадающие в класс MissingMesh / устаревшая классификация census. Устранить `miss_end=1` или доказать его корректность.

### 3.1 Witness (до изменений)

1. Release-сборка: `cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8`.
2. Эталонный полёт (`World_223`, cruise `(-550,110)`, 83 с, `--metrics`, без trace) — подтвердить воспроизводимость `missing_mesh=27`, `miss_end=1`.
3. Trace-запуск того же маршрута с `focus_slice_trace` (см. правила базового плана) на финальном miss witness: снять focus slice, demand masks, CPU/GPU quad counts, owner flags, overlay masks для witness-координаты (`miss_cx/cy/cz`, `miss_witness_age_frames` — `FramePerfMonitor.cpp:3328-3372, 2943-2950`).
4. Построить таблицу всех 27 колонок census: класс `ClassifyFocusColumnVisual`, `GetColumnRenderableState.reason`, `draw_ok`, `IsTerrainColumnCompleteFast`, `face_debt_mask`, residency чанков (`GpuResident`, `GpuQuadCount`, intentional empty), horiz от focus. Дополнительно — возраст witness (`miss_witness_age_frames`, `miss_stuck_run_frames`).

### 3.2 Гипотезы и проверки

- **H1 — zero-quad residency считается missing.** Intentional empty публикуется как `GpuResident=true, GpuQuadCount=0` (`ChunkMeshCache.cpp:8541-8551`) и по комментарию должен удовлетворять `HasMeshSatisfying`. Проверить цепочку `GetColumnRenderableState` (`World.cpp:3662, 3840-3858`): считывает ли `saw_missing_solid` residency через тот же предикат. Если partial-колонка с полностью заменёнными zero-quad resident slices всё ещё даёт reason=MissingMesh — это неверный переход состояния, конкретная точка фикса.
- **H2 — реальные незамешированные resident slices.** Если witness-срезы имеют desired geom rev > published и живой attempt — это честный FirstMesh долг; тогда вопрос уходит в PERF-0.31 (почему не продвигается после остановки) и в PERF-0.30 (почему не сходится demand). В этом случае `miss_end=1` может быть корректен — но тогда он должен сопровождаться `demand_stop_converged=false` по конкретной записи, а не висеть при `dirty=0` и пустых очередях.
- **H3 — stale census / witness.** Кэширование census: `facing_sample_cd` (`WorldStreaming.cpp:3090-3126`, каденция 4–24 кадра), `UnfinishedVisualCache` (`World.cpp:11892-11920`), probe memo (`missing_near_probe_memo_hit`). Проверить, обновляется ли census после последнего изменения состояния колонки; не держится ли `FocusMissingMesh=1` по memo при уже исчезнувшем долге. Проверить lifecycle witness-пина: снимается ли `ClearSatisfiedScreenRayRepairPin`/`miss_witness_retarget` для устаревшей координаты.
- **H4 — fog latch.** `FogLatchFocusMissingMesh` (`WorldStreaming.cpp:6255, 6733`): не держит ли latch старое состояние после фактической готовности.

### 3.3 Изменения

- Только конкретный неверный переход состояния/владельца (по результатам 3.2). Не маскировать счётчик, не ослаблять census, не менять политику по одной гипотезе.
- Ожидаемые точки фикса: предикат residency в `GetColumnRenderableState`; сброс witness при ретаргете; инвалидиация `UnfinishedVisualCache`/`facing_sample_cd` при публикации последнего missing slice колонки.

### 3.4 Тесты и приёмка

- Юнит: расширить `MissFirstMeshClassTest.cpp` (или добавить `FocusColumnVisualClassTest`) на класс найденной ошибки (например: partial-колонка, все resident slices intentional empty → не MissingMesh).
- Критерий базового плана: `miss_end=0` (и `miss_end_stop=0`, gate `flight_sim_phase_gate.py:362-363`) либо документированное доказательство корректности оставшегося сигнала; `missing_mesh` не растёт; `face_debt=0`, `visual_holes=0` сохраняются.
- Отдельно проверить, что убрать источник `miss_end_or_stuck` (`RelightFifoPolicy.h:1630`) не меняет поведение relight FIFO в худшую сторону (сравнение trace-запусков).

## 4. PERF-0.30 — добиться сходимости после остановки

**Цель:** `demand_stop_converged=true` без постоянного фонового churn.

### 4.1 Witness

1. Отдельный trace-запуск эталонного маршрута (полёт без trace остаётся эталоном; churn мерить только trace-vs-trace).
2. Из JSONL извлечь по `JobStage` (`JobStageTrace.cpp:1615-1627`): счётчики `priority_chunk_emerge_repair`, `priority_relit_install_repair`, `priority_light_dependency_repair` по координатам; найти повторные ремонты на неизменных входах (одинаковые incarnation/content rev/light rev/coverage gen).
3. На финальном stop-сегменте вывести дамп неудовлетворённых записей `UChunkRenderDemandStore` (все 5 причин `StopConverged`, `ChunkRenderDemand.cpp:1045-1104`): какая запись и какая причина держит `false`. Диагностический дамп — opt-in, только для trace-прогонов.
4. Измерить `ColumnEmergeDemandStopMs` (`World.cpp:3089`) — время до сходимости/нетодходимости.

### 4.2 Гипотезы

- **H1 — orphan attempt.** Записи с `has_active_attempt && active_stage==Created && last_progress_ms<=0` никогда не прогрессируют (attempt создан, но job не назначен или назначен и отменён). Классифицировать источник (кто ставит attempt) и устранить создание осиротевших attempt.
- **H2 — coverage без публикации.** `desired_coverage_gen > published_coverage_gen` (`DemandCoverageGenToPublish`, `ChunkRenderDemand.h:336-345`): peer coverage, которая не публикуется, потому что соседи — intentional zero-quad и после PERF-0.28 больше не нотифицируют FirstDrawableCoverage. Если так — extension notification не нужен, но пересмотр `CoverageSatisfied` для этого класса (zero-quad peer считает coverage исполненным) может быть корректным точечным фиксом. Внимание: не связывать coverage с geometry revision (правило 7 базового плана).
- **H3 — infinite Retain.** `retained_awaiting_successor` при желании поднять rev, но без живого attempt (покрыто тестами `ChunkRenderDemandTest.cpp:494-514`) — проверить, не возникает ли на практике желание без attempt из-за caps (`repair_adm.max_schedule`, `ChunkEmergeCoordinator.cpp:5916, 6141-6216`).
- **H4 — stall-пограничное.** In-flight с `last_progress_ms` близко к `kStallFailMs`: ремонт продвигается микрошагами и не даёт стоп converge; найти, какие stage пишут прогресс без реального продвижения.

### 4.3 Изменения

- Расширять дедупликацию только по доказанному input identity: incarnation + content/light revisions + coverage generation (поля уже есть в `ChunkRenderDemand.h:85-121`). Ключ не должен включать `peer_geom_rev` (регрессия ранее) и не должен запрещать repair revisions глобально.
- Точечные фиксы по результатам witness (осиротевшие attempt, coverage для zero-quad peer, stall-прогресс).
- Каждое изменение — отдельная ветка, отдельный witness «было/стало» по счётчикам повторных ремонтов.

### 4.4 Приёмка

- `demand_stop_converged=true` в эталонном полёте после остановки; `dirty`, `missing_mesh`, `face_debt`, `miss_end` не растут относительно базы раздела 1.
- Счётчики повторных ремонтов на неизменных входах не растут между сопоставимыми trace-запусками.
- `ChunkRenderDemandTest.cpp` расширен кейсом найденной причины; профильные demand/relight тесты зелёные.

## 5. PERF-0.31 — визуальное отставание за пределами кольца

**Цель:** сократить время от входа колонки в визуальное кольцо до её видимой публикации. Выполнять после PERF-0.29/0.30 (их результаты меняют нагрузку frontier) либо параллельно, если witness 0.29 не требует production-фикса.

### 5.1 Witness (переиспользование PERF-0.11)

1. Trace-запуск с `CUBA_VISUAL_BLACK_TRACE=1` + `CUBA_VISUAL_BLACK_TRACE_CPU_ONLY=1` (убирает framebuffer probes, сохраняет CPU trace).
2. По `first_mesh_frontier_trace` и `focus_terrain_incomplete_trace` (`sample_kind=20`) построить для конкретных XZ-колонок четыре сегмента задержки от входа в кольцо: terrain load → light settle → mesh capture/build → GPU publish. Данные уже частично есть: `column_emerge_demand_stop_ms`, `miss_reserved_ms`, `world_streaming_phase_ms`, `mesh_emerge_ms`, `scene_ms` (PERF-0.11).
3. Проверить бюджеты: где насыщение — `max_schedule` (232 выборки в PERF-0.11), `first_mesh_cap` (57), capture reserve; сопоставить с dirty age (median 26 / p95 64 / max 94).
4. Проверить голодание: доли бюджета у frontier-колонок vs дальние repair/fifo задачи — распределение scheduled по horiz, влияние `repair_adm.max_schedule` (`ChunkEmergeCoordinator.cpp:5916`) и relight FIFO soft cap (`RelightFifoPolicy.h:1630`).
5. Отдельно классифицировать TerrainIncomplete по дистанции/FOV (PERF-0.11: в основном trailing edge h=5) — не смешивать с near-FOV долгом.

### 5.2 Изменения (только после доказанного bottleneck)

- Кандидаты в порядке предпочтения: выделение гарантированной доли schedule для near-FOV FirstMesh (burst/diet уже есть — `GetEnterGameMeshBurstFrames`, rim cadence), приоритизация по расстоянию/направлению взгляда (`NearFovWorkPriority.h`), изоляция дальних repair от frontier очереди.
- Не поднимать caps вслепую: PERF-0.11 показал, что простое увеличение cap перераспределяет main-thread/GPU нагрузку. Любая правка — с A/B по эталонному полёту.

### 5.3 Приёмка

- Меньше время до визуальной готовности frontier (сегменты из 5.1) при сопоставимом FPS (база fly FPS ~18.3 в диагностическом CPU-only прогоне; эталон без trace — отдельно зафиксировать до изменений).
- Без роста хвостов после остановки: `face_debt`, `missing_mesh`, `miss_end`, `stop dirty` не хуже базы раздела 1.

## 6. PERF-0.32 — кратковременные дыры при удалении блока

**Цель:** убрать кадр, где старый mesh скрыт/освобождён, а заменяющий ещё не опубликован.

### 6.1 Witness

1. Трассировка цепочки edit → dirty → capture → build → GPU replace на одном блоке (trace-запуск с edit-сценарием; stage-тайминги `JobStageTrace`).
2. Зафиксировать witness свободного кадра: `CpuReplaceFreeFirstWouldHole`-ветка (`ChunkMeshCache.cpp:8562, 12143`) срабатывает для GPU-only drawable колонок; для обычного edit-пути (старый drawable заменяется новым drawable) проверить, есть ли окно между `GpuPipeline->FreeChunk` и публикацией successor — счётчики `FreeChunkLiveN`, `MeshReplaceHoleAvoided`, `ForceFlatRebuildNext`.
3. Визуальный edit-burst сценарий (ручной + запись) как база сравнения.

### 6.2 Изменения

- Атомарная замена drawable representation: не освобождать/скрывать прежний GPU-образ до готовности подтверждённого successor — перенос keep-until-bind/grace (`HasWitnessSwapGraceAt`, intentional-empty residency) с частных веток на общий путь replace; где невозможно (нехватка GPU слота) — публиковать successor до free (double-buffer слот).
- Расширить механизм grace hold с witness-условий (`kI18WitnessComfortEnabled`, underfeet grace) на edit-путь либо ввести явный pending-successor state в `chunkMesh`.
- Регрессионный тест последовательности публикации: старый drawable остаётся bound до подтверждения successor (юнит на уровне `ChunkMeshCache`/`MeshApplyPolicy`).

### 6.3 Приёмка

- Нет free-first hole в edit-burst сценарии (визуально и по счётчикам), нет застрявших pending edits (очередь drain после bursts).
- Регресс: `MeshReplaceHoleAvoided` не растёт на эталонном полёте; `face_debt`/`missing_mesh`/`miss_end` не хуже базы.

## 7. PERF-0.33 — стоимость сборки и диагностики

- Зафиксировать базовое время чистой и инкрементальной Release-сборки по модулям (модульный сплит уже выполнен: `7ed6c1b64 build: split MSVC desktop into object modules`, `131b21378 merge: incremental MSVC Release build modules`). Замеры повторять после каждого этапа серии.
- Проверять повреждённые `.obj` после принудительного завершения компиляции; удалять только конкретный повреждённый объект и повторять сборку (без full clean).
- Не расширять модульность без измеренной выгоды; контролировать размер Unity/groups и параллелизм (`--parallel 8`) под доступную память.
- Диагностические анализаторы держать вне production runtime (существующая практика: opt-in env-переменные, `sample_kind` ring, CPU-only trace); trace — только для диагностических прогонов.

## 8. Порядок выполнения и зависимости

1. **PERF-0.29** (классификация missing) — первый: его результат определяет, является ли `miss_end=1` дефектом или честным долгом, и снимает/подтверждает часть причин несходимости 0.30.
2. **PERF-0.30** (сходимость) — после 0.29; witness 0.30 может частично переиспользовать таблицу колонок 0.29.
3. **PERF-0.31** (frontier) — после 0.30 (или параллельно, если 0.29 не потребовал production-фикса).
4. **PERF-0.32** (edit hole) — независимо; можно вставить в любой момент, требует отдельного edit-burst стенда.
5. **PERF-0.33** (сборка) — фоновый, замеры параллельно с этапами.

Каждый этап: отдельная ветка от актуального `develop` → witness до изменений → Release-сборка → эталонный полёт → юнит-тесты → commit → `git merge --no-ff` в `develop`.

## 9. Контрольный набор перед завершением серии

- `mesh_neighbor_policy_test`, `miss_first_mesh_class_test` и профильные demand/relight тесты — зелёные.
- Эталонный cruise/stop полёт `World_223` без trace: не хуже базы раздела 1 по `face_debt`, `missing_mesh`, `miss_end`, `stop dirty`; цель — `miss_end=0` и `demand_stop_converged=true`.
- Полёт с trace — только при необходимости анализа причин.
- Ручной визуальный контроль (`tools/flight_sim_run.py` + `bin/Cubatarium.exe`): загрузка мира, выход за кольцо, остановка, удаление блока; отсутствие черноты, мерцания и дыр.

## 10. Запрещённые подходы (из базового плана, неизменно)

- Глобальный запрет repair revisions — регрессия, не применять.
- Возврат overlay repair key на `peer_geom_rev` — регрессия, не применять.
- Связывание coverage generation с geometry revision — не применять.
- Маскировка счётчиков (`missing_mesh`, `miss_end`) вместо исправления переходов состояния.
- Увеличение FirstMesh cap / snapshot reserve / relight FIFO до доказанного bottleneck (PERF-0.11).
