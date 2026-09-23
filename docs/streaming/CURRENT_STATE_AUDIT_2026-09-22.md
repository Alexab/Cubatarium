# Повторный аудит отображения и стриминга мира

> Это аудит базы `dcc02e27` от 22 сентября 2026 года, он сохраняет исторические
> доказательства и исходные findings. Актуальная оценка HEAD `243e817d` —
> [A31 re-audit](A31_REAUDIT_2026-09-23.md); следующий порядок работ —
> [A31 remediation plan](../../.cursor/plans/remediation_after_a30_open.plan.md).

Дата завершения: 22 сентября 2026. Данные: ручные и автоматические пролеты 21 сентября.

Проверенный код: `dcc02e27201542c1ef95ad87217af9843bd69368`, ветка `cursor_audit6_impl`.
Производственные исходники не изменялись. Созданы только документы и изолированные проверки аудита.

Продолжение: [детальный план доработки](E:/Work/Home/Cubatarium/docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md).
Доказательства: [метрики](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_21/metrics.json), [анализатор](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_21/analyze_current.py), [контрпримеры](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_21/CurrentContractRepro.cpp), [результаты проверок](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_21/test_results.md).

## 1. Вывод

Прогресс реален: исправлены конкретные ошибки времени жизни GPU-данных, пустой замены и переключения представлений. Возвращать всю кодовую базу на старый коммит не рекомендую. Но текущая система еще не обеспечивает согласованность между данными мира, запеченным освещением, опубликованным мешем и списками рисования.

Главный архитектурный недостаток — не отсутствие еще одного бюджета. Состояние готовности складывается из нескольких частично пересекающихся механизмов: ColumnRecord, Emerge/FSM, Dirty/RAA, PendingLight, GPU pending, SoftDefer, FaceDebt, enter-gate и статистических классификаторов. Иногда событие одного чанка завершает долг всей колонки; иногда постановка в очередь считается прогрессом публикации; иногда производный результат используется при изменившихся входах.

Найдены прямые нарушения контрактов:

1. Deadline-ветка пропускает отсечение без проверки актуальности камеры, обходя существующий `CullInputKey`.
2. Оптимизация прозрачности может бесконечно сохранять старый порядок после перемещения камеры.
3. Полный и инкрементальный снимки одного состояния мира дают разные правила видимости границы.
4. Инкрементальное обновление освобождает кредит памяти, оставляя снимок в кэше.
5. Ветвь выхода из ожидания GPU содержит несовместимые условия; зависание может только стареть.
6. Проверка материала сравнивает хеш всех материалов чанка с хешем одного прохода.

Все шесть нарушений проверены изолированным executable на уровне snapshot/store или production predicates. Это не шесть подтвержденных причин конкретного пикселя на видео: видеозаписи и покадровой GPU-трассы нет. Граница между доказанной ошибкой кода и ее вкладом в наблюдаемый дефект ниже указана явно.

Дополнительно: наиболее тяжелые рывки последнего ручного пролета связаны с картой поверхности жидкости, а не с длительным Kick. В одном кадре из 212.253 мс `fluid_map_cpu_ms` занимает 196.560 мс. Предлагаемый `RingReadinessBudget` этого механизма напрямую не устраняет.

## 2. Что и с какими ограничениями проверено

- Сопоставлены HEAD, оба указанных пользователем коммита, изменения после аудита 16 сентября и план `.cursor/plans/ring_readiness_budget.plan.md`.
- Повторно разобраны 17 исходных JSONL за 21 сентября, включая ручные `112357`, `145008`, `161139`, `183133` и группы AF v4/v5/v6/ownership.
- Прочитаны реализации snapshot/capture, публикации CPU/GPU, visibility/sorting, relight planner/executor, FaceDebt, fluid surface, admission и производителей/потребителей метрик.
- Пересобраны исполняемые файлы 32 зарегистрированных CTest-проверок и четыре дополнительные проверки. Недостаточно запускать старые бинарники: до пересборки `publication_audit` проходил; после пересборки падает.
- Проверен реальный GL driver-test существующего набора. Новый аудитный executable использует production snapshot/planner/policy-код, но не запускает игру.
- Проверены первичные источники по инкрементальным вычислениям, lifetime, budgeted work, voxel lighting и graceful degradation. Это выборочная предметная проверка, не формальное доказательство корректности всего движка.

В tracked-файлах исходного дерева изменений на момент начала не было; многочисленные существующие untracked-планы, отчеты и временные файлы сохранены. Старые perf JSONL за 18–20 сентября в проверенных папках `bin` и среди tracked perf-файлов не найдены. Для старых якорей доступны документы и scorecards, но не эквивалентная сырая покадровая выборка.

