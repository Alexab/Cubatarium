# План дальнейших работ по производительности и хвостам загрузки

Дата: 2026-11-06. База: `develop` после PERF-0.28.

## Зафиксированный результат

PERF-0.24—0.26 устранили часть повторных ремонтов, согласовали остановку peer с визуальным кольцом и исправили потерянную перезагрузку частично удалённых колонок. На эталонном маршруте после остановки в PERF-0.26 достигнуты `face_debt=0`, `dirty=0`, `ready=63`, `visual_holes=0`, но остаются `missing_mesh=27`, `miss_end=1` и `demand_stop_converged=false`.

PERF-0.28 исправляет ложное событие `FirstDrawableCoverage` при повторной публикации намеренно пустого zero-quad mesh. Теперь событие возникает только при реальном переходе к drawable mesh. Это разрывает доказанный цикл `FaceDebtCallback ↔ PriorityChunkEmergeRepair`. Юнит-тест пройден. Полёт не показал регрессии ключевых показателей относительно PERF-0.26: `face_debt=0`, `dirty=0`, `ready=63`, `missing_mesh=27`, `miss_end=1`. Проблема хвостов полностью не решена.

## Правила выполнения

1. Каждый следующий этап выполнять в отдельной ветке от актуального `develop`.
2. Перед изменением воспроизвести дефект и записать witness/метрики; не менять политику по одной гипотезе.
3. Собирать только Release: `cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8`.
4. Проверять мир и визуал через `tools/flight_sim_run.py` и `bin/Cubatarium.exe`.
5. Основной маршрут: `World_223`, cruise `(-550,110)`, полёт с остановкой, 83 секунды, `--metrics`, без trace для сопоставимых метрик.
6. Этап принимать только при отсутствии роста `face_debt`, `missing_mesh`, `miss_end` и stop dirty. После успеха — commit и `git merge --no-ff` в `develop`.
7. Не использовать глобальный запрет repair revisions и не возвращать overlay repair key на `peer_geom_rev`: оба подхода ранее давали регрессии.

## PERF-0.29 — классифицировать оставшиеся missing_mesh

Цель: разделить 27 resident missing meshes на настоящие визуальные дыры, намеренно скрытые zero-quad meshes и устаревшую классификацию готовности.

- Для финального miss witness снять `focus_slice_trace`, demand masks, CPU/GPU quad counts, owner flags и overlay masks.
- Проверить, почему `miss_end=1` сохраняется при `visual_holes=0`, `face_debt=0`, `dirty=0` и отсутствии активных GPU/CPU работ.
- Исправить только конкретный неверный переход состояния или владельца; не маскировать счётчик.
- Критерий: `miss_end=0` либо доказанное корректное обоснование оставшегося сигнала без роста реальных дыр.

### Результат (2026-11-06, ветка `feature/perf-0-29-missing-mesh-classification`)

Классификация witness-колонки `(-568,4,108)` (horiz=2): resident solid slice (17 блоков), `mesh_revision=0` — реальные незамешированные срезы FirstMesh, не misclassification/zero-quad/stale census. Блокировка: `pending_light` (mesh ждёт света), relight-тикет в persistence priority FIFO. Цепочка дефектов найдена и исправлена двумя фиксами:

1. `RelightChunkCoordsOnGrid` заливал свет отдельно на каждый чанк снапшота (O(chunks×grid) на задачу; full-height band −64:319 = до 24 фладов по grid). Заменено на один union-флад (тот же max-plus fixed point); регресс-тест точного затухания через два шва — `chunk_lighting_propagation_test` кейс E.
2. Ранний frame-budget `return` streaming tick обслуживал capture-дрен только при `PendingLightFocus>0 || UnderfeetPendingLight!=0`; rim-колонки визуального кольца вне радиусов этих проб (27 census MissingMesh при PendingLightFocus=0) теряли единственного потребителя захвата на весь stop. Добавлен сервис capture при `FocusVisualMissingMeshN>0` (1 колонка/кадр, bounded).

Диагностика: opt-in `CUBATARIUM_RELIGHT_WATCHDOG=1` (счётчики конвейера раз в 5 с, не возмущает тайминги).

Эталон после фиксов (`acceptance_run2`): `face_debt=0`, `missing_mesh=27` (без роста; ready 63→64, fifo 30→дренаж, stop=8), `miss_end=0`, stop dirty=0, `demand_stop_converged=false` (перенесено в PERF-0.30). Побочно: wall fly median 115→46 мс, stop wall median 98→28 мс, effective FPS fly 8.7→21.7. Остаточный хвост: 20 чанков с orphaned pending-light маркерами при пустых очередях релайта (поверхностный clamp Y-band против pending-light band) — вход в PERF-0.30.

