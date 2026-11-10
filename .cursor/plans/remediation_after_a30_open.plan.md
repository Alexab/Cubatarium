---
name: A31 remediation — far-flight black chunks, seam, fluid
overview: Исправить OPEN findings A31 после HEAD 243e817d. Зафиксировать воспроизводимый дальний маршрут и pixel-to-artifact trace. Параллельно с диагностикой устранить подтверждённые кодом разрывы demand ownership, pre-publication validation, seam coverage и fluid cache/worker delivery. Причину конкретных чёрных кадров выбрать по трассе, затем пройти stop-to-publish и визуальную приёмку на одном чистом бинарнике. Ring/P8 оставить выключенными до correctness gate.
todos:
  - id: p0-repro
    content: Зафиксировать воспроизводимый near/far flight manifest, poses, холодный/тёплый маршрут, кадры и baseline
    status: pending
  - id: p1-trace
    content: Добавить bounded per-chunk artifact/job/light/cull trace и независимую классификацию дефекта
    status: pending
  - id: p2-root
    content: Включить единый production demand lifecycle с attempt/incarnation ownership и сделать CPU/GPU publication validation до изменения resident state
    status: pending
  - id: p3-seam
    content: Убрать fabricated peer generation; связать seam debt с реальными peer face generations, epoch/incarnation и опубликованным seam artifact
    status: pending
  - id: p4-fluid
    content: Исправить fluid cache identity по всем читаемым Y-slices и реализовать bounded producer/worker/install lifecycle без синхронного fallback
    status: pending
  - id: p5-contracts
    content: Атрибутировать и исправить конкретный black-frame класс; проверить полный demand→attempt→manifest→publication→draw контракт по CPU/GPU/cross/shell путям
    status: pending
  - id: p6-acceptance
    content: Пройти свежую матрицу cold/warm/manual/turn/stop/far-distance и измерить кадры, stop convergence, memory и fluid hitch
    status: pending
  - id: p7-quality
    content: Оставлять RingReadiness OFF и P8 пропущенным; пересмотреть их после прохождения correctness gates
    status: pending
isProject: false
---

# A31 remediation plan

Дата: 2026-09-23
Основание: [A31 re-audit](../../docs/streaming/A31_REAUDIT_2026-09-23.md), HEAD `243e817d`.
Связанный системный контракт: [ENGINE_REMEDIATION_PLAN](../../docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md).

## Цель

Устранить чёрные/отсутствующие чанки при дальнем полёте и доказать конечную сходимость, сохранив ограниченную стоимость работы. В A31 уже доказаны конкретные ошибки контрактов demand/publication/seam/fluid, но связь каждой из них с наблюдаемыми чёрными кадрами пока не установлена. Диагностика кадра и исправление этих ошибок могут идти параллельно; условные ветви culling/light/precision начинаются после атрибуции.

## Жёсткие ограничения

- Не вводить новые blanket remesh/PreferKick/age scans: A30 варианты ухудшали holes.
- Не считать `FullyDark`, `near_focus_holes`, нулевой framebuffer proxy или `dirty_dropped` достаточным pixel oracle.
- Не считать provisional seam, queued job или Retain эквивалентом опубликованного successor.
- Не включать RingReadiness/P8, пока дальний visual gate и stop convergence не пройдены.
- Каждое задание имеет ограниченный объём; бюджетный отказ сохраняет latest demand и его причину.
- Исторические A29/A30 scorecards имеют другие SHA и ненулевой dirty diff; они служат регрессионными ориентирами, а не baseline для HEAD. `eye_proxy` и `dirty_dropped` не замещают whole-route/pixel/stop gates.

## План работ

### P0 — Зафиксировать воспроизведение и базу

**Цель:** повторить дефект на точном артефакте и знать, как далеко летел игрок.

