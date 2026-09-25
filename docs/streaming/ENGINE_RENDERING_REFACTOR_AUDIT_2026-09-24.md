# Аудит и план рефакторинга voxel rendering/streaming

Дата: 2026-09-24
Проверенный HEAD: 40a9a56c (cursor_audit7_impl), parent 4d1155df
Контрольная точка пользователя: dd7871ab0ce374d158e4d8afae3ec9475b42431d
Объём: история от начального terrain streaming до HEAD; актуальные world/mesh/light/render пути; tracked и untracked полётные данные. Production-код этим аудитом не менялся, новые прогоны и тесты не запускались.

## Краткий вывод

Первичный диагноз «стриминг не успевает» описывает только один вариант дефекта. История и текущие трассы показывают как минимум четыре разных причины плохого изображения: геометрия ещё не опубликована; mesh опубликован с устаревшим светом; входы mesh и границ чанка изменились во время фоновой работы; renderer использует устаревшее решение видимости/порядка. Они дают похожий экранный симптом, но требуют разных ремонтов.

Наиболее сильная текущая проблема — владение визуальным долгом. Очередь, которая должна породить исправный mesh, может быть опустошена SoftDefer, отложена лимитом Capture или вытеснена FirstMesh. Одновременно колонная готовность и PendingLight агрегируют несколько Y-срезов, а demand и публикация работают по отдельным chunk-срезам. Новая VisualObligation пока не является единым production-источником истины: общий classifier не вызывается из production-кода, draw продолжает принимать решение по старым predicates, а enum вручную записывают из нескольких мест.

Итог последнего доступного A42 эксперимента подтверждает незакрытую проблему качества, но не является доказательством поведения на полном дальнем полёте: visible_black_focus_fly_med=31, holes_rate≈0.84, post-stop convergence FAIL; маршрут прошёл около 304 блоков и явно помечен far_flight=false. Средняя скорость кадра и средняя задержка выглядят приемлемо. Значит, следующий этап должен устранить потерю/зависание конкретных repair-demand и только затем менять производительность.

Рекомендация: оставить рабочими версионный async builder, immutable mesh snapshots, старый published mesh до успешной замены, CPU worker pool и уже защищённые camera/sort cache keys. Перестроить вокруг них lifecycle одного chunk-slice: единый demand/ticket, валидируемые входные версии, атомарная публикация, гарантированный ограниченный прогресс. Не добавлять новый слой readiness/watchdog поверх существующих.

## Состояние репозитория и качество доказательств

- HEAD — 40a9a56c, ветка на один коммит впереди origin/cursor_audit7_impl. Tracked-файлы не изменены; есть 948 untracked entries, включая .cursor/plans, полётные JSON/JSONL и временные анализаторы. Их не удалял и не редактировал.
- dd7871ab — commit с документацией и scorecards Sysreset v3, без production-кода. Он фиксирует аудитную точку, а не «исправленный бинарник». От него до HEAD прошло 67 коммитов; изменены 235 tracked-файлов, из них 67 файлов в src (около 7.2k добавленных и 284 удалённых строк). Основной объём полной истории — отчёты и evidence.
- bin/suite_reports/a42/cold_a42e.json содержит манифест: базовый SHA 4d1155df, непустой dirty_diff_hash, Release exe hash, World_164, product-174657, speed scale 1. Там же manifest_acceptance_pass=false; не записаны GPU/driver, разрешение, настройки дистанции света и seed/hash мира. Эксперимент относится к A42 dirty build, но не к чистому SHA. Он ценен для диагноза, не для merge-green.
- В том же отчёте отмечены eye_proxy_stop_line и west_route_coverage, а не запись кадра/пиксельный oracle. far_flight=false, far_distance_blocks=304. Прокси-метрики подтверждают debt, но не различают цвет чёрного пикселя и точную причину GPU draw.
- A42 и предыдущие A41 отчёты признают Gate 8 и human eye OPEN/FAIL. Самостоятельно не воспроизводил GUI-картинку и не запускал бинарник.

## История: как проблема меняла форму

| Период | Что менялось | Что осталось/вернулось |
|---|---|---|
| Июнь 2026, первые UAsyncMeshBuilder и terrain streaming | Синхронное построение заменено генерацией/IO/meshing по этапам и worker jobs; приоритет загрузки привязан к расстоянию. Первые отдельные mesh/light readiness состояния. См. [EVOLUTION](EVOLUTION.md). | Commit terrain не означает, что свет и drawable mesh уже готовы. |
| Июль | Lightmap, async relight, PendingLightBeforeMesh, Column FSM, ColumnFlow, preview/SoftDefer и budgets. Были быстрые regress/fix/revert циклы на idle drain, полностью пустых чанках, hitch и сохранении старого mesh. | «Готов к работе» и «безопасно показывать» часто смешивались; три pipeline (mesh, light, streaming) оставались с разными владельцами. Это уже записано в [ROOT_CAUSE_2026-07](ROOT_CAUSE_2026-07.md). |
| Август, Era14–40+ | Добавлялись near-FOV admission, capture floors, relight FIFO, witness pin, empty placeholder, seam repair и SLA. Одна правка исправляла starvation, другая возвращала hitch или starvation соседнего класса. [ROOT_CAUSE_2026-08](ROOT_CAUSE_2026-08.md) описывает цикл frame budget → stall → gate не запускает repair. | Большое число local escape paths не превратилось в конечную гарантию завершения работы. Многие планы помечены done/GO по автопролёту, но оставляют ручной eye или конкретный режим OPEN. |
| dd7871ab, Sysreset v3 (20 сентября) | В отчёте зафиксированы x-ray от Unknown/SoftDefer соседа и отдельные hitch классы; cold часть полётной матрицы улучшилась. | unfinished, warm/dive eye, emerge/spikes и merge_green остались OPEN/UNTESTED. dd7871 только записывает evidence. |
| 21 сентября, v4–v6, ownership cutover | FaceDebt, overlay, BecameKnown remesh, GPU kick/progress и новые приемочные метрики. Изменения закрывают конкретные случаи, но несколько коммитов возвращают старую PreferKick/focus admission логику. | Симптомы разделились на x-ray, black/stale light, texture/publish race и hitch; ни один из v3–v6 evidence-документов не закрыл все gates. |
| 22 сентября, A21–A30 | Введён per-chunk ChunkRenderDemand; работа над publication epochs, LegalDark vs stale, multi-Y ready, boundary/face debt, remesh starvation. Несколько remediation возвращают каждую из веток обратно после обнаружения holes или thrash. | [CURRENT_STATE_AUDIT_2026-09-22](CURRENT_STATE_AUDIT_2026-09-22.md) уже указывал на несколько state models, неверную гранулярность, budget без единицы полезной работы и тесты, не доказывающие user-visible исправление. Эти системные замечания остаются актуальны. |
| 23–24 сентября, A31–A42 | Правки demand/pub/seam/fluid/Cross, затем VisualObligation, SLA remint, SoftDefer исключение, Capture reserve и ClearPending. | Empty/enter отдельные smoke-gates проходят, но A42e всё ещё: vb_fly_med=31, stale_vl_fly_med=32, near_focus_holes=1, post-stop convergence FAIL. Последовательность A42b→e снимает несколько конкретных блокировок, но остаточный дефект остаётся. См. [A42 follow-up](A42_COLD1_BLACKS_FOLLOWUP.md). |

Исторический regression-каталог следует сохранить как отдельную матрицу, а не считать закрытым из-за старого PASS: camera-cull при движении; transparent order; x-ray/дырки на chunk seam; wrong/stale texture во время замены mesh; underfeet/frontier first-mesh; light bake; fluid walls; enter warmup; кадры-рывки. Старый [A21 audit](CURRENT_STATE_AUDIT_2026-09-22.md) и untracked N04/flight reports описывают эти случаи. Они не все являются текущей причиной A42 black, но каждый должен остаться регрессионным gate.

## Актуальные данные последнего полёта

Данные: untracked bin/suite_reports/a42/cold_a42e.json и связанный bin/logs/perf_20260924-190819_27040.jsonl.

| Сигнал | A42e | Что это доказывает |
|---|---:|---|
| visible_black_focus_fly_med / stale-VL fly med | 31 / 32 | Долг устаревшего света в focus-ring сохраняется. Это CPU proxy, не классификация каждого пикселя. |
| holes_rate / effective holes rate | 0.837 / 0.837 | Near-focus визуальный backlog частый, несмотря на отдельные низкие blink-показатели. |
| post_stop_convergence_pass | false | Остановка игрока не приводит к сходимости: missing/holes не уходят, pending/demand не закрываются. |
| dirty_med / max | 481 / 752 | Очередь остаётся большой; считать сам факт Dirty-ticket прогрессом неверно. |
| chain_stall_sec / relight capture partial rate | 24 с / 0.659 | Есть длительное окно без продвижения и значимая доля частичных capture. |
| cruise_schedule_ok_med, cruise_relight_completed_med | 2, 0 | Schedule принимает часть работы, но relight completion throughput в этом сегменте отсутствует. Причина требует stage trace по одному ticket. |
| wall / fly FPS | 13.5 мс median / 68.7 FPS | Средняя скорость кадра сама по себе не объясняет black/hole. Редкие пики и возраст конкретного debt — отдельные метрики. |
| Gate status | manifest_acceptance=false, eye/proxy/stop не green | Этот запуск не подтверждает merge readiness. |