## PERF-0.30 — добиться сходимости после остановки

Цель: `demand_stop_converged=true` без постоянного фонового churn.

- Измерить revision churn после PERF-0.28 отдельным trace-запуском; сравнивать числа ревизий только между trace-запусками.
- Найти оставшиеся повторные `priority_light_dependency_repair`, `priority_relit_install_repair` и `priority_chunk_emerge_repair` на неизменных входах.
- Расширять дедупликацию по доказанному input identity: incarnation, content/light revisions, coverage generation. Не связывать coverage с geometry revision.
- Критерий: очереди и desired/published revisions стабилизируются, dirty и missing не растут.

### Результат (2026-11-06, ветка `feature/perf-0-30-stop-convergence`, в работе)

Witness1 (`bin/suite_reports/perf_0_30/witness1`, `CUBATARIUM_RELIGHT_WATCHDOG=1`): persistence FIFO дренирован (`pcols=0`), но телеметрия `RelightFifoN` замерла на 19 — ранний frame-budget return не обновляет post-emerge sample. `ShouldEnqueueLatchSideRelight` читает устаревшее 19 → backpressure(≥16) → `OnLitPendingNeeded` не энкуит `RelightThenMesh` → 20 чанков с orphaned pending-light маркерами, 27 census MissingMesh.

Фикс: обновление `RelightFifoN`/`PendingLightN` на пути раннего выхода (`WorldStreaming.cpp`). Приёмка `acceptance1`: `pending_light_n` 20→0, missing 27→26, `face_debt=0`, `miss_end=0`, stop dirty=0 (критерий «без роста» выполнен), FPS без регрессии (21.7→22.0).

Оставшийся слой (следующий шаг этапа): 26 колонок census MissingMesh при `pending_light_n=0` и полностью опустошённых очередях релайта; 93 dirty чанка revisited (`dirty_revisit_same_n=39`) с ~1 schedule/кадр без завершений; demand store: `demand_unsat_coverage=370`, `demand_unsat_face=375`, `demand_unsat_geom=91`, `demand_unsat_light=9`. Гипотеза: mesh-build для rim-срезов отклоняется гейтом settled-light/литости при остановке, а demand records накапливают coverage/face долги без повторных попыток. Инструмент: `focus_slice_trace` на финальном witness + watchdog.

### Результат продолжения (2026-11-06, ветка `feature/perf-0-30-convergence-mesh-gate`)

Trace-witness идентифицировал класс: missing-срезы — **интроерные полностью заполненные чанки** (`non_air_blocks=4096`), published intentional 0-quad mesh (`mesh_rev=2`, GPU-resident, 0 quads), `has_settled_light=1`, `desired==published` по geom и coverage, но `face_debt_mask=1` против **незагруженного peer'а** за границей кольца. Два потребителя читали паркованный долг как «missing»/«не сошлось»:

1. `HasMeshSatisfyingColumnReady` включал `face_debt_mask != 0` в coverage-проверку → срез вечно unsatisfied → census MissingMesh.
2. `StopConverged` отвергал любую запись с `face_debt_mask != 0`.

Фикс: face debt блокирует удовлетворённость/сходимость только при непубликованном demand (`desired geom/coverage > published`); паркованный долг обслуживается при загрузке peer'а. Регресс-тест обоих случаев в `chunk_render_demand_test` (паркованный — сходится; непубликованный — блокирует).

Приёмка `acceptance2`: критерий «без роста» выполнен (`face_debt=0`, `miss_end=0`, stop dirty=0, missing 26–27 в пределах разбега прогонов); FPS 22.0→26.0, wall fly median 45.5→38.5 мс. `demand_stop_converged` остался false — см. следующий слой.

**Следующий слой этапа (зафиксирован для продолжения):** `demand_unsat_face≈378` — это **живые** (не паркованные) записи интерьерных чанков границы кольца с `desired_coverage_gen > published_coverage_gen`: coverage-требование поднято, peer не загружен — публикации не будет до его загрузки. ~24 колоночки × 16 чанков ≈ 378 — сходится. Требуется решение этапа: либо сервис/ретирмент coverage-требований к отсутствующим peer'ам при остановке, либо скоупинг `StopConverged` на кольцо вовлечённости. По правилам плана — отдельный witness-цикл (`focus_slice_trace` с полями `desired/published_coverage_gen` по всем записям, не только census-класса) перед изменением.

