# План системной доработки отображения и стриминга мира

> Архитектурные контракты и общий порядок фаз остаются reference. Текущие
> execution statuses и конкретный successor для HEAD `243e817d` приведены в
> [A31 re-audit](A31_REAUDIT_2026-09-23.md) и [A31 remediation plan](../../.cursor/plans/remediation_after_a30_open.plan.md).

Дата: 22 сентября 2026. База: `dcc02e27201542c1ef95ad87217af9843bd69368`, `cursor_audit6_impl`.

Основание: [текущий аудит](E:/Work/Home/Cubatarium/docs/streaming/CURRENT_STATE_AUDIT_2026-09-22.md). Идентификаторы A21-01…10 и исследовательские источники S1…11 относятся к нему. Все этапы ниже — предложения, не выполненные изменения production-кода. Произведены только аудит, сборка проверок и подготовка доказательств.

## 1. Решение и границы

Продолжать от HEAD. Не откатывать все изменения к `27beca1c` или `dd7871ab`: оба коммита преимущественно фиксируют документацию/evidence, а текущий код содержит полезные исправления ownership и publication. Старые состояния нужны для воспроизводимого A/B, а не как доказанная корректная архитектура.

Цель: отсутствие необъяснимых дыр, неверных материалов и мигания при движении/редактировании; конечная сходимость геометрии и света; контролируемая нагрузка без длинных синхронных операций на основном потоке. Снижение census не является самостоятельной целью.

`RingReadinessBudget` из [пользовательского плана](E:/Work/Home/Cubatarium/.cursor/plans/ring_readiness_budget.plan.md) преобразовать в поздний controller качества и admission. До него исправить зависимости, публикацию, учет ресурсов и bounded work. Не принимать результат только внутри уменьшенного самим controller кольца.

Не требуется немедленно менять OpenGL на Vulkan, greedy meshing на SVO/Nanite или запеченный свет на отдельную GPU light volume. Это самостоятельные дорогостоящие решения, не устраняющие ошибки владения и версий автоматически. Допустимость большого рефакторинга означает возможность убрать конфликтующие владельцы состояния, а не необходимость переписать весь renderer.

## 2. Целевые контракты

### 2.1 Единица идентичности и производный артефакт

Единица correctness — chunk XYZ с incarnation в определенном мире, а не колонка XZ. Колонка остается единицей группировки загрузки/приоритета, но ее readiness вычисляется по набору необходимых chunks.

Предлагаемый `ArtifactManifest` содержит:

- World/session epoch, chunk XYZ и incarnation: исключают поздний результат после unload/reload или смены мира.
- Content/material-catalog revisions и версии всех действительно прочитанных соседних данных. Версия центра не доказывает актуальность halo.
- Light field dependencies, если свет запечен в вершины; light validity отдельно от численного значения света.
- Representation/backend, artifact generation, точные per-pass descriptors и фактические source stamps. Backend не создает отдельную истину о желаемом мире.
- Для временного boundary coverage — отдельный seam manifest и поколения peer coverage. Не добавлять renderer visibility в ключ постоянной геометрии, если она от этого действительно не зависит.

Checksum подтверждает соответствие содержимого, но не заменяет идентичность источника. Material ID не обязан совпадать с прежним ID того же batch index: редактирование мира законно меняет материал. Хеши сравниваются только в одинаковом домене, с определенной семантикой порядка.

Обоснование: корректность incremental computation — соответствие повторному вычислению по тем же зависимостям, а scheduling — отдельный вопрос ([Build Systems à la Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf), S1).

### 2.2 Спрос, работа и опубликованное состояние — разные сущности

Предлагаемый per-chunk record:

- `desired`: требуемые версии геометрии, света и coverage; переживает отказ admission.
- `activeAttempt`: один job с входным manifest, stage, владельцем исполнения и временем последнего реального продвижения.
- `queuedLatest`: одно объединенное последнее требование, без очереди одинаковых Dirty-запросов.
- `published`: фактически установленный artifact/manifest и удерживаемый fallback, если он нужен.
- `waitingDependencies`: точные отсутствующие версии, а не исторический флаг «прогресс был».

Повторный запрос уже желаемой версии — no-op; новая версия объединяется в latest demand. Завершение job не означает удовлетворения demand. Отказ бюджета не означает отмены demand. Отмена/устаревший completion не может завершить более новый job.

