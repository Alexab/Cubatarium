# Аудит текущей архитектуры отображения и стриминга мира

## Резюме

Текущую реализацию следует развивать, а не откатывать целиком. Воспроизводимые исправления освещения, GPU lifetime, идентичности mesh jobs и восстановления потерянной работы сохранены. Ручной полёт после dual-lane изменений выглядит приемлемо; это существенный результат, который нужно сделать защищённым baseline.

Однако визуальное улучшение пока достигнуто на фоне дорогой управляющей части конвейера, неполной автоматической приёмки и нескольких воспроизводимых нарушений контрактов. Следующая цель — не ещё один набор коэффициентов и repair floors, а сокращение бесполезной работы и устранение неоднозначности между desired state, job progress и реально опубликованным изображением.

Главные выводы:

1. **Главный измеренный резерв — main-thread streaming и подготовка meshing, не вычисление GPU culling.** В сохранённом ручном прогоне wall порядка 70 ms; участок `prep_schedule_policy` занимает около 22 ms внутри mesh-emerge. Индивидуальные виновники этого участка ещё не измерены.
2. **Есть две конкретные ошибки renderer, требующие регрессий до дальнейшего упрощения защит:** потеря dirty demand при частичной публикации нескольких batches одного чанка; неверное извлечение frustum planes из GLM-матрицы. Обе воспроизведены на текущем production-коде в изолированном контрпримере.
3. **Диагностические классы ещё не являются независимым эталоном изображения.** `FalseNegCull` синтезируется из несовпадения двух счётчиков, а FullyDark при наличии repair-state считается StaleVertexLight без проверки пикселей. Поэтому нельзя объявлять десятки реально чёрных чанков вопреки визуальному наблюдению — но нельзя и объявлять эти счётчики доказательством исправности.
4. **Q6/Q8/Q9 выполнены частично, несмотря на названия и отдельные PASS в истории.** Record продолжает восстанавливаться из legacy maps; deadline начинается после streaming; Q9 допускает неполные данные и повторное использование одного лога как нескольких прогонов.
5. **Свежая проверка — 25/26 CTest PASS, не полностью зелёный набор.** Один executable содержит две падающие проверки remesh-квот. CI push-filter снова не покрывает текущую ветку с суффиксом `4`.

Рекомендуемый порядок: защитить текущий визуальный baseline и исправить локальные доказанные дефекты → построить достоверный visibility/light oracle и трассу critical path → объединить владение demand, ресурсные квоты и deadline → только затем оптимизировать capture, culling и дальнюю геометрию.

## 1. Область проверки и доказательства

Проверена ветка `cursor_audit_impl4`, HEAD **`420335e9f1965b911ea8d56228805f6f42e274e9`**, по состоянию на 14 сентября 2026. После предыдущего документального checkpoint `7f8f8883` в этой линии 82 коммита; diff затрагивает 112 файлов. Текущие локальные tips:

| Ветка | Tip | Роль в исследованной истории |
|---|---|---|
| `cursor_audit_impl` | `7f8f8883` | Предыдущая сверка аудита |
| `cursor_audit_impl2` | `d80ec866` | Owner/deadline, north autofly checkpoint |
| `cursor_audit_impl3` | `78cc11ab` | Диагностический мост FalseNegCull |
| `cursor_audit_impl4` | `420335e9` | Dual-lane и ручной визуальный checkpoint |

Основание: [исходный A01–A15](E:/Work/Home/Cubatarium/docs/streaming/ARCHITECTURE_AUDIT_2026-09-10.md), [план M00–M16](E:/Work/Home/Cubatarium/docs/streaming/ARCHITECTURE_REMEDIATION_PLAN_2026-09-10.md), [сверка Q0–Q10](E:/Work/Home/Cubatarium/docs/streaming/AUDIT_CLOSURE_REVIEW_2026-09-12.md), код и история последующих изменений, [журнал F5](E:/Work/Home/Cubatarium/docs/streaming/FLIGHT_F5_174657_RETEST.md), raw JSONL и suite reports.

Обозначения: **E** — исполненный контрпример/тест; **C** — установленный путь в коде; **M** — измерение в сохранённом прогоне; **R** — риск или гипотеза, требующая отдельного измерения. P1 — следующая обязательная работа до принятия соответствующего контракта; P2 — системное улучшение после локальных защит. Приоритет не означает, что дефект сейчас виден в каждом кадре.

### Проверки на текущем исходном коде

- Пересобраны все 25 executable targets зарегистрированного streaming-набора; отдельное имя driver test запускает тот же production executable с `--driver`.
- `ctest --test-dir build/desktop-msvc -C Release --output-on-failure`: **25/26 PASS**. Real-driver GL smoke действительно выполнен, не skipped. `streaming_render_ready_invariants_test` падает на `K3 +1 remesh under cooled rim miss` и `M3 +1 remesh at pending=12 rim miss`.
- `python tools/test_AnalyzePhase57Scorecard.py`: PASS. Дополнительные контрпримеры ниже выявляют непокрытые случаи.
- `python -X utf8 tools/audit/check_include_rules.py`: PASS, **41 legacy exception, 31 reverse Render→World edge**. Без UTF-8 script в текущем Windows console encoding падает на печати символа стрелки; это отдельный tooling portability defect, не ошибка include graph.
- Изолированный C++ repro использует реальные `GreedyVertexPool.cpp`, `GreedyGpuPublication.cpp`, `Frustum.h`, `MeshWorkAdmission.h`, существующую GL-подмену production test. Он подтверждает N01/N02 и граничный случай N07.

Новый полный полёт мира и новый performance A/B не выполнялись. Полная игровая сборка не заменялась; пересобирались тесты. Сохранения мира не изменялись. Анализ производительности ниже относится к сохранённому прогону, а не к заново снятому профилю HEAD. Android/GLES, дискретные GPU других производителей, sanitizer world-switch и длинный edit stress в этой проверке не испытаны. Production-код в рамках аудита не исправлялся.

## 2. Что действительно улучшилось

| Решение | Оценка | Что сохранить / где предел доказательства |
|---|---|---|
| Draw fences и retired allocations в `UGreedyVertexPool` | Правильное направление, проверенные локальные инварианты | Не возвращать диапазон по timeout/failed; сохранять старые байты при growth. Mock и driver smoke не заменяют стресс всех render paths |
| Pass-local AABB max, история cull, строгий `CullInputKey` | Корректное устранение исходных смешений passes/cache identity | Сохранить. Корректный ключ не исправляет неверную математику frustum, см. N02 |
| Установка рассчитанного skylight/blocklight без повторного vertical reseed | Подтверждённое исправление | Roof-side-light, канальные merge и unchanged revision tests проходят; не возвращать старую seed-ветку |
| `TryEnqueue`, RAII credits, overflow retry, pool lifetime | Полезные контракты сохранены | Не подменять отказ потерей demand; это ещё не сквозное ограничение памяти, N10 |
| Mesh catalog pin для faces/liquid/movement/cross/GPU-extract eligibility | Существенный прогресс Q4 | Основные mesh reads перенесены на pinned view. Relight остаётся на live registry, N09 |
| FirstMesh/RemeshLit lanes с фиксированным порядком capture | Лучше miss-driven перестановок стадий; визуальная регрессия `134914` устранена в наблюдённом классе | Сохранить как baseline; доказать сквозную fairness и согласовать старые тесты, N07 |
| Delayed cull stats через staging+fence | Устранён прежний намеренный readback сразу после dispatch | Сохранить асинхронность; добавить pass/frame identity и корректные barriers, N11 |
| Строгие обязательные raw fields в Phase57 и default manifest | Исходный «три строки только movement_speed → PASS» больше не является текущим поведением | Не писать, что Q0 отсутствует полностью. Оставшиеся пробелы — N08 |
| Различение north autofly, west manual и высоты полёта | Правильная методологическая коррекция | Adequacy proxy не равен product acceptance; не смешивать эти роли |

Общая идея OpenGL + MDI + pools + CPU/GPU greedy не устарела сама по себе. Современные практики в первую очередь требуют явной идентичности ресурсов, завершённого lifetime и проверяемых read/write dependencies; смена API не устраняет этих обязанностей.[^1][^2]

## 3. Производительность: что известно из данных

### 3.1. Визуальный checkpoint и условия