В perf нет надежного manifest с SHA бинарника, хешем мира и всех настроек. Поэтому привязка прогонов к этапам сделана по репозиторным evidence-документам и времени файлов; идентичность бинарника текущему исходнику для ручного `183133` не доказана. Игра для новых полетов не запускалась, пользовательский мир не изменялся.

## 3. Что означают два «лучших» коммита

| Точка | Что фактически закоммичено | Что можно заключить |
|---|---|---|
| `27beca1c` | Изменение `AUDIT16_S9_CLOSURE_MATRIX.md`; code anchors в нем — `c340efc2` prior-lit hold и `b17cc0dd` ring clamp | Подход мог давать более стабильную картинку. Сама матрица оставляет ручной left-black gate UNTESTED, dual-lane OPEN, FD stall около 61–65 |
| `dd7871ab` | Scorecards и evidence v3; production-изменения перед ним — `bfb4201d`, `7f90f8d3` | У AF v3 r2 mid FD-stalled 0–1 и меньший prior-lit hold. Warm eye-proxy FAIL; unfinished/hitch OPEN; operator UNTESTED |
| HEAD | После v4–v6 и ownership cutover | Уменьшился unfinished, но остались stalled/dark и тяжелые кадры; появились небезопасные visibility shortcuts |

Слова «black-clean anchors» в новом ownership-документе — более сильное утверждение, чем позволяют исторические matrices. Наблюдение пользователя важно, но его следует подтвердить A/B воспроизведением, а не подменять им проверку.

Особенно важен `83a7b8dd` после `dd7871ab`: в нем появились deadline-cull и transparent-sort shortcuts, рассмотренные в A21-01. Это конкретный кандидат регрессии, который надо проверить независимо от изменений relight. Файлы `MeshCaptureStore.cpp`, material gate и fluid cache не менялись в интервале `dd7871ab..HEAD`: найденные в них недостатки не следует объявлять новыми регрессиями именно этого интервала.

Рекомендация: сохранить HEAD как основу исправлений, а оба якоря — как отдельные visual baselines. Допустим временный выбор старого бинарника для демонстрации; он не является решением архитектурного долга.

## 4. Повторный анализ метрик

### 4.1 Методика

`period.wall_ms` — среднее за период, а не время последнего кадра. Ряд вложенных длительностей и счетчиков остается значением последнего кадра: `AverageFromSession` начинает с `avg = last` и усредняет только перечисленные поля. Нельзя складывать медианы вложенных таймеров и нельзя объявлять percentile по period-строкам кадровым P99.

Использованы отдельные выборки:

- Все period-строки — описание сессии.
- Движение: `2 < movement_speed < 20`; исключает стартовое значение скорости 1376.42, но не делает маршруты идентичными.
- Общий пространственный коридор: дополнительно `cx=-3..6`, `cz=3`, `y=48..65`.
- Spike-строки — конкретные тяжелые кадры; это отобранные события, не все кадры.
- `sampled_max` в приложении — максимум по имеющимся строкам любых типов, не гарантированный максимум каждого счетчика по всем кадрам.

В `183133` JSON повторяет 23 имени полей. Конфликтующих значений в проверенных логах не найдено; анализатор явно проверяет это и использует последнее значение, как стандартный JSON parser. Схему все равно надо исправить.

### 4.2 Ручные пролеты: одинаковый фильтр движения

| Пролет | Moving periods | Медиана period wall, мс | Медиана VB | Медиана FD-stalled | Max dark-face proxy в moving periods | Max unfinished в moving periods |
|---|---:|---:|---:|---:|---:|---:|
| `112357` | 15 | 18.01 | 45 | 13 | 106 | 79 |
| `145008` | 20 | 19.66 | 45 | 7.5 | 498 | 70 |
| `161139` | 12 | 17.76 | 48 | 11 | 103 | 67 |
| `183133` | 20 | 31.84 | 55 | 17.5 | 381 | 23 |

В общем коридоре взвешенное числом кадров среднее wall: `161139` 17.71 мс, `183133` 30.25 мс. Это свидетельство ухудшения в имеющихся данных, но не причинная оценка одного коммита: отличаются высоты, длительности остановок, состояние мира и кэшей.

Последний `183133`: 29 period, 10 spike, 7 blink, 1 shutdown. По period: holes=0, publication material flips=0, dual backend=0, `stale_vl_rev_n=0`; unfinished max=23. По всем наблюдениям: VB max=82, FD-stalled max=66, dark-face proxy max=468. Значения 381/62/81 в period и 468/66/82 в spike объясняют различия с исходным планом; смешивать их без указания выборки нельзя.

