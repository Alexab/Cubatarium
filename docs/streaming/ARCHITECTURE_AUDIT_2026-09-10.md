# Архитектурный аудит Cubatarium

Дата: 10 сентября 2026. Исследуемый код: `b1badb4f`, ветка `codex/world-streaming-audit-fix`, база `ff5052c1` (`perf_opt19`).

## Резюме

Главная проблема — отсутствие сквозного контракта между актуальностью данных мира, жизненным циклом фоновой задачи, опубликованным mesh и памятью, которую ещё читает GPU. Это несколько независимых классов ошибок, а не один неудачно выбранный streaming budget. Увеличение производительности генератора не исправляет потерянный ticket; готовый mesh не гарантирует корректный draw; корректный свет в чанке не гарантирует обновление света в вершинах.

Найдены 15 групп проблем. Самые срочные: небезопасное повторное использование GPU-диапазонов, общий буфер верхних AABB для независимых render passes и ошибки идентичности/отмены задач в column scheduler. Дополнительно обнаружены пропуск инвалидирования mesh после изменения света, неполные ключи visibility caches, небезопасная отмена capture jobs и оценщик, способный выдать положительный результат без основных логов.

Рекомендация: сначала восстановить корректность и сделать её автоматически проверяемой; затем объединить управление версиями, admission и публикацией; после этого оптимизировать измеренные горячие участки. Переписывание движка на Vulkan, замена всего мира на octree или добавление ещё одного watchdog сейчас не обоснованы.

Подробная последовательность работ и критерии приёмки: [план доработок](E:/Work/Home/Cubatarium/docs/streaming/ARCHITECTURE_REMEDIATION_PLAN_2026-09-10.md).

## Границы и достоверность исследования

Проверены пути streaming → generation/I/O → lighting → capture → meshing → apply → GPU upload → culling → draw, состояние очередей, shutdown-контракты, измерения, CMake и CI. WorldGen, persistence, physics, content и UI рассмотрены на уровне архитектуры и границ с этим конвейером. Это не полный аудит всех алгоритмов физики, формата сохранений, безопасности ресурсов или каждого shader.

Использованы исходный код, история последних исправлений, существующая документация, минимальные исполняемые контрпримеры и первичные публикации авторов движков/библиотек. Отчёт не выдаёт чужие benchmarks за результаты Cubatarium.

Обозначения доказательности:

- **E — воспроизведено:** поведение проверено отдельным запуском на текущих исходниках.
- **C — подтверждено кодом:** показаны конкретные данные, условие и путь нарушения; визуальный эффект на целевом GPU отдельно не воспроизводился.
- **R — архитектурный риск:** структура создаёт риск или накладные расходы; величина влияния требует трассы/нагрузочного теста.

P0 здесь означает «исправить до дальнейшей настройки производительности и принятия релиза», P1 — «следующий обязательный этап», P2 — «структурная доработка после защиты поведения». Это не CVSS и не утверждение об эксплуатации уязвимости.

В этой итерации production-код не менялся. Уже сделанное исправление позиции камеры сохранено в `b1badb4f`. Полная Release-сборка и `miss_first_mesh_class_test` проходили в предыдущей итерации. Сейчас отдельно скомпилирован и запущен scheduler repro и проверен fail-open оценщика. Нового контролируемого GPU-прогона, кадрозахвата и замера прироста FPS нет. Старые ручные/автоматические логи относятся к другим изменениям и режимам; их нельзя считать baseline текущего коммита.

## 1. Как система устроена фактически

Основной стек — C++17, OpenGL, GLFW/GLEW, собственные chunk/job/mesh системы. Есть CPU greedy meshing, GPU/packed-варианты, MDI и vertex pools; desktop и Android/GLES имеют разные возможности. Сама эта комбинация не является устаревшей архитектурой.

| Участок | Фактическая ответственность | Важная граница |
|---|---|---|
| App / world binding | Порядок фаз кадра, ввод, loading/settle, telemetry | Main thread определяет время потребления результатов |
| World streaming / emerge / flow | Интерес к колонкам, readiness, dirty/repair, бюджеты | Несколько представлений одной незавершённой работы |
| WorldGen / I/O | Стадийная генерация и асинхронная доставка чанков | Готовность voxels ещё не означает готовность light/mesh |
| Lighting | Snapshot, worker compute, main-thread merge, remesh notifications | Версии входа и решение об инвалидировании |
| Mesh capture / builder / cache | Копирование voxels и shell, построение и сохранение mesh | Версии центра, соседей и мира должны совпадать |
| Geometry / GPU store | Резидентность, upload, списки команд, cull, draw | CPU-освобождение не равно завершению GPU-чтения |
| Diagnostics / harness / CI | Счётчики, ручной и scripted маршрут, policy tests | Измерение не должно менять workload или пропускать отсутствующие данные |

Генерация и mesh compute действительно вынесены в workers. Однако захват данных и установка результата остаются на main thread, а `MeshCaptureWorker` не выполняет тяжёлую часть capture. Поэтому наличие фоновых потоков само по себе не доказывает малый serial critical path.

Есть полезные основы, которые следует сохранить: facade `UWorldMeshService`, backend-интерфейсы, snapshots, mesh epoch/job-id checks, bounded completed queues с восстановлением dropped work, readiness policies, сохранение старой геометрии до замены, стадийный WorldGen, тесты физики и генерации. Проблема в неодинаковом соблюдении контрактов на соседних границах, а не в полном отсутствии архитектуры.

## 2. Что означает сравнение с индустрией и SOTA

Сравнение разделено на три уровня, чтобы не путать обязательную корректность с экспериментальной оптимизацией.