1. Собрать manifest для каждого рейса: source SHA и dirty hash, binary hash/build flags, renderer/backend, GPU/driver, world save/seed hash, block/material catalog, render/light settings, resolution, route/pose hash и режим cold/warm.
2. Записать start/end position в world units и chunk XYZ, расстояние от spawn, скорость, высоту, yaw/pitch, моменты появления/исчезновения дефекта, stop duration. Маршрут включает длинный прямой участок, 180° поворот, остановку, вертикальное перемещение и обратный полёт.
3. Сохранить отдельные кадры до/во время/после дефекта с depth, chunk ID, material ID и light/artifact generation debug views. Область измерения остаётся той же при A/B.
4. Сравнить текущий HEAD с выбранными историческими anchor-коммитами на независимых копиях одного save; ни один anchor заранее не считать корректным. Выполнить несколько чередующихся запусков для cold и warm режимов. Если baseline binary отсутствует, сначала собрать/идентифицировать его; не заменять его историческим scorecard.
5. Разделить frame samples, period aggregates, event deltas и census; сохранить точные timestamps, чтобы queue age и time-to-publish не вычислялись из счётчика событий.
6. Исправить метаданные прогона: если заявлен warm, сохранить явный cache warmup/reset protocol; отвергать `cold_or_unspecified` для сравнительного A/B. Поля world seed/hash, GPU/driver, GL capabilities, resolution, light settings и route start/end обязательны. Сохранять committed SHA, dirty hash и exe hash вместе; для окончательной приёмки dirty hash пустой.
7. Текущий A30 `focus_cx_min=-10`, `max=7` не объявлять дальним маршрутом. Сделать отдельные контрольные точки на расстояниях 0, 2^13, 2^16 и 2^19 world blocks при одинаковом локальном voxel окружении; для настоящего полёта сохранить длину и время до дефекта, чтобы отделить расстояние от накопления очередей/памяти. Предварительно проверить, что world generator и wrap не меняют сравниваемую сцену.
8. В baseline отдельно записывать full-route число периодов с mesh holes, долю дефектных кадров, `visible_black_focus_n`, time-to-publish после stop, backlog/age по стадиям, `fluid_map` p95/max и wall-frame p50/p95/p99/max. Не использовать `fly_fluid_map_cpu_max` как замену отдельному spike trace.

**Gate:** рейс воспроизводит класс дефекта; manifest связывает кадр с чистым HEAD-бинарником, миром и точным маршрутом; пройденная дистанция объективна. Если дефект не воспроизводится, сохранить отрицательные прогоны и перейти к сравнимому пользовательскому save/route, не объявляя проблему закрытой.

### P1 — Атрибутировать каждый дефект

Добавить ограниченный ring-buffer trace для выбранных/изменившихся chunk XYZ. Каждое событие связывается с world epoch, chunk incarnation, attempt ID, source/desired/published geometry/light/coverage generations, face и peer generation, queue owner, stage и monotonic time. Писать enqueue/coalesce/admit/deny/start/build/upload/publish/retain/reject/cancel/unload и причину каждого перехода. Для каждого stage хранить число событий, возраст oldest obligation и количество записей без владельца; `dirty_dropped` считать только своей исходной категорией.

На дефектном кадре заморозить camera pose, depth, chunk/draw ID, material ID, opaque/translucent pass, active artifact generation, cull decision и привязку пикселя к chunk XYZ. Снять тот же кадр с отключённым cull и reference geometry/light для небольшой области. Event trace должен включать причину отсутствия draw, а не только факт отсутствия меша. Полный сбор включать только для отслеживаемых чанков и короткого окна до/после симптома, чтобы instrumentation не меняла timing без учёта.

Для дефектного пикселя/чанка получить одну из взаимоисключающих категорий:

| Категория | Проверка |
|---|---|
| Геометрия не построена/не опубликована | Есть voxel input, но нет соответствующего published artifact |
| Геометрия отсутствует в draw | Artifact существует, но cull/command list его исключил |
| Свет устарел/невалиден | Mesh light source/halo versions не совпадают с требуемыми |
| Seam/peer coverage потеряно | Нужная грань/peer generation отсутствует или subscription опередил публикацию |
| Материал/публикация неверны | Uploaded pass descriptors/source stamps не совпадают с artifact manifest |
| Координатная точность | Тот же чанк и кадр ломаются по мере роста абсолютной позиции при стабильных версиях/очередях |
| Корректная темнота | Независимый reference подтверждает закрытую пещеру/валидный нулевой свет |

Не связывать chunk black count с `FullyDark` без проверки того же GPU artifact и кадра. Добавить CPU reference/captured-world comparison на небольшой области; инструментировать cull reason, camera key и transparent order key.

**Gate:** для каждого выбранного black/hole образца есть одна первичная категория, подтверждённая независимым кадром, и причинный trace от demand до draw. При нескольких одновременных дефектах записать первичный блокирующий этап и сопутствующие признаки, не смешивая mesh hole telemetry с pending-light scheduling predicate.

### P2 — Закрыть доказанные разрывы demand и publication