Нулевые holes и material-flip counters не доказывают отсутствие дыр или ошибочных пикселей: это ограниченные CPU/proxy-показатели. `PreferKick=0` тоже не означает отсутствие исполнения GPU: pending queued в period достигает 22. Нужна привязка pending и обязательства к одному chunk/job, а не сравнение общих счетчиков.

### 4.3 AF ownership не закрывает визуальную задачу

В raw moving-выборках ownership cold/warm/dive unfinished max = 25/25/19. Dark-face proxy max = 85/1618/1140. Медиана period wall = 33.27/28.56/22.09 мс. В evidence-документе другие выборки дают местами иные медианы; это не ошибка само по себе, но критерии должны фиксировать сегмент.

У `ownership_manual_183133_score.json` eye-proxy PASS, `merge_green=false`, operator UNTESTED и adequacy FAIL `miss_stuck_too_low`. Получился важный парадокс приемки: улучшение времени зависания способно сделать тест «неадекватным». Подробности A21-10.

### 4.4 Где именно возникают рывки

| Пролет / wall | Главные измеренные области в том же spike | Вывод |
|---|---|---|
| `183133`, 212.253 мс | fluid CPU 196.560; world streaming 7.959; scene 3.310 | Синхронная работа fluid surface, не Kick |
| `183133`, 203.536 мс | fluid CPU 175.223; world streaming 14.661 | Тот же независимый класс |
| `183133`, 173.030 мс | fluid CPU 135.015; world streaming 26.150 | Сочетание fluid и streaming |
| `183133`, 100.913 мс | world streaming 90.008; fluid CPU 0.0004 | Отдельный streaming hitch; нужен finer trace |
| `145008`, 182.114 мс | transparent 155.079; world streaming 18.256 | Другой класс: прозрачный проход / driver work |
| `183133`, первый кадр 254.243 мс | fluid CPU 77.033; app update 95.921; render total 138.554 | Стартовый hitch; не смешивать с cruise |

В последнем пролете 7 из 10 сохраненных spike-кадров содержат более 80 мс fluid CPU. Этот таймер охватывает CPU staging и получение slice; он не является чистым GPU execution time. На desktop `TryGpuScanFluidColumns` вызывает CPU scan, несмотря на имя; приписывать эти 197 мс синхронному GPU readback неправильно.

Обычные кадры тоже не стали достаточно дешевыми: в последней сессии медиана period world-streaming 16.45 мс и scene 4.12 мс. Оптимизация только редких пиков не обеспечит устойчивые 60 FPS.

## 5. Находки в коде

Приоритет P1 — исправлять до нового quality controller; P2 — обязательный системный долг/верификация. «Доказано» ниже означает контракт или измерение, а не автоматически установленную причину всех визуальных жалоб.

### A21-01 — P1. Небезопасное повторное использование visibility и transparent order

Код: [GpuPassRefreshPolicy.h:235](E:/Work/Home/Cubatarium/src/Render/Camera/GpuPassRefreshPolicy.h:235), [GeometryEngine.cpp:1982](E:/Work/Home/Cubatarium/src/Render/Engine/GeometryEngine.cpp:1982), [GeometryEngine.cpp:2402](E:/Work/Home/Cubatarium/src/Render/Engine/GeometryEngine.cpp:2402).

Обычные ветки reuse проверяют `CullInputKey`. Новая deadline-ветка добавлена через OR и проверяет лишь remaining time, active compact и несколько debt-флагов. При изменившейся камере и старом compact допускается skip. Это нарушает строгий контракт ключа; старый результат отсечения не обязан содержать вновь видимые объекты. При повторяющемся дефиците времени дефект может сохраняться.

Transparent skip проверяет неизменность меша/refs и `prev_cmd_reorder_n==0`, но не camera sort revision. После нулевого reorder можно каждый кадр переиспользовать старый порядок, снова получать нулевой reorder и никогда не пересортировать стабильную сцену при движении камеры. Это источник неправильного alpha blending; не доказательство порчи material ID.

Контрпримеры воспроизводят оба разрешающих решения production predicates. Pixel/driver воспроизведение именно этих сценариев еще требуется.

Решение: reuse только при равенстве полного ключа либо доказанном консервативном покрытии; при истекшем бюджете — корректный fail-open кандидатный набор, а не старая отрицательная видимость. Для прозрачности — актуальный ключ порядка и bounded update. Экономию получать уменьшением стоимости/числа кандидатов и раздельными geometry/order buffers. Обоснование: зависимости производных результатов [S1], lifetime/order ресурсов [S2].