| Уровень | Проверяемый принцип | Состояние Cubatarium |
|---|---|---|
| Базовая корректность | GPU retirement после последнего чтения, уникальные tickets, полное invalidation | Есть конкретные нарушения A01–A07 |
| Зрелая production-архитектура | Явные resource dependencies, versioned publish, backpressure, приоритет исполнения, автоматические integration gates | Реализовано частично, разрозненными механизмами |
| Условные оптимизации | Binary greedy, packed quads/vertex pulling, дальний LOD/HLOD, GPU-driven visibility | Часть уже есть; необходимость остального определяется профилем, а не названием технологии |

У NVIDIA NVRHI lifetime ресурсов привязан к завершению выполняющих их command lists, а не к окончанию CPU-функции загрузки [S03]. Filament описывает зависимости render passes через явно объявленные чтения и записи ресурсов [S04]. Voxel Tools использует общий приоритетный пул, где близкий mesh может опережать дальнюю генерацию [S10]. Это подходящие ориентиры; копирование их API или переход на их движок не требуется.

World Partition у Epic отдельно задаёт streaming source, целевое состояние и приоритет ячейки, включая предварительную загрузку места телепортации [S18]. Это полезная модель **интереса**, но не готовое решение для изменяемых voxel/light halos. Старый Starlight изучен как инженерный разбор освещения: проект архивирован, а автор помечает старые сравнения с Vanilla как неприменимые к новым версиям. Его численные ускорения не используются как прогноз для Cubatarium [S08, S09].

## 3. Реестр проблем

| ID | Приоритет / свидетельство | Проблема | Возможное проявление |
|---|---|---|---|
| A01 | P0 / C | Повторное использование GPU-памяти до завершения чтения | Мигание, повреждённая геометрия, нестабильные stalls |
| A02 | P0 / C | Верхние AABB и часть истории cull общие для passes | Неверное исчезновение opaque/transparent batches |
| A03 | P1 / C | Неполные ключи кэшей culling | Дыры при движении/повороте/смене проекции |
| A04 | P1 / C | Пересечение visible sets подменяет полноту residency | Новый видимый chunk не попадает в draw |
| A05 | P0 / E | Коллизии координат, ABA-отмена, потеря urgency | Потерянная работа, starvation, «занято без задачи» |
| A06 | P1 / C | В CPU-ветке merge света не выставлен changed | Свет обновился, mesh остался старым |
| A07 | P1 / C,R | Неполная проверка зависимостей light/mesh | Устаревший свет, пограничный remesh churn |
| A08 | P1 / C,R | Capture worker только пересылает готовый snapshot | Serial copies, ожидания и лишняя стадия |
| A09 | P1 / C,R | Неполная отмена capture и lifetime workers | Поздний commit, ABA мира, риск use-after-free |
| A10 | P1 / R | Несколько владельцев readiness и repair debt | Повторные repair loops и сложность доказать прогресс |
| A11 | P1 / C,R | Нет сквозного admission по ресурсам и deadline | Просадки кадра, накопление/сброс готовой работы |
| A12 | P1 / C | CPU submit назван GPU time; readback зависит от HUD | Неверная локализация bottleneck, непохожие прогоны |
| A13 | P1 / E,C | Scorecard допускает неполные данные и раздельные verdicts | Ложный зелёный результат |
| A14 | P1 / C | Policy tests не обеспечивают GPU/lifecycle regression gates | Повторение ошибок после следующих коммитов |
| A15 | P2 / C,R | Facades не ограничивают фактическую связанность | Высокая стоимость и риск каждого изменения |

### A01. GPU vertex pool не защищает время жизни диапазонов

**Доказательство.** [GreedyVertexPool.cpp:152](E:/Work/Home/Cubatarium/src/Render/Engine/GreedyVertexPool.cpp:152) немедленно возвращает диапазон в free list. `Allocate` пишет через `GL_MAP_UNSYNCHRONIZED_BIT` (строки 215, 251). `Reserve` сбрасывает bump offsets даже если существующей ёмкости достаточно и новое хранилище не выделяется (строка 109). В [GreedyGpuBackend.cpp:577](E:/Work/Home/Cubatarium/src/Render/Engine/GreedyGpuBackend.cpp:577) старый dirty batch освобождается перед загрузкой замены. Значит, запись может пересечь ещё выполняющийся draw предыдущего кадра.

Опциональный `CUBATARIUM_POOL_SYNC=1` не доказывает безопасность: `WaitUploadFence` игнорирует `GL_TIMEOUT_EXPIRED`/`GL_WAIT_FAILED`, удаляя fence; `SignalUploadComplete` вызывается в конце refresh, **до последующего draw**. Ограничение количества unsync uploads также не проверяет отсутствие GPU-reader. Khronos прямо определяет результат перекрытия unsynchronized map с незавершёнными операциями как undefined [S01]; timeout не означает сигнал завершения [S02].

**Принятый подход и применение.** Fence-retired allocation по аналогии с lifetime tracking NVRHI [S03]: новая геометрия получает новый диапазон, затем публикуется, старый уходит в retired list с fence после последнего использующего его draw. Возврат в free list — только после успешного nonblocking poll. При недостатке места — defer upload, контролируемое расширение/новое хранилище или безопасный синхронный fallback. Persistent mapping может уменьшить overhead, но не отменяет этот контракт.

**Проверка.** Tiny pool, многократный remesh одного чанка, искусственно задержанный fence, рост VBO/EBO, ошибочный/несигнальный fence, memory pressure. Ни одна запись не должна пересекать live/retired диапазон. У growth уже есть full-rewrite fallback в backend — нельзя удалять его без эквивалентного сохранения всех живых allocations. Вклад в наблюдаемый FPS/мигание пока не измерен.

### A02. Pass-local AABB хранятся в store-global буфере