[Ручной checkpoint 154921](E:/Work/Home/Cubatarium/bin/suite_reports/g1_a10_relight/dual_lane_s4_manual_154921.json) сообщает `operator_visual=PASS`, без edit stress; направление западное, приблизительно `(7,3)→(-3,3)`. Его сводка: wall median 71.1 ms, stream 31.2 ms, mesh-emerge 28.0 ms, scene 5.5 ms. Это наблюдение визуальной приемлемости, а не закрытие всех product gates; сам report сохраняет `product_g1_vs_141350=OPEN`.

Raw [perf_20260914-154921_33492.jsonl](E:/Work/Home/Cubatarium/bin/logs/perf_20260914-154921_33492.jsonl) содержит 27 period, 51 spike, 5 blink, 1 shutdown. Renderer — `AMD Radeon(TM) Graphics`; строка GL — `3.3.0 Core Profile Context 23.19.14.07.240801`, compute/SSBO capability flags включены. Версия контекста сама по себе не доказывает отсутствие расширений; нужен manifest фактических capabilities.

Уточнение маршрута: raw samples включают `focus_cz=4` на части пути, высоту 48–70 и начальный speed≈779 при установке позиции. Поэтому документальный shorthand `(7,3)→(-3,3)` недостаточен для точного replay, а фильтр «speed>2» ошибочно захватывает стартовый скачок. Прогон не следует объявлять teleport cruise только по этому первому sample.

### 3.2. Повторный расчёт на явно выбранном участке

Для диагностического среза выбраны только period rows: `2 < movement_speed < 20`, `-3 <= focus_cx <= 6`, `player_y >= 54`. Получилось 18 period samples, представляющих 532 кадра. Это прозрачный анализ сохранённого участка, **не новый acceptance gate**.

| Поле | Медиана period-значений, ms | Максимум period-значения, ms |
|---|---:|---:|
| `wall_ms` | 69.1800 | 114.2400 |
| `stream_ms` | 29.7610 | 42.7591 |
| `mesh_emerge_ms` | 27.3299 | 41.9901 |
| `mesh_emerge_prep_ms` | 22.6435 | 30.0039 |
| `prep_schedule_policy_ms` | 21.7123 | 26.7961 |
| `scene_ms` | 5.4627 | 31.8053 |
| `mesh_snapshot_ms` | 1.1155 | 1.8046 |
| `relight_capture_ms` | 0.8356 | 4.6101 |
| `gpu_cull_exec_ms` | ≈0.0009 | ≈0.0016 |

`FramePerfMonitor` усредняет wall и ряд времён внутри периода; spike writes ограничены шестью на период, а blink дублирует period payload. **Нельзя получить честные per-frame p95/p99, сложив эти типы записей или посчитав percentile от period means.** Нельзя также складывать `world_streaming_phase` с уже входящими в неё stream/emerge, а `prep_schedule_policy` — ещё раз поверх mesh-emerge.

Ключевые наблюдения:

- Управляющий участок schedule-policy намного дороже snapshot capture в этом срезе. Перенос всех копий в worker не объясняет и не устраняет около 22 ms управляющей работы.
- GPU cull timestamp очень мал, но это только один dispatch, не весь GPU frame и не гарантия точной sub-microsecond атрибуции. Задержки драйвера могут проявляться в другом GL-вызове.
- `frame_deadline_remaining_ms=0` во всех 18 samples; `ok_fm` median 0, `ok_remesh` median 2. Наличие ненулевых квот не доказывает пропускную способность всех стадий.
- VB median 79 и StaleVertexLight median 77.5 при визуальном PASS — основание проверять определение метрик, а не автоматически увеличивать relight budgets.
- Около 14 FPS — разумное описание порядка наблюдаемой производительности, но `1000/median(period wall)` не заменяет распределение FPS/latency.

Первый profiling experiment должен атрибутировать `UpdateStreaming`, `TickAsyncChunkSystems`, `schedule_policy` и driver waits. Нужны created→queued→started→computed→applied→published timestamps с task identity, а не только сумма времени worker compute. Такой разбор критической цепочки поддерживает, например, Task Graph Insights.[^3]

## 4. Реестр оставшихся проблем

| ID | Приоритет / факт | Проблема | Основной эффект |
|---|---|---|---|
| N01 | P1 / E | Частичный upload снимает dirty всего чанка | Смешанные версии materials, потерянная замена при OOM |
| N02 | P1 / E,C | Frustum извлекается из columns вместо rows; широкий distance bypass | False negatives вне защитного радиуса и лишний draw внутри |
| N03 | P1 / M,C | Deadline начинается поздно; floors и вторые drains не расходуют единый ресурс | Длинный serial frame, неограниченный суммарный overrun |
| N04 | P1 / C,M | Census proxy выдаётся за классы ошибки изображения | Ложная атрибуция, потенциальный repair feedback loop |
| N05 | P1 / C,E | Owner cutover и debt age не доказывают единственного владельца/прогресс | Скрытая starvation, нулевая mismatch telemetry без сравнения |
| N06 | P1 / C | Packed cooldown key теряет идентичность колонок | Несвязанные колонки подавляют enqueue друг друга |
| N07 | P1 / E,C,M | Dual-lane не охватывает все ресурсы; cap/floor контракт неоднозначен | Квота есть, work не стартует; несогласованные тесты |
| N08 | P1 / E,C | Q9 и остатки Phase57 fail-open / недостаточная provenance | Невалидное закрытие acceptance, несопоставимые benchmarks |
| N09 | P1 / C | Visual input не валидируется; relight читает live catalog | Неявные seam зависимости, риск смешанного content snapshot |
| N10 | P2 / C,R | Credits не покрывают payload lifetime; нет общего CPU budget | Пиковая память, redundant copies и oversubscription |
| N11 | P2 / C | Async stats без pass/sequence; неполный buffer barrier | Некорректная/устаревшая telemetry, driver-dependent поведение |
| N12 | P1/P2 / E,C | CI branch gap, узкие fixtures, monolithic boundaries | Недостаточная защита от повторных регрессий |

### N01. Публикация сейчас batch-granular, а dirty state — chunk-granular

**Доказательство.** [PublishPassInputs](E:/Work/Home/Cubatarium/src/Render/Engine/GreedyGpuPublication.cpp:141) добавляет координату в `published_ok` после успеха любого batch. Если другой batch той же координаты не аллоцируется, сохраняется его старый predecessor. В конце координата всё равно удаляется из `PendingGeometryDirty`, а pass revisions продвигаются.

Исполненный сценарий: начальная публикация двух batches A/B одного чанка, ограничение pool capacity, новая маленькая A и слишком большая B. Результат:

```text
partial: accepted=1 batches=2 a=18 b=9 dirty=0
after capacity restored: old_b_stuck=1
```

Новая A имеет blockId 18, B сохраняет старый blockId 9 вместо 19. Даже после снятия cap и завершения fences следующий вызов без нового dirty event оставляет старую B. Существующий production test проверяет OOM одного batch, поэтому проходит.

**Решение.** Выбрать транзакционную единицу `chunk + pass + target revision`: staging всех её batches, commit только после успеха группы. Соседние чанки могут независимо прогрессировать. Альтернатива — полноценный per-batch target/published registry, но он сложнее для mesh-version и material removal. Не менять грязность всей координаты по частичному успеху. Старые allocations освобождать только после публикации согласованной группы и завершения читателей.[^1]

**Приёмка.** Два/три материала одного чанка; отказ второго и последнего allocations; удаление/перестановка material batches; успех соседнего чанка при отказе этого; восстановление capacity без нового dirty event. Группа либо полностью старая, либо полностью новая, и обязательный retry сохраняется.

### N02. Некорректная математика frustum маскируется консервативными обходами

[Frustum::FromViewProjection](E:/Work/Home/Cubatarium/src/Render/Camera/Frustum.h:15) использует `m[3] ± m[i]`. У `glm::mat4::operator[]` это **столбцы**.[^4] Для принятой в renderer матрицы `projection * view` и column vectors плоскости извлекаются из **строк**: row3±row0/1/2. Это следует непосредственно из clip inequalities.[^5] [GeometryEngine](E:/Work/Home/Cubatarium/src/Render/Engine/GeometryEngine.cpp:1569) не транспонирует `vp` перед передачей.