Эти изменения обоснованы production-кодом и не зависят от того, какая ветвь в P1 объяснит конкретный чёрный кадр. Делить поставку на небольшие изменения с отдельными contract gates; не включать неполный store как единственный источник истины одним флагом.

**P2.1 — Demand ownership и liveness** (`ChunkRenderDemand.h/.cpp`, `MarkRelitInstall.cpp`, `ChunkMeshCache.cpp`, `ChunkEmergeCoordinator.cpp`):

1. Составить таблицу всех writers/readers для voxel, light, neighbor/face, mesh, CPU/GPU install, unload и world switch. Для каждого указать текущий owner и момент, когда legacy debt закрывается. Устранить смысловую путаницу `kChunkDemandShadow=false` и `ChunkDemandCutoverEnabled=true`: сначала синхронизировать shadow по событиям и проверить расхождения, затем переносить authority по одному виду долга с rollback flag.
2. Ключ записи: world epoch + chunk XYZ + incarnation; desire включает geometry, light и coverage/face requirements. `AlreadySatisfied` проверяет *published* generations всех обязательных компонентов, а не `desired_coverage_gen` против самого себя. Пустая геометрия — явный validated empty artifact, а не отсутствие записи.
3. Каждое attempt получает неизменный ID и captured desire. `NoteStageProgress` принимает только текущий ID и монотонное продвижение. Install/Retain/Reject/Cancel несут ID и incarnation; stale completion не снимает новый attempt. Budget deny/coalesce оставляют latest obligation в очереди или waiting состоянии с owner/reason/retry deadline.
4. `ReconcileMaintenance` различает newly Created от orphan по monotonic age, проверяет ownership и либо заново планирует, либо сообщает явный nonterminal failure. Retain требует существующего successor, а не только нового desire. Stop convergence проверяет все required revisions, face debt, отсутствие ownerless pending и максимальный возраст без прогресса; не возвращает PASS только из-за однажды продвинувшегося attempt. Отдельно измерять stop-to-publish latency и верхнюю границу ожидания dependency.
5. Стресс-сценарии: concurrent edit/light change, budget deny, queue full, reordered completion, Retain без successor, unload/reload тех же координат, world switch, face generation bump, два Y slices одной колонки. Утверждать liveness только по production scheduler/installer, а не по `ChunkRenderDemandTest` helper.

**P2.2 — Валидировать до publication** (`ChunkMeshCache.cpp`, `GreedyGpuPublication.cpp`, manifest/retirement code):

1. Снять immutable manifest с captured source, всех прочитанных light halo revisions, material/catalog identity, seam/coverage requirements и pass descriptors. Перед заменой batches/resident table и очисткой dirty сравнить candidate с актуальной world/chunk incarnation и live desires *каждого* chunk/pass, а не одного элемента `published_ok`.
2. При mismatch оставить прежний валидный artifact, сохранить dirty/demand с `RejectedRetryable` или `RetainedAwaitingSuccessor`, снять ресурсы candidate после GPU fence. Ни CPU, ни GPU ветка не должны обновлять published revisions/epochs до успешного commit; reject shared validator должен реально предотвращать commit.
3. Отделить `artifact_generation`, `resident_table_revision`, `cull_key_generation`, `transparent_order_key`. Draw сверяет captured epoch с текущим; reuse разрешён только при совпадении нужного ключа. CPU, packed GPU, cross и shell должны иметь одинаковую семантику outcome. Fault injection меняет source между capture и commit и заставляет validator отказать; проверить, что старый artifact остаётся видимым, новый не попадает в draw и долг не теряется.

**Gate P2:** на реальном CPU/GPU apply пути поздний completion и mismatch не меняют опубликованное состояние; ни один latest demand не остаётся без owner; `StopConverged` не проходит при незакрытой coverage/face/Retain зависимости. Rollback flag возвращает предыдущий writer без смешанного ownership.

### P3 — Довести seam contract из A31-01

Изменить `ChunkEmergeCoordinator`, `SeamCoverageManifest` и callsites так, чтобы:

1. Peer readiness читать из фактически опубликованного artifact/face manifest. Отсутствующий demand record сам по себе не доказывает ни готовность, ни отсутствие drawable; никогда не заменять неизвестную generation числом `1`.
2. Каждое face debt содержало world epoch, chunk XYZ/incarnation, face и required peer coverage generation.
3. Peer readiness считывалась из опубликованного peer artifact для нужной грани; шесть направлений проверялись независимо.
4. Provisional seam generation назначалась фактической publication операцией после проверки manifest. Не подставлять в expected и got одно значение из одной переменной. `NoteFaceDebt` при повторном требовании повышает required generation, а satisfy сравнивает установленную peer generation с requirement и правильной гранью; нулевой/чужой generation не снимает долг.
5. Событие peer publish до subscriber registration закрывалось level-triggered reconciliation; stale incarnation/epoch не мог закрыть долг.
6. Проверки проходили по фактическому publication/draw пути, а не только helper predicates.