### A21-02 — P1. Snapshot имеет два несовместимых контракта границы

Код: [ChunkMeshSnapshot.cpp:197](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshSnapshot.cpp:197), [MeshCaptureStore.cpp:200](E:/Work/Home/Cubatarium/src/Render/Mesh/MeshCaptureStore.cpp:200), [ChunkMeshCache.cpp:3469](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:3469).

Полный Capture устанавливает Missing только для отсутствующего соседа. IncrementalShell делает это для `!loaded || !visually_drawable`. При одинаковом мире загруженная пустая соседняя ячейка — Air в первом случае и Unknown во втором; Unknown подавляет грань. `InputsStillValid` при этом возвращает true. Контрпример использует настоящий store и world, а не переписанную модель.

`BoundaryOverlayState` по названию — независимые временные грани, но фактически это маска внутри snapshot, влияющая на обычный mesher. Отдельного renderable seam layer данная структура не создает. Комментарий «Unlit emits» тоже недостаточен: обычный mesher после проверки state читает raw block, который может оставаться твердым. Тест типа enum не проверяет итоговую геометрию.

Решение: единый snapshot контракт; постоянная геометрия зависит от voxel/material/light данных, временное покрытие — отдельный versioned output. Если отдельные seam meshes не вводятся сразу, все влияющие на геометрию входы обязаны участвовать в валидации. Нельзя одновременно менять результат по drawable и считать эту зависимость отсутствующей. [S1, S3].

### A21-03 — P1. FaceDebt и ready имеют неверную гранулярность

Код: [ColumnRecord.h:70](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnRecord.h:70), [ChunkEmergeCoordinator.cpp:503](E:/Work/Home/Cubatarium/src/World/Streaming/ChunkEmergeCoordinator.cpp:503), [ChunkEmergeCoordinator.cpp:600](E:/Work/Home/Cubatarium/src/World/Streaming/ChunkEmergeCoordinator.cpp:600).

Шесть битов долга общие для всех Y-чанков колонки. Публикация одного lit drawable вызывает `ClearFaceDebt(col)` и может ставить колонку в RenderReady. FirstDrawable callback также очищает весь долг своей колонки, причем после записи маски overlay в GPU commit. Следовательно, это не доказательство отсутствия долга у остальных slices.

`BecameKnown` фактически привязан к `!had_mesh`, не к каждому переходу peer light/coverage generation. Он обходит четыре горизонтальных соседа; Y-соседи отсутствуют. Фильтры моря и ring<=4 ограничивают repair, но не дают общего контракта сходимости для скал, подземелья и вертикального движения. Новая маска при уже опубликованном peer может не получить будущего first-drawable события.

Решение: debt keyed by `(worldEpoch, chunkXYZ, face, requiredPeerGeneration)`, clear только при commit соответствующего результата; состояние колонки — агрегат по требуемым chunks, а не событие «хотя бы один готов». Подписки на изменение геометрии/света/coverage generation всех нужных соседей, с level-triggered reconciliation против потерянного события. Применимы dependency tracking [S1] и neighbor update/coalescing [S3].

### A21-04 — P1. «Владелец прогресса» не гарантирует продвижение

Код: [RelightInstallPlanner.h:273](E:/Work/Home/Cubatarium/src/World/Streaming/RelightInstallPlanner.h:273), [RelightFifoPolicy.h:741](E:/Work/Home/Cubatarium/src/World/Streaming/RelightFifoPolicy.h:741), [MarkRelitInstall.cpp:119](E:/Work/Home/Cubatarium/src/World/Core/MarkRelitInstall.cpp:119), [ColumnFlowExecutor.cpp:995](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnFlowExecutor.cpp:995).

В `TryPreferKickOrForceDirty` вызов `ShouldForceDirtyAfterPreferKickStall` находится под `!pending_gpu_or_raa`; сам predicate требует pending. Эта альтернатива недостижима. Для dirty+pending+no-progress даже age=100 остается только `note_prefer_kick_stall`. Это воспроизведено. Не следует механически разрешать повторный Dirty поверх живой GPU-работы: сначала определить, что pending означает и кому принадлежит job.

`MarkDirty*` не возвращает успешность перехода; executor все равно увеличивает `dirty_admitted_n` и вызывает `NotePublishProgress`. Повторная постановка, RAA и продвижение до GPU не разделены. `publish_progress_frames` и stall increments — числа событий, не elapsed frame time; прогресс одного slice распространяется на все slices колонки. Проверка ticket-without-fifo вызывается уже после раннего skip для существующего ticket/progress, с hardcoded ticket=true и заменой FIFO на исторический publish progress. Поэтому ее название сильнее фактической защиты.