В A42_COLD1_BLACKS_FOLLOWUP.md приведено сравнение: FullyDark-stall снижен до нуля, unlit max — до 13, но black proxy остаётся 31, holes=1 на контрольных периодах и Gate 8 OPEN. Поэтому «уменьшился один census» не равняется «исправилось изображение».

## Аудит текущей реализации

### P0. VisualObligation пока не является источником истины

VisualObligationPolicy.h содержит полезный ClassifyVisualObligation и VisualObligationAllowsDraw, однако поиск по production-коду показывает, что эти два общих решения используются только тестом. В production enum записывают напрямую из World.cpp, MarkRelitInstall.cpp и callback-ов ChunkEmergeCoordinator.cpp. Draw в UWorld::IsChunkSliceRenderReady по-прежнему вычисляет has live GPU, Satisfying, FullyDark, stale, pending/ticket отдельно и только затем заглядывает в visual_obligation==LightRepair.

Следствие: документированные A41 инварианты (Draw ⇔ LitDrawable ∨ LegalDark, Hide ⇒ один активный ticket) не следуют из одного обязательного перехода. Это новый label поверх старых gate-ов; две системы могут одновременно дать противоречивый ответ. Проверка классификационной функции в unit-тесте не доказывает использование функции runtime.

Точки: [VisualObligationPolicy.h](../../src/World/Streaming/VisualObligationPolicy.h), [World.cpp:1983](../../src/World/Core/World.cpp), [MarkRelitInstall.cpp:630](../../src/World/Core/MarkRelitInstall.cpp), [ChunkEmergeCoordinator.cpp:445](../../src/World/Streaming/ChunkEmergeCoordinator.cpp).

### P0. Смешаны единицы состояния: chunk-slice и column XZ

ChunkRenderDemandRecord и mesh/GPU job используют {x,y,z}, а ColumnRecord, PendingLightBeforeMesh, legal_dark_settled и visual_obligation — только {x,z}. При этом callbacks после публикации одного slice ставят ColumnRecord.visual_obligation=LitDrawable; MarkRelit может поставить всей колонке LegalDark или LightRepair; IsLightRepairRemesh потом применяет эту метку к любому Y этой колонки.

Визуальная проблема и опубликованная версия относятся к конкретному slice. Колонный job допустим как управляющая работа, но его «готово/ремонтируется» должно агрегироваться из детей, а не заменять их статусы. Пока один slice может перезаписать классификацию другого, а stale-долг конкретного Y выражен колонной записью.

Дополнительный пример слабого completion-контракта: IsColumnLitReady() возвращает true для Empty, Meshing, LitReady, RenderReady; ClearPendingLightAfterMeshCommitted() использует этот общий predicate и проверяет наличие хотя бы одного greedy mesh в Y-диапазоне. Это не равно условию «все затронутые slices опубликованы с ожидаемыми версиями». Имя/comment указывает на терминальную готовность, а predicate кодирует более широкое «можно продолжать lifecycle». Нужны разные запросы CanSchedule, HasLitInput, AllRequiredSlicesPublished.

Точки: [ColumnRecord.h](../../src/World/Streaming/ColumnRecord.h), [ChunkRenderDemand.h](../../src/World/Streaming/ChunkRenderDemand.h), [World.cpp:1907](../../src/World/Core/World.cpp), [World.cpp:4366](../../src/World/Core/World.cpp), [ChunkEmergeCoordinator.cpp:656](../../src/World/Streaming/ChunkEmergeCoordinator.cpp).

### P0. Попытка восстановления не владеет сквозным ticket

В системе одновременно живут: ColumnEmergeStates, зеркальный ColumnRecord, legacy ColumnVisualState, per-chunk ChunkRenderDemandStore, PendingLightBeforeMesh, StickyRemeshAfterLight, ColumnFlow jobs, ChunkDirtySet с FirstMesh/remesh полосами, async builder InFlight и bounded Completed, Capture pending, PendingGpuApplies, RemeshAfterApply и SoftDeferHeld. В комментариях ColumnRecord прямо сказано, что он dual-written с legacy state; GetColumnEmergeState() по-прежнему читает отдельную map в первую очередь.

LightRepair attempt ID создаётся в нескольких независимых static counters; ColumnRecord.visual_attempt_id не совпадает по контракту с ChunkRenderDemandRecord.active_attempt_id и не несёт world epoch/incarnation. Deadline — таймер на колонке, но stage progress и cancellation распределены по другим контейнерам. Поэтому «ticket существует» может означать, что запись осталась в Dirty, хотя schedule уже много кадров не мог сделать Snapshot.

Решение: одно поколение demand на chunk-slice; очереди и stages ссылаются на его attempt_id/epoch. Запись в очередь — accepted scheduling, не progress. Продвижение — capture завершён, worker завершён на актуальных входах, upload/publish закончен. Только terminal publish закрывает долг.

Точки: [ColumnRecord.h](../../src/World/Streaming/ColumnRecord.h), [ChunkRenderDemand.h](../../src/World/Streaming/ChunkRenderDemand.h), [AsyncMeshBuilder.cpp](../../src/Render/Mesh/AsyncMeshBuilder.cpp), [ChunkMeshCache.cpp:6461](../../src/Render/Mesh/ChunkMeshCache.cpp), [ColumnFlowExecutor.cpp](../../src/World/Streaming/ColumnFlowExecutor.cpp).

### P0. Между видимым дефектом и очередью нет bounded progress guarantee

Текущий A42 код всё ещё имеет отдельные first_mesh_cap, remesh_cap, snapshot-time budget, miss starvation, SoftDefer исключения, pending-GPU ownership и Capture reserve. В ChunkMeshCache для miss сначала допустимо обнулить remesh_cap; затем LightRepair может получить отдельную reserve, но она всё ещё зависит от условия remesh_cap>0, объёма capture и порядка полос. Комментарии к A42c прямо фиксируют случай, когда Capture reserve была недостижима из-за remesh_cap=0.

Последний flight JSON подтверждает симптом, а не один окончательный code-level root: remesh budget иногда 0; средний remesh output низкий; skip_snapshot высок; relight completion в cruise равен нулю; ticket stall 24 сек. Новые исключения пропускают отдельные блокирующие ветки, но не задают гарантию «активный near-focus ticket за конечное время попытается capture». Нужен один fair admission owner с отдельными минимальными квотами на FirstMesh и LightRepair/Remesh и очередями по возрасту/срочности.

Не считать queue length, MarkDirty, PreferKick, live GPU task или PendingLight доказательством продвижения. Полезный progress должен быть измерен переходом stage и изменением актуального опубликованного revision.

Точки: [ChunkMeshCache.cpp:6461](../../src/Render/Mesh/ChunkMeshCache.cpp), [ChunkMeshCache.cpp:6908](../../src/Render/Mesh/ChunkMeshCache.cpp), [MeshWorkAdmission.h](../../src/World/Streaming/MeshWorkAdmission.h), [A42 report](../../bin/suite_reports/a42/cold_a42e.json).

### P1. Публикация boundary-dependent mesh требует единого input contract

ChunkMeshSnapshot::Capture() сохраняет center+шесть neighbor geom/light stamps. Но результат shell meshing также зависит от neighbor_visually_drawable: comment явно говорит, что эта функция меняет shell occlusion preview, при том что stamp имеет только geom/light. FaceDebt/BecameKnown повторно инвалидируют шов другими callback-ами, то есть смена drawable/coverage состояния соседа не полностью представлена в самом input stamp.

Нужно принять одно из двух правил и тестировать его: (1) mesh геометрически зависит только от voxel/material/light данных, а boundary visibility оформлена отдельным immutable overlay с собственным revision; либо (2) visual coverage соседа входит в stamps результата. Нельзя оставить нештампованную зависимость и надеяться, что сторонний dirty callback успеет обогнать завершение worker. Это важный кандидат для X-ray/шва, но не доказанная единственная причина текущего black.

Точка: [ChunkMeshSnapshot.cpp](../../src/Render/Mesh/ChunkMeshSnapshot.cpp); historical source finding: [CURRENT_STATE_AUDIT, A21-02/A21-03](CURRENT_STATE_AUDIT_2026-09-22.md).

### P1. Black, hole и legal-dark — разные ответы, а не один счётчик

FullyDark, still_stale, StaleVertexLight, open_sky, LegalDark, missing geometry, missing command и false-negative cull сейчас расходятся между census, ready, draw oracle и queue admission. A42 black proxy в основном совпадает со stale vertex light, но в продукте есть легально тёмные пещеры. Одна метрика visible_black не может автоматически означать «создать remesh».

Для каждого snapshot/кадра диагноз должен отвечать: какой chunk-slice; что камера должна была видеть; был ли resident drawable mesh; какие desired/published geometry/light/coverage versions; какой material command ушёл в pass; что дал depth/object-ID/pixel oracle; классифицирована ли темнота как LegalDark. Без этого FIFO и LightRepair могут повышать лишнюю работу или скрывать источник черноты.