### Результат продолжения 2 (2026-11-07, ветка `feature/perf-0-30-coverage-desire-gate`)

Три дефекта сходимости, найденные сэмплами demand-записей (watchdog `demand_sample`):

1. **Безусловное поднятие coverage.** `OnFaceDebt`/`OnFaceDebtMask` и re-note путь World поднимали `desired_coverage_gen=1` при любом face debt — в том числе против незагруженного peer'а, который не может очистить долг до своей загрузки. Гейт: coverage требуется только когда peer'ы долговых граней загружены.
2. **Осиротевшие demand без владельца допуска.** Отмена сироты (ReconcileMaintenance) и A40-remint оставляли живое желание без Dirty-записи — продюсер никогда не доходил до NoteDemand (witness: des_g росло 23→24 при pub_g=8 и пустых очередях). ReconcileMaintenance теперь возвращает reclaim-координаты, `MaintainChunkRenderDemandStore` пере-dirty их (кулдаун 2 с, скип при `MayMeshColumn=false`).
3. **Скоупинг StopConverged.** Опциональные предикаты `peer_loaded`/`mesh_withheld` выводят из сходимости остановки паркованные coverage-долги (все долговые peer'ы незагружены) и удерживаемые geometry-желания (soft-defer/stand diet); «паркованность» считается до orphan-проверки. Вычисление с предикатами троттлится до 4 Гц (вызов лямбд по ~400 записям каждый кадр стоил видимого main-thread времени).

Диагностика: `SampleUnsatisfiedRecords` (до 6 сэмплов открытых записей в watchdog).

Эталон после фикс-серии (acceptance8): ready 63→71–88 (по прогонам), missing 27→20–27, **FPS 26→51.0, wall fly median 38→19.6 мс**, `face_debt=0`, `miss_end=0`, stop dirty=0 — критерий «без роста» выполнен. `demand_stop_converged` остался false.

**Следующий слой (зафиксирован):** ремонтный цикл — у части записей mesh-ревизия растёт без завершения сборки (witness: (-57,1,0) des_g 9→10 при pub_g=2, debt 0x05 (-X,-Y); peer загружен, но публикация не наступает; reclaim пере-dirty каждые 2 с не завершает цикл). Требуется trace bump-источников ревизии (`MeshRevisionBumpReason`) и причин отбора сборок на этой паре чанков.

### Результат продолжения 3 (2026-11-07, ветка `feature/perf-0-30-repair-cycle`)

Trace `demand_transition_trace` по churn-чанку (-67,4,3) установил источник цикла: **сам reclaim**. Цепочка: reclaim пере-dirty → `MarkDirty` бампает mesh-ревизию (reason `mark_dirty_enqueued`) и продвигает цель спроса без изменения контента и без сборки → attempt создаётся, scheduler (rim diet) его не строит → сирота → grace → reconcile отменяет → reclaim (кулдаун 2 с)… Каденция ~250 кадров, des_g 12→18 при pub_g=3, ноль attempt между бампами.

Фикс: счётчик безрезультатных reclaim'ов на координату (сброс при публикации); после двух — координата перестаёт пере-dirty'ться, а цель demand честно ребейзится к опубликованной ревизии (`RebaseDesiredToPublished`); реальную устарелость независимо ловят stale-light/halo сканы, следующее изменение контента поднимает demand через NoteDemand.

Эталон (acceptance9): **FPS 51.3, wall fly median 19.5 мс**, stop dirty=0, `face_debt`/`miss_end`/missing без роста. `demand_stop_converged` остался false.

**Следующий слой (зафиксирован):** face debt с ЗАГРУЖЕННЫМ peer'ом, ремонт которого не публикуется (census `face_debt_end=1` в части прогонов; demand-сэмплы: des_c=1 pub_c=0 debt=0x01 при загруженном -X peer'е). Требуется trace `EnqueueFaceDebtRepair`/публикаций peer'а на этой паре: почему ремонт не выполняется или его результат не очищает долг (waiting_peer_gen vs публикация).

### Результат продолжения 4 — финал серии (2026-11-07, ветка `feature/perf-0-30-convergence-final`)