Контрпример: камера `(100,50,100)`, взгляд по −Z, perspective 60°, aspect 1.6, near .1, far 500; AABB ±.1 вокруг `(100,50,80)`. Центр и маленький box находятся внутри clip-space. Production frustum при distance bypass=0 отвергает и chunk, и entity; reference с корректными строками принимает:

```text
frustum clip_inside=1 chunk=0 entity=0 row_reference=1
```

Дополнительно `IntersectsChunkAABB` пропускает три плоскости и сразу принимает объект в `maxDistance`, независимо от направления взгляда. GPU AABB shader повторяет этот допуск. Это может объяснять, почему ближний мир визуально устойчив несмотря на дефект, но его вклад в лишние draw пока не измерен. Две реализации, повторяющие одну ошибку, не образуют независимый oracle.

**Решение.** Сначала исправить и протестировать extraction относительно clip-space; затем измерить отдельные plane mask и always-admit radius. Не снимать distance guard одним коммитом с исправлением математики. AABB, пересекающий near plane или содержащий камеру, должен обрабатываться математически корректно, а не глобальным отказом от top/bottom culling.

**Приёмка.** Перевод/поворот камеры, non-origin world, perspective/ortho/FOV/resize, обе стороны каждой плоскости, boxes на границе, CPU/GPU parity, creatures и transparent passes. Ноль false-negative necessary draws; отдельно считать conservative overdraw. Источники по culling подчёркивают необходимость избегать синхронных visibility queries ради экономии draw.[^6]

### N03. Deadline не ограничивает весь streaming critical path

[WorldViewBinding](E:/Work/Home/Cubatarium/src/World/Core/WorldViewBinding.cpp:1034) сначала выполняет `UpdateStreaming()` и `TickAsyncChunkSystems()`, а затем вызывает `UFrameDeadline::BeginFrame(phase_budget)` на строке 1057. Это новая точка отсчёта **после** уже потраченного stream time, а не общий deadline от начала фазы. До неё потребители singleton могут видеть состояние предыдущего кадра.

`ShouldDeferProducer(critical_progress=true)` вообще игнорирует истечение. Дополнительные consume/finish/post-drain вызовы получают собственные floors, например [extra_budget≥4 ms](E:/Work/Home/Cubatarium/src/World/Streaming/ChunkEmergeCoordinator.cpp:5510). В normal production capture основной путь `CaptureAndCommitOnMain`, тогда как одна из deadline-проверок стоит в выключенном `DrainCaptureWorkerCommits`. Наличие класса `UFrameDeadline` не означает сквозной контроль времени.

Измеренный участок schedule-policy остаётся большим. [Вызов IsSpawnMeshRingReady](E:/Work/Home/Cubatarium/src/World/Streaming/ChunkEmergeCoordinator.cpp:2103) по-прежнему eager даже при неактивном enter gate; он выполняет readiness/visibility/dirty queries. Это конкретный кандидат на short-circuit, **но ему нельзя приписывать все 21.7 ms** без substage trace.

**Решение.** Один `FrameWorkContext` со steady-clock deadline создать до streaming. Передавать его явно всем production consumers. Для обязательного progress иметь ограниченный многокадровый запас, оплачиваемый фактическими затратами, а не независимый minimum в каждом helper. Длинные scans переводить на resumable cursor/changed-set; этап, который уже начался, может иметь задокументированный bounded overrun.

**Приёмка.** Trace покрывает весь stream→capture→apply→upload interval; каждый перерасход имеет имя атомарной операции. Stop/near demand сходится и при малом бюджете; отсутствие работы не запускает полные обходы. Перед переносом work в threads измерить critical path и конкуренцию CPU.[^3][^7]

### N04. «Oracle» классифицирует предположения, а не фактически нарисованные пиксели

[DrawOracle.h](E:/Work/Home/Cubatarium/src/World/Diagnostics/DrawOracle.h) содержит canned `SmallOracleWorldExpectations`: это таблица bool probes, не сцена мира с материалами. `AccumulateDrawOracleFromVbCensus` передаёт для VB `has_gpu=true`, `in_commands=true`, `cpu_hit=true`. `EstimateObjectIdMissesFromCensusMismatch` возвращает единицу при `unfinished==0 && VB>0`; [WorldStreaming](E:/Work/Home/Cubatarium/src/World/Streaming/WorldStreaming.cpp:1320) записывает её в `DrawOracleFalseNegCullN`. Это не object-id readback и не обнаруженный false-negative cull.

[CountVisibleBlackFocusMeshes](E:/Work/Home/Cubatarium/src/World/Core/World.cpp:3438) ищет тёмные faces в band вокруг focus, а не чёрные пиксели текущего изображения. [VisibleBlackAttribution](E:/Work/Home/Cubatarium/src/World/Streaming/VisibleBlackAttribution.h) определяет LegalDark по отсутствию pending replacement/repair state. [IsMeshLightStaleGpu](E:/Work/Home/Cubatarium/src/World/Streaming/MeshLightStalePolicy.h:17) считает `gpu_has_dark_face` признаком stale даже при одинаковых light revisions.

Получается риск положительной обратной связи: тёмный face → ticket → «не LegalDark» → StaleVertexLight → повышенный remesh/kick budget → новый ticket. Сам факт бесконечного цикла на текущем маршруте не воспроизведён; возможность лишней работы подтверждается зависимостями в коде. Удалять эти защиты без независимого измерения опасно.

**Решение.** Развести три сущности: `mesh revision debt`, `potential dark face`, `measured visible defect`. В proxy обязательно маркировать `source=cpu_census`, availability и age; не экспортировать guessed count под именем фактического FalseNegCull. Отдельный reference renderer/command comparison должен сохранять материалы, освещение и прозрачность. Pixel/object-ID miss проверять против reference visibility и depth: невидимость из-за обычной occlusion не является ошибкой culling. Для прозрачности требуется отдельная стратегия сравнения, не один nearest object ID.

**Приёмка.** Небольшие реальные сцены: освещённый навес, законная пещерная темнота, hidden backface, stale vertex light, отсутствующий command, намеренно испорченный cull. Каждая ошибка имеет координату, generation, pass и воспроизводимый capture; LegalDark не рождает бесконечный demand. Диагностический сбор не создаёт work. Автоматизация GPU captures возможна через replay/debug tooling, но сам интерфейс тестов не заменяет сцены.[^6][^8]

### N05. Cutover ещё не означает независимый source of truth и доказанную liveness

В [ColumnRecordCoordinator](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnRecordCoordinator.cpp:12) default уже `EvictionOwner`, и `Decide*` возвращают record decision до выполнения shadow comparison. Поэтому нулевой `shadow_mismatch_n` после cutover **ожидаем по структуре кода**; это не доказательство идентичности решений двух независимых owners.

[SyncColumnJobStageFromWorld](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnFlowExecutor.cpp:88) продолжает сканировать world/cache и собирать record из legacy truth. Legacy enqueue decision частично выравнивается по `record_want` ещё до сравнения. Реальный token улучшен относительно синтетической единицы, но [QueryLiveGpuResidencyToken](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:600) кодирует только `(quad_count, slot_index)`: это не allocation generation и не полный идентификатор нескольких slices/passes колонки. CPU-backed drawable path также не сводится к этому token.

Есть неполная миграция secondary writers: GPU RAA path использует `ShouldSkipSecondaryFullyDarkDirty`, но CPU result branches около [4842](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:4842) и [4950](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:4950) сохраняют отдельные FullyDark→`MarkDirtyPriority` переходы. Это не доказательство двойного enqueue в каждом кадре, но «sole Dirty path для всех backend» пока неверно.

Liveness telemetry тоже ослаблена. `SyncFromWorldTruth` вызывает `TouchDebtProgress` при каждом `truth.render_ready`, включая неизменный predecessor. `AdvanceOldestDebtAgeFrames` сбрасывает возраст при уменьшении общего количества debt или **любом** GPU finish. Контрпример: одна старая задолженность живёт 100 кадров, другая завершается — «oldest age» становится 1. Следовательно график малого oldest age не исключает starvation конкретной координаты.