Решение: per-job monotonic stages, очередь с одним active attempt и одним latest desired revision; pending без исполнителя запрещен, cancellation сопровождается epoch/generation. Возраст — время с последнего реального stage/revision advance, не вызовов MarkRelit. Состояние долга сохраняется при budget denial; бюджет ограничивает выполнение, а не существование обязательства. [S1, S3, S4].

### A21-05 — P1. Material validation не является строгой проверкой публикации

Код: [ChunkMeshCache.cpp:5239](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:5239), [GreedyGpuPublication.cpp:443](E:/Work/Home/Cubatarium/src/Render/Engine/GreedyGpuPublication.cpp:443), [MeshPublishContract.h:57](E:/Work/Home/Cubatarium/src/Render/Mesh/MeshPublishContract.h:57).

Expected material hash строится из всех CPU batches чанка. Got hash — из `group_fresh` одного pass. При наличии opaque+transparent материалов сравниваются разные множества; легальная замена с изменившимся blockId может быть отвергнута. Хеш также зависит от порядка. Контрпример проверяет production hash/helper; полный mixed-pass GL integration пока не добавлен.

`ShouldAcceptMeshPublish`, проверяющий geom/light/material, используется тестами, но production writer вызывает другой helper. `got.light_rev` просто копируется из expected, geom в этом helper не сравнивается; путь packed имеет отдельные проверки и не проходит через этот material gate. Поэтому зеленый unit «accept matching revs» не доказывает строгий production Accept. `any_fresh=false` на отказе одного material group также стирает накопленный признак прогресса других групп — это надо включить в transaction tests.

Решение: immutable publication manifest с идентичностью артефакта, source stamps и exact per-pass batch descriptors; отдельно checksum байтов. Разрешать легальную смену материала при изменившемся world revision. Сравнивать descriptor с источником именно данного upload, а не запрещать изменение индекса batch. Atomic publication и lifetime [S2, S5]; корректный ключ производного результата [S1].

### A21-06 — P1. Retain не всегда сохраняет обязательство обновления

Код: [ChunkMeshCache.cpp:3843](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:3843), [ChunkMeshCache.cpp:4890](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:4890).

Успех: RetainedPrior больше не считается опубликованным Completed; source light revision не заменяется безусловно текущим world revision. Но D4 на accepted-stale drawable возвращает false без Dirty/FaceDebt. Сохранить старое изображение безопаснее неполной замены, однако liveness корректна только если отдельный owner уже гарантирует свежий successor. Этот контракт на уровне Return/Result не выражен.

Проверить сценарий: light изменился после snapshot, кандидат удержан, другой scheduler считает свою задачу завершенной, следующего light события нет. Это риск, установленный по пути кода, а не новый воспроизведенный в полете deadlock.

Решение: `Published / RetainedAwaitingSuccessor / RejectedRetryable / CancelledSuperseded` с required successor generation. Completion работы и удовлетворение visible obligation — разные факты. Retain не должен сам запускать многократные remesh, но не может молча уничтожать долг. [S1, S3].

### A21-07 — P1. Нулевой свет используется как признак неготовности

Код: [ChunkMeshCache.cpp:919](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:919), [MeshLightStalePolicy.h:19](E:/Work/Home/Cubatarium/src/World/Streaming/MeshLightStalePolicy.h:19), [VisibleBlackAttribution.h:34](E:/Work/Home/Cubatarium/src/World/Streaming/VisibleBlackAttribution.h:34).

`BatchesHaveFullyDarkFace` возвращает true уже на одном vertex с sky=block=0, кроме нижней грани. Это не «весь чанк черный» и даже не обязательно вся грань. `IsMeshLightStaleGpu` считает dark flag stale даже при равных revisions. LegalDark-классификация опирается на отсутствие tickets/progress, а не доказательство корректного светового решения. Планировщик и диагностика оказываются взаимозависимыми.

`dark_face_stale_near_n` тоже имеет разные единицы: CPU-путь считает vertices в радиусе, packed GPU-путь — chunks. Переход backend способен изменить величину без сопоставимого изменения картинки. В последнем пролете `stale_vl_rev_n=0` при большом dark census: это не доказательство ни корректного света, ни необходимости безусловного remesh всех этих чанков; недостаточны halo-light/provenance/coverage доказательства.

Решение: раздельные `LightValidity`, версия light field и необязательный observational dark census; нулевое значение допустимо в валидной пещере. Mesh freshness — по всем реально прочитанным light dependencies, включая halo. Черный артефакт — расхождение ожидаемого/отрисованного света, а не сам ноль. Flood fill с отдельными sky/block channels [S6] не требует приравнивать темноту к отсутствию работы.