Добавить негативные сценарии: peer отсутствует, generation=0, peer устарел, одна из шести граней отстаёт, peer-ready до подписки, unload/reload тех же coordinates, новый мир на прежних coordinates и поздний completion.

**Gate:** недоказанная требуемая peer generation блокирует seam commit; debt закрывается только publication нужной версии и грани. Тесты проходят при peer publish до и после subscribe, на всех шести гранях и после смены мира; кадры подтверждают отсутствие открытого шва на target route.

### P4 — Реализовать fluid summary lifecycle из A31-02

Сейчас bounded enqueue не является worker delivery, а возврат `false` запускает синхронный scan во внешнем builder. Внутренний reuse stamp читает только content revision Y=0. Исправить обе ошибки как одну версионированную подсистему (`FluidSurfaceColumnSlice.cpp`, `FluidColumnSummary.h`, `ChunkMeshCache.cpp`, `FluidSurfaceMap`):

1. Создавать job из immutable/safe-to-read voxel or flag snapshot и полного ключа: world identity/epoch, column/chunk incarnation, revisions **всех прочитанных Y slices**, fluid/material catalog generation, y range и scan hint. При content edit, unload и world switch invalidation должна доходить до внутреннего static pack cache; адрес snapshot не использовать как единственную catalog revision.
2. Coalesce одинаковую колонку/версию; bounded queue обязана возвращать явный `Enqueued / Coalesced / RejectedRetryable`, не silent drop при `size>=8`. Каждое обязательство имеет owner, age и retry schedule.
3. Worker строит summary из snapshot вне render/main thread; главный поток drain-ит completion, проверяет версии и устанавливает summary/cache/map. Каждый drain заканчивается `Installed / StaleDiscarded / RetryQueued`, ready result не теряется. `DrainOne` на текущем потоке не считать worker.
4. При defer вернуть явный `Pending`/последний валидный slice с bounded lifetime и запланировать redraw после install; внешний `BuildFluidSurfaceColumnSlice` не должен переходить в синхронный 16×16×height scan. Удалить код, который enqueue-ит и тут же снимает произвольную работу без install. Если до готовности нужен визуальный fallback, его область, допустимая длительность и цена кадра измеряются отдельно.
5. Отменять старые jobs при world switch/unload и учитывать очередь в telemetry/memory budget.

Добавить случаи: empty/non-empty, water→lava при неизменной occupancy, edit в Y≠0 при том же ground revision, смена мира на тех же координатах, изменение высоты, queue-full, superseded completion, teleport/wrap и unload во время build. Проверять настоящий `GetFluidSurfaceSlice`/map consumer, cache hit/miss и pixel outcome, а не только summary helper.

**Gate:** каждый принятый job достигает явного исхода, корректная версия eventually становится ready, stale версия никогда не публикуется, а hitch route не сканирует height×16×16 синхронно.

### P5 — Исправить атрибутированный black-frame класс и проверить весь путь

После P1 выбрать причинный класс для каждого воспроизведённого симптома. Если P2–P4 уже устранили его, это нужно подтвердить тем же replay/кадром; если нет — исправить только показанную трассой ветвь:

- **Stale/invalid light:** валидировать все прочитанные light halo dependencies и revisions, хранить различие legal zero и invalid/unknown. Ремешить только artifact с устаревшей зависимостью; `FullyDark` census не использовать как trigger массовой работы.
- **Culling/order:** reuse разрешать при совпадении полного camera/frustum/cull/resident/order key либо доказанном conservative superset. При deadline exhaustion вновь видимый candidate остаётся в draw set; отдельно проверить opaque и transparent pass, поворот 180° и unload/reload.
- **Coordinate precision:** сравнить одинаковую локальную сцену при абсолютных смещениях P0 и неизменных source revisions. Если сбой появляется с ростом координаты при здоровых очередях/артефактах, перейти к camera-relative transforms/origin rebasing; проверить frustum, shaders, fluid surface, ray/collision, shadow и save coordinates. `glm::vec3` абсолютной позиции — гипотеза, а не доказанная причина.
- **Material/pass mismatch:** проверить vertex data, palette/catalog generation, pass descriptors и GPU resident mapping для того же пикселя. Разделить неверный материал от отсутствующего draw и legal dark.