**Решение.** Events `demand_changed/job_started/computed/published/evicted` обновляют authoritative record; reconcile остаётся диагностикой. Pending и published получают world epoch, chunk incarnation, target revision и allocation generation; aggregate column state вычисляется из slices. Debt хранит время на конкретный key, сбрасываемое только его publish/cancel-interest. Сравнение old/new решений проводится до переключения без взаимного подмешивания.

**Приёмка.** Один writer каждого domain/backend; повторный sync не меняет progress timestamps; завершение соседа не обнуляет oldest; slot reuse при таком же quad count не проходит как та же публикация. Lifecycle-тест включает активные jobs и настоящий мир, а не только idle pool. Модель явных зависимостей и lifetime важнее наличия классов с названием Coordinator/Owner.[^1][^2][^9]

### N06. CooldownKey всё ещё допускает коллизии координат

[ColumnFlowExecutor::CooldownKey](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnFlowExecutor.cpp:359) пакует `(ux << 32) | (uz << 8) | kind` в 64 бита. Биты старшей части Z перекрывают младшие биты X. Это не injective представление 32+32+8 бит, хотя комментарий утверждает full-width.

Для одного `kind=1`:

```text
(-1,-1) -> 0xffffffffffffff01
(-2,-1) -> 0xffffffffffffff01
( 0,-1) -> 0x000000ffffffff01
(255,-1) -> 0x000000ffffffff01
```

Значения перепроверены арифметически по формуле; полный world enqueue trace этого случая не снимался. [Enqueue](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnFlowExecutor.cpp:279) использует key для cooldown, поэтому dispatch одной колонки может временно подавить другую. Исправленный scheduler с full-coordinate key не исправляет этот внешний map.

**Решение и приёмка.** Структурный key `{int32 x,z; WorkKind kind}` с equality по всем полям; hash может коллидировать, identity — нет. Проверить отрицательный Z, соседние отрицательные X, границы int32 и разные kinds. Тестировать executor cooldown, не только scheduler heap. Для очередей с reprioritisation разделять task identity, priority и живую запись; аналогичный принцип показан в стандартном разборе mutable priority queue.[^10]

### N07. Dual-lane защищает порядок, но ещё не сквозную fairness

Текущий фиксированный remesh-snapshot→FM порядок следует сохранить. Его обратная перестановка уже дала визуальную регрессию; повторное управление порядком по текущему miss — слабый способ исправлять нехватку ресурсов.

Но [ComputeDualLaneSchedule](E:/Work/Home/Cubatarium/src/World/Streaming/MeshWorkAdmission.h:268) оперирует schedule slots; snapshot capacity, refresh count, pending GPU, worker slots и deadline применяются отдельно. `ok_fm=0` в части samples при effective cap 4 подтверждает, что quota≠admission. В raw log есть периоды с snapshot skips 33–59, но без per-ticket trace нельзя однозначно приписать все skips FM lane.

Граничный helper-тест: `cap=1`, обе lanes с demand, `rr_token=1`, `protect_remesh_floor=2` возвращает remesh=2. То есть API не обеспечивает сумму ≤ cap. Production caller сейчас может заранее увеличить cap; воспроизведение helper не доказывает, что именно этот случай вызвал текущий frame overrun.

Падающие K3/M3 tests отдельно требуют ≥2 remesh при `remesh_queue_n=0`. Новая логика считает отсутствие remesh demand основанием отдать quota FM. Нужно принять явный контракт: либо исправить fixture, добавив реальный demand, либо документировать сохранение резерва. Просто ослабить assert до нуля без проверки desired behaviour недостаточно.

**Решение.** Одна стадийная admission decision резервирует измеряемый набор ресурсов: capture bytes/refresh, compute credit, completion space и upload allowance. FM и RemeshLit получают справедливый минимум в rolling window, с учётом реальной стоимости и age. Неиспользованный резерв передаётся второй lane без бессмысленной capture. Floors не могут расширять hard cap неявно; невозможная конфигурация возвращает overload reason.

**Приёмка.** Property/model tests cap 0/1/2/large, одна/обе lanes, разные стоимости, отказ capture/queue/result/GPU, changing focus, cancel. Проверять publish latency конкретного demand, а не лишь round-robin token. Общий приоритетный compute executor и token-based admission — подходящие ориентиры, но не обязательная замена библиотеки.[^11][^12]

### N08. Приёмка всё ещё может быть зелёной при недоказанной валидности

**Q9.** [drawable_ok](E:/Work/Home/Cubatarium/tools/q9_acceptance_suite.py:111) требует наличия stale median, но не ограничивает её; отсутствующие unfinished/holes допускаются. Исполнено:

```python
drawable_ok({'metrics': {'mesh_apply_stale_med': 999999}})  # True
```

Enter errors, отсутствие enter, pool timeout и ряд collected metrics не участвуют в этом verdict. `analyze_one` смешивает JSON row kinds и считает последнюю 80% часть строк «cruise». Manifest берёт git/exe hash во время анализа, не проверяет provenance самого run; seed может быть null. [Q9 report](E:/Work/Home/Cubatarium/bin/suite_reports/q9_20260913_final.json) и процедура F5 используют один warm log три раза. Три непересекающихся lap segments в одном процессе могут быть полезны, но трижды проанализировать **целый одинаковый файл** — не три независимых samples и даже не три разных segment measurements.

**Phase57.** Устранён первоначальный missing-fields→zero дефект. Но [validate_run_inputs](E:/Work/Home/Cubatarium/tools/AnalyzePhase57Scorecard.py) допускает отсутствующий набор hard gates и любые непустые строки SHA/config/route. Numeric gate `0` не трактуется как false и не отвергается; отрицательный `wall_ms` проходит raw schema. Контрпримеры выполнены через настоящие функции. Manifest `teleport_cruise` проверяется на наличие, а CLI auto извлекает teleport только из top-level report: противоречия двух представлений не сверяются. Diagnostic overrides всё ещё могут печатать слово PASS; downstream обязан различать их.

**Решение.** Одна версия acceptance schema и один engine verdict, Q9 — лишь batch aggregator над ним. Нельзя принимать неизвестный/missing/null/non-bool gate или произвольный identity manifest. `run_id + exe/config/content/route hashes + phase intervals + actual start/exit` записываются при запуске. Parser отвергает повторные `(run_id, interval)` и смешение mode/build; warm laps имеют явные границы и рассматриваются как correlated samples.

**Приёмка.** CLI-level fixtures: частичный raw field, отрицательные/NaN/type errors, отсутствующий gate, forged hash, teleport mismatch, bad enter, duplicate/overlapping intervals, wrong route. Отдельный вывод `DIAGNOSTIC`, никогда acceptance PASS. До/после — одинаковая сцена и фактический workload; несколько повторов, A/A control и заранее фиксированные критерии. Repetitions/variance нужны именно потому, что одиночные timings зависят от состояния системы.[^13]

### N09. Snapshot остаётся функцией не всех валидируемых входов

Отказ учитывать visual flip в geom stamps уменьшил stale churn, но [Capture](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshSnapshot.cpp:115) по-прежнему меняет `shellBlocks`/neighbor state через `neighbor_drawable`. [InputsStillValid](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshSnapshot.cpp:68) намеренно игнорирует этот callback. Следовательно два разных geometry inputs могут иметь один valid stamp. Тест `InputsStillValidVisualTest` закрепляет отсутствие invalidation, но не проверяет согласованность результата двух captures с реальными solid borders.

Правильная граница может исключать visual residency из meshing, **только если вычисление geometry тоже от неё не зависит**. Иначе необходимо отдельно моделировать preview seam/border result и его lifetime. Простое возвращение visual flip в общий stamp рискует вернуть старую thrash; это не рекомендуемый универсальный фикс.

Mesh catalog pin распространён на многие production reads — полезное исправление. Однако [AsyncRelightBuilder](E:/Work/Home/Cubatarium/src/World/Lighting/AsyncRelightBuilder.cpp:60) всё ещё вызывает `snapshot.Compute(*registryPtr)`; `ChunkRelightSnapshot` читает live liquid/emission/transparency metadata. `input_catalog` удерживает lifetime и проверяется на install, но это не гарантирует чтение одного immutable catalog внутри compute. Наличие безопасного запрета reload до join ограничило бы риск; такой запрет должен быть явным контрактом, а не допущением.