**Доказательство.** [MdiVertexPoolStore.h:102](E:/Work/Home/Cubatarium/src/Render/Engine/MdiVertexPoolStore.h:102) содержит единственный `CullAabbMaxSsbo`. Нижние границы находятся в `cache.BatchSphereSsbo`, то есть принадлежат конкретному `GreedyGpuPassCache`. [RebuildIndirectCmdTable:523](E:/Work/Home/Cubatarium/src/Render/Engine/MdiVertexPoolStore.cpp:523) загружает верхние границы в общий buffer, а dispatch на строке 862 связывает именно его. `GeometryEngine::MeshStore()` возвращает один Store для обоих passes; transparent refresh использует его на строке 2323. Default desktop cull mode — AABB.

Контрсценарий: opaque загружает min/max своей последовательности batches; transparent обновляет общий max; следующий opaque dispatch без geometry refresh читает opaque-min вместе с transparent-max. Ёмкость буфера не исправляет несовпадение порядка и содержимого. Общие `LastGoodCullOpaqueOn_` и probe counters дополнительно смешивают историю passes.

**Практика.** Явные read/write dependencies Filament помогают обнаруживать такое ошибочное aliasing [S04]. Здесь сначала достаточно перенести persistent max/history в pass cache и ввести `passId + batchTableRevision` для всех составляющих cull input. Общий scratch допустим только если заново заполнен перед каждым читателем; это другой контракт, его нельзя смешивать с persistent cache.

**Проверка.** Маленькая GL-сцена с разными opaque/transparent AABB и количеством batches; менять только один pass; сверять GPU flags с CPU reference. Порядок passes не должен менять их независимые результаты. Это подтверждённый путь нарушения данных, но кадрозахват ещё нужен для демонстрации конкретного артефакта.

### A03. Visibility caches не описывают все входы вычисления

**Доказательство.** [ChunkMeshCache.cpp:2735](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:2735) допускает ранний reuse по camera chunk, mesh revision и квантованным углам 2°. Перемещение внутри чанка, FOV, aspect/projection и изменение дальности не входят в это условие. GPU не сможет вернуть объект, который CPU уже исключил из списка кандидатов.

На [GeometryEngine.cpp:1937](E:/Work/Home/Cubatarium/src/Render/Engine/GeometryEngine.cpp:1937) `cull_stable` проверяет позицию/revision, но не все параметры ориентации/проекции; light-cruise проверяет yaw, а alternate reuse допускает ненулевое изменение углов. Исправление `b1badb4f` добавляет позицию только в один класс reuse и не делает весь ключ полным.

**Практика.** Dirty Flag требует инвалидировать производные данные при изменении любого существенного входа [S07]. В разборе coherent culling NVIDIA предыдущая видимость используется как предположение с проверкой, а не как разрешение пропускать ставшие видимыми объекты [S05].

**Решение.** Базовый режим — точный `CullInputKey`: pass, bounds/table revision, camera/view-projection, cull distance/mode и валидность результата. Оптимизированный reuse разрешать лишь при доказанном включении текущего frustum в ранее рассчитанный **консервативный** объём кандидатов. Допуск в градусах сам такого доказательства не даёт. Проверять sub-chunk движение, pitch/yaw, FOV, resize, perspective↔orthographic и teleport.

### A04. Overlap visible sets не гарантирует наличие всех нужных batches

**Доказательство.** [GpuPassRefreshPolicy.h:36](E:/Work/Home/Cubatarium/src/Render/Camera/GpuPassRefreshPolicy.h:36) требует rebuild только при нулевом/менее 10% пересечении. [GreedyGpuBackend.cpp:302](E:/Work/Home/Cubatarium/src/Render/Engine/GreedyGpuBackend.cpp:302) при совпадении mesh/sort revisions и отсутствии rebuild возвращается без загрузки недостающих refs. Например, 99 из 100 пересекаются, но новый сотый batch отсутствует в cache. Такой случай возможен при изменении видимости уже построенной геометрии без изменения её содержимого. Ни новая cull revision, ни близкое число команд не доказывают полноту списка.

**Практика и решение.** Принцип conservative visibility [S05] допускает лишних кандидатов, но не пропавших необходимых. Разделить resident geometry table и видимый command list; сравнивать identity/version каждого требуемого ref, загружать недостающие, а не эвристически решать «набор достаточно похож». Временное решение — строгий set delta; долгосрочное — стабильные chunk/mesh handles и residency revision. Тест: `{A,B} → {B,C}` при неизменном mesh revision, где `C` ранее не resident; `C` должен попасть в draw без ожидания случайного remesh.

### A05. Column scheduler теряет идентичность задачи

**Доказательство.** [ColumnFlowScheduler.cpp:11](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnFlowScheduler.cpp:11) хранит только 16 бит второй координаты; `(x,z)` и `(x,z+65536)` получают одинаковый task key. `ColumnOnlyKey` при этом использует полную координату, так что разные индексы самой очереди расходятся. Аналогичное усечение есть в `ColumnFlowExecutor::CooldownKey`. Signed left shift отрицательной координаты в C++17 также надо заменить unsigned packing или структурным ключом.

В `Enqueue` одинаковый kind немедленно игнорируется: новая urgency и `cy` теряются. Отмена помечает **ключ**, а не экземпляр ticket, причём сразу оба `scan_full_focus` variants. Старый tombstone может удалить более новый ticket. `Size()` возвращает размер heap со stale entries, не число живых работ.

**Исполняемый результат.** [ColumnSchedulerAuditRepro.cpp](E:/Work/Home/Cubatarium/tools/research/ColumnSchedulerAuditRepro.cpp) на `b1badb4f` дал пять нарушений проверяемых контрактов. В ABA-сценарии `Remesh(10) → FirstMesh(20) → drain → Remesh(30)` возвращается старый `Remesh(10)`. В сценарии несуществовавшего full-scan variant `DrainOne=false`, но `ContainsColumn=true`. Коллизия дальней координаты сама по себе не объясняет сбой у spawn; ABA и потеря urgency не требуют больших координат. При этом существующий `ColumnFlowSchedulerTest.cpp`, отдельно пересобранный на тех же исходниках, проходит. Проверки refresh urgency и live count задают рекомендуемый новый API-контракт; текущий API явно делает same-kind deduplication и возвращает raw heap size. Потеря ticket при ABA и коллизии координат — самостоятельные ошибки, не вопрос названия счётчика.