### P1. Крупные orchestration-файлы несут слишком много policy

World.cpp — около 10.4k строк; ChunkMeshCache.cpp — около 8.2k; ChunkEmergeCoordinator.cpp — около 6.9k; WorldStreaming.cpp — около 5.1k; MeshWorkAdmission.h — около 1.6k. Размер не доказывает ошибку, но решения по admission, cancellation, ready, capture и repair переплетены внутри нескольких крупных координаторов. Из-за этого policy добавляют if-ветками и local exceptions, а изменение планировщика одновременно меняет визуальные и performance gates.

После закрепления state contract выделить ChunkVisualDemand, MeshJobScheduler, MeshSnapshotBuilder, MeshPublishTransaction, LightRepairCoordinator и ColumnAggregate. Переносить по одному владельцу, чтобы новая структура действительно заменила старую, а не стала ещё одной map.

### Сохранить уже закрытые renderer regressions

После истории stale camera cull текущий ShouldDeferOpaqueCompactCullForDeadline() уже требует совпадения CullInputKey; его callsite передаёт результат CullInputKeyAllowsCacheReuse. Transparent full resort сверяет sortRevision. Это правильные предохранители, их нельзя потерять при render cleanup. Однако regressions нужны как scene-level tests с движением камеры, а не только policy assertions.

Также сохранить atomic/old-drawable replacement, revision/job/epoch validation, backend-specific GPU ownership, per-face seam gates и отсутствие ненужного GL context switching. Не начинать с переписывания greedy mesher или GPU renderer: A42 evidence указывает на старый/stale publication и отсутствие progress, а не на недостаток polygon throughput.

## Research: практики зрелых voxel pipelines