Новые `MeshInputs/LightInputs` из [MeshInputs.h](E:/Work/Home/Cubatarium/src/World/Streaming/MeshInputs.h) используются в tests, но не заменили production dependency protocol. Реальные snapshot stamps нельзя считать завершёнными лишь по существованию этих структур.

**Решение.** Geometry выводится из voxel/light/material identity. Visual fallback/seam overlay — отдельно; либо явный, ограниченный border dependency. Relight получает immutable lighting catalog/view, охватывающий все метаданные. Read region и propagation settings описывают фактические зависимости; проверить крышу выше captured band. Аналогия с incremental build systems полезна: валидность результата требует учитывать действительные, в том числе динамические зависимости.[^14]

**Приёмка.** Solid border меняет только drawable state; capture/apply/reuse дают ожидаемую поверхность без лишней полной перестройки. Catalog swap между submit/compute/apply, sky-only/block-only, roof outside band, neighbor load/unload и world epoch switch. Синхронный reference считает тот же immutable input.

### N10. Admission ограничивает часть стадий, но не полный footprint

[CaptureAndStore](E:/Work/Home/Cubatarium/src/Render/Mesh/MeshCaptureStore.cpp:83) и `CaptureAndCommitOnMain` резервируют 64 KiB до copy, но локальный guard освобождается при выходе, тогда как store продолжает владеть snapshot. `TryGet` возвращает snapshot по значению; присваивание `std::move` структуры из `std::array` не делает её backing storage разделяемым. В текущей MSVC-конфигурации `sizeof(ChunkMeshSnapshot)=24432` байта: estimate не является недооценкой одного snapshot, проблема — число копий и длительность учёта.

[AsyncMeshBuilder](E:/Work/Home/Cubatarium/src/Render/Mesh/AsyncMeshBuilder.cpp:175) резервирует result bytes после compute и создания vectors. Capture worker по-прежнему выключен. Нет доказанного единого cap для store+pending+worker scratch+completed+GPU staging/growth.

[ComputeWorkerThreadCount](E:/Work/Home/Cubatarium/src/Core/Jobs/JobThreadBudget.cpp) вычисляет лимит отдельно для каждого pool, а не распределяет общий compute budget. Это риск oversubscription; реальное число одновременно работающих потоков/CPU contention на целевом устройстве не измерялось. Не нужно просто увеличивать mesh workers, пока main-thread policy занимает десятки миллисекунд.

**Решение.** Разделить resident cache budget и in-flight budget, учитывать их по lifetime payload. Использовать immutable shared snapshot pages либо короткий try-lock+bulk copy в worker — выбрать одну модель после замера. Резервировать conservative result capacity до compute, корректировать по факту; оставить отдельный completion reserve, чтобы не создать admission deadlock. Общий compute pool/лимит плюс отдельный blocking I/O, не сумма hardware_concurrency по subsystems.[^7][^11][^12]

**Приёмка.** Peak process memory и accounted categories сходятся; cancel/reject/exception/shutdown возвращают credits. Worst-case mesh/material fragmentation, streaming overload и capped memory не теряют near demand. Передавать данные между стадиями без повторных full snapshot copies, если это подтверждено профилем.

### N11. Async stats нуждаются в resource identity и завершённой синхронизации

[PollCullStatsAsyncRing / ArmCullStatsAsyncSample](E:/Work/Home/Cubatarium/src/Render/Engine/MdiVertexPoolStore.cpp:296) читают staging после signaled fence — это правильнее старого немедленного `glGetBufferSubData`. Но slots не несут pass ID, submit sequence, camera/table revision или age. Один `StagedCullStatsVisible_` обслуживает общий store; opaque/transparent samples могут смешиваться, а обход слотов по физическому индексу не гарантирует выбор самого нового submit после wrap.

[Dispatch](E:/Work/Home/Cubatarium/src/Render/Engine/MdiVertexPoolStore.cpp:1110) использует shader-storage/command barrier, после чего staging copy читает shader-written stats через `glCopyBufferSubData`. Для этого перехода Khronos определяет **`GL_BUFFER_UPDATE_BARRIER_BIT`**; fence после copy не заменяет visibility barrier до copy.[^15] Влияние на данном driver не воспроизводилось; это подтверждённый пробел протокола, не доказательство искажённой геометрии.

**Решение.** Sample `{pass, submit_seq, frame_id, table_version, valid, age}`; выбирать новый завершённый sample только своего pass. Добавить barrier для фактического следующего consumer. На ring-full пропускать измерение, не работу; unavailable не выдавать за current zero. Проверять аналогичные shader→copy/update transitions во всех GPU paths отдельно.

**Приёмка.** Alternating opaque/transparent counts, delayed slots, wrap, unavailable/failed fence, выключение/включение HUD и очистка мира. Отдельно проверять command correctness и telemetry correctness: это разные контракты. Явные pass dependencies предотвращают повторение такого aliasing.[^2][^15]

### N12. Тесты и границы модулей пока слабее заявленных контрактов

[Windows CI](E:/Work/Home/Cubatarium/.github/workflows/windows-release-smoke.yml:5) добавил точное имя `cursor_audit_impl`, но не `cursor_audit_impl2/3/4`. Push HEAD не соответствует фильтру; PR в перечисленные target branches и manual dispatch остаются возможны. Exact branch name не является wildcard.[^16] Серверный запуск в этой проверке не выполнялся.

CTest вырос до 26 имён, но 25 CPU tests всё ещё имеют общий label `unit;streaming`; некоторые — integration. GPU smoke рисует allocator fixture, не все MDI/material/light paths. `NormalShutdownTest` проверяет drain/cancel двух pool instances, не destruction активного world + mesh + relight + renderer. Flight `_Exit` также не доказывает нормальную цепочку разрушения. Joining-thread lifetime — необходимый, но не достаточный инвариант всего владельца.[^9]

Include checker полезен, однако reverse edges только перечисляются. Сейчас 41 forward exception и 31 reverse edge. Размеры центральных файлов: World.cpp 9882 строки, ChunkMeshCache.cpp 7127, ChunkEmergeCoordinator.cpp 5902, WorldStreaming.cpp 4783, MeshWorkAdmission.h 1306. Размер сам по себе не баг, но здесь он сопровождается пересечением policy, work execution, diagnosis и lifetime.

**Решение.** Закрыть branch coverage и несогласованный regression; разделить unit/CPU integration/driver/world acceptance. Вынести build targets `WorldData`, `Lighting`, `StreamingScheduler`, `MeshBuild`, `RenderWorld`, `Diagnostics` с направленными зависимостями. Ограничивать поведение APIs, а не только прямые `#include`: renderer публикует события, но не инициирует lighting repair. Каждый exception получает владельца и удаляющий commit; новые reverse edges запрещены.

**Приёмка.** CI на актуальном SHA, нулевой/пропавший тест ломает job, GPU skip не считается GPU acceptance. В lifecycle stress проверяются активные jobs, world switch, invalidation и destructor order. Reverse-edge allowlist не растёт; diagnostics не меняет production work.

## 5. Сопоставление с исходным аудитом

| Исходный пункт | Текущий вывод |
|---|---|
| A01 lifetime | Исходный allocator defect локально исправлен; публикация требует N01 и stress остальных allocators/passes |
| A02 pass-local cull | Исходное смешение AABB исправлено; telemetry pass identity N11 — другая незавершённая граница |
| A03 cull cache | Ключи усилены; N02 выявляет математический дефект вне cache-key контракта |
| A04 residency/publication | Retain-until-replace улучшен; multi-batch partial publish некорректен |
| A05 job identity | Scheduler probes проходят; внешний cooldown map N06 остаётся некорректным |
| A06 lighting install | Конкретный дефект merge/reseed исправлен, регрессии проходят |
| A07 input dependencies | Mesh catalog значительно улучшен; visual/capture и relight catalog N09 не закрыты |
| A08 capture | Основная работа всё ещё на main; оптимизацию приоритизировать после policy trace |
| A09 lifetime | RAII/join исправления полезны; полного world lifecycle proof нет |
| A10 owner | Cutover flags включены, но authoritative migration и liveness N05 неполны |
| A11 budgets/admission | Есть работающие локальные ограничения и lanes; общей resource/time модели ещё нет |
| A12 telemetry | GPU timings/delayed readback улучшены; oracle validity и pass/age остаются открыты |
| A13 scorecard | Исходный minimal-data repro закрыт; Q9 и оставшиеся Phase57 validation gaps N08 открыты |
| A14 tests/CI | Набор вырос; текущий fresh run красный, текущая suffix branch вне push-filter |
| A15 architecture | Facades и include audit есть; реальные module targets/ownership boundaries не завершены |