**Практика.** Первичная документация `heapq` отдельно разбирает обновление приоритета и lazy deletion: помечается конкретная запись, а live map указывает на новую; sequence обеспечивает однозначность [S06]. Алгоритмический приём применим к C++ без Python dependency.

**Решение.** `ColumnCoord` с полным equality/hash; live ticket `{generation, kind, priority, requestedSlices}`; heap entry с неизменяемым generation и sequence. Pop принимает запись только при совпадении с live map. Обновление urgency создаёт новую generation, запросы диапазонов объединяются по явной политике. Метрики отдельно `liveCount`, `heapCount`, `staleCount`, возраст и superseded count. Существующий repro — начало регрессий, не достаточное покрытие модели.

### A06. CPU merge света не сообщает об изменении

**Доказательство.** В [World.cpp:4472](E:/Work/Home/Cubatarium/src/World/Core/World.cpp:4472) CPU fallback записывает `light_packed`, вызывает `BumpLightFieldRevision()` и добавляет `relit_coords`, но не выставляет `any_light_changed=true`. В GPU-seed ветке строкой выше флаг выставляется. Ниже условие `!any_light_changed && !force_unchanged_relit` очищает pending/inflight без `MarkRelitChunksForMesh`.

Это проявляется, если изменения пришли только через fallback и эвристика force не сработала. Fallback — не мёртвый код: `ApplyGpuSkylightSeedToChunk` возвращает false на Android/GLES и без GL-контекста; он также обходится при `include_skylight=false`. Отдельно: функция с GPU в имени на текущем desktop применяет **CPU** seed, а не GPU readback — делать противоположный вывод по названию нельзя.

**Практика и решение.** Это прямое нарушение Dirty Flag [S07]. Merge должен возвращать единый `LightChangeSet`, а решение о remesh выводиться из него, вместо независимого bool и vector. Локальное исправление пропущенного флага можно сделать первым маленьким коммитом, затем унифицировать контракт. Проверить CPU/GPU-seed/fallback, только block light, отсутствие изменений и валидно тёмную пещеру. Потемнение не следует лечить запретом любого нулевого света.

### A07. Версия центра не описывает зависимости light и mesh

**Доказательство.** [ChunkRelightSnapshot.h:65](E:/Work/Home/Cubatarium/src/World/Lighting/ChunkRelightSnapshot.h:65) передаёт `job_id`, глобальный `submitEpoch` и свет по координатам, но не версии geometry/light/инкарнаций всех прочитанных чанков. `AsyncRelightBuilder::DrainCompleted` фильтрует глобальную epoch; [World.cpp:4446](E:/Work/Home/Cubatarium/src/World/Core/World.cpp:4446) устанавливает результат в найденный по координате текущий chunk. Глобальная отмена защищает смену сессии, но не автоматически все изменения voxels и соседнего света внутри неё.

Mesh snapshot копирует центр и шесть граней shell. [ChunkMeshSnapshot.cpp:109](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshSnapshot.cpp:109) ещё и преобразует блок соседа в зависимости от его **визуальной drawable-готовности**. Поэтому topology mesh зависит не только от voxels, но и от сменяющегося состояния renderer. Center revision и ручные remesh notifications не являются самостоятельным доказательством актуальности всех этих входов. Это риск устаревшего commit и циклов seam-repair; не утверждение, что каждая задача сейчас устанавливается неправильно.

**Практика.** Starlight связывает light storage с жизнью chunk и явно учитывает отсутствующие/неинициализированные sections [S08, S09]. Работа Build Systems à la Carte разделяет scheduling и проверку актуальности по зависимостям [S12]. Применение к mesh — архитектурная аналогия: mesh является производным артефактом, а не системой сборки программы.

**Решение.** На входе фиксировать world epoch, chunk incarnation, content/light revisions и stamp всех реально прочитанных halo regions; на commit сверять их с текущими, устаревшее отклонять/перепланировать. Для больших relight region — согласованный read/write-set или региональное владение, не только шесть соседей центра. `Unknown`, `Air`, `Unlit` и `LitDark` должны различаться. Provisional border geometry допустима, но с явной причиной и направленной инвалидизацией при изменении boundary. Не блокировать весь мир до готовности большого кольца соседей.

### A08. Capture worker не переносит capture с main thread

**Доказательство.** [MeshCaptureWorker.cpp:28](E:/Work/Home/Cubatarium/src/Render/Mesh/MeshCaptureWorker.cpp:28) в worker лишь переносит уже готовый `band` в completed queue. Полный `ReadChunkBandForCapture` выполняется до enqueue, например [ChunkMeshCache.cpp:3348](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:3348). `PumpCaptureWorkerCommits` ещё может ждать до 2 ms. Это отдельная стадия без существенного вычисления, а не настоящий async capture.

`Capture` копирует массивы blocks/fluid/light и делает lookup соседнего chunk на каждую cell shell. [MeshCaptureStore.cpp:31](E:/Work/Home/Cubatarium/src/Render/Mesh/MeshCaptureStore.cpp:31) возвращает snapshot по значению; `CaptureAndStore` также копирует сохранённые данные. Пересылка snapshot с inline arrays не обязательно становится дешёвой из-за `std::move`.

**Практика и решение.** Voxel Tools рассматривает bulk access, краткие spatial locks и immutable copies/COW как варианты для работы с voxel regions [S11]. Для Cubatarium сначала убрать пустой worker-hop, оставить явный bounded main-thread capture и измерить bytes/copies. Далее сравнить immutable shared chunk pages с коротким read-lock + worker capture; нельзя просто передать mutable `BlockWorld` по ссылке в поток. Предварительно разрешать соседние pointers один раз на грань. Приёмка — одинаковый snapshot/hash и уменьшение измеренного main-thread capture p95 без гонок.