Затем пройти единый replay: voxel/light/neighbor demand → active attempt → captured dependencies → mesh/light build → pre-publication validation → CPU/GPU install/retire → cull/order → draw. Проверить per-Y slices, все шесть faces, concurrent edits/light changes, reordered completion, backend parity, OOM/Retain, teleport и world switch. Очереди остаются bounded; rejected demand сохраняется для retry или имеет явную отмену по новой incarnation.

**Gate:** production-path regression на причинный класс и независимый кадр; при randomized reorder/unload/reload нет ownerless pending и lost latest demand; candidate старой incarnation не публикуется; после прекращения ingress видимые обязательства сходятся в согласованный SLA, включая coverage и fluid.

### P6 — Приёмка дальнего маршрута и производительности

1. Повторить cold/warm near и long-distance route, обе стороны, 180° turn, dive/climb, stop plateau, unload/reload и новый мир на тех же координатах.
2. Не менее пяти A/B запусков каждого основного сценария, чередовать baseline/current, использовать неизменные machine/settings/world conditions и одинаковые camera poses. Публиковать все прогоны, включая неудачные, с SHA/dirty/exe/world/settings/route hashes; не смешивать с A29/A30 dirty-build scorecards.
3. Сверить независимые кадры: неверная opaque поверхность/материал, чёрный вместо ожидаемо освещённого чанка и отсутствующий near-focus draw — 0 на полном принятом маршруте; dark cave проходит только при подтверждении reference light/geometry. Сохранить кадры и pixel-to-artifact trace каждого отклонения, долю кадров и длительность fallback.
4. Зафиксировать `near_focus_holes=0` по всем периодам маршрута, no orphan/pending ownerless demand, stop-to-publish ≤ заранее объявленного продуктового SLA, memory return after unload, dirty-drop delta ≤800 и отдельные admitted/denied/coalesced/retried/drop счётчики. Публиковать p50/p95/p99/max для wall-frame, stream, fluid map и upload, а также число spikes > целевого frame budget; отсутствие регрессии определять по парным запускам.
5. Выбрать и записать продуктовый frame target на целевом устройстве. Startup, teleport и steady cruise публиковать отдельными результатами.

**Gate:** human visual PASS и machine trace PASS на одной и той же чистой сборке/manifest для всего маршрута, включая дальние координаты и stop plateau. Незакрытый период, непроверенный кадр или пустое обязательное поле manifest остаются FAIL/UNTESTED, а не PASS по median `mid_corridor`.

### P7 — RingReadiness и P8

Сохранить `RingReadinessBudget` OFF и P8 profile SKIP. После P6 отдельно определить минимальную область гарантированного видимого покрытия, halo, prefetch и residency. Controller может менять admission/prefetch/quality только при корректном fallback и неизменной области correctness-оценки.

**Gate:** quality degradation предсказуема, не создаёт holes/invalid material, не осциллирует и восстанавливается при снижении нагрузки. Иначе оставить controller выключенным.

## Зависимости и поставка

`P0 → P1 → P5 → P6 → P7` — путь доказательства дальнего симптома. `P2`, `P3` и `P4` могут выполняться параллельно с P0/P1, так как их дефекты уже подтверждены кодом; перед P6 их production gates обязательны. Рекомендуемый порядок поставки: (1) manifest/trace без смены поведения; (2) demand shadow parity и coverage/attempt schema; (3) pre-publication CPU/GPU gate; (4) seam face generations; (5) fluid invalidation и worker delivery; (6) исправление атрибутированного light/cull/precision/material класса; (7) acceptance. Каждый PR содержит проверяемую гипотезу, production-path regression, ограниченный diff одного контракта, evidence с точным бинарником, явный rollback/cutover и conformance update. Если регрессия затрагивает непроверенный слой, сохранить repro/trace и остановить расширение recovery эвристик.

## Текущие стоп-линии

- Не повторять A30 SoftDefer owned scan, blanket PreferKick/PromoteRelight на `nh<=1`, MissWitness age 8–30 и CaptureRefresh×8 under starvation.
- Не включать shell-light mirror, force-stale heal loop, pending-FD `RemoveChunk` и Ring ON.
- Не объявлять A30 holes gate закрытым на основании dirty-drop PASS или worker enqueue.
- Не принимать численный риск large-world precision за установленную причину до измерения absolute coordinates.