Не следует переносить старое утверждение «чёрные чанки наблюдаются» на текущий manual baseline. Следует сохранить более точный статус: **manual visual accepted для наблюдённого сценария; renderer correctness, bounded latency и системная приёмка ещё не доказаны**.

## 6. План системных доработок

Оценки S/M/L — относительный объём, не календарное обещание. Каждый этап выпускается маленькими коммитами: regression → минимальная реализация → evidence. Смена workload/порогов не может служить доказательством ускорения.

### D0. Зафиксировать измеряемый baseline и восстановить доверие к gates — S/M

Связь: N08/N12, Q0/Q1/Q9. Делать первым; локальные renderer regressions D1 можно разрабатывать параллельно по смыслу, без смешивания результатов.

1. Зафиксировать manifest visual baseline: HEAD/exe/content/save/config, GPU/driver/capabilities, resolution, VSync, HUD, autosave, yaw/pitch/Y/speed и точные phase boundaries. Сохранённый 154921 — исторический visual anchor, а не эталон всех будущих route hashes.
2. Исправить fail-closed batch/schema validation и uniqueness intervals; вывести отдельно validity, visual correctness, absolute SLA и comparative performance. Диагностический mode не имеет acceptance PASS.
3. Покрыть `cursor_audit_impl*` в CI либо перейти на согласованный общий branch policy. Исправить K3/M3 исходя из demand contract, не произвольной замены ожидаемых чисел.
4. Добавить frame-level trace/ring histogram. Period means оставить для обзора, не для p99.

Выход: каждый зелёный отчёт восстанавливается из однозначного run/segment; свежий обязательный набор зелёный; A/A разброс известен. Ответственные границы: tooling/CI и AppRunner manifest. Rollback — не возврат fail-open, а сохранение старого analyzer только как явно diagnostic.

### D1. Два локальных renderer контракта и cooldown identity — S/M

Связь: N01/N02/N06. Разделить минимум на три commits.

1. Production regression multi-material OOM; atomic chunk-pass group publish с сохранённым dirty target. Контролировать peak staging/live/retired bytes.
2. Clip-space regression translated camera; корректная row extraction. Сначала сохранить guards, затем отдельно исследовать возможность их уменьшения.
3. Структурный cooldown key; model tests executor, включая отрицательные координаты.

Выход: приведённые контрпримеры превращены в assertions правильного поведения; manual anchor не ухудшен. Ошибку нельзя скрыть новым repair trigger. GPU no-hole retention остаётся обязательным.

### D2. Истинные причины visual debt — M

Связь: N04/N11. Зависит от базовой валидности D0, использует fixtures D1.

1. Переименовать census proxies и добавить source/validity/age. Не менять work policy одновременно с переименованием метрики.
2. Реальная SmallOracle scene проходит production material/light/MDI paths. Командный и пиксельный reference независимы от исследуемого cull shortcut.
3. Для sampled coordinate сохранять desired/published revisions, allocations, commands, CPU clip result, depth/object hits. Legal darkness устанавливается по свету/поверхности/reference, не по отсутствию ticket.
4. Исправить asynchronous stats identity/barriers; фиксировать unavailable, не ноль.

Выход: controlled fault injection различает MissingResident, MissingCommand, FalseNegCull и StaleVertexLight. Чёрные faces вне наблюдаемой поверхности не расходуют budget как доказанный visual fault. Overhead oracle измерен; дорогой capture работает в отдельном diagnostic mode.

### D3. Убрать измеренную serial работу — M

Связь: N03/N10. Инструментацию можно начать сразу; acceptance производительности — после D0/D1/D2.

1. Использовать существующий `CUBA_ZONE`/Tracy facade, добавить scopes и visit counts внутри schedule-policy, `UpdateStreaming`, async tick, relight capture, publication и GL boundary calls.
2. Выделить thread execution, blocking, driver submit и GPU execution. Собирать CPU sampling/context switches на целевом iGPU устройстве, не считать всякую wall-паузу собственным compute функции.
3. Первое безопасное изменение — lazy spawn readiness при неактивном enter gate; callback-count regression и truth table подтверждают неизменность decision.
4. Затем заменить подтверждённые repeated full scans на once-per-frame memo с полным ключом или changed-set. Для незавершённого обхода сохранять cursor и debt; не просто прекращать ремонт.

Выход: уменьшение атрибутированного main-thread work при той же сцене, route, speed, visual coverage и publish latency. Доказанное ускорение сопровождается до/после traces. Нельзя обещать заранее, что один getter устранит все 22 ms.

### D4. Закончить owner и dependency protocol — L, по domains

Связь: N05/N09. Зависит от D2; migration маленькими вертикальными срезами.

1. Определить authoritative `ChunkRenderRecord`: `desired version`, `published generation`, `pending token`, actual dependency set, per-key debt timestamps. Column — aggregate, не замена идентичности chunk slice.
2. Сначала mesh lifecycle, затем relight replacement, seam и eviction. Для каждого domain перечислить все GPU/CPU writers; secondary path делегирует owner, а не создаёт параллельную dirty-семантику.
3. Events становятся штатным обновлением record; legacy polling сравнивает снимки и обнаруживает пропущенные события, не восстанавливает нормальную работу.
4. Развязать geometry и visual residency; pinned relight metadata; adversarial world-switch/reload/capture tests.

Выход: один owner per domain, stale не публикуется, повторный sync не «омолаживает» debt. Остановившаяся отдельная задача видна независимо от прогресса соседей. Rollback переводит domain на один прежний writer, не включает два одновременно.

### D5. Сквозные квоты ресурсов и времени — L

Связь: N03/N07/N10. Зависит от owner/events D4 и measurements D3.

1. Создать явный `FrameWorkContext` до UpdateStreaming; все capture/apply/kick/finish/repair работают с тем же deadline и accounting.
2. Budget object учитывает CPU time, snapshot/result bytes, queue slots, GPU staging/retired reserve. Reserve производится до дорогой аллокации; credit перемещается вместе с payload.
3. Сохранить FM/RemeshLit lanes, но определять fairness по actual progress/cost в rolling window. Cap=1 чередуется; невозможный floor не расширяет cap незаметно. Stale/cancel outcome сохраняет demand и возвращает ресурсы.
4. Объединить compute concurrency cap, отделить blocking I/O. Completion имеет резерв, чтобы upstream не удерживал все кредиты, необходимые собственному завершению.
5. Мигрировать capture на shared immutable pages или spatial try-lock copy лишь после оценки copy cost/lock pressure. Нельзя читать mutable world из worker без нового ownership protocol.

Выход: bounded peak memory, объяснимый overrun, near-demand latency ограничена при допустимой нагрузке. При overload — явная политика, сохранённое изображение и возврат к норме после остановки, не silent retry forever.

### D6. Сократить draw/workload без ухудшения изображения — M, по профилю

Связь: N02/N11. После исправления математики и oracle.

1. Уменьшать overly conservative radius/plane skips по одному; измерять CPU visibility, command count, GPU time, pixels и bandwidth.
2. Проверить repeated command-table rebuild, transparent sort/upload, full buffer growth и deferred destruction. Отдельно анализировать fluid-map spikes: в начале 154921 есть большой `fluid_map_cpu_ms`, но это не steady-cruise median и не основание переписать fluid backend целиком.
3. Если дальняя сцена действительно доминирует, экспериментировать с coarser far representation/LOD. Локальные edits, жидкости, alpha-cutout и light parity должны остаться корректны.
4. Binary greedy/packed quad или fast-first/refine — отдельный эксперимент, только если mesh compute/bytes оказываются bottleneck. Авторы meshing-работ прямо рассматривают компромисс размера mesh и latency; чужое время meshing не является прогнозом FPS этого проекта.[^17][^18]

Выход: снижение измеренной цены draw/upload или compute с pixel/geometry parity. Переход на Vulkan, полный octree rewrite и новый watchdog не являются обязательной частью плана.