**BlockerCensus**: классификатор StopConverged выделен в member-функцию с 9 классами блокеров; watchdog раз в 5 с логирует census с сэмплами координат. Первая атрибуция (census_run2): `orphan=198, stall=28, no_attempt=12, face_revs_behind=6` при замороженной `dirty_n=90` и `mesh_dirty_schedule_skip_outside_focus_n=55/период`.

**Найден главный цикл**: stop-diet удаляет outside-focus drawable remesh из очереди (`DeferRemeshCoord`), demand остаётся открытым → сироты recreated через reclaim. Фикс: `IsRemeshDeferred` экспонирует deferred-ring диета; `mesh_withheld`-предикат сходимости и reclaim-скип считают такие координаты паркованными — demand-магазин и scheduler согласованы.

Итог (acceptance10): missing 27→**23**, ready→**80**, **FPS 49.2, wall fly median 20.3 мс**, `face_debt=0`, `miss_end=0`, stop dirty=0, `dirty_n` стабильна, desired/published стабилизировались.

**Критерий этапа 0.30 из базового плана выполнен** («очереди и desired/published revisions стабилизируются, dirty и missing не растут»). Булевый `demand_stop_converged` остаётся false на хвосте: `orphan=65, no_attempt=45, stall=28` — вытеснения из ограниченного deferred-ring диета теряют работу при открытых записях. Следующий шаг зафиксирован: пере-поставка вытесненных из ring координат (bounded) либо расширение семантики паркинга на eviction; после этого флаг ожидаемо закрывается.

### Результат продолжения 5 (2026-11-07, ветка `feature/perf-0-30-deferred-eviction`)

Утечка deferred-ring подтверждена и устранена: `kRemeshDeferredRingMax=64` был меньше населения остановки (~120–200); requeue берёт с фронта, diet возвращает в хвост — каждый тик ротация безвозвратно теряла координаты за cap'ом при открытых записях (источник no_attempt=45). Ring → 256, размер населения в watchdog (`deferred_ring=`).

Паркинг расширен до устойчивого правила диета `IsOutsideScheduleFocus` (horiz от mesh-фокуса > радиуса): членство в ring «моргает» из-за той же ротации. Эталон: без роста (face_debt=0, miss_end=0, stop dirty=0), missing 23–27, ready 65–80, FPS 42–51.

**Вскрытый слой (следующий witness-цикл):** census теперь показывает ранее-потерянный бэклег как `orphan=175` — записи FirstMesh-очереди с drawable мешем: pass-1 шедулера скипает их безусловно (`HasDrawableGreedyMesh → skip`), не маршрутизируя в remesh-lane; попытки остаются в Created. Требуется разбор маршрутизации lane'ов (FullyDark → FirstMeshQ по «Closeout C» против pass-1 skip) на паре witness-координат.


## PERF-0.31 — визуальное отставание за пределами кольца

Цель: сократить задержку появления мира после быстрого перелёта и загрузки.

- Отдельно измерить задержки terrain load, light settle, mesh capture/build и GPU publish относительно входа колонки в визуальное кольцо.
- Проверить бюджеты FirstMesh и relight на frontier; исключить голодание ближних видимых колонок из-за дальних repair/fifo задач.
- Настраивать приоритет по расстоянию и направлению взгляда только после доказанного bottleneck.
- Критерий: меньше время до визуальной готовности frontier при сопоставимом FPS и без роста хвостов после остановки.

### Результат (2026-11-06, измерение на `perf_0_30/trace_witness`)

Production-изменение не потребовалось: frontier-задержка устранена фиксами PERF-0.29/0.30 как побочный эффект устранения ливелоков.

- `first_mesh_completion_trace` (727 завершений): `latency_ms` median **31**, p95 **82**, max **169**; по фазам полёта early 30/76, mid 26/78, late 37/110 — деградации к хвосту нет. База PERF-0.11: dirty age median 26 кадров (~1.4 с) — улучшение ~45×.
- Насыщение `max_schedule` сохраняется (536/735 frontier-записей, капы 2–6), но `frontier_pipeline_inflight` median 3 из cap 42, `frontier_tick_elapsed_ms` median 4.3 — капBinding по дизайну GPU-защиты (`pending_gpu >= 24 → DeepBacklog cap 2`), а не голодание: обслуженные срезы завершаются за десятки мс.
- Релеевский soft cap relight FIFO и приоритеты `NearFovWorkPriority` не менялись — вмешательство не доказано.

## PERF-0.32 — кратковременные дыры при удалении блока