### A21-08 — P1. Fluid surface: дорогой синхронный rebuild и неполный cache key

Код: [FluidSurfaceMap.cpp:260](E:/Work/Home/Cubatarium/src/Render/Engine/FluidSurfaceMap.cpp:260), [FluidSurfaceColumnSlice.cpp:57](E:/Work/Home/Cubatarium/src/Render/Mesh/FluidSurfaceColumnSlice.cpp:57), [GpuFluidColumnScan.cpp](E:/Work/Home/Cubatarium/src/Render/Mesh/GpuFluidColumnScan.cpp).

`patch_one` синхронно получает slice; cache miss строит flags проходом по height*16*16 ячейкам, затем хеширует их. Даже pack-cache hit найден лишь после этого прохода. Численный лимит chunks/frame не ограничивает длительность chunk job. Fluid work не подчиняется общему `FrameDeadline`.

В static `FluidPackReuseCache` хеш включает бинарный признак «жидкость», height/y_min, но возвращается весь slice с `FluidId`. Замена water→lava при прежней геометрии сохраняет flags/hash, следовательно, может вернуть старый FluidId. Reset кэша найден только в тестах; world/catalog identity в ключе нет. Это отдельный риск некорректной жидкости между загрузками/изменениями материалов, не доказанная причина всех сообщений о подмене текстур.

Решение: versioned per-column fluid summary, обновляемый по изменившимся voxel/fluid данным, асинхронный bounded rebuild на snapshot, main-thread upload только готового результата. Ключ включает мир, incarnation/content/fluid/catalog versions и scan-domain. Occupancy/tops можно переиспользовать отдельно от material IDs. Scroll — tiled/toroidal working set и только вошедшие полосы. Подход incremental clipmaps применим к 2D surface map [S7]; перенос полного heightfield renderer на объемный мир с пещерами не предлагается. Main-thread time budget и размер неделимой работы — [S8].

Дополнительный платформенный риск: Android-путь dispatch→map использует только SSBO barrier перед CPU mapping. Для такого потребителя нужен соответствующий buffer-update barrier и корректный completion; это не причина desktop-пролета. Проверить по [S9].

### A21-09 — P1/P2. Бюджет есть, но учет ресурса и единица работы неполны

Код: [MeshCaptureStore.cpp:206](E:/Work/Home/Cubatarium/src/Render/Mesh/MeshCaptureStore.cpp:206), [FrameDeadline.h:82](E:/Work/Home/Cubatarium/src/Core/FrameDeadline.h:82), [AsyncMeshBuilder.cpp:91](E:/Work/Home/Cubatarium/src/Render/Mesh/AsyncMeshBuilder.cpp:91).

После обычного Capture credit живет с store entry — это правильное исправление прежнего аудита. Но IncrementalShell вызывает Commit без передачи guard. Замена entry уничтожает старый guard; store продолжает хранить snapshot без учета. Реальный контрпример: 65536 bytes → 0, `TryGet` успешен.

One-critical-unit-after-deadline лучше бесконечного bypass, но единица не имеет верхней границы длительности. Прямой focus bypass Dirty admission и множество отдельных caps не образуют end-to-end гарантии. Заявленный общий worker slot envelope применяется только в AsyncMeshBuilder: production вызовов из relight/gen не найдено. Не утверждается, что именно oversubscription вызвал измеренный hitch — это неполнота контракта ограничения.

Решение: учитывать реальные payload lifetimes, переносить ownership кредита при refresh, разделить storage bytes и queued/executing work slots. Единый scheduler выполняет bounded units по стоимости, downstream capacity и приоритету; спрос сохраняется отдельно. Для тяжелых работ нужны continuation/slices, не еще один `max(1, budget)`. [S5, S8].

### A21-10 — P1 для приемки. Метрики и тесты пока не доказывают закрытие задачи

Код: [FramePerfMonitor.cpp:2425](E:/Work/Home/Cubatarium/src/World/Diagnostics/FramePerfMonitor.cpp:2425), [flight_sim_run.py:192](E:/Work/Home/Cubatarium/tools/flight_sim_run.py:192), [n01_v21_scorecard.py:165](E:/Work/Home/Cubatarium/tools/n01_v21_scorecard.py:165).

Исправлено: `operator_visual=None` теперь UNTESTED, west coverage обязателен, нельзя получать PASS лишь из постоянно открытой дыры без абсолютного ограничения. Осталось: `merge_green` зависит от adequacy, которая требует минимум missing/stuck/VB. Она проверяет воспроизведение класса старой проблемы и не годится как обязательное свойство исправленного движка.