### D7. Формальная приёмка и закрепление архитектуры — M/L

Зависит от реализованных путей D0–D6. Domain extraction можно вести по мере D4, не откладывая всю работу до конца.

- На immutable save copy: исходный west route, другой yaw/quadrant с отрицательным Z, camera return, stop-to-convergence, edit/remove emissive block, boundary edits, unload/reload, normal shutdown. Отдельно cold и warm, с непересекающимися intervals и явным cache state.
- Минимум три cold и три warm наблюдения на одинаковых условиях; если warm внутри одного процесса — учитывать корреляцию, не выдавать laps за независимые process runs. Для сравнений чередовать baseline/candidate, включать A/A контроль.
- Зафиксировать целевой hardware/SLA заранее. Возможный продуктовый ориентир 60 FPS не превращается в обещание «16.6 ms на любом устройстве». Отдельно публиковать p50/p95/p99 frame time, worst stalls, time-to-first-correct-paint, edit-to-correct-mesh и per-key oldest debt.
- GPU driver/world acceptance, CPU integration и unit gates независимы. Отсутствующая платформа означает «не проверено».
- Build-domain boundaries и reverse include policy становятся обязательными; diagnostics остаётся read-only. Удалить migration flags только после паритета и coverage всех backend paths.

Выход: воспроизводимая визуальная и performance приёмка текущей реализации, а не объявление закрытия по одному manual полёту или агрегатному `all_drawable_ok`.

## 7. Ближайшие commits и запреты на преждевременную оптимизацию

Практический старт: **(1)** fixtures для fail-open Q9/Phase57 и CI suffix branches; **(2)** regression/fix multi-batch publication; **(3)** regression/fix frustum extraction с сохранением guards; **(4)** cooldown key; **(5)** согласование dual-lane tests; **(6)** schedule substage tracing и lazy spawn readiness; **(7)** настоящий oracle/classification. Перенос ownership/admission после этого опирается на измеряемые контракты.

Не использовать как доказательство улучшения: нулевой shadow mismatch после отключения сравнения; малый oldest age, сброшенный чужим finish; lower wall при пропавшей геометрии; north proxy вместо west manual; уменьшенный render distance/скорость; повторение одного JSONL трижды; PASS policy classifier вместо renderer oracle. Сохранить текущий визуальный успех и объяснить его архитектурно — важнее продолжения серии локальных floor/order toggles.

## Приложение A. Воспроизводимость evidence

Полный source SHA: `420335e9f1965b911ea8d56228805f6f42e274e9`.

Считанный при аудите SHA256 `bin/Cubatarium.exe`: `7fae041bbef8934b809bc2f7a307f80270b91f28c9f5c438ad78a82c80776a7d`. Это identity лежащего на диске exe, **не автоматически доказанная связь** с историческим запуском 154921.

SHA256 raw perf 154921: `ea1043f5fe4b058f0b52969ad83e350c7399e22850316caa76956b2dacac535b`.

Диагностический [C++ repro](E:/Work/Home/Cubatarium/build/audit-20260914/Repro.cpp) и [изолированный CMake project](E:/Work/Home/Cubatarium/build/audit-20260914/CMakeLists.txt) находятся в build tree, не входят в production targets. В них подключена существующая GL mock fixture; production allocator/publication скомпилированы без изменений. Repro возвращает 0, когда **дефект воспроизведён**, а не когда correctness gate пройден.

```powershell
cmake -S build/audit-20260914 -B build/audit-20260914/out -G "Visual Studio 17 2022" -A x64
cmake --build build/audit-20260914/out --config Release --parallel 3
build/audit-20260914/out/Release/audit_repro.exe
ctest --test-dir build/desktop-msvc -C Release --output-on-failure
python tools/test_AnalyzePhase57Scorecard.py
python -X utf8 tools/audit/check_include_rules.py
```

Build tree может быть очищен обычной процедурой clean. Для постоянной защиты перенести соответствующие assertions в production regression targets в D1, не принимать диагностический exit code за тест исправности. Raw logs и предыдущие reports не редактировались.

## Источники и применимость

Первичные публикации и документация проверены 14 сентября 2026. Старые статьи используются для устойчивых математических/ownership принципов, не как свидетельство текущих сравнительных скоростей. Рекомендации для Cubatarium являются применением этих принципов к приведённому коду, а не утверждением, что другой движок решает все его задачи без адаптации.