### A09. Cancellation и destruction не образуют замкнутый lifetime

**Доказательство.** `MeshCaptureWorker::CancelPending/CancelCoord` очищают maps, но уже поставленная lambda всё равно добавляет completion: совпадение job-id проверяет только удаление inflight. Completion не содержит epoch/job-id. [ChunkMeshCache.cpp:3164](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:3164) без проверки исходной epoch коммитит его в store; `MeshCaptureStore::Commit` помечает вход **текущей** epoch. После `BumpWorldEpoch` старый snapshot таким образом может выглядеть новым.

В [MeshCaptureWorker.h:62](E:/Work/Home/Cubatarium/src/Render/Mesh/MeshCaptureWorker.h:62) и `AsyncMeshBuilder.h` pool объявлен раньше данных, используемых callbacks. При неявном destruction эти данные уничтожаются раньше pool, чей деструктор только затем делает join. У некоторых владельцев есть внешние WaitIdle/cancel-пути, но сами классы не гарантируют безопасность своего API при уничтожении с активной работой. `ShutdownForProcessExit` также содержит detach: это требует строгого доказательства жизни всех захваченных объектов до прекращения процесса.

**Практика.** C++ Core Guidelines CP.23/24 рассматривают lifetime объектов, доступных joining/detached threads [S13]. Отмена запроса не равна завершению исполняющего его потока.

**Решение.** Completion несёт полный token, проверка до публикации обязательна даже после отмены. Ввести `stop accepting → cancel queued → cooperative stop active → join → destroy callback state`. Деструктор владельца явно завершает pool до разрушения остальных members; реестр definitions живёт как immutable pinned snapshot. Проверить cancel/re-enqueue, world switch и normal shutdown под задержанной задачей и sanitizer. Риск destruction не следует выдавать за уже воспроизведённый crash.

### A10. Readiness и repair debt имеют несколько владельцев

**Доказательство.** `UWorld` хранит `PendingLightBeforeMesh`, sticky remesh, relight inflight и `ColumnEmergeStates`; cache имеет собственные pending/capture/GPU states; executor — scheduler, `column_job_stage_`, cooldown и holds. [ColumnFlowExecutor.cpp:86](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnFlowExecutor.cpp:86) восстанавливает job stage опросом world/cache по Y-slices, а не исполняет единственный authoritative dependency graph. [ColumnJobGraph.h:49](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnJobGraph.h:49) предпочитает RenderReady даже при pending work.

Последнее не обязательно ошибка само по себе: старый опубликованный mesh может законно рисоваться, пока строится новая версия. Ошибка архитектурного описания — пытаться выразить оба состояния одним stage и затем чинить противоречия дополнительными флагами. Поэтому предложение просто сделать одну монотонную FSM `Absent → RenderReady` недостаточно.

**Практика и решение.** Разделение целевого состояния/интереса в World Partition [S18] и scheduler/rebuilder в [S12] даёт подходящий ориентир. Ввести запись с независимыми `desiredVersion`, `residentData`, `publishedMeshVersion`, `pendingWork(token,stage)` и конкретными dependencies. Команды меняют запись в одном owner; render-ready означает валидный опубликованный handle, а не отсутствие новой работы. Производные счётчики обновляются из событий. Watchdogs наблюдают нарушение invariant/возраста, но не должны быть штатным producer той же работы.

### A11. Ограничения отдельных очередей не обеспечивают bounded pipeline

**Доказательство.** [JobThreadPool.cpp:75](E:/Work/Home/Cubatarium/src/Core/Jobs/JobThreadPool.cpp:75) принимает FIFO jobs без собственной ёмкости/приоритета; внешние producers ограничивают часть admission, но очередь исполнения не знает изменившейся срочности. `ComputeWorkerThreadCount` считает budget отдельно для каждого pool: сумма активных CPU-workers не ограничена единым бюджетом. Фактическую oversubscription надо измерять с включёнными backends, а не складывать все максимумы как обязательный runtime факт.

[AsyncMeshBuilder.cpp:155](E:/Work/Home/Cubatarium/src/Render/Mesh/AsyncMeshBuilder.cpp:155) явно игнорирует `maxPerFrame` и делает `DrainAll`. Capture commits тоже drain без малого лимита. Лимиты completed queues и `MemoryBudgetController` уже существуют, но item count/private MB с реактивными thresholds не резервируют bytes будущих snapshots/meshes/uploads. Потерянные completed результаты восстанавливаются, однако это дополнительная работа и задержка полезного FirstMesh.

Main-thread capture, relight merge, scans/readiness, upload выполняются последовательными кусками; проверка времени после большого куска не делает его preemptible. Локальные floors/overrides и несколько регуляторов могут одновременно реагировать на один slow frame. Причина конкретного пика требует CPU trace; наличие ручки `StreamingPhaseBudgetMs` не является доказательством верхней границы.

**Практика.** oneTBB показывает limiter/token-based flow control, где разрешение освобождается после прохождения ограничиваемой части graph [S14]. Voxel Tools приоритизирует задачи в общей execution queue [S10]. Ни одна библиотека не добавляется автоматически: важны контракты.

**Решение.** Сквозные credits по snapshot bytes, result bytes, GPU pending/retired bytes; единый CPU worker budget, отдельная квота blocking I/O. Приоритеты edit/near/first-paint/background обновляются перед исполнением, с aging и гарантированным ресурсом для завершения уже начатого. Один frame deadline, bounded work units, явные причины defer. Coalesce устаревшие версии до тяжёлого compute; не выбрасывать готовый нужный mesh ради заполнения очереди новым дальним. Пропускную способность оценивать в принятой/опубликованной полезной работе, не в числе enqueue.