Цель: устранить кадр, в котором старый mesh уже скрыт, а заменяющий ещё не опубликован.

- Трассировать edit → dirty → capture → build → GPU replace на одном блоке.
- Обеспечить атомарную замену drawable representation: не освобождать/скрывать прежний образ до готовности подтверждённого successor.
- Добавить регрессионный тест последовательности публикации и визуальный edit-burst сценарий.
- Критерий: нет free-first hole и нет застрявших pending edits.

### Результат (2026-11-06, witness `perf_0_32/dig_witness`, сценарий visual-dig на World_223)

Production-изменение не потребовалось: общий путь замены уже keep-until-bind (старый GPU-образ жив до привязки successor; free-first ветка `CpuReplaceFreeFirstWouldHole` остаётся только для GPU-only drawable колонок с недrawable successor и защищена grace-hold).

- 10 периодов с редактированием, 4 блока сломано: `visual_holes=0` во **всех** активных edit-периодах; `mesh_replace_hole_avoided=0` — free-first ветка не срабатывала.
- `edit_to_first_mesh_ms` по блокам: 45.4 / 55.6 / 33.2 / 37.8 — менее 2 кадров при текущем FPS.
- Residual `visual_holes=1` на хвосте (после прекращения копания) и 2 blink-события до начала edits — относятся к слою сходимости PERF-0.30 (задокументирован выше), не к edit-пути.

## PERF-0.33 — стоимость сборки и диагностики

- Зафиксировать базовое время чистой и инкрементальной Release-сборки по модулям.
- Проверять повреждённые `.obj` после принудительного завершения компиляции; удалять только конкретный повреждённый объект и повторять сборку.
- Не расширять модульность без измеренной выгоды; контролировать размер Unity/groups и параллелизм под доступную память.
- Хранить диагностические анализаторы отдельно от production runtime; trace использовать только для диагностических прогонов.

### Результат (2026-11-06, baseline `feature/perf-0-33-build-baseline`)

Замеры Release `--parallel 8` ( develop после PERF-0.30 ):

| Профиль | Время |
|---|---|
| Чистая (`--clean-first`) | **12 м 18 с** |
| Инкрементальная no-op (только link + verify) | **26 с** |
| Touch `src/World/Core/World.cpp` (world_core) | **1 м 02 с** |
| Touch `src/Render/Mesh/ChunkMeshCache.cpp` (render mesh) | **2 м 15 с** |

- Самый тяжёлый TU — `ChunkMeshCache.cpp` (2 м 15 с инкрементальной): кандидат на будущий сплит модуля, но расширение модульности отложено до измеренной выгоды (правило этапа).
- Процедура повреждённых `.obj` — без изменений: удалять только конкретный повреждённый объект.
- Диагностические анализаторы (watchdog, trace, sample rings) остаются opt-in и вне production-путей.

## Контрольный набор перед завершением серии

- `mesh_neighbor_policy_test`, `miss_first_mesh_class_test` и профильные demand/relight тесты.
- Эталонный cruise/stop полёт без trace.
- Полёт с trace только при необходимости анализа причин.
- Ручной визуальный контроль: загрузка мира, выход за кольцо, остановка, удаление блока, отсутствие черноты/мерцания и дыр.

### Статус выполнения (2026-11-07)

- Тесты: `mesh_neighbor_policy_test`, `miss_first_mesh_class_test`, `chunk_render_demand_test` (новые кейсы парковки/сходимости), `chunk_lighting_propagation_test` (новый кейс E — union-флад через два шва), `chunk_bulk_blocklight_test`, `relight_result_install_test`, `mark_relit_characterization_test` — **PASS**.
- Эталонный cruise/stop полёт без trace — выполнен многократно (acceptance2…12); хвосты не растут.
- Визуальные сценарии (автоматический прокси ручного контроля, World_223):
  - `visual-blue`: **0 дыр во всех 11 периодах**, blink=1 (кадр 181), финал ready=107/missing=1;
  - `visual-dig` (удаление блоков): 0 дыр во всех edit-периодах, edit→mesh 33–56 мс;
  - `visual-edge` / `visual-flicker`: единичные транзитные дыры (4/54 и 6/54 периодов) **только при движении** с активным pending_light/fifo — фронт загрузки (frontier); финалы чистые (holes=0, near_holes=0, face_debt=0);
  - чернота/мерцание в устойчивых состояниях отсутствуют.
- Оставшийся ручной шаг: финальный осмотр в окне пользователем (push также выполняет пользователь).