1. Alexey Panteleev / NVIDIA, [Writing Portable Rendering Code with NVRHI](https://developer.nvidia.com/blog/writing-portable-rendering-code-with-nvrhi/), 20.08.2021. Resource lifetime, command-list ownership; N01/N05.
2. Filament authors, [FrameGraph](https://google.github.io/filament/notes/framegraph.html), актуальная документация. Явные dependencies/read-write/lifetime; N05/N11/D4, без требования внедрять Filament.
3. Epic Games, [Task Graph Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/task-graph-insights-in-unreal-engine-5), документация UE 5.8. Critical path и lifecycle timestamps задач; N03/D3.
4. GLM authors, [type_mat4x4.hpp, 1.0.3](https://github.com/g-truc/glm/blob/1.0.3/glm/detail/type_mat4x4.hpp). `operator[]` возвращает column; N02.
5. Fabian Giesen, [Frustum planes from the projection matrix](https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/), 31.08.2012. Вывод plane extraction из clip inequalities; N02.
6. Michael Wimmer, Jiří Bittner / NVIDIA GPU Gems 2, [Hardware Occlusion Queries Made Useful](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-6-hardware-occlusion-queries-made-useful), 2005. Visibility и latency запросов; N02/N04, исторические speedup numbers не используются.
7. Zylann / Voxel Tools, [Performance](https://voxel-tools.readthedocs.io/en/latest/performance/), разделы main-thread timeout и spatial locking; spatial lock revision 17.06.2023. N03/N10; engine-specific workaround не переносится автоматически.
8. RenderDoc authors, [Python API](https://github.com/baldurk/renderdoc/blob/v1.x/docs/python_api/index.rst), ветка v1.x. Возможность автоматизации GPU inspection/captures; D2, не готовый oracle.
9. C++ Core Guidelines, [CP.23: joining thread as a scoped container](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp23-think-of-a-joining-thread-as-a-scoped-container). Lifetime объектов worker; N05/N12.
10. Python Software Foundation, [heapq: Priority Queue Implementation Notes](https://docs.python.org/3/library/heapq.html#priority-queue-implementation-notes). Разделение identity/live entries и priority; N06, не рекомендация смены языка.
11. Zylann / Voxel Tools, [Development: Threads](https://voxel-tools.readthedocs.io/en/latest/development/#threads). Общий pool и приоритет ближнего mesh; N07/N10.
12. Intel oneTBB, [Create a Token-Based System](https://www.intel.com/content/www/us/en/docs/onetbb/developer-guide-api-reference/2022-1/create-a-token-based-system.html), 2022.1. Resource tokens на входе и возврат после retirement; N07/N10. Следует учитывать также buffered upstream objects.
13. Google Benchmark authors, [User Guide: repetitions and statistics](https://github.com/google/benchmark/blob/main/docs/user_guide.md), актуальная документация. Повторы и variance; N08/D0/D7. Не подмена world replay микробенчмарком.
14. Andrey Mokhov, Neil Mitchell, Simon Peyton Jones, [Build Systems à la Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf), ICFP 2018. Actual/dynamic dependencies и incremental recomputation; N09/D4.
15. Khronos Group, [glMemoryBarrier reference source](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glMemoryBarrier.xml), OpenGL 4.x. `GL_BUFFER_UPDATE_BARRIER_BIT` для shader→copy/get/update; N11.
16. GitHub, [Workflow syntax: branches and filters](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax), актуальная документация. Exact names и globs; N12.
17. Mikola Lysenko, [Meshing in a Minecraft Game](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/), 30.06.2012. Компромисс mesh size/latency; D6.
18. cgerikj, [Binary Greedy Meshing](https://github.com/cgerikj/binary-greedy-meshing), авторская реализация. Packed quads/bitwise meshing как условный эксперимент; D6, benchmarks проекта не экстраполируются на Cubatarium.

[^1]: Panteleev / NVIDIA, [NVRHI: resource lifetime](https://developer.nvidia.com/blog/writing-portable-rendering-code-with-nvrhi/), 2021.
[^2]: Filament authors, [FrameGraph](https://google.github.io/filament/notes/framegraph.html).
[^3]: Epic Games, [Task Graph Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/task-graph-insights-in-unreal-engine-5).
[^4]: GLM authors, [mat4 column access, v1.0.3](https://github.com/g-truc/glm/blob/1.0.3/glm/detail/type_mat4x4.hpp).
[^5]: Giesen, [Frustum planes from the projection matrix](https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/), 2012.
[^6]: Wimmer, Bittner, [Hardware Occlusion Queries Made Useful](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-6-hardware-occlusion-queries-made-useful), 2005.
[^7]: Voxel Tools, [Performance](https://voxel-tools.readthedocs.io/en/latest/performance/).
[^8]: RenderDoc, [Python API](https://github.com/baldurk/renderdoc/blob/v1.x/docs/python_api/index.rst).
[^9]: C++ Core Guidelines, [CP.23](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp23-think-of-a-joining-thread-as-a-scoped-container).
[^10]: Python documentation, [Priority Queue Implementation Notes](https://docs.python.org/3/library/heapq.html#priority-queue-implementation-notes).
[^11]: Voxel Tools, [Threads](https://voxel-tools.readthedocs.io/en/latest/development/#threads).
[^12]: Intel oneTBB, [Token-Based System](https://www.intel.com/content/www/us/en/docs/onetbb/developer-guide-api-reference/2022-1/create-a-token-based-system.html).
[^13]: Google Benchmark, [User Guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md).
[^14]: Mokhov et al., [Build Systems à la Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf), 2018.
[^15]: Khronos, [glMemoryBarrier](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glMemoryBarrier.xml).
[^16]: GitHub, [Workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax).
[^17]: Lysenko, [Meshing in a Minecraft Game](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/), 2012.
[^18]: cgerikj, [Binary Greedy Meshing](https://github.com/cgerikj/binary-greedy-meshing).

## Execution status (wall-diet track, 2026-09-14)

Plan: `.cursor/plans/west_cruise_wall_diet.plan.md`. Branch tip at freeze: `7a25581b`
(execution on `cursor_audit2_impl`; audit snapshot HEAD was `420335e9`).

### Wall-diet land vs reopen (bisect 200927→205626)

| ID | Status | Note |
|---|---|---|
| N02 row frustum | **KEEP** | B1 revert = disaster (chunk holes/black) |
| N06 structural CooldownKey | **KEEP** | landed A3 |
| N01 atomic publish (A1 `58f65ffe`) | **REOPEN→epoch-split→narrow any_fresh→full-cache dirty publish** | `134038` retain storm SoT (incomplete on frustum refs); expand + `cache⊆upload` |
| D3.3 lazy spawn (assume ready off-enter) | **cache+ring_dirty** | S3 cache; invalidate on `NeedsSpawnRingCatchUp` |
| N04 / sticky mid black | **REOPEN (T2 heal)** | H1–H4: no LegalDark greenwash; remesh/force/accept-refresh; SoT `204501`; autofly `213719` eye KEEP, stalled honest |
| N03 BeginFrame-before-stream | **done** | S4 landed |
| N05 per-key debt age | **PARTIAL** | class-level age; per-key deferred |
| N07/N08/N12 fail-closed pieces | N08 **eye_proxy mid-corridor** | fly-only stale med=2 missed mid=17 on autofly `095545` |
| A5 stream census diet | **deferred** | stop-line VB/stale |
| Product G1 / pixel oracle / Q9 acceptance | **OPEN** | — |

Follow-on plan: `.cursor/plans/n01_rework_mid-black.plan.md` /
`thrash_autopsy_autofly` (121131 autopsy).
Interim eye-safe WT (pre-N01-v2): A2+A3 ON, A1 OFF, eager ring, A4 telem ON.
Bisect reports: `bin/suite_reports/g1_a10_relight/bisect_b*_result.md`, `bisect_b3b_205626_result.md`.
Wall-diet reports: `bin/suite_reports/g1_a10_relight/wall_diet_*.json`.

### Follow-on land (n01_rework + v2.1 + thrash epoch-split after `102527`)

| Step | Status |
|---|---|
| S0 docs / reopen N01 | done |
| S0b `--visible` + dual-lane stop-line | done; mid stalled≤5 gate |
| S1 N01 v2 group-commit | done; epoch-split → **narrow: pubVer only on any_fresh** (C1 landed) |
| S2a `stale_vl_rev_n` / `fully_dark_census_n` | done |
| S2b / T2 mid FullyDark | **in progress** — H1–H4 landed (`5dc1eb4b`); H5 autofly `213719` eye PASS, legal_dark=0, stalled~64, VB~97; dual-lane/G1 OPEN; operator vs `204501` pending |
| S3 spawn-ring cache | done + ring_dirty invalidate |
| S4 BeginFrame before stream | done (diet deferred) |
| T0 eye_proxy stop-line | done → **mid-corridor + swap-without-holes** (N08) |

| Flight | Role | mid stale_visual | mid stalled | holes | note |
|---|---|---:|---:|---:|---|
| Autofly `095545` | pre-epoch postfix | 17 (fly med **2**) | 104.5 | 0 | adequacy PASS; fly eye_proxy blind |
| Manual `085208` | post-N01-v2 | — | ≈48 | 0 | eye FAIL |
| Manual `102527` | v2.1 thrash SoT | 8 (Δmed 3) | 54 | 0 | B4 swap/blink/per-block |
| Manual `121131` | **post-epoch regress** | **21.5** | 23.5 | 0 | worse thrash; T2 partial |
| Manual `134038` | **retain-storm SoT** | ~8.5 | — | 0 | overload retain mid≈40; meshRev lag≈535; C1 OK |
| Manual `160234` | **N01 retain closed** | 3.5 (Δ=3) | ~54 | 0 | incomplete=0; eye Δ FAIL → post-N01 thrash track |
| Autofly `173946` | **post-N01 P2 thrash** | 7 (Δ=0) | ~62–116 | 0 | eye_proxy PASS (mid≤8 class); T2 still OPEN |
| Autofly `202118` | **post-N01 P3 T2** | 5 (Δ=0) | **0** | 0 | equal-rev→LegalDark greenwash; VB~150; mid_stalled_gate must FAIL (`legal_dark_masks_stalled`) |
| Manual `204501` | **N04 greenwash SoT** | 8 (Δ=3) | ~0–1 | 0 | operator FAIL; legal_dark~84; incomplete=0 KEEP |
| Autofly `213719` | **N04 H1–H5 heal** | 8 (Δ=0) | **~63–70** | 0 | legal_dark=0; eye PASS; VB~97; stalled honest FAIL; G1 OPEN |

Scorecards: `n01_thrash_manual_102527.json`, `n01_thrash_manual_121131.json`,
`n01_thrash_postfix_cold_score.json`, `n01_retain_manual_160234.json`,
`post_n01_p2_final_af_score.json`, `post_n01_p3l_t2_af_score.json`,
`n01_manual_204501.json`, `n04_heal_h5_af_{cold,score}.json`.

**N01 full-cache dirty publish:** `RefreshPassRefs` expands dirty uploads from
full `GreedyCache` pass set; incomplete = `cache⊆upload` (not `resident⊆upload`);
telem split `publication_incomplete_material_n` / `publication_oom_retain_n`.
C1 KEEP; post-N01 P0–P2 thrash cut KEEP; **P3 LegalDark greenwash reverted (H1)**;
H2–H4 remesh/force/accept-refresh landed; H5 autofly eye KEEP, stalled debt visible.
eye_proxy incomplete mid≤5. G1 remains **OPEN** (VB/stalled above dual-lane).
Operator west retest still required vs `204501`.

**Merge signals (N08):** `adequacy_pass` ∧ `dual_lane_stop_line_pass` ∧
`eye_proxy_stop_line_pass` (mid-corridor `focus_cx∈[2,5]`, not fly-only;
stale mid≤8 thrash class, Δ≤1.5, holes blink, incomplete_material mid≤5) ∧
operator west eye. Adequacy alone is not merge-green. `near_focus_holes≈0` is
**not** proof of no per-block defects (missing-mesh telem only).
G1 / pixel oracle / Q9 remain **OPEN**.