### A12. Телеметрия смешивает GPU time, CPU submit и вмешательство наблюдателя

**Доказательство.** [MdiVertexPoolStore.cpp:868](E:/Work/Home/Cubatarium/src/Render/Engine/MdiVertexPoolStore.cpp:868) меряет `steady_clock` вокруг `glDispatchCompute + glMemoryBarrier` и сохраняет как `LastCompactCullGpuMs_`. Это CPU duration вызовов/возможных stalls, не длительность GPU execution. На строке 888 следует immediate `glGetBufferSubData`, когда включён HUD либо one-shot telemetry; [GeometryEngine.cpp:1873](E:/Work/Home/Cubatarium/src/Render/Engine/GeometryEngine.cpp:1873) связывает режим с `ShowPerformance`.

**Практика.** Khronos описывает GPU timestamp queries и неблокирующую проверку доступности результата [S15, S16]. Синхронное получение результата GPU способно остановить CPU; это не означает, что каждый такой вызов обязательно дорог на каждом кадре.

**Решение.** Переименовать нынешнюю величину в `cull_submit_cpu_ms`, добавить query ring с delayed read, frame/pass id и признаком availability. Статистику visibility читать асинхронно и редко; не использовать вчерашнее число как доказательство текущей корректности. CPU trace отдельно выделяет capture, compute wait, apply, upload, driver wait, present. HUD on/off сравнивается как отдельная A/B-проверка overhead.

### A13. Scorecard может выдать зелёный verdict без измерений

**Доказательство.** [AnalyzePhase57Scorecard.py:495](E:/Work/Home/Cubatarium/tools/AnalyzePhase57Scorecard.py:495) допускает `periods` из report без perf; отсутствие INFO не отвергается. `evaluate_product` почти все проверки выполняет лишь при наличии perf. Исполнено: `evaluate_fidelity({'periods': 1}, None, None, None) == []` и `evaluate_product(None,None) == []`. Main печатает `FIDELITY_OK`/`PRODUCT_OK` на пустых списках. Результаты hard gates на строке 675 только печатаются и не объединяются с этим verdict.

Также [AppRunner.cpp:564](E:/Work/Home/Cubatarium/src/App/Platform/AppRunner.cpp:564) выключает autosave, на строке 616 включает minimal overlay, а scripted exit на строке 981 использует `_Exit`. Это допустимо для изолированного benchmark, но не эквивалентно ручной игре и не тестирует нормальное разрушение объектов.

**Практика и решение.** Статья Starlight содержит показательный отзыв собственных невалидных benchmark-результатов после обнаружения изменившегося workload [S09]; это методологический пример, не внешний тест Cubatarium. Оценщик должен различать `INVALID_RUN`, `CORRECTNESS_FAIL`, `PERFORMANCE_FAIL`, `PASS`. Обязательны полнота схемы, build/config/route hashes, число кадров, GPU/driver, режим HUD/autosave и все hard gates. Missing не равен нулю. Сохранить быстрый microbench и добавить отдельный product replay с обычными настройками и normal shutdown.

### A14. Сборка тестов не гарантирует их запуск и не проверяет GPU-контракт

**Доказательство.** [CMakeLists.txt:1349](E:/Work/Home/Cubatarium/CMakeLists.txt:1349) создаёт readiness, miss, scheduler и GPU store test targets, но в корневом CMake нет `enable_testing`/`add_test`/CTest registration. [.github/workflows/windows-release-smoke.yml](E:/Work/Home/Cubatarium/.github/workflows/windows-release-smoke.yml) запускает явный неполный список executable tests; `column_flow_scheduler_test`, `miss_first_mesh_class_test`, `streaming_render_ready_invariants_test`, `mesh_gpu_store_mdi_test` в нём отсутствуют. Push/PR branch filters также не покрывают любое имя feature/perf-ветки автоматически.

[MeshGpuStoreMdiTest.cpp:18](E:/Work/Home/Cubatarium/src/Test/MeshGpuStoreMdiTest.cpp:18) использует `FakeBatch/FakeCache` и локальную модель построения команд. Это полезный unit test, но он не исполняет настоящий OpenGL allocator и не может обнаружить A01/A02. Существование тестов не следует обесценивать; недостаёт уровня проверки связей.

**Практика.** CMake документирует регистрацию tests и exit-status contract [S17]; RenderDoc предоставляет scripting API для автоматизации инспекции захватов [S19]. Рекомендация не сводится к одному screenshot: GL API memory hazards могут зависеть от драйвера и задержки GPU.

**Решение.** CTest registry с labels unit/integration/gpu, отказ CI при нуле ожидаемых tests, обязательные unit/model tests на каждый коммит. GL smoke на выделенном GPU runner, CPU-vs-GPU cull oracle, delayed job/fence fault injection, reference mesher/light parity. Для image tests фиксировать seed/камера/время суток/анимации; использовать depth/object-id masks вместе с ограниченным image diff. Нужны проверки отсутствия ложного удаления, а не только совпадения числа команд.

### A15. Разделение на каталоги и facade не ограничивает связанность

**Доказательство.** В исследуемом коммите `World.cpp` — 9 738 строк, `ChunkEmergeCoordinator.cpp` — 5 858, `ChunkMeshCache.cpp` — 6 746, `GeometryEngine.cpp` — 3 971. Сам размер не является ошибкой; существенно, что эти файлы одновременно принимают policy-решения, обновляют derived state, вызывают соседние подсистемы и формируют диагностику.

[check_include_rules.py:30](E:/Work/Home/Cubatarium/tools/audit/check_include_rules.py:30) проверяет только World `.h` → Render, исключая adapter. Реальные `.cpp` dependencies и обратные запреты не проверяются; например, executor включает `Render/Camera/Camera.h`, а `World.cpp` — render factory/caps и mesh policies. Документация содержит более широкую таблицу запретов, чем фактическая проверка. В ней также остался старый streaming lookup `.json` рядом с корректным описанием primary `.cchunk`.

