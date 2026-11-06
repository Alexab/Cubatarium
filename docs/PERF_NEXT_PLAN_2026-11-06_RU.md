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

## PERF-0.31 — визуальное отставание за пределами кольца

Цель: сократить задержку появления мира после быстрого перелёта и загрузки.

- Отдельно измерить задержки terrain load, light settle, mesh capture/build и GPU publish относительно входа колонки в визуальное кольцо.
- Проверить бюджеты FirstMesh и relight на frontier; исключить голодание ближних видимых колонок из-за дальних repair/fifo задач.
- Настраивать приоритет по расстоянию и направлению взгляда только после доказанного bottleneck.
- Критерий: меньше время до визуальной готовности frontier при сопоставимом FPS и без роста хвостов после остановки.

## PERF-0.32 — кратковременные дыры при удалении блока

Цель: устранить кадр, в котором старый mesh уже скрыт, а заменяющий ещё не опубликован.

- Трассировать edit → dirty → capture → build → GPU replace на одном блоке.
- Обеспечить атомарную замену drawable representation: не освобождать/скрывать прежний образ до готовности подтверждённого successor.
- Добавить регрессионный тест последовательности публикации и визуальный edit-burst сценарий.
- Критерий: нет free-first hole и нет застрявших pending edits.

## PERF-0.33 — стоимость сборки и диагностики

- Зафиксировать базовое время чистой и инкрементальной Release-сборки по модулям.
- Проверять повреждённые `.obj` после принудительного завершения компиляции; удалять только конкретный повреждённый объект и повторять сборку.
- Не расширять модульность без измеренной выгоды; контролировать размер Unity/groups и параллелизм под доступную память.
- Хранить диагностические анализаторы отдельно от production runtime; trace использовать только для диагностических прогонов.

## Контрольный набор перед завершением серии

- `mesh_neighbor_policy_test`, `miss_first_mesh_class_test` и профильные demand/relight тесты.
- Эталонный cruise/stop полёт без trace.
- Полёт с trace только при необходимости анализа причин.
- Ручной визуальный контроль: загрузка мира, выход за кольцо, остановка, удаление блока, отсутствие черноты/мерцания и дыр.