Eye-proxy не считывает материал/глубину/свет пикселя. Некоторые поля — события пайплайна, часть — census, единицы темных поверхностей смешаны. Из этих счетчиков нельзя закрывать подмену текстур и мигание, возникающее между period samples.

После пересборки CTest: 31/32 PASS; `publication_audit` FAIL на order-only publication epoch. В коде это намеренно измененный контракт: reorder не повышает publicationVersion, а MDI имеет отдельное восстановление таблицы. Поэтому падение не доказывает dangling memory; оно доказывает неразрешенное противоречие production и regression test. Требуется явное разделение geometry epoch, resident table epoch, command/order epoch и проверка каждого потребителя.

Вне CTest `relight_install_planner_test` FAIL: `P7: skip remesh when FullyDark light rev matches`. Это еще одно неразрешенное противоречие политики и теста, не повод просто удалить assertion. Characterization, fluid faces и fluid pack reuse проходят. Для старых binaries результат отличался — обязательна freshness сборки.

Решение: manifest + покадровая трасса + независимые coverage/material/depth/light проверки; тесты delayed/reordered completions, двух Y-slices, разных passes, budget exhaustion, stop/turn/teleport. Adequacy только по входной нагрузке/маршруту, не по наличию дефекта. Валидация: [S4, S10], инварианты зависимостей: [S1].

## 6. Что следует сохранить

| Изменение | Вердикт |
|---|---|
| Pooled batch retention, allocation generation, stale Free rejection | Сохранить; прежние alias/double-free контрпримеры теперь не воспроизводятся |
| Authoritative empty replacement; explicit Remove / RepresentationSwitch | Сохранить; соответствующие production repro проходят |
| Source light provenance вместо записи текущей revision поверх старой | Сохранить; расширить до полного dependency manifest |
| RetainedPrior != Completed | Сохранить; добавить successor/liveness контракт |
| Frustum extraction из строк матрицы, строгий CullInputKey | Сохранить; убрать новый небезопасный обход ключа |
| Разделение постоянной геометрии и временного покрытия как идея | Верно, но нынешний BoundaryOverlay — еще не независимый render output |
| Snapshot guard, общий deadline, bounded critical unit | Верное направление; refresh leak и неделимые операции не закрыты |
| Operator UNTESTED, абсолютные hole checks и route coverage | Сохранить; убрать symptom-dependent adequacy из product acceptance |
| Census не должен напрямую запускать repair storm | Сохранить; demand возникает из версий/обязательств, не из цвета |

## 7. Оценка ring_readiness_budget.plan.md

Исходный файл не изменялся. Согласен с необходимостью единой политики качества и backpressure. Не согласен с порядком и рядом критериев.

| Предложение | Оценка и необходимая корректировка |
|---|---|
| «Дыры CLOSED» из holes=0 / SoftDefer=0 | Только наблюдение proxy на данном прогоне. A21-01/02 оставляют воспроизводимые пути потери видимости |
| Корень — hardcoded lit ring=4 | Рабочая гипотеза нехватки capacity, не доказанный корень всех дефектов. Ошибки ключей, lifetime и fluid rebuild независимы |
| RingReadinessBudget первым | Перенести после контрактов, диагностики и bounded work. Он должен заменить старые решения, не стать еще одним owner над ними |
| Сжать lit ring 4→2 и считать отсутствие black только внутри него успехом | Недопустимо как единственный критерий: текущий hide predicate вне ring позволяет темный draw. Нужны fallback/coverage во всем согласованном видимом объеме и отдельная метрика деградации качества |
| Сжать ring по FD-stalled и текущему RemainingMs | Сигналы частично обусловлены самим планировщиком и моментом вызова. Использовать измеренную стоимость, queue age/arrival/service, память и готовый coverage frontier; hysteresis и rate limits |
| PreferKick>0 when pending | Не цель. Pending может быть kicked, RAA или чужой chunk. Цель — завершение именно нужного job/обязательства в ограниченное время |
| SoftDefer-until-lit как dark-face решение | Без схемы зависимостей может вернуть взаимное ожидание/дыры. Light validity и neighbor coverage должны быть раздельными |
| dark max<=103 | В текущем mixed-unit proxy не годится для SLA. Нужны одинаковая единица, область, backend и независимая проверка |
| Hitch C KEEP | Частично отклонить: camera-key bypass и camera-blind transparent skip небезопасны |
| Wall max trend вниз | Недостаточно: отдельно startup/cruise/turn/stop, frame distribution и time-to-ready. Fluid surface — отдельная обязательная фаза |