1. **Чанки и viewer interest.** Voxel Tools описывает terrain как чанки вокруг viewer; viewer определяет радиус загрузки и приоритет mesh update, тяжёлые операции выполняются в worker threads. Для очень большой дистанции есть отдельная LOD/octree terrain модель. Это поддерживает явное разделение «нужные игроку данные» и «какую детализацию сейчас можно показать», а не бесконечное увеличение радиуса grid chunks. [Overview](https://voxel-tools.readthedocs.io/en/latest/overview/).
2. **Ограниченный набор синхронных стадий и backpressure.** Их документация отдельно описывает async generation, loading, mesh update и streaming. Практический вывод: иметь stage owner и измерять latency/queue age отдельно для каждой стадии; при высоком движении приоритет должен учитывать время до появления на экране, не только расстояние. [Performance](https://voxel-tools.readthedocs.io/en/latest/performance/), [Streams](https://voxel-tools.readthedocs.io/en/latest/streams/).
3. **Консистентный voxel input для worker.** Voxel Tools обсуждает стоимость блокировок вокруг центрального блока и соседей, необходимость одинакового порядка lock и риск блокировки main thread; в других режимах лучше читать из копии/буфера. Для Cubatarium это аргумент за immutable snapshot с halo + stamps, а не за новые локи вокруг live chunks. [Voxel Tools performance: threading](https://voxel-tools.readthedocs.io/en/latest/performance/).
4. **Mesh cost против mesh latency.** Greedy meshing сокращает число квадов и может потребовать больше работы на построение, чем простое culling. Поэтому улучшение треугольников не заменяет SLA FirstMesh/repair и не должно приниматься по FPS в одиночку. [0fps: Meshing in a Minecraft Game](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/).
5. **OpenGL context ownership.** Worker CPU может захватывать immutable данные и вычислять mesh. Один OpenGL context нельзя одновременно использовать в двух threads; текущую render/upload работу безопаснее держать у render-owner пока профилирование не докажет смысл отдельного shared context. [Khronos OpenGL context](https://www.khronos.org/opengl/wiki/Get_Context_Info).

## Предлагаемая целевая архитектура

Одна запись на chunk-slice (x,y,z) — authority для желаемого результата, попытки и опубликованного результата. Колонна остаётся aggregate/job orchestration unit, но не записывает terminal состояние вместо дочерних slices.

Поток данных: world changes/viewer interest → desired input versions → ChunkVisualDemand → fair priority scheduler/bounded budgets → immutable stamped snapshot → CPU mesh worker → validated result → render-owner upload и atomic publish. Старая опубликованная геометрия остаётся drawable до успешной замены. Demand закрывается только после продвижения published revisions.

У demand хранить: world epoch, chunk incarnation, desired geometry/light/material/coverage versions, опубликованные версии и GPU allocation generation, активный attempt_id, stage, created/progress timestamps и retry reason. Результат устаревшего attempt не может закрыть новый demand. Результат с несовпавшим snapshot stamp не публикуется и создаёт/коалесцирует demand по последним версиям.

VisualObligation следует сделать derived state/policy для этой записи: LitDrawable, LegalDark, LightRepair, GeomRepair, Missing/Deferred. SoftDefer — причина scheduler defer, а не новый terminal owner. Hide допускается только при доказанном repair demand, который наблюдает один lifecycle. RenderReady, PendingLight и FullyDark становятся производными агрегатами, не конкурирующими writable flags.

## Пошаговый план работ

### Этап 0 — зафиксировать воспроизводимость и разделить дефект-классы (P0)

**Работа**

- Сформировать clean baseline и кандидат с манифестом: commit + dirty diff, hash exe, GPU/driver/API, разрешение, render/light distance, seed/save hash, render settings, route/input hash, speed, cold/warm, frame rate и каждый checkpoint.
- Нужны четыре воспроизводимых сценария: пустой first-mesh; drawable stale-light remesh; legal-dark cave; пограничный сосед load/unload. Отдельно: slow walk, sustained flight при production speed, flight scale=1 stress, stop-and-drain, быстрый yaw/strafe. Дальний маршрут должен быть длиннее текущих 304 блоков и достигать явно заданных расстояний/checkpoints.
- Записывать в контрольные моменты screenshot + depth/object-id или equivalent renderer oracle и chunk coordinate. Для каждой проблемной координаты сохранить stage trace одного attempt.
- INVALID_RUN при missing/неизвестном mandatory manifest field; прокси и human-eye verdict не смешивать.

**Готово, когда** одинаковый input воспроизводит дефект и однозначно различает black bake, missing mesh, false cull, seam hole и legal-dark. Baseline/candidate относятся к clean/known source и одному сценарию.

### Этап 1 — ввести единый per-slice demand как единственного владельца (P0)

**Работа**

- Перенести визуальный attempt в ChunkRenderDemandStore либо заменить оба старых stores одной записью. Запись keyed by {x,y,z, world_epoch, incarnation}; больше не создавать независимые static attempt counters.
- Добавить явный lifecycle Requested → Capturing → Meshing → UploadPending → Published и терминальные причины LegalDark, EmptyAir, CancelledSuperseded, RetryableFailure. Attempt меняет стадию только с тем же ID.
- Определить прогресс как завершённую стадию/изменение version, не постановку queue item. Watchdog отсчитывает от last useful progress; expired attempt отменяется/переоткрывается тем же demand с новой generation.
- Сначала включить новую запись в audit/shadow mode, сравнивая решения. После parity по routes выключить legacy writers и удалить старые статусы по одному, а не держать два authoritative пути.

**Готово, когда** все hide/draw/queue transitions можно связать с текущим demand attempt; no attempt can be cleared by stale completion; shadow mismatch = 0 на полной матрице.

### Этап 2 — нормализовать input snapshot и публикацию (P0/P1)

**Работа**

- Один MeshSnapshotBuilder для mesh worker, GPU extraction и relight path. Все входы явны: voxel/material/fluid/light revisions, catalog generation, chunk incarnation, boundary coverage policy/generation, world epoch.
- Выбрать boundary semantics: неизвестный сосед не маскировать под «известный воздух»; provisional shell/overlay имеет свой generation и явное закрытие после peer publish.
- MeshBuildResult несёт те же dependency stamps, что snapshot. При apply сравнить с current live dependency set; superseded result получает retryable discard с причиной.
- MeshPublishTransaction: старый drawable ресурс остаётся активным пока новый буфер готов; bind/swap происходит на render owner атомарно; old range retired только после GPU completion/fence.

**Готово, когда** тесты с neighbor load/unload, concurrent edit/light change, definition reload, delayed worker и reordered completion не могут опубликовать результат, собранный из несовместимых входов; прежний drawable не пропадает при retry/OOM.

### Этап 3 — один fair scheduler с измеримым временем до draw (P0)

**Работа**

- Конвертировать старые admission policies в score/input к одному scheduler. Приоритет вычислять из camera frustum + predicted path/time-to-visible, miss age, stage/deadline, replacement criticality, expected cost и memory pressure.
- Сохранить две логические полосы (FirstMesh и Repair/Remesh), но выдать обеим гарантированные минимальные доли ресурса и aging; scheduler может перераспределять оставшийся budget, но не ставить repair cap в ноль при активном near-focus LightRepair. Не резервировать Capture только по количеству, если byte/millisecond budget уже исчерпан.
- Каждая стадия получает bounded CPU ms/bytes/slots. Неначатую работу оставлять в очереди с неизменным age; дедуплицировать по chunk+target versions, заменяя устаревшую работу новой целью. In-flight CPU/GPU — отдельный state, не phantom queue owner.
- Бюджет считать по фактическому cost class/измеренной длительности, а не только chunks/frame. Ограничить World.cpp/ChunkEmergeCoordinator дорогие scans time-slice cursor-ом.

**Готово, когда** на focus demand есть ограниченный верхний предел от запроса до capture, от capture до worker complete и от complete до publish; ни один class не starvation при постоянных дырах. Размер очереди может быть большим, но oldest focus age обязан падать.

### Этап 4 — развести geometry, light и legal darkness (P0)

**Работа**

- Определить LightRepair сравнением desired_light_rev и опубликованного meshed-light stamp, плюс отдельной явной invalidation-причиной для неверного open-sky bake. Ревизия поля сама по себе не доказывает pixel correctness; vertex/face-light oracle должен иметь единое правило.
- LegalDark выдавать только при явном terminal evidence для конкретных slices; не классифицировать все равные/нулевые revisions как legal. Сохранить cave baseline без бесконечного remesh.
- PendingLight сделать aggregate по открытым child demands. Не очищать его по Empty==lit-ready или от наличия одного произвольного mesh в Y-band. Очищать только когда все затронутые slices terminal: published revisions удовлетворяют target или LegalDark одобрен.
- ClearPendingLightAfterMeshCommitted заменить на reducer по demand events; RenderReady/column readiness вычислять из актуальных child records.

**Готово, когда** тесты open sky, cave, fully-dark equal-revision, light revision advance и partial Y-band доказывают: open-sky repair имеет один конечный remesh attempt; LegalDark не зацикливает repair; clear никогда не теряет световой debt.

### Этап 5 — вырезать duplicate owners и локализовать модули (P1)

После прохождения Этапов 1–4 по одной группе отключать и удалять writer-ы: legacy ColumnEmergeStates mirror, лишние ColumnVisualState/ColumnRecord writable flags, обходы Admit*, duplicate MarkDirty ingress, отдельный SoftDeferHeld owner, восстановления через census, которые создают новую работу в обход demand scheduler. Перед каждым удалением перечислить заменяющий event и rollback.

Разнести code ownership:

- ChunkVisualDemand — состояние и чистые переходы;
- MeshSnapshotBuilder — live world → immutable capture;
- ChunkJobScheduler — очереди, fairness, resource budgets;
- MeshBuildWorkers — CPU-only computation;
- MeshPublishTransaction — freshness, residency, GPU swap/retire;
- LightRepairCoordinator — desired/applied light revisions и repair request;
- ColumnAggregate — reducer из child slices, без самостоятельной видимости.

**Готово, когда** production VisualObligationAllowsDraw — единственное решение show/hide; один demand owner; event/state transitions проверяемы отдельно от огромного World.cpp tick.

### Этап 6 — регрессионная матрица и оптимизация после корректности (P1)

Добавить и постоянно гонять:

1. CPU state-machine/property tests: duplicate requests coalesce; retry; cancel/supersede; stale completion; timeout; all chunk Y combinations; legal dark; zero/empty mesh.
2. Mesh tests: naive face reference vs greedy output, deterministic seam fixtures, unknown/pending peer, fluid boundary, edits during worker.
3. Renderer/driver tests: camera translate/rotate/FOV/resize invalidates opaque CullInputKey; transparent sort revision changes; old mesh remains through replacement; cull false-negative oracle; delayed fence and allocation pressure.
4. Product flights: spawn/enter, west corridor, true far flight, ocean, underfeet, fly-stop drain, edit during flight, warm repeat. Routes должны содержать одинаковые checkpoint positions и input traces в baseline и candidate.
5. Gates: zero unexpected black pixels/false-negative draws in controlled lit scene; visual-hole rate within agreed threshold; no near-focus hidden slice without valid repair ticket; repair ages within SLA; stop convergence; bounded hitch p95/p99; no regression of fluid/seam/material/transparent paths. Cave fixture отдельно разрешает LegalDark.

Только после этого на реальных traces решать, требуется ли GPU meshing rewrite, binary greedy meshing, chunk-size changes, far LOD/octree, или перенос CPU capture/other heavy scans. Для дальнего полёта coarse far terrain/LOD может уменьшить пустой горизонт, но он не исправит неверный light bake и не заменяет repair lifecycle.

## Порядок реализации и rollback

1. **P0 baseline + image/metadata trace**, никакого изменения policies.
2. **P0 per-slice demand + state transition model** в shadow, test harness и сравнение решений.
3. **P0 cutover draw predicate** на demand, сначала только в focus ring; старый code path сохранять flag-gated для короткого rollback.
4. **P0 scheduler fairness** для focus FirstMesh и LightRepair с явной stage aging.
5. **P0 light terminal reducer** и PendingLight derivation.
6. **P1 snapshot dependency stamps + publication transaction**; затем seam/neighbor overlay unification.
7. **P1 remove legacy owners** по одной responsibility и прогонять всю matrix на каждом шаге.
8. **P2 измеренные perf/LOD changes**; не перемешивать их с состоянием readiness.

Откат — на последнюю фазу по feature flag/state shadow; не возвращать целиком старый commit, потому что history сохраняет полезные fixes по GPU lifetimes, stamp validation, camera culling, face coverage, async jobs и per-chunk demand.

## Исполнение: baseline Release/no-teleport, 2026-09-24

- Чистая Release-сборка `61a2b496`, executable SHA-256 `0401ec84…1fef4e97`.
- Первую попытку `product-174657-far` остановил по сигналу оператора о чрезмерной скорости. В ней scenario сам выставил `CUBA_FLIGHT_MOVE_SPEED_SCALE=28`; артефакт `baseline_far.json` и perf-log сохранены как **непригодный для штатной скорости speed-stress/прерванный запуск**, acceptance из него не делать.
- Повторный `product-174657 --visible`, `teleport_cruise=false`, scale `1`, Release exe без изменений. Harness подтвердил старт `(7,3)`, конец `(-12,3)`, 19 чанков / 304 блока. Результат: `holes_rate=1.0`, `fly_visible_black_max=48`, `dirty_med/max=477/744`, `wall_med/fly_med=16.06/17.51 ms`; eye-proxy и post-stop convergence FAIL. Это воспроизводит rendering-дефект на нормальной скорости; маршрут покрывает west corridor, не дальний стресс.
- Проверена collision-цепочка: `UCamera::ProcessKeyboard` → `UWorld::ResolveMovement` → `UWorldCollision::ResolveMovement`, с collision-aware axis stepping и движением камеры; `AppRunner` также динамически корректирует высоту по рельефу при CruiseEyeY. Поэтому штатный scale-1 запуск использует физическую коррекцию. Фар-сценарий при scale 28 не подходит для этой проверки: большой delta меняет условия collision stepping.
- `AppRunner` держит заданный yaw постоянным; отдельного горизонтального obstacle-avoidance/route planner в этом пути нет. Для данного запроса оператор подтвердил, что имеется в виду физическая остановка/смещение при столкновении. GUI был запущен, однако артефакты содержат telemetry, не screenshots или pixel oracle.
- Run artifacts (оставлены в рабочем дереве отдельно от production-коммита): [scale-1 report](../../bin/suite_reports/engine_refactor/baseline_nominal.json), [scale-1 perf](../../bin/logs/perf_20260924-202153_24360.jsonl), [scale-28 interrupted report](../../bin/suite_reports/engine_refactor/baseline_far.json), [scale-28 perf](../../bin/logs/perf_20260924-200557_24204.jsonl).

## Исполнение: VisualObligation shadow, 2026-09-24

- Коммит `131960ea` добавил opt-in `CUBA_VISUAL_OBLIGATION_SHADOW=1`: per-slice `(x,y,z)` classifier сравнивается с legacy draw decision, но не меняет его. В perf JSONL записываются кумулятивные `visual_obligation_shadow_sample_n` и `visual_obligation_shadow_mismatch_n`. Без env-флага draw path не делает дополнительных запросов.
- Release-сборка прошла. Повторный GUI `product-174657` при scale `1`, без teleport, достиг 19 чанков / 304 блока. За 45 периодов зарегистрировано 304004 shadow samples и 0 draw mismatches; `holes_rate=0.837`, `fly_visible_black_max=48`, `dirty_med=486`, `wall_med/fly_med=13.39/14.60 ms`; post-stop convergence и near-hole gates остаются FAIL. Это поддерживает следующий приоритет: восстановление работы/публикации repair-demand, а не замена draw predicate сама по себе. Одного маршрута недостаточно для глобального cutover — следующий шаг ограничить его focus ring и перепроверить.
- Артефакты: [shadow flight report](../../bin/suite_reports/engine_refactor/visual_shadow_nominal.json), [shadow perf log](../../bin/logs/perf_20260924-205429_28160.jsonl).
- Отдельный кодовый аудит полёта: `AppRunner` держит постоянный yaw и отправляет W через `UCamera::DoMovement`; FreeMove доходит до `UWorld::ResolveMovement` с capsule collision. Это даёт physical stop/axis-slide, но не изменяет yaw/маршрут и не вызывает walking step-up animation. Текущий product route не был поставлен так, чтобы гарантированно задеть obstacle, поэтому collision response отдельно не считается подтверждённым этим полётом.
- Commit `06cf0635` добавил обратимый `CUBA_VISUAL_OBLIGATION_CUTOVER=1`, применяющий per-slice draw classification только внутри focus radius. Повторный Release-пролёт: `holes_rate=0.837`, `fly_visible_black_max=48`, `dirty_med=487`, `wall_med/fly_med=14.11/14.73 ms`, движение `5.4` блока/с; 389550 samples / 0 shadow mismatches. Число holes не изменилось, stop convergence всё ещё FAIL. Cutover оставлен opt-in: он показывает эквивалентность решения draw в этом маршруте, но не ремонтирует mesh miss/repair progress.
- Артефакты: [focus cutover report](../../bin/suite_reports/engine_refactor/focus_cutover_nominal.json), [cutover perf log](../../bin/logs/perf_20260924-211203_6468.jsonl).
- Следующий кодовый срез: `ClearPendingLightAfterMeshCommitted` проверяет наличие mesh и состояние GPU/inflight в band текущего focus-y, хотя `PendingLightBeforeMesh` хранит точные `min_y/max_y`; проверка может относиться к другим chunk slices. Согласовать reader с диапазоном ticket, сохранив двухэтапное завершение dirty/remesh и terminal LegalDark/LitReady.
- Коммит `05f1d033` поменял PendingLight clearance на ticket range и проверку каждого resident non-air child slice (mesh или live capture/GPU owner; air-only slice terminal). Release no-cutover shadow flight сохранил `holes_rate=0.837`, `fly_visible_black_max=48`, `dirty_med=484`, `wall_med/fly_med=13.21/12.26 ms`; `pending_light_focus` median=4, `cruise_unlit_med=0`, `post_stop_missing_max=90`, convergence FAIL. 364668 samples / 0 draw mismatches. Это targeted correctness fix без доказанного улучшения screen-hole rate.
- Лог этого прогона показывает следующий bottleneck: near miss остаётся с `miss_stuck_run_frames=5085`, demand unsatisfied geom/light/face `768/160/47`, `mesh_dirty_schedule_skip_snapshot_n=95`, `mesh_dirty_schedule_ok_fm_n=3`, `mesh_dirty_schedule_ok_remesh_n=1`; доминирует `empty_fm_queue`, а oldest missing resident age — 1395 frames. Нужен разбор причины snapshot Deferred и aging/fair service для устойчивого FirstMesh и LightRepair owner. [Pending-range report](../../bin/suite_reports/engine_refactor/pending_range_shadow_nominal.json), [perf](../../bin/logs/perf_20260924-211841_28636.jsonl).
- Коммит `548055de` сохранил первый enqueue frame задачи в `UChunkDirtySet` и при активном focus miss обслуживает FirstMesh, ожидающий не менее 120 scheduler frames внутри focus radius, по FIFO-возрасту. Повторные `MarkDirtyPriority` сохраняют возраст; продвижение из Remesh тоже не обнуляет его. Это ограниченная fairness-защита, а не новый владелец demand или бюджет.
- Release-сборка и visible no-teleport `product-174657` после этой правки завершились. На маршруте в 19 чанков медианная измеренная скорость составила `5.70` блока/с; 372801 shadow-проверка не нашли draw-decision mismatches. При этом `holes_rate=0.860` (предыдущие прогоны `0.837`), `fly_visible_black_max=48`, `post_stop_missing_max=90`, stop convergence и eye-proxy/A24 визуальные gates остались FAIL. `dirty_med=429` ниже прежних ~484, но этот одиночный прогон не подтверждает причинного улучшения. Report имеет `pass=false`; продуктовые near-hole gates действительно упали, а manifest также не получил `git_sha` от harness.
- Артефакты fairness-прогона: [report](../../bin/suite_reports/engine_refactor/firstmesh_fairness_nominal.json), [perf JSONL](../../bin/logs/perf_20260924-220931_15776.jsonl). Старый Far speed-stress не включался; ходовая скорость 5.1–6.0 блока/с согласуется с nominal scale-1 прогонами. Этот сценарий не гарантирует столкновение с препятствием и в данных нет collision impact/blocked-distance счётчика; физический stop/slide остаётся проверенным по коду, но пока не подтверждён отдельным controlled contact run.
- Коммит `7ff14458` уже разделил причины snapshot `Deferred` в telemetry. Не расширять приоритет FirstMesh только на основании очереди: следующий шаг — проследить на одних и тех же per-slice координатах цепочку FirstMesh capture/admission, LightRepair ticket → relight apply → mesh enqueue → GPU publication. Для collision нужно добавить измерение requested-vs-resolved camera displacement и провести короткий no-teleport contact flight с контролируемым obstacle.

## Исполнение: резерв FirstMesh и проверка видимого прогона, 2026-09-24

- Коммит `0abcfbd4` добавил отдельный count-slot для одного ожидающего focus FirstMesh: remesh slice исполняется до FirstMesh и мог расходовать общий refresh count. Также visible GLFW window явно повторно показывается и получает focus после GL-инициализации; приложение пишет в лог результат `GLFW_VISIBLE`. Release-сборка прошла, статическая Windows executable verification — PASS.
- Пользователь подтвердил, что в этом прогоне окно видел. Прогон `product-174657` был no-teleport, Release, 45 периодов, 19 чанков / 304 блока; измеренная скорость в движении — медиана `5.1` блока/с (диапазон `5.1–6.0`), то есть прежнее замечание о завышенной скорости в этом сценарии не воспроизвелось. Артефакты: [report](../../bin/suite_reports/engine_refactor/firstmesh_reserved_visible.json), [perf JSONL](../../bin/logs/perf_20260924-225938_22472.jsonl). Окно видно; отдельного pixel screenshot в артефактах нет.
- Кодовый счётчик подтверждает, что FirstMesh стал получать обслуживание чаще: на движущихся периодах `mesh_dirty_schedule_ok_fm_n` медиана выросла с `0` до `1`, `dirty_fm_n` снизилась с `379` до `224`. Но графический симптом не улучшился: отчет `pass=false`, `holes_rate=1.0` (в этом прогоне `hole_key=unfinished_visual`, это не частота pixel-hole), медиана `unfinished_visual=90` против `73` в fairness-прогоне, `visible_black_focus_n` медиана `35`, максимум `58`; после остановки остаются missing и convergence FAIL.
- Черная нагрузка остается в основном FullyDark repair: moving-period медианы `visible_black_fully_dark_repair_n=25`, `visible_black_fully_dark_stalled_n=4`, `visible_black_fully_dark_no_ticket_n=0`, `visible_black_stale_lit_n=0`. `relight_fifo_n` медиана `11`, `relight_completed_n` медиана `0`; `mesh_dirty_schedule_ok_remesh_n` медиана `2` против `4` в прежнем fairness-прогоне. Одновременно snapshot refresh-count defer медиана `160`, при `mesh_snapshot_ms` медиана `0.31 ms`; прочие defer-причины в moving periods нулевые. Это локализует дефицит в обслуживании/согласовании очередей и счетного бюджета, но один прогон не доказывает причинность изменения FirstMesh reserve.
- Решение: не увеличивать FirstMesh reserve дальше. Приоритет — пер-slice связать уже существующие demand attempt, relight queue, dirty/remesh schedule и фактический GPU publish, затем гарантировать service для обоих классов при замере oldest-demand age. Текущий маршрут не задевает препятствие, поэтому collision stop/slide по-прежнему требует отдельного controlled contact flight.

## Исполнение: snapshot quota baseline, 2026-09-25

- Коммит `87de051a` разделил высокочастотный cull trace и lifecycle trace: они больше не вытесняют записи друг друга. Release сборка прошла; в видимом no-teleport `product-174657` harness подтвердил нормальную скорость `5.99991` блоков/с и маршрут в 19 чанков / 304 блока.
- Симптом воспроизведён: `holes_rate=0.837`, `visible_black_focus_n` median `26`, чёрное состояние держалось до 43 периодов; после остановки `post_stop_missing_max=90`, demand convergence — FAIL. Это не краткий flicker.
- Раздельные shutdown-буферы сохранили 64 `job_trace` и 64 `cull_trace` записи. Lifecycle ring содержит реальные `admitted`/`published` записи (не более частые cull-события), но ещё не описывает весь путь capture → worker → GPU finish; это остаётся пробелом диагностики.
- На устойчивом miss `dirty_fm_n=12`, `dirty_remesh_n=49–54`, `first_mesh_schedule_effective_cap=12`, но за период проходит лишь `1–2` FirstMesh и `0–2` remesh. `mesh_dirty_schedule_skip_n=33–34` целиком совпадает с `mesh_snapshot_defer_refresh_budget_n`; pipeline skip, worker in-flight и pending GPU в tail равны нулю.
- На том же tail snapshot work занимал около `0.33–1.01 ms`, при этом scheduler repeatedly исчерпывал count quota. В коде эта квота берётся как `int(MeshSnapshotBudgetMs * 0.35)`, затем дополнительно делится между FirstMesh/LightRepair reserve. Это сильнее указывает на count quota до CPU/GPU очереди как на текущий bottleneck, чем на медленный GPU publication. Данные не исключают другие причины для отдельных зависших срезов.
- Долгий focus miss (`cx=-12, cz=3`, один из незакрытых Y slices) сохранялся 36 последовательных report frames; после stop пять низких Y slices оставались missing. Dominant analyzer labels `empty_fm_queue` / `ticket_stuck` сами по себе недостаточны: runtime capture показывает ненулевую FirstMesh очередь и нулевой in-flight/GPU backlog.
- Следующий ограниченный эксперимент: заменить фиксированное число refresh captures на оценку из измеренной стоимости snapshot и `MeshSnapshotBudgetMs`; уже существующая проверка `LastMeshSnapshotMs` остаётся жёстким временным пределом. Сравнить capture defers, FM/remesh service, frame time и post-stop convergence. Не менять FirstMesh reserve повторно и не расширять GPU apply quota в этом эксперименте.
- Артефакты: [раздельный trace report](../../bin/suite_reports/engine_refactor/separate_trace_visible.json), [perf JSONL](../../bin/logs/perf_20260925-101748_24408.jsonl).

## Исполнение: capture budget и scheduler clamp, 2026-09-25

- Коммит `e83255c2` заменил `0.35 * snapshot_ms` count quota на bounded refresh count, вычисленный по rolling EMA стоимости snapshot; прежняя проверка фактически потраченного snapshot time осталась главным пределом. Release build и статическая проверка EXE — PASS.
- Тот же видимый no-teleport маршрут: нормальная скорость `5.99991` блока/с; `mesh_snapshot_defer_refresh_budget_n` и time-budget defer в последних периодах стали `0` (до изменения refresh-budget defers были `33–34`). В tail `mesh_snapshot_ms≈0.17–0.39 ms`.
- Симптом полностью не исправлен: `holes_rate=0.837` (без улучшения относительно предыдущего trace baseline), `fly_visible_black_max=68`, `post_stop_missing_max=99`, post-stop convergence — FAIL. `visible_black_focus_n` снизился в tail до `14`, но FullyDark stalled median в середине маршрута вырос до `20`; eye-proxy и A24 частные stop-lines PASS, dual-lane stop-line и общий report FAIL.
- В tail `mesh_schedule_final=2` при `first_mesh_schedule_cap=14`, effective FirstMesh cap `12`, remesh cap `2`, очереди `dirty_fm=9–13` и `dirty_remesh=52–64`. При этом snapshot/pipeline skips равны нулю, а throughput остаётся `1` FirstMesh + `1` remesh за период. Из кода ровно один clamp выставляет общий schedule к `2`: «saturated async on lit cruise» (`moving && !visual_holes && !missing_underfeet && pending_async >= 28`). Он не учитывает FullyDark/stale-light debt, который на этом же прогоне остаётся видимым.
- Следующий targeted change: разрешать этот no-hole saturation clamp только когда отсутствуют FocusMissingMesh, FullyDark repair debt и stale vertex-light debt. Оставить admission lane caps и реальные временные бюджеты; это будет проверка scheduler classification, а не увеличение произвольной квоты.
- Артефакты: [cost-based budget report](../../bin/suite_reports/engine_refactor/capture_cost_budget_visible.json), [perf JSONL](../../bin/logs/perf_20260925-103111_25520.jsonl).

## Исполнение: emergency floor не видел light-repair debt, 2026-09-25

- Коммит `b0112c2c` исключил насыщенный cruise clamp при известных repair obligations, но повторный маршрут всё ещё показал `mesh_schedule_final=2` в хвосте: `phase_abort_heavy=1` и `abort_schedule_final=2`. Значит следующая ветка AbortDrip перекрывает cruise policy.
- В том же хвосте: `focus_missing_mesh=0`, `visible_black_fully_dark_repair_n=12`, `draw_oracle_fully_dark_debt_n=12`, `draw_oracle_stale_vertex_light_n=12`; `relight_fifo_n` и `pending_light_focus_n` к остановке опустились до нуля, хотя чёрный ремонт сохранился. Поздний AbortDrip reinforce поднимал schedule только по missing mesh, underfeet и SoftDefer evidence, поэтому black mesh без missing geometry выпадал из service floor.
- Итог прогона не прошёл: `holes_rate=1.0`, `fly_visible_black_max=82`, FullyDark stalled median `19`, `post_stop_missing_max=99`, demand convergence FAIL. `schedule_ok_zero_rate=0` и eye-proxy/A24 частные checks PASS не меняют общий визуальный FAIL. Скорость осталась `5.99991` блоков/с.
- Следующая правка расширяет только measured repair branch: при FullyDark/stale-light debt AbortDrip сохраняет небольшой schedule floor и четыре remesh slots; обычные abort и frame-time budgets остаются.
- Артефакты: [repair-debt scheduler report](../../bin/suite_reports/engine_refactor/repair_debt_schedule_visible.json), [perf JSONL](../../bin/logs/perf_20260925-103818_11176.jsonl).

## Исполнение: repair floor переживает phase abort, 2026-09-25

- Коммит `537b8c7e` сохраняет четыре remesh admission slots и schedule/drain floor 6 при фактическом FullyDark или stale-vertex-light repair debt. Floor проходит и ранний, и поздний AbortDrip clamp; telemetry cap синхронизирован с admission, переданным mesh service.
- Release build и статическая проверка Windows executable — PASS. Видимый no-teleport `product-174657` завершился штатно: 45 периодов, 19 чанков / 304 блока, скорость `5.99991` блока/с. Это тот же короткий west corridor, не дальний маршрут.
- Политика сработала как задумано: при `phase_abort_heavy=1` schedule и abort schedule держались на 6 (пики 16), remesh cap был 4; по moving periods remesh scheduling-ok вырос до медианы 4. В одном сравнительном прогоне pending-GPU максимум уменьшился с 241 до 149; видимый чёрный focus максимум снизился с 82 до 75, медиана — с 20 до 17, post-stop missing — со 112 до 90. Одного прогона недостаточно, чтобы приписать всё улучшение floor.
- Исправления проблемы пока нет: общий report `pass=false`, `holes_rate=1.0`, route `visible_black_focus_n` остаётся ненулевым, а после остановки FullyDark/stale-light debt держится на 17 и `post_stop_demand_stop_converged=false`. Pending GPU queue к остановке опустилась до 0, relight FIFO — до 1, но чернота не исчезла. Значит queue starvation была частью проблемы, но floor одного общего remesh lane не связывает repair с точными slice coordinates и не доказывает, что корректный light target был опубликован для каждого чёрного среза.
- Во время движения queue всё ещё накапливает до 149 pending GPU applies; moving-period медианы — 16 pending light FIFO, 17 FullyDark/stale-light obligations и 86 unfinished visual. Это требует backpressure/coalescing по downstream capacity, иначе дополнительная admission может обменивать задержку ремонта на растущий GPU backlog.
- Следующая работа: писать bounded per-slice trace для одного и того же `{x,y,z, incarnation, attempt_id}` через obligation → relight ticket/apply → dirty lane/admission → GPU kick/finish → publication, с terminal reason и target/published light stamp. Отдельно измерить oldest repair age и сохранить координаты оставшихся black obligations после stop. До этого не увеличивать count caps дальше.
- Артефакты: [repair abort floor report](../../bin/suite_reports/engine_refactor/repair_abort_floor_visible.json), [perf JSONL](../../bin/logs/perf_20260925-104920_23488.jsonl).

## Исполнение: поздний вертикальный seam debt и повторный no-teleport прогон, 2026-09-25

- Коммит `d2a8691a` добавил deduplicated FIFO для вертикального FaceDebt. Она повторно проверяет до восьми срезов за тик, переживает отсутствие/отставание соседа и сбрасывается при смене world epoch. Когда поколение drawable-соседа достаточно, срез передаётся штатному `PendingMeshDependencyInvalidations_`; отдельный прямой Dirty-поток не добавлялся. Release-сборка и статическая проверка EXE прошли.
- Видимый no-teleport `product-174657` снова завершился с корректной скоростью `5.1` блока/с, 19 чанками и 304 блоками пути. Полный report `pass=false`, `holes_rate=1.0`, post-stop convergence — FAIL. Сравнение с точным light-halo flight не показывает убедительного выигрыша: `fly_visible_black_max=69` против `71`, `visible_black_focus_n=42` против `16`, stale-dark near `49` против `24`, post-stop black `11` против `9`, missing `92` против `93`, stop dirty delta `35` против `34`. Одного маршрута и этих малых разнонаправленных дельт недостаточно для причинного вывода; вертикальный debt не является доминирующим ограничением.
- Последний opt-in trace содержит 256 повторно отобранных black-срезов: 250 классифицированы как `StaleDarkWithLitField`, у 236 совпадают center light revisions, 149 имеют Dirty и 8 — in-flight. Это счётчики trace-сэмплов, не число уникальных чанков. Большая часть симптома остаётся у drawable-срезов с недоопубликованной geometry revision и живым Dirty/attempt ownership, поэтому текущий face-debt repair закрывает только узкую ветку.
- Важное ограничение диагностики: `MarkRelitInstall.cpp` пишет в универсальные `JobStageSpan.desired_rev/published_rev` значения light revision, но в `source_rev` — mesh revision. Например, повторные `admitted` записи `(-15,2,6)` показывают `desired_rev=published_rev=12` и `source_rev=210…214`; эти поля нельзя интерпретировать как одну геометрическую последовательность. В следующем trace нужно разнести `desired_geom_rev`, `source_geom_rev`, `published_geom_rev` и независимые light stamps, а затем связать этапы того же attempt ID, включая причину, почему admission остаётся без capture/worker/GPU прогресса.
- Артефакты: [vertical debt report](../../bin/suite_reports/engine_refactor/vertical_debt_reconcile_visible.json), [perf JSONL](../../bin/logs/perf_20260925-133901_24592.jsonl). Маршрут остаётся коротким и не заменяет исходную far-distance проверку.

## Исполнение: скорость far harness и demand identity, 2026-09-25

- Ошибочный far запуск автоматически получил `CUBA_FLIGHT_MOVE_SPEED_SCALE=28` из `tools/flight_sim_run.py`, хотя рядом в комментарии и A37 evidence документирован scale `12`. Оператор подтвердил чрезмерную скорость. Запущенный мной видимый процесс был остановлен; partial log/report сохранены как невалидный speed-stress. За 74 периода focus дошёл до `cx=-472`, нагрузка достигла 3456 resident chunks, wall median вырос до ~195 ms; это не baseline renderer и не корректная проверка collision response. Коммит `c1aa74a9` вернул default `12`, сохранив возможность явного override.
- Затем выполнен длинный nominal no-teleport flight с `CUBA_FLIGHT_MOVE_SPEED_SCALE=1` и видимым GUI: 167 периодов, 108 чанков / 1728 блоков по маршруту, median `movement_speed=5.99853` блоков/с. Он не дошёл до far checkpoint 8192, поэтому считается длинным speed-correct renderer repro, а не far-distance acceptance. `holes_rate=1.0`, `fly_visible_black_max=71`, `visible_black_focus_n=5`, post-stop black=0, но `post_stop_missing_max=90` и demand convergence — FAIL. Eye-proxy был PASS в covered corridor; A24 whole-route hole gate — FAIL, corridor hole periods — 0. Не смешивать эти агрегаты.
- Тот же trace показал разрыв между моделью demand и chunk lifetime: все 256 последних visual-black записей имеют `world_epoch=0` и `incarnation=0`, хотя `Chunk::Incarnation` назначается ненулевым. В `ChunkRenderDemandRecord` эти поля объявлены, но не заполняются ни одним production writer; store индексируется только координатой. При этом `UChunkMeshCache::RemoveChunk/RemoveColumn` очищает mesh, dirty, revision и capture state, но demand record не удаляет. После выгрузки и повторного появления `{x,y,z}` старые published/desired revisions, face debt или attempt могут быть приняты за состояние новой incarnation. Это пока не доказанная первопричина black bake, но это прямой correctness-дефект stale-demand/convergence и высокий риск для capture/apply ownership.
- Следующий срез: связать запись demand с `{world_epoch, chunk_incarnation}`, сбрасывать именно эту запись при unload, и на каждом result/update отвергать mismatch до изменения current demand. Затем проверить trace: ненулевые identities на всех новых demand records, ноль stale updates после unload/reload, корректные face-debt поколения; только потом повторять speed-1 route и отдельно far-scale route.
- Артефакты: [invalid scale-28 partial report](../../bin/suite_reports/engine_refactor/vertical_debt_reconcile_far_visible.json), [invalid scale-28 perf](../../bin/logs/perf_20260925-134446_29832.jsonl), [long speed-1 report](../../bin/suite_reports/engine_refactor/long_nominal_speed1_visible.json), [long speed-1 perf](../../bin/logs/perf_20260925-134829_28620.jsonl).

## Исполнение: штатная скорость, demand identity и временная шкала попыток, 2026-09-25

- Оператор повторно сообщил о сверхбыстром пролёте. Причина оставалась в far harness: multiplier `12` всё ещё задавал примерно 60 блоков/с вместо штатных 5–6. Коммит `6af09ab3` установил default `CUBA_FLIGHT_MOVE_SPEED_SCALE=1`; явный override оставлен только для намеренных speed-stress запусков. Видимый маршрут следует измерять при `1×`, дальность набирать длительностью.
- Release-прогон после speed-fix: видимый `product-174657`, `--no-teleport-cruise`, 30 секунд движения, 16 fly periods, 10 чанков / 160 блоков; median `movement_speed=5.09994` блока/с. Это устраняет некорректно быстрый маршрут и подтверждает штатную скорость; это по-прежнему короткий west corridor, не far acceptance.
- Commit `bc1cb500` привязал production demand writers к `{world_epoch, chunk_incarnation}`, очищает demand при удалении mesh slice и сбрасывает identity-free/чужую запись при первом bind новой incarnation. В новом `visual_black_trace` все 256 записей имеют ненулевые epoch/incarnation (99 различных incarnation), что подтверждает заполнение identity на исследуемом пути.
- Чёрные чанки остались. До исправления времени demand trace дал 193/256 `StaleDarkWithLitField`, 35 `FullyDarkPendingRepair`, 28 `FullyDarkStalledTicket`; у 150 из 193 stale samples `desired_geom_rev != published_geom_rev`, при этом только у 5/193 не совпадали field и meshed light revision. 125 stale samples несли active attempt; 82 были на стадии `Admitted`, 51 имели dirty и 10 — in-flight. Это повторные trace samples, не уникальные чанки. FaceDebt не доминирует над геометрическим отставанием.
- Одновременно найден clock-contract bug: `MarkRelitInstall.cpp` передавал `span.stage_ms` (длительность операции) в `NoteStageProgress`, где параметр — монотонное абсолютное время; локальный `ReconcileMaintenance` также вызывался с default `now_ms=0`, который трактуется как истёкшая grace period. Commit `b7b3668a` передаёт `VisualObligationNowMs()` в обе точки. Это устраняет недостоверный stall/orphan age, но само по себе не доказано как первопричина визуального дефекта.
- Повторный видимый speed-1 маршрут после timestamp-fix прошёл те же 10 чанков с median `5.09994` блока/с. Итоговые критерии: `holes_rate=1.0`, `fly_visible_black_max=75`, focus median=67, `post_stop_visible_black_max=90`, `post_stop_missing_max=91`, demand convergence FAIL; eye-proxy и A24 corridor checks PASS. Это не paired experiment: изменение счётчиков нельзя приписывать timestamp fix. Второй trace: ноль identity gaps; причины 218 stale / 24 pending / 14 stalled из 256 повторных samples.
- Критическая граница наблюдаемости остаётся в `job_trace`: в shutdown dump 54/64 и 55/64 sampled mesh stage events имеют `world_epoch=0, incarnation=0`, хотя соседний visual-black trace заполнен. `JobStageSpan.incarnation` уже уже `uint32_t`, чем реальный `UChunk::GetIncarnation()` (`uint64_t`); writers частично оставляют identity нулём, а старые `desired_rev/published_rev` в MarkRelit смешивают light и geometry domains. Поэтому black sample с attempt ID пока нельзя надёжно соединить с его Admit → capture/build → upload → publish/reject цепочкой. Следующий срез — сделать typed revision fields и полную identity для job spans, затем сопоставить долгоживущие `Admitted` attempts с capture-defer reason, result epoch, GPU apply и terminal publication.
- Артефакты: [speed-1 trace report](../../bin/suite_reports/engine_refactor/speed_control_product174657_20260925.json), [speed-1 perf](../../bin/logs/perf_20260925-140621_5684.jsonl), [clock-fix report](../../bin/suite_reports/engine_refactor/demand_clock_product174657_20260925.json), [clock-fix perf](../../bin/logs/perf_20260925-141457_12892.jsonl).

## Исполнение: трасса CPU mesh worker, 2026-09-25

- Оператор подтвердил, что последний полёт был нормальной скорости. Повторный видимый `product-174657`, `World_164`, без teleport, с явным `scale=1` измерил `5.09994` блока/с и прошёл 10 чанков / 160 блоков. Скорость не менялась; текущий запуск служит baseline для новой трассы.
- Коммит `ef20ffbd` добавил `Started` и `Built` в `job_trace`, внутренний `job_id` отдельно от render-demand `attempt_id`, плюс длительность CPU worker. Это telemetry-only изменение. Release build и статическая проверка Windows EXE прошли.
- Свежий report `async_stage_trace_product174657_20260925.json`: `holes_rate=1.0`, `fly_visible_black_max=74`, `visible_black_focus_n=66`, `post_stop_visible_black_max=86`, `post_stop_missing_max=92`, demand stop convergence — FAIL. Eye-proxy и A24 остаются частными corridor/safety gates и не отменяют общего визуального FAIL.
- Shutdown dump содержит 128 job events: 79 `published`, 21 `built`, 19 `started`, 9 `admitted`; все 128 имеют world epoch, incarnation, source geometry и source light stamps. Для 97/128 event rows заполнен render attempt. 19 внутренних job ID имеют пару `Started` + `Built`; время worker build: median 4.886 ms, max 7.204 ms. Это не похоже на долгую CPU-генерацию mesh для этих выборочных jobs.
- В 256 black samples: 195 `StaleDarkWithLitField`, 26 `FullyDarkPendingRepair`, 35 `FullyDarkStalledTicket`; у 152/195 stale samples desired geometry опережает published geometry. В 3 stale samples trace отмечает in-flight, в 77 — dirty, в 133 — активную попытку. Это повторные samples, не число уникальных chunk slices.
- Сопоставление по `(coord, attempt_id)` находит `Published` span у 18/21 собранных worker jobs. Три построенных результата не имеют соответствующего Published span в shutdown dump. Это только отсутствие связанного события в bounded trace: без propagation `job_id` в `ApplyMeshResult`/`PendingGpuApply` и terminal reason нельзя отличить незавершённый GPU pipeline от отмены, rejection, замены попытки или пропуска telemetry.
- На этом прогоне dominant schedule blocker — `empty_fm_queue`, completion stall — `complete`; CPU worker build быстрый, но общая чернота сохраняется. Это указывает на границу после worker или на churn целевых demand revisions, но не позволяет объявить причиной GPU fence или конкретную очередь. Не увеличивать admission caps по одной этой трассе.
- Следующий диагностический срез: перенести неизменный `job_id` вместе с captured attempt и identity через CPU result, GPU queued/dispatched/kicked/fence-complete и конечный commit/reject/cancel. Для каждого terminal event писать typed source/target/published stamps и reason; отдельно записать повторное изменение target revision и возраст oldest focus demand. После этого можно выбрать scheduler/backpressure fix либо исправление publication lifecycle на основании одной coordinate-level цепочки.
- Артефакты: [async-stage report](../../bin/suite_reports/engine_refactor/async_stage_trace_product174657_20260925.json), [perf JSONL](../../bin/logs/perf_20260925-144029_9972.jsonl).

## Исполнение: stale GPU-pass completion и ticket backlog, 2026-09-25

- Сверка последнего `gpu_trace_followup` обнаружила, что `GreedyGpuPublication.cpp` записывал повторную загрузку уже resident CPU mesh в общую job-stage трассу как `Published` и передавал текущий attempt ID в `NoteInstallResult`. Это не результат соответствующего mesh job: pass загружает имеющийся `GreedyMeshBatch`. В ряде срезов target/source geometry был 800+, а resident CPU mesh — 230–280. Такое событие могло закрыть свежий активный demand старым артефактом, одновременно вытесняя реальные worker events из кольца на 256 строк.
- Коммит `4ca7e5ea` ограничил изменение lifecycle в GPU pass точным совпадением его CPU source geometry/light revisions с текущим demand; повторная публикация старого артефакта не закрывает и не отменяет активный demand. GPU-pass refresh больше не записывается в job lifecycle trace, поскольку он не имеет собственного mesh `job_id` и не является стадией CPU mesh job.
- Release-сборка и статическая проверка Windows EXE прошли. Видимый no-teleport прогон `product-174657`, `World_164`, сохранил скорость `5.09994` блока/с и прошёл 10 чанков / 160 блоков. Он воспроизвёл дефект: `holes_rate=1.0`, `fly_visible_black_max=71`, focus black `64`, post-stop missing `94`, demand convergence — FAIL. Это подтверждает, что stale-pass completion был lifecycle-дефектом, но не единственной причиной чёрных чанков.
- После удаления ложных GPU `Published` events bounded trace содержит реальные job spans: 82 `Started`, 79 `Built`, 81 `Published`, 14 `Admitted`, 85 различных job IDs. Для 79 sampled builds медиана CPU worker — 1.486 ms, максимум — 20.502 ms. В sampled `Started/Built/Published` source geometry совпадает с desired stamp; наблюдаемого медленного worker или stale-result apply в этих событиях нет.
- Контрольный период одновременно показывает backlog/ownership проблему: `dirty_remesh_n=52`, `dirty_fm_n=0`, запланировано 4 remesh jobs; cumulative `dirty_dropped=5472`, среднее отчёта — 149.14 за период. В black trace 179 активных demand samples имели стадии только `Created/Admitted`; 161 из этих samples не отмечали `Dirty`, ни один — CPU inflight или RAA, четыре — GPU pending. Это повторные samples из bounded census, не число уникальных срезов. Причина «ticket_stuck» в report — эвристический классификатор; сама по себе она не устанавливает владельца потерянной работы.
- Следующий срез — проследить точное владение между per-slice demand, column repair ticket, `Dirty`/`RemeshAfterApply`, dirty prune/drop и admission. Прежде чем поднимать scheduler caps, нужно выяснить, почему ticket остаётся живым при отсутствии slice queue/inflight owner и почему в движении FirstMesh lane опустошается при сохраняющемся missing-resident счётчике. Существующие flight counters — proxy/census, не pixel-level visibility oracle; вывод о видимой области кадра требует отдельного frustum/pixel доказательства.
- Артефакты: [guard run report](../../bin/suite_reports/engine_refactor/pass_pub_guard_product174657_20260925.json), [guard run perf](../../bin/logs/perf_20260925-151333_29432.jsonl).

## Исполнение: bounded ColumnFlow scan и повторный no-teleport прогон, 2026-09-25

- Коммит `d78f76e1` убрал head-of-line остановку `DrainBudget`: при deadline-deferred work executor теперь просматривает ограниченную часть очереди (4–16 элементов), временно сохраняет отложенные билеты и продолжает искать допустимый FirstMesh/repair. Выполненная работа по-прежнему ограничена исходным `n`; телеметрия `column_flow_deferred_n` считает повторные проверки, не уникальные билеты.
- Release visible no-teleport `product-174657`, `World_164`, завершился штатно на скорости `5.09994` блока/с, 10 чанков / 160 блоков; пользователь подтвердил нормальную скорость последнего пролёта. Отчёт `columnflow_hol_product174657_20260925.json` имеет `pass=false`: `holes_rate=1.0`, `fly_visible_black_max=67`, медиана visible-black focus `45`, `post_stop_missing_max=92`, demand convergence — FAIL.
- В 23 report periods `phase_abort_heavy=1` на каждом периоде. `DrainBudget` выполнил 17 билетов, отложил 325 проверок; `dirty_fm_n` имел медиану `32` и максимум `83`. За период FirstMesh scheduling median был `2`, Remesh — `4`; flow enqueue дал 19 FirstMesh dirty отметок. Это подтверждает, что обход deadline-deferred queue работает, но его throughput мал относительно сохраняющегося visual debt.
- Во время движения `pending_gpu_applies_n` имел медиану `31` и максимум `96` (`pending_gpu_queued_n`: медиана `29`, максимум `95`). При этом `mesh_pending_capture_n=0` и `mesh_worker_inflight_n=0` в report periods; GPU kick/finish продолжались, но не устранили backlog за маршрут. После остановки pending GPU очередь опустилась до `5`, однако `chunk_not_ready` остался `92` и demand convergence не наступил. Это отделяет остаточную missing-mesh проблему от простого ожидания уже поставленных GPU результатов.
- Black census по-прежнему в основном относит найденные drawable slices к `StaleDarkWithLitField`: медианы `visible_black_stale_lit_n=33` и `visible_black_fully_dark_repair_n=7`. В bounded trace 234/256 повторных samples имеют CPU-batch `slice_stale_dark`, а у всех 256 совпадают center `field_light_rev` и `meshed_light_rev`; это не доказывает равенство light halo у mesh и мира. Следующий диагностический шаг должен дать точное ребро/voxel и ревизии light source, а затем проверить очередь mesh-dependency invalidation и её scheduling.
- В отчёте нет cull-decision записей, совпадающих с координатами black census: все 64 bounded cull events относятся к одному координатному срезу и содержат нулевые epoch/incarnation. Они не годятся для доказательства, что конкретный stale-dark mesh попал в текущий frustum/draw. Текущие hole/black метрики — сильный сигнал незакрытого долга, но не pixel-level измерение.
- Release-сборка после кода `d78f76e1` прошла; тесты не запускались. Артефакты: [bounded-scan report](../../bin/suite_reports/engine_refactor/columnflow_hol_product174657_20260925.json), [perf JSONL](../../bin/logs/perf_20260925-154355_20248.jsonl).

## Основные ссылки

- [Sysreset v3 evidence на dd7871ab](SYSRESET_V3_AF_EVIDENCE.md)
- [A41 audit и plan](A41_AUDIT_AND_PLAN_2026-09-24.md)
- [A42 conformance](A42_CONFORMANCE_2026-09-24.md)
- [A42 cold-black follow-up](A42_COLD1_BLACKS_FOLLOWUP.md)
- [A21/A30 current-state audit](CURRENT_STATE_AUDIT_2026-09-22.md)
- [A41 untracked implementation plan](../../.cursor/plans/a41_visual_sot_root_cause.plan.md)
- [A42e untracked report](../../bin/suite_reports/a42/cold_a42e.json)
- [A42e untracked raw trace](../../bin/logs/perf_20260924-190819_27040.jsonl)