Результаты установки: `Published`, `RetainedAwaitingSuccessor`, `RejectedRetryable`, `CancelledSuperseded`. При Retain должен существовать successor или явная зависимость, которая его разрешит; иначе это нарушение liveness. Не компенсировать его бесконечной повторной постановкой Dirty.

Stage timestamps: created → admitted → started → built → uploaded → published → retired. Только реальный переход stage/required revision обновляет progress time. Dirty/RAA, счетчик вызовов MarkRelit и публикация другого Y-slice не являются прогрессом данного обязательства.

Практические аналоги: coalescing и single in-flight в [Luanti mesh queue](https://raw.githubusercontent.com/luanti-org/luanti/master/src/client/mesh_generator_thread.cpp), S3; причинные timestamps в [Task Graph Insights](https://dev.epicgames.com/documentation/unreal-engine/task-graph-insights-in-unreal-engine-5), S4.

### 2.3 Coverage и свет

Постоянный mesh вычисляется из согласованного data snapshot. Loaded/unloaded, light known/unknown и drawable/hidden не взаимозаменяемы. Snapshot full и incremental обязаны быть эквивалентны при одинаковых входах.

Для границы выбрать и зафиксировать в ADR один контракт:

1. Постоянная геометрия не зависит от drawable соседа; временное покрытие — отдельный mesh/output с собственными версиями и правилами удаления.
2. На время миграции единый mesh допускается, но все влияющие на него boundary/coverage inputs входят в manifest и invalidation.

Целевой вариант — первый. Нельзя назвать маску snapshot «отдельным overlay» и оставить ее неучтенной зависимостью обычного mesher.

Долг границы: `(world, chunkXYZ, face, requiredPeerGeneration)`. Закрывает его commit результата, удовлетворяющего этому требованию, а не любое событие готовности колонки. Учитывать все шесть направлений и вторичные зависимости, реально читаемые алгоритмом освещения/AO. На подписку/commit выполнять level-triggered reconciliation: если peer стал готов до регистрации долга, событие не должно потеряться.

Избежать циклического ожидания «оба соседа станут drawable сначала»: readiness данных строится из конечного voxel/light halo и определенных граничных условий. Если нужных данных нет, остается явное provisional coverage с ограниченным сроком и качеством, а не статус Ready. Для прозрачной воды нельзя без проверки переносить правила непрозрачной заглушки.

Ноль sky/block light — допустимый результат. `LightValidity` выражает соответствие решению на нужных входах, не отсутствие черных вершин. См. [Voxel lighting](https://0fps.net/2018/02/21/voxel-lighting/), S6.

### 2.4 Публикация и команды рисования

Единый путь CPU/GPU: построить immutable candidate → проверить его собственный manifest → подготовить все необходимые passes → установить выбранное представление → освободить прежнее после окончания GPU-потребителей.

Логическая атомарность означает, что renderer не увидит смесь несовместимых passes разных поколений. Это не требует ожидать fence на основном потоке. Допустимые частичные обновления должны явно описывать совместимость, например order-only изменение без смены geometry artifact.

Разделить поколения:

| Версия | Меняется при | Что инвалидирует |
|---|---|---|
| Artifact generation | Изменении геометрии/material/light payload | Ссылки на artifact, descriptors и производные команды |
| Resident table revision | Изменении таблицы диапазонов/индексов | MDI/table references, даже если вершины прежние |
| Cull key | Камере/frustum, candidates, bounds или соответствующих поколениях | Compact visibility |
| Transparent order key | Камере, влияющей на порядок, refs или geometry | Порядок прозрачных команд |

Deadline не отменяет эти зависимости. При недоступном свежем compact допустим консервативный набор всех необходимых кандидатов или заранее доказанное расширенное покрытие. Старое отсечение с другой камерой не является корректным fallback. Его стоимость ограничивается residency и стоимостью команд, а не пропуском видимых объектов.

Ownership pool/allocation generations сохранить. Удержание CPU-ссылки, validity GL barrier и завершение исполнения GPU — разные условия. Ссылки на submitted resources живут до fence retirement. Основание: [Filament FrameGraph](https://google.github.io/filament/notes/framegraph.html), S2, и [NVRHI lifetime](https://developer.nvidia.com/blog/writing-portable-rendering-code-with-nvrhi/), S5.

## 3. Этапы реализации

Все этапы пока `TODO`. Каждый завершается проверяемым gate; отсутствие визуального измерения остается UNTESTED, а не PASS.

### P0 — Воспроизводимая база и приемка

Покрывает A21-10; блокирует заключение «исправлено», но не локальные unit fixes.

Работы:

1. Записывать run manifest: git SHA, dirty diff hash, hash executable, build flags, backend, GL capabilities/extensions, GPU/driver, разрешение, настройки света/дальности, seed и hash исходного мира, route hash, cold/warm mode.
2. Разделить per-frame timings, period aggregates, event deltas и census. Убрать дубли JSON-ключей, указать единицы и aggregation в versioned schema. Хранить кадры или histogram по каждому кадру, не считать P99 по period means.
3. Job trace связывать с chunkXYZ, incarnation, attempt ID, desired/source/published versions, queue reason, стадией и временем. Не логировать полный мир каждый кадр; использовать компактные spans и ограниченный ring buffer для аварийной выгрузки.
4. Adequacy определять по входам: покрытие маршрута/поворотов/высот, скорость, число запрошенных chunks, длительность, режим кэша. Старый symptom reproduction gate оставить диагностическим, убрать его из обязательного product acceptance.
5. Собрать и зарегистрировать релевантные тесты; сохранять build fingerprint. Разобрать оба текущих FAIL как несогласованность контракта, не просто удалить assertions.
6. Записать воспроизводимую последовательность camera poses, edits и world-load events. Для HEAD и двух anchors использовать независимые копии одного исходного мира; cold и warm — отдельные сценарии. Не менять пользовательский рабочий мир.
7. Для сравнения — как минимум пять повторов каждого сценария, чередование A/B при одинаковом питании, разрешении и фоновой нагрузке. Фиксировать разброс; маршруты сравнивать по сегментам, а не только всей сессии.

Области: `FramePerfMonitor`, flight runner/scorecards, CMake tests, offline analyzer. Источники: S4, [Google Benchmark repetitions/interleaving](https://github.com/google/benchmark/blob/main/docs/user_guide.md), S10.

Gate: один и тот же файл однозначно связан с бинарником и миром; схема различает frame/period; исправление missing/stuck до нуля не ухудшает adequacy; fresh test binaries воспроизводят текущий baseline. Исторические логи без manifest не объявлять внезапно эквивалентными.

### P1 — Локальные correctness-исправления до крупной миграции

Покрывает A21-01, A21-02, A21-05, A21-09. Зависит только от контрпримеров, не от controller качества.

1. Убрать camera-key bypass из deadline reuse; добавить проверку на реальном command-building пути. При exhausted budget и повороте камеры newly visible chunk присутствует в командах.
2. Transparent skip разрешать только по актуальному order key. Тестировать многократное движение камеры в неизменной сцене после кадра с нулевым reorder. Разделить обновление порядка и перестройку геометрии; исправление не обязано возвращать полный дорогой rebuild каждый кадр.
3. Унифицировать shell classification full/incremental. Дифференциальный тест сравнивает не только enum, но faces, материалы, свет и occlusion результата для loaded AIR/solid, hidden peer, unloaded peer и всех шести направлений.
4. Передавать/удерживать snapshot memory credit вместе с фактическим payload. Если новый payload разделяет storage со старым, учитывать shared lifetime; если копирует — учитывать оба до освобождения. Проверить eviction, Refresh, exception/cancel и cache replacement.
5. Исправить material gate на одинаковый домен и source provenance. Добавить интеграционный mixed opaque/transparent test с легальной сменой ID, reorder, empty pass и отказом одного из нескольких chunks. Проверить обычный и packed пути.

Gate: пять относящихся к фазе аудитных проверок имеют ожидаемую семантику в production regression tests; отдельно проходит render-path тест, а не только helper. Memory credits равны учитываемым живым payloads. Старые alias/empty/switch tests остаются зелеными. Шестой контрпример, планировщик из A21-04, не исправлять «разрешением еще одного Dirty»; он остается открытым до P2.

### P2 — Один владелец demand и продвижения

Покрывает A21-03, A21-04, A21-06. Самая важная архитектурная фаза.

1. Ввести per-chunk render work record из §2.2; Dirty/tickets/RAA/GPU pending пока адаптеры к существующим исполнителям, а не новые независимые владельцы.
2. Перевести запись demand из world/light/neighbor events на этот record. Запрос возвращает `NewDemand / Coalesced / AlreadySatisfied`, поэтому повторный no-op не увеличивает admitted/progress.
3. Сначала shadow mode: новый record только наблюдает реальные события старого пути, сообщает расхождения. После проверки включать единственного writer по подсистеме; не оставлять два планировщика, одновременно запускающих repair.
4. Pending должен иметь active job/queue owner и stage. Счетчики возраста заменить monotonic timestamps. Незавершенный pending без владельца — диагностируемая ошибка с явным recovery/cancel, не бесконечное note stall.
5. Перенести FaceDebt на chunkXYZ/face/generation. Ready колонки вычислять по требуемым Y-slices; публикация одного slice не закрывает остальные.
6. Добавить reconciliation требуемой и опубликованной версии на событиях и в ограниченном maintenance pass. Это level check, не повторный global dark scan и не remesh всей колонки каждый кадр.
7. RetainedAwaitingSuccessor сохраняет требование. Поздний completion старой incarnation уничтожается без изменения новой записи. Budget denied остается queued/waiting, а не теряется.
8. После cutover удалить старые setters readiness/progress, sea-specific debt clears, дублирующие ownership paths и несовместимые ticket invariants. Удаление — часть фазы, иначе сложность только увеличится.

Области: `ColumnRecord`, `ChunkEmergeCoordinator`, `ColumnFlowExecutor`, `RelightInstallPlanner`, `MarkRelitInstall`, cache completion/admission adapters. S1, S3, S4.

Gate: randomized event tests с reorder/cancel/unload/reload, два Y-slices, шесть соседей, peer-ready-before-subscribe, stale light completion. При прекращении изменений и справедливом исполнении конечное множество требований достигает Published/явного постоянного error; не остается orphan pending или бесконечного Retain. При ошибке ввода/ресурса не ставится ложный Ready.

### P3 — Общая транзакция публикации CPU/GPU

Покрывает A21-05/06 и сохраняет успешные lifetime fixes. Зависит от P1 и структуры demand P2.

1. Ввести immutable manifest и общий validator; реальные CPU/GPU callers используют его, а не разные helpers с похожими названиями.
2. Candidate хранит actual source light/material/neighbor revisions. Не подставлять expected/current значение вместо provenance кандидата.
3. Stage ресурсов отделить от install. Отказ одного pass/chunk не стирает накопленный успех других независимых transactions. Empty replacement и representation switch — явные результаты, не отсутствие fresh batches.
4. Явно определить epochs из §2.4. Разрешить конфликт `publication_audit`: order-only изменение либо меняет нужный epoch, либо каждый потребитель доказанно использует отдельный table/order epoch. Тест должен проверять stale draw rejection, а не произвольное увеличение счетчика.
5. Deferred retirement с allocation generation/fence ownership. Не освобождать диапазон, еще используемый submitted command; не устранять stalls блокирующим ожиданием каждой публикации.

Области: `MeshPublishContract`, `ChunkMeshCache`, `GreedyGpuPublication`, `MeshGpuStore`, MDI/compact building. S2, S5; platform barriers — [Khronos glMemoryBarrier](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glMemoryBarrier.xml), S9.

Gate: mixed passes, empty result, packed↔CPU switch, memory pressure, failed allocation, table reorder, delayed GPU completion и повторное использование диапазона. Все references указывают на живую generation; визуальный ID buffer соответствует выбранному manifest. Новая ошибка не закрывается глобальным disable pooled path.

### P4 — Корректный свет и отдельное временное покрытие

Покрывает A21-02/03/07. Зависит от manifests и demand; может проектироваться вместе с P2/P3.

1. Описать light boundary conditions для unloaded/loaded neighbors, sky и block channels. Версионировать все прочитанные light fields; определить propagation/remove propagation при изменении источника/occluder.
2. Разделить value=0, validity, freshness и visibility. Убрать управление repair по «есть черная вершина»; оставить census наблюдательным с одинаковыми единицами CPU/GPU.
3. На малых мирах создать независимый медленный CPU reference полного пересчета света. Сравнивать incremental результат после появления/удаления света, загрузки соседа, вертикального пролета, закрытия пещеры и смены материала.
4. Вынести seam output из постоянного mesh либо завершить временную manifest-validating схему до выноса. Удаление seam и публикация заменяющей геометрии согласованы; нет кадра без обоих и нет длительного двойного z-fighting.
5. Убрать ring/sea special cases из семантики готовности. Они могут влиять на приоритет, но не на истинность dependency satisfaction.
6. Проверить полностью темную валидную пещеру: конечное число задач после стабилизации, отсутствие endless relight. Проверить устаревший halo при совпадающей center revision: корректный reject/rebuild.

Gate: совпадение с reference, finite convergence после stop, no seam gap на 6 направлениях, корректная прозрачная граница. Разрешить противоречие `relight_install_planner_test` согласно новому контракту, не согласно желаемому цвету счетчика. S1, S3, S6.

### P5 — Ограниченная стоимость исполнения и сквозной backpressure

Покрывает A21-04/09. Не ждать этой фазы для удаления явных ошибок P1.

1. Инвентаризировать все неделимые main-thread операции: capture, shell refresh, light install, emerge/generation commit, upload, sort, fluid, retirement. Для каждой — distribution duration, bytes, input size и call count.
2. Тяжелые CPU операции разбить на resumable units с сохранением cursor и manifest либо вынести на workers. При отмене результаты не публикуются, но ресурсы освобождаются по lifetime. Проверка deadline между чанками не ограничивает стоимость одного чанка.
3. Единый admission учитывает shared workers и независимые ресурсные лимиты: snapshots, queued outputs, staging/GPU in-flight и retirement. Подключить generation/relight либо явно обосновать и ограничить их отдельные pools. Общее число workers не выводить из одного mesh counter.
4. Принимать upstream работу только при возможности bounded downstream buffering; зарезервировать drain/commit capacity, чтобы заполненный pipeline мог освободиться. Не держать дефицитный credit при ожидании другого ресурса так, чтобы образовался цикл.
5. Приоритет: collision/near visible coverage, завершение уже дорогой in-flight работы, light/geometry debt по возрасту и time-to-visibility, затем prefetch. Добавить aging и ограничить долю любого класса. Защитить от постоянного вытеснения дальних долгов ближними.
6. Модель стоимости — rolling estimates по виду/размеру работы с запасом, не count chunks. Ограничение неделимого quantum проверяется отдельно от среднего прогноза. Desktop driver может блокировать непредсказуемо: измерять GPU/driver spans и не заявлять жесткую real-time гарантию.
7. Удалить прежние безусловные bypass и caps только после переноса их safety requirements. «Одна critical job после deadline» разрешена лишь для измеренно ограниченного quantum, не для полного тяжелого rebuild.

Gate: нагрузка выше service capacity не создает неограниченную очередь/память; queuedLatest остается bounded per chunk; нет lost demand или starvation после прекращения ingress; frame time не содержит скрытой неучтенной synchronous фазы. Подход main-thread budget/worker limits: [Voxel Tools Performance](https://voxel-tools.readthedocs.io/en/latest/performance/), S8; lifetime S5.

### P6 — Fluid surface как инкрементальная подсистема

Покрывает A21-08. Высокий приоритет производительности: начинать после P0/P1 параллельно по зависимостям с проектированием P2–P4; не ждать controller.

1. Сразу исправить cache identity: world/incarnation, scan domain, content/fluid/material-catalog versions. Добавить water→lava при неизменной occupancy, смену мира на тех же coordinates и изменение высотного диапазона. Текущий pack-reuse test этих доказательств не заменяет.
2. Выделить versioned per-column summary: top/occupancy и material information. Обновлять изменившиеся столбцы из voxel/fluid events; удаление верхней ячейки может требовать bounded rescan конкретного столбца.
3. Cache lookup по версиям до полного прохода данных; не вычислять полный flags hash ради проверки каждого hit. Если occupancy reuse сохраняется, material IDs пересчитываются/валидируются отдельно.
4. Rebuild missing summary — worker/continuation по immutable input. Main thread устанавливает только готовый slice актуальной версии; не синхронно читает height*16*16 world cells через получение карты.
5. Surface map — tiled/toroidal logical origin. При scroll обновляются вошедшие полосы и dirty tiles; полный copy/upload допускается при reset/teleport как отдельная bounded процедура. При отрицательных координатах и wrap проверять addressing и sampling границы.
6. На desktop назвать CPU scan явно. На Android проверить dispatch→barrier→mapping/fence по фактическому потребителю; не называть CPU staging временем GPU и не приписывать desktop hitch readback.
7. Fallback на незавершенном tile должен сохранять корректный material/coverage и явно иметь возраст; не возвращать произвольный старый tile после world switch.

Gate: при движении по неизменному миру стоимость пропорциональна вошедшим/изменившимся данным, а не полной высоте каждой resident колонки; на main thread нет полного world rescan. Flight trace больше не содержит fluid jobs длительностью десятки/сотни миллисекунд. Размеры квантов и uploads проходят P5; water/lava, wrap, teleport и material tests проходят.

Обоснование incremental/toroidal surface updates: [Geometry clipmaps](https://hhoppe.com/proj/geomclipmap/) и [GPU Gems chapter](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry), S7. Это перенос структуры 2D working set, а не heightfield-замена объемного мира.

### P7 — Адаптивная дальность и качество после correctness

Переосмысленный `RingReadinessBudget`. Зависит от P2–P6 и baseline acceptance P0.

1. Разделить collision safety, required visible coverage, data/light halo, prefetch и retained GPU set. Halo следует из алгоритмов, prefetch — из скорости/направления и измеренной latency, retention — из памяти и цены повторной загрузки.
2. Controller наблюдает backlog age, arrival/service rate, memory pressure, latency distributions и available coverage frontier. Не использовать dark census как доказательство физической неготовности и RemainingMs одной точки кадра как устойчивую capacity оценку.
3. Выходы: admission rate, prefetch extent, quality target, retention priorities. Hysteresis, минимальное время режима и ограничение изменения за шаг предотвращают thrashing. Точные значения подбираются по модели нагрузки и фиксируются в manifest.
4. Нижняя граница качества/дальности задается до запуска и не меняется оценщиком. Деградация отражается отдельными показателями: доля времени, объем, длительность и скорость восстановления. Нельзя улучшить score исключением проблемного кольца из знаменателя.
5. Fog может согласовывать переход качества, но не закрывает дыру/неверный материал внутри тестируемой области. Если текущий движок не имеет корректного fallback, адаптивное уменьшение visible coverage не включать до его реализации.
6. Prefetch distance оценивать из скорости * latency плюс запас, с ограничением памяти; проверять поворот/телепорт, где линейного прогноза недостаточно. Не фиксировать всю семантику мира на числе 2/3/4.

Gate: при контролируемом дефиците ресурсов система снижает качество предсказуемо, не создает holes/invalid material, не осциллирует и восстанавливается после снижения нагрузки. Тесты оценивают одинаковую минимальную область для всех вариантов. Аналогия измеримой quality degradation: [Epic memory pool residency](https://dev.epicgames.com/documentation/unreal-engine/virtual-texture-memory-pools-in-unreal-engine), S11; не предлагается внедрение VT ради этих багов.

### P8 — Оптимизации второго порядка и итоговый cutover

Только после профиля исправленного baseline:

- Если доминирует relight-remesh, рассмотреть отделение light field от geometry. Прототип сравнивает стоимость обновления GPU lighting, память/halo и визуальную эквивалентность; это не обязательное условие первых исправлений.
- Если доминируют upload/retirement stalls, исследовать persistent mapping/ring uploads при поддерживаемых capabilities, batching и bounded retirement. Сначала timestamp/fence evidence, затем API-specific решение; GLES fallback обязателен для поддерживаемой платформы.
- Если доминирует transparent ordering, уменьшать иерархически область пересортировки с корректным ключом. Weighted OIT не считать drop-in для воды с refraction/слоями; потребуются отдельные quality требования и тесты.
- Если steady-state слишком дорог даже без spikes, рассмотреть LOD/HLOD как самостоятельную подсистему с boundary/transition contracts. Не подменять им исправление stale visibility.
- Удалить временные shadow adapters, obsolete flags и дублирующие controllers. Сохранить diagnostic snapshots и самостоятельные regression fixtures.

Gate: каждая оптимизация имеет профиль причины, до/после по одинаковому workload и неизменные correctness gates. Без доказанного выигрыша не включать сложность в default path.

## 4. Общая матрица приемки

### 4.1 Визуальная корректность

Не ограничиваться бинарным «черный/нечерный» и snapshot counters. В небольших детерминированных сценах выводить debug buffers: chunk/artifact ID, material ID, depth, source light generation. Независимый CPU ray/reference mesher определяет ожидаемую поверхность по миру, а не по тому же cache, который проверяется.

Для движущегося replay сравнивать каждый кадр с ожидаемой геометрией и корректными camera poses: появление/исчезновение поверхности из-за реального occlusion допустимо, исчезновение без изменения входов — нет. Свет проверять относительно reference/допуска, а не относительно нулевого цвета. Вода, alpha/cutout и refraction имеют отдельные маски/правила и визуальный review.

Обязательные сценарии: cold/warm cruise в обе стороны; разворот на 180° при нулевом remaining budget; остановка; вертикальный dive/подъем; unload/reload; teleport; редактирование opaque↔transparent и water↔lava; сосед появляется до/после capture; свет меняется во время meshing; долг у двух Y-slices; давление памяти; задержанные/reordered completions; смена мира на тех же координатах.

Ошибочный material ID и потеря ожидаемой непрозрачной поверхности в согласованной проверяемой области — 0. Для динамического изменения мира разрешенные transition states и максимальный срок задаются заранее. Не требовать мгновенно финальную геометрию в еще не загруженном мире, но и не объявлять provisional fallback конечным Ready.

### 4.2 Производительность и сходимость

Целевой FPS и класс устройства пользователь не задал. Предлагаемый стартовый профиль — 60 FPS на согласованной машине при фиксированном качестве; 30 FPS, если нужен, оформлять отдельным профилем, не скрытой сменой критерия. Это продуктовые ориентиры, не универсальные industry constants.

- Для 60 FPS начальные цели: steady-state frame P95 ≤16.7 мс, P99 ≤33.3 мс, отсутствие кадров >100 мс вне заранее выделенных load/reset событий. Эти пороги проверяются по всем кадрам, с доверительным разбросом повторов; startup/teleport публикуются отдельно и не исчезают из отчета.
- Main-thread streaming/upload budget распределяется из общего frame budget после измерения simulation/render; нынешние ~16 мс world-streaming нельзя принять как бюджет для стабильных 60 FPS. Число workers/мс на subsystem не назначать из чужого default.
- Time-to-ready измерять от возникновения конкретного demand до публикации нужной версии, отдельно queue/build/upload/light wait. Для движения допустимый срок определяется временем до входа в required-visible область. Если capacity недостаточна, controller обязан заранее применить согласованное quality решение.
- После stop и прекращения edits: отсутствие бесконечных долгов, orphan pending и повторных no-op repairs. Численный SLA фиксировать на эталонной сцене после P0: например, 95% обязательств ≤1 с и максимум ≤3 с как первоначальная проверяемая гипотеза, не уже достигнутая гарантия. Превышение объясняется по job trace, а не списывается на «мало PreferKick».
- Memory accounting: каждый учитываемый payload имеет владельца/credit до последнего потребителя; занятые диапазоны не пересекаются, freed generations не используются, после unload/retirement baseline возвращается в установленный диапазон.
- Soak минимум 15 минут движения/разворотов/остановок с повторными загрузками; не менее пяти независимых cold/warm replay. Для редких багов добавить randomized event scheduling/property tests, не полагаться только на длительность ручного полета.

Порог допуска и исключенные зоны сохранять до запуска. Любой необследованный driver/backend, ручная картинка или отсутствующий reference помечается UNTESTED. PASS proxy никогда не заменяет PASS независимого визуального gate.

## 5. Порядок изменений и управление риском

Практическая последовательность первых небольших изменений:

1. Добавить production regression tests на шесть контрпримеров и зафиксировать schema/build freshness; не копировать helper в тест вместо вызова реального пути.
2. Отдельно исправить cull key и transparent order key; отдельный A/B с `83a7b8dd`-предшественником.
3. Отдельно исправить snapshot full/incremental и lifetime credit; повторно проверить geometry output.
4. Исправить per-pass material provenance и fluid cache identity с соответствующими integration tests.
5. Утвердить ADR для per-chunk demand, manifests и seam contract; запустить shadow diagnostics без второго writer.
6. Cutover P2/P3, затем light/seam P4 и bounded work P5. Fluid P6 допускает независимую реализацию после своих prerequisites.
7. Только после этих gates внедрить P7; P8 — по оставшемуся профилю.

Каждый cutover включает тесты, изменение одного владельца, наблюдаемость и удаление вытесненного пути. Rollback локального этапа возвращает его предыдущего владельца и сохраняет доказательства; он не должен незаметно включать два пути одновременно. Feature flags временные, с указанным gate удаления.

Не оценивать готовность количеством коммитов. Закрытие исходного аудита — это прошедшая матрица визуальной корректности, конечная сходимость и измеренный frame budget при фиксированном качестве. Документирование очередного снижения FD/VB без этих доказательств остается промежуточным результатом.