Кольца надо разделить: collision safety, required visible coverage, prefetched voxel/light halo, retained GPU working set. Одно число не может безопасно выражать все эти множества. Адаптивное качество разрешено лишь поверх корректного fallback и с нижней границей игрового качества, а не через переименование плохих чанков во «вне обещания».

## 8. Research: что переносить в этот движок

Это не утверждение, что единственный алгоритм является SOTA для всех voxel engines. Выбраны первичные материалы с применимыми контрактами. Конкретная схема доработки — инженерный вывод данного аудита.

| ID | Изученный первичный источник | Применение и предел применимости |
|---|---|---|
| S1 | Mokhov, Mitchell, Peyton Jones, ICFP 2018: [Build Systems à la Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf), §§3.6–4 | Разделить dependency correctness и scheduling; cache hit должен соответствовать повторному вычислению. Аналогия для incremental engine, не готовый renderer |
| S2 | Google: [Filament FrameGraph](https://google.github.io/filament/notes/framegraph.html) | Явные read/write dependencies, versioned resources и lifetime. Внедрять маленький publication graph, не копировать весь Filament |
| S3 | Luanti: [mesh_generator_thread.cpp](https://raw.githubusercontent.com/luanti-org/luanti/master/src/client/mesh_generator_thread.cpp) | Coalesce mesh demand, запрет двух одновременных builders одного блока, pinned input blocks, обновления соседей. Исходник содержит собственные ограничения; не объявлять его безошибочным эталоном |
| S4 | Epic: [Task Graph Insights](https://dev.epicgames.com/documentation/unreal-engine/task-graph-insights-in-unreal-engine-5) | Отдельные timestamps create/launch/start/finish/complete, связи prerequisites и critical path. Здесь использовать собственные spans/Tracy, не требуется Unreal |
| S5 | NVIDIA: [Writing Portable Rendering Code with NVRHI](https://developer.nvidia.com/blog/writing-portable-rendering-code-with-nvrhi/) | Ресурс удерживается также submitted command list до fence completion. Применить принцип ownership к pool, pending publication и retirement; API миграция не требуется |
| S6 | Mikola Lysenko: [Voxel lighting](https://0fps.net/2018/02/21/voxel-lighting/) | Sky/block fields, инкрементальное распространение и word-level optimization. SIMD — после корректности границ и validity |
| S7 | Losasso/Hoppe, SIGGRAPH 2004: [Geometry clipmaps](https://hhoppe.com/proj/geomclipmap/); Asirvatham/Hoppe: [GPU-based geometry clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry) | Инкрементальные полосы, ограниченный working set, плавная деградация. Прямо полезно fluid surface map; не замена объемной геометрии пещер |
| S8 | Zylann Voxel Tools: [Performance](https://voxel-tools.readthedocs.io/en/latest/performance/) | Main-thread time budget, размер неделимой работы, ограничение workers, создание/удаление мешей и driver stalls. Численные defaults не переносить без замеров |
| S9 | Khronos: [glMemoryBarrier reference](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glMemoryBarrier.xml) | Barrier выбирается по следующему потребителю, SSBO barrier не универсален; completion и visibility — разные контракты |
| S10 | Google Benchmark: [User guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md), repetitions / random interleaving | Повторные сопоставимые прогоны, отделение cold/warm и влияние состояния машины; игровой acceptance требует еще визуальных проверок |
| S11 | Epic: [Virtual Texture Memory Pools](https://dev.epicgames.com/documentation/unreal-engine/virtual-texture-memory-pools-in-unreal-engine) | Residency pressure можно уменьшать явным снижением качества. Здесь переносится принцип измеримого trade-off, не virtual texturing как лечение voxel holes |

Полный Nanite PDF найден, но инструмент не загрузил его из-за размера; он не используется как доказательная основа. Страница Khronos Buffer Object Streaming вернула 403 при открытии; выводы о barriers опираются на доступный первичный reference S9, а lifetime — на S5.

## 9. Итоговое решение

Продолжать от текущего кода, но сменить направление следующего этапа: сначала воспроизводимые инварианты и единый контракт world→artifact→publication→draw, затем bounded pipeline и fluid surface, затем адаптивное качество. Безусловное сохранение всех «KEEP» из прежнего плана не обосновано.

Не обещать исчезновение всех артефактов по снижению VB или unfinished. Закрытие задачи требует независимого визуального результата на воспроизводимом маршруте, корректного поведения при исчерпании бюджета и измеренной сходимости после остановки.