**Практика и решение.** Component у Robert Nystrom объясняет разделение разных доменов поведения вместо объекта, знающего всё [S20]. Это не аргумент в пользу обязательного ECS. Разделить `WorldData`, `StreamCoordinator`, `LightService`, `MeshBuildService`, `RenderResidency` и read-only `DiagnosticsView` по владельцам mutable state. Сначала зафиксировать порты и invariant tests; затем переносить ответственность, не просто разрезать большой cpp. CMake targets и include-check должны запрещать фактические нежелательные зависимости в `.h` и `.cpp`, с явным списком временных исключений.

## 4. Почему многочисленные локальные исправления могли не закрыть проблему

Это вывод из текущих путей данных и истории revert, а не оценка качества труда авторов. Локальная policy может быть верна в unit test и всё равно принимать решение по неверным входам: queue size со stale tickets, mixed-pass AABB, устаревший snapshot или CPU-time, названный GPU-time. В такой ситуации изменение thresholds переносит bottleneck и иногда скрывает дефект, но не восстанавливает invariant.

Получается обратная связь: дыра/тёмный mesh создаёт repair demand; repair создаёт capture/remesh/upload; это увеличивает main-thread latency; budget gates уменьшают admission; незавершённая работа стареет; recovery добавляет новые попытки. A05 способен создавать задолженность без живого ticket, A06 — задолженность без обязательного invalidation, A01/A02 — артефакт даже при уже готовой геометрии. Одним регулятором очереди эти три ситуации не лечатся.

Отдельно важно не смешивать визуальные классы: «нет draw», «draw ошибочно culled», «битая GPU-геометрия», «устаревшее vertex lighting», «настоящая тёмная пещера» и «ещё не загруженный дальний горизонт». Для каждого нужны собственный diagnostic reason и тест. `visible_black`/`fully_dark` без эталона освещения не являются универсальным oracle.

## 5. Что из новых техник имеет смысл после исправлений

**Binary greedy / packed quads.** Первичная реализация Binary Greedy Meshing v2 использует occupancy masks, битовую обработку faces и vertex pulling [S21]. Это хороший кандидат на A/B CPU-mesher, если trace покажет именно mesh compute bottleneck. Проверять реальную семантику Cubatarium: material boundaries, light/AO, cutout, fluids, decor, размеры chunk и memory cost. Чужие микросекунды не переносятся на этот набор условий. Если основной wall расходуется на capture/apply, смена mesher может почти не изменить кадр.

**Двухуровневое качество.** Быстрый корректный first mesh с последующей оптимизированной заменой описан ещё в 0fps [S22]. Подходит после versioned publication и byte admission; иначе удвоит uploads, churn и давление на allocator. «Быстрый» не должен означать неверный свет или скрытие необходимой поверхности.

**LOD/HLOD и interest prediction.** Для дальнего мира уменьшение объёма полной геометрии может быть выгоднее настройки десятков очередей. Начать с интереса от скорости/направления и времени доставки, затем исследовать отдельный coarse far representation. HLOD у Epic — ориентир разделения представлений [S18], а не готовый voxel seam algorithm. Не вводить LOD в ближайшем кольце до устранения дыр и корректного versioned handoff.

**GPU-driven renderer / frame graph.** MDI и GPU cull уже есть. Приоритет — стабильные resident handles, pass-local resources и отсутствие CPU readback в hot path. Полный render graph можно вводить постепенно после A02, не превращая небольшой repair в месячную замену API. Mesh shaders, ray-traced terrain и Vulkan не являются обязательными шагами этого плана.

## 6. Итог и ограничения

Есть достаточные основания для небольших адресных исправлений A01–A06 и для структурного изменения контрактов A07–A15. Нет достаточных данных обещать конкретный FPS, утверждать единственную причину всех чёрных чанков или считать проблему решённой коммитом `b1badb4f`.

Правильная цель следующей итерации: воспроизводимая визуальная корректность на контролируемом маршруте, отсутствие потерянной/устаревшей работы, доказанное безопасное GPU retirement и измеряемый deadline до первого правильного изображения. Затем — стабильный frame time и достаточная скорость доставки мира при заданных hardware, render distance и скорости игрока.

## Источники и применимость

Все ссылки проверялись 2026-09-10. Использованы первичные спецификации, документы производителей, публикации авторов и исходные репозитории. Для живой документации дата версии указана, только если она доступна; branch `main/master` не является immutable revision. Разборы выше содержат собственное применение к коду, а не утверждение, что источник рассматривает Cubatarium. Страницы Khronos Registry были недоступны для чтения; изучены официальные XML refpages в репозитории KhronosGroup.

- **S01. Khronos Group, OpenGL `glMapBufferRange` reference** (официальный XML, copyright 2014, live main). Изучены flags invalidation/unsynchronized и undefined overlap. Основание A01. [Источник][S01].
- **S02. Khronos Group, `glClientWaitSync` reference** (2010–2014, live main). Изучены четыре return statuses и timeout. Основание A01. [Источник][S02].
- **S03. NVIDIA, Writing Portable Rendering Code with NVRHI** (2021). Раздел Resource lifetime management: fence-tracked command list instances и garbage collection после завершения GPU. Ориентир A01, не требование внедрить NVRHI. [Статья][S03].
- **S04. Google Filament, FrameGraph** (живая документация). Изучены nodes read/write, ordering, resource lifetime и import/export истории. Ориентир A02 и границ render passes. [Статья][S04].
- **S05. Michael Wimmer, Jiří Bittner, GPU Gems 2, Chapter 6: Hardware Occlusion Queries Made Useful** (2005). Изучены conservative bounds, delayed results и проверка temporal guesses. Принцип A03/A04; это не рекомендация заменить frustum cull на per-chunk occlusion queries. [Статья][S05].
- **S06. Python Software Foundation, heapq: Priority Queue Implementation Notes** (Python 3.14.7 docs, updated 2026-09-09). Изучены entry finder, update priority, removed entry и sequence. Алгоритмический контрпример A05. [Документация][S06].
- **S07. Robert Nystrom, Game Programming Patterns: Dirty Flag** (книга 2014, текст автора в репозитории). Изучены invalidation при каждом изменении primary state и стоимость отложенного recompute. Основание A03/A06. [Глава][S07].
- **S08. PaperMC / Spottedleaf, Starlight README** (архивный проект, ветка fabric). Изучена привязка light sections к ChunkAccess и ограничение обновлений временем жизни chunk. Ориентир A07. [Документ][S08].
- **S09. Spottedleaf, Starlight Technical Details** (исторические сравнения 1.16–1.19; есть explicit obsoletion notice). Изучены propagation, absent/uninitialized sections, FPS impact и отзыв некорректных gen benchmarks. A07/A13; коэффициенты ускорения не экстраполируются. [Статья][S09].
- **S10. Zylann / Voxel Tools, Development — Threads** (живая документация). Изучены единый priority pool и serial group для I/O. Ориентир A10/A11. [Документация][S10].
- **S11. Zylann / Voxel Tools, Performance** (живая документация). Изучены threads, main-thread work, voxel access, copy/locks и bulk operations. A08/A11; исторические проблемы Godot GL не приписываются Cubatarium. [Статья][S11].
- **S12. Andrey Mokhov, Neil Mitchell, Simon Peyton Jones, Build Systems à la Carte** (ICFP 2018, DOI 10.1145/3236774). Изучены разделение scheduling/rebuilding и verifying traces зависимостей (§4). Аналогия для A07/A10, не voxel implementation. [Публикация и PDF][S12].
- **S13. Bjarne Stroustrup, Herb Sutter и contributors, C++ Core Guidelines CP.23/24** (живая редакция, заголовок Jun 14, 2026). Изучен scope/lifetime joining и detached threads. Основание A09. [Руководство][S13].
- **S14. Intel oneTBB, Create a Token-Based System / Limiting Resource Consumption** (документация 2022.1). Изучен контроль количества сообщений в pipeline через возвращаемые tokens. Ориентир A11; byte-weighted credits — предлагаемое расширение для Cubatarium. [Документация][S14].
- **S15. Khronos Group, `glQueryCounter` reference** (2010–2014, live main). Изучены GPU timestamps и availability. Основание A12. [Документ][S15].
- **S16. Khronos Group, `glGetQueryObject` reference** (live main). Изучены `GL_QUERY_RESULT_AVAILABLE`/`NO_WAIT` и ограничения версии GL. Основание A12. [Документ][S16].
- **S17. Kitware, CMake `add_test`** (документация 4.4.3). Изучены registration, enable_testing и интерпретация exit status. Основание A14; обновление CMake до этой версии не требуется. [Документация][S17].
- **S18. Epic Games, World Partition** (документация Unreal Engine 5.8). Изучены streaming sources, priorities, Loaded/Activated и preloading teleport destination. Ориентир для interest model/дальней геометрии; не сравнение FPS разных игр. [Документация][S18].
- **S19. Baldur Karlsson / RenderDoc, Python API** (официальный репозиторий, v1.x). Изучены scripting и доступность API инспекции. Ориентир автоматизации GPU captures A14. [Документация][S19].
- **S20. Robert Nystrom, Game Programming Patterns: Component** (книга 2014, исходный текст автора). Изучено разделение доменов и способы связи компонентов. Ориентир A15; полный ECS не предписывается. [Глава][S20].
- **S21. cgerikj / Ethan Gore / contributors, Binary Greedy Meshing v2** (live master). Изучены occupancy/face masks, packed quads, vertex pulling и ограничения benchmark/АО. Кандидат на последующий эксперимент. [Реализация и разбор][S21].
- **S22. Mikola Lysenko, Meshing in a Minecraft Game** (2012-06-30). Изучены latency/mesh quality tradeoff, greedy и fast-first/refine-later. Ориентир поэтапного качества, не доказательство bottleneck Cubatarium. [Статья][S22].

[S01]: https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/gl4/glMapBufferRange.xml
[S02]: https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/gl4/glClientWaitSync.xml
[S03]: https://developer.nvidia.com/blog/writing-portable-rendering-code-with-nvrhi/
[S04]: https://google.github.io/filament/notes/framegraph.html
[S05]: https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-6-hardware-occlusion-queries-made-useful
[S06]: https://docs.python.org/3/library/heapq.html#priority-queue-implementation-notes
[S07]: https://raw.githubusercontent.com/munificent/game-programming-patterns/master/book/dirty-flag.markdown
[S08]: https://github.com/PaperMC/Starlight
[S09]: https://github.com/PaperMC/Starlight/blob/fabric/TECHNICAL_DETAILS.md
[S10]: https://voxel-tools.readthedocs.io/en/latest/development/#threads
[S11]: https://voxel-tools.readthedocs.io/en/latest/performance/
[S12]: https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf
[S13]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp23-think-of-a-joining-thread-as-a-scoped-container
[S14]: https://www.intel.com/content/www/us/en/docs/onetbb/developer-guide-api-reference/2022-1/create-a-token-based-system.html
[S15]: https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glQueryCounter.xml
[S16]: https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glGetQueryObject.xml
[S17]: https://cmake.org/cmake/help/latest/command/add_test.html
[S18]: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine
[S19]: https://github.com/baldurk/renderdoc/blob/v1.x/docs/python_api/index.rst
[S20]: https://raw.githubusercontent.com/munificent/game-programming-patterns/master/book/component.markdown
[S21]: https://github.com/cgerikj/binary-greedy-meshing
[S22]: https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/
