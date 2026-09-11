# План доработок streaming/render architecture

Дата: 2026-09-10. Исходная точка: `b1badb4f`, ветка `codex/world-streaming-audit-fix`.

Основание: [архитектурный аудит с доказательствами и 22 первичными источниками](E:/Work/Home/Cubatarium/docs/streaming/ARCHITECTURE_AUDIT_2026-09-10.md). Обозначения A01–A15 относятся к его реестру. Ниже — план исполнения, а не заявление, что перечисленные изменения уже внесены.

## Цель и способ работы

### Коррекция после review `cc7063ff` (2026-09-10)

Продолжаем от `cc7063ff`, сохраняя исправления A02/A03/A05/A06/A13; полный откат к `891844d5` не требуется. Старый статус ниже — исторический, а не приёмка текущего кода.

| Очередь | Контракт и связь с аудитом | Обязательная проверка |
|---|---|---|
| R1 | A09/A11: никакой потери demand при отказе enqueue; кредиты живут вместе с payload; join до разрушения callback state | Переполнение очереди, cancel queued job, result отказ, destruction; actual job pool |
| R2 | A01: хранить незавершённые поколения fence, unknown/failed не разрешает reuse; growth сохраняет live storage | Production allocator с подменёнными GL-вызовами: delayed/failed fences, growth, tiny cap |
| R3 | A01/A04: транзакционный GPU refresh; retain-until-replace; точные добавления/удаления batch keys | Замена/удаление material batch, allocation failure, неизменность старой публикации |
| R4 | A07/A09: stale relight сохраняет demand, origin не sentinel, validation читает полный read set; настоящие incarnation | Изменение блока/света/соседа, unload/reload, origin, повторная постановка |
| R5 | A12/A14: пересобирать тесты до запуска, исправлять контрактные несоответствия без ослабления gates; delayed GPU timing | Полный CTest, scorecard; GPU measurement unavailable не равен zero |
| R6 | M10–M16 исходного плана | Профилирование и последующая миграция owner/deadline/modules после R1–R5, не замена correctness эвристиками |

R1–R5 — ближайший implementation checkpoint. Для каждого шага ниже записывать реально выполненные проверки и ограничения. Production-allocator mock проверяет используемый код, но не заменяет реальный GL scene/oracle и manual A/B. До GL replay нельзя объявлять мигание/FPS/streaming решёнными; G1–G4 остаются открытыми до своих критериев. Обязательная приёмка: три cold и три warm прогона на одинаковом manifest, без изменения мира пользователя, fidelity/скорости/render distance.

Система должна выдавать правильное изображение непрерывно при движении, редактировании и асинхронном обновлении мира; укладывать работы main thread в измеренный бюджет; доводить нужные колонки до видимости без бесконечного repair. Меньшая очередь или более высокий FPS при пропавшей геометрии не считается улучшением.

Работать небольшими коммитами в текущей отдельной ветке, не переписывая `perf_opt19`. Один коммит — один контракт и регрессия на него. Не объединять allocator fix, новый scheduler и изменение render distance в один A/B-кандидат. После каждой группы сохранять исходные logs, config/route/build hashes и verdict. Не менять пользовательские миры: воспроизведение на отдельном тестовом мире/копии с явно заданным seed.

Размеры S/M/L ниже обозначают относительную сложность, не обещанные сроки: S — локальный контракт, M — несколько владельцев/интеграционный тест, L — миграция архитектурной границы. Календарную оценку уточнять после baseline и первого GPU harness.

## Непереговорные инварианты

1. `publishedMesh` с валидной версией и GPU handle остаётся drawable до успешной публикации замены либо явной eviction по interest policy. Ошибка новой работы не удаляет старый корректный результат.
2. CPU не пишет в GPU allocation, пока любой ранее отправленный draw может его читать. `timeout`/`wait failed` не переводят allocation в free.
3. Каждый cull input имеет один pass, одну batch-table version и согласованные bounds. Любой нужный видимый resident mesh представлен в командном списке; false-negative culling запрещён.
4. Для `(worldEpoch, chunkIncarnation, coord, workDomain)` есть однозначный живой token. Cancel/supersede относится к token, не навсегда к координате.
5. Результат устанавливается только при совпадении его read dependencies. Возврат в ту же координату после unload/reload не воскрешает старую работу.
6. Изменение light/voxels/boundary обязательно инвалидирует соответствующий производный mesh, но законная темнота не является неисправностью.
7. У очереди не бывает состояния «занято, но ни живого ticket, ни исполняющейся работы, ни документированной зависимости нет». Любая задолженность имеет reason, owner и время возникновения.
8. Memory admission резервирует ресурсы до создания большого payload; кредиты возвращаются на всех путях success/cancel/fail/evict. Обязательный progress не должен зависеть от освобождения кредита той же заблокированной стадией.
9. Измерение missing/unknown никогда не превращается в нулевую нагрузку или `PASS`.

## Этап 0 — надёжный baseline и минимальная страховка

### M00. Manifest и единый verdict (A12–A14), S

Создать manifest каждого run: git SHA, dirty flag, executable hash, compiler/build type, GPU/driver/GL capabilities, CPU, resolution/VSync, config и content hashes, seed/save state, route, speed, HUD/autosave/fog settings, начало/конец и количество кадров.

Перевести scorecard на четыре результата: `INVALID_RUN`, `CORRECTNESS_FAIL`, `PERFORMANCE_FAIL`, `PASS`. Обязательные поля/логи отсутствуют, недостаточно cruise samples или неизвестна конфигурация — `INVALID_RUN`, ненулевой exit. Все hard gates участвуют в итоговом verdict. Diagnostic `--expect-product-red` не должен использоваться в acceptance job. JSON результата — source of truth, консоль только отображает его.

Регрессии: нет perf; нет INFO при требуемой схеме; пустой/обрезанный JSONL; отсутствует отдельная метрика; false hard gate при хороших остальных значениях; mismatch build/route/config; insufficient duration. Все эти случаи должны отвергаться однозначно.

### M01. Зарегистрировать тесты и создать reference режим (A14), M

Подключить CTest и явно зарегистрировать существующие executable tests, начиная с scheduler/readiness/mesh policies. На CI проверять непустой ожидаемый набор и exit каждого теста, а не только сборку. Проверить triggers для реально используемых веток и PR targets; GPU-тесты — отдельный label/runner, unit tests не зависят от наличия GPU.

Добавить маленькую GL fixture с настоящими `MdiVertexPoolStore`/`GreedyVertexPool`, а не копией их логики. Эталон — тот же greedy mesh, материалы, освещение и transparent composition, но строгая CPU visibility и безопасный upload без reuse shortcuts. Не использовать «выключить все flags» как эталон: legacy instanced renderer не эквивалентен fluid/decor paths.

Снять baseline на `b1badb4f`: минимум три одинаковых запуска cold route и три warm route, отдельно HUD on/off; после исправления корректности повторить. Трасса нужна и для короткого stalled frame, и для steady cruise. Изменения fixture не должны сами становиться замером оптимизированного renderer.

**Выход G0:** оценщик отвергает неполные runs; список выполненных tests сохраняется; известны исходные frame-time quantiles и причины пропавшего draw. Отсутствие GPU runner не блокирует CPU fixes, но блокирует объявление GPU correctness подтверждённой.

## Этап 1 — локальные нарушения корректности

| Работа | Изменение | Зависимость | Размер / владелец | Приёмка |
|---|---|---|---|---|
| M02 / A05 | Full-width coord key, token generation, update urgency/slices, live queue count | M01 CPU tests | M / Streaming scheduler | Все 5 probes проходят; property/model tests не находят ghost occupancy |
| M03 / A06 | Исправить changed в CPU merge; единый LightChangeSet | M01 CPU tests | S / Lighting apply | CPU/fallback/block-only обновляют mesh; unchanged не создаёт лишний remesh |
| M04 / A02 | AABB max/history на каждый pass, batch-table version assertions | M01 GL fixture | M / GPU store | Изменение transparent не меняет opaque cull при прежних входах |
| M05 / A01 | Allocate→publish→retire; fence после последнего draw; корректные timeout/error/growth paths | M01 GL fixture | L / GPU allocator + Geometry | Ни один upload не пересекает live range при delayed fences и tiny pool |
| M06 / A03 | Полный cull key, строгая invalidation CPU/GPU caches | M04 | M / Camera, MeshCache, Geometry | Move/rotate/FOV/resize/projection тесты совпадают с conservative CPU oracle |
| M07 / A04 | Resident geometry отделена от visibility; точный missing-ref delta | M05, M06 | M / MeshCache + GPU backend | `{A,B}→{B,C}` и равный count/другой состав не теряют `C` |
| M08 / A09 | Token/epoch в capture completion, discard late, явный stop/join lifetime | M02 CPU harness | M / Capture и job owners | Cancel/re-enqueue/world switch/destruction не принимают старый snapshot |

M02, M03 и CPU-часть M08 можно выполнять независимо от разработки GL fixture; изменения смежных файлов интегрировать последовательно. Не откладывать маленький подтверждённый light fix до завершения большого refactor.

Для M05 сначала полезен диагностический safe upload режим (обычный synchronized upload или гарантированное новое storage) как контроль. Он может быть медленнее и не является окончательной performance-версией. Не считать opt-in `CUBATARIUM_POOL_SYNC=1` готовой страховкой: текущий fence стоит не после последнего draw и результат ожидания игнорируется.

Публикация mesh должна быть транзакцией на уровне render owner: проверить новую геометрию/allocations/bounds → подготовить commands → заменить published handle → записать retirement старого. Нельзя сначала `Free(old)`, а после неудачного allocation обнаружить, что fallback уже потерян. Нужно учитывать все subpasses, использующие shared geometry.

**Выход G1:** scheduler/light регрессии зелёные; GL stress и oracle не обнаруживают false-negative cull/mixed-pass bounds; retain-until-replace сохраняется при OOM/defer; controlled replay не показывает исчезновения уже опубликованной нужной геометрии. Performance здесь фиксируется, но не компенсирует нарушение correctness.

## Этап 2 — единая модель версий и зависимости

### M09. Versioned snapshot и commit validation (A07, A09), L

Ввести компактный `WorkToken` и `DependencyStamp`. Минимальные поля:

```text
WorkToken = worldEpoch + coord + chunkIncarnation + domain + generation
MeshInputs = contentRevision + lightRevision + materialCatalogRevision
             + stamps реально прочитанных halo regions
LightInputs = stamps read/write region + propagation settings revision
```

Сравнение stamps не должно требовать хешировать весь мир каждый кадр. Revisions выдаёт один owner при изменении соответствующих данных. Хеш нужен для тестового oracle и выявления забытых revision bumps, а не вместо дисциплины владения.

Relight может читать/писать больше шести соседних граней — записывать его реальный dependency set. Для конфликтующих relight jobs выбрать serialisation по region либо validation/retry; не допускать частичного применения несовместимых результатов. При высокой частоте edits coalesce latest target, ограничить retries и гарантировать прогресс, чтобы строгая validation не превратилась в новую starvation.

Политика границ: `Unknown` отличается от air; unlit отличается от lit-dark. Временная seam geometry имеет явный stamp и reason. Событие изменения границы инвалидирует затронутый border, а не всё кольцо безусловно. Проверки — random completion order, сосед появился/исчез, свет/блок изменился в середине job, unload/reload same coord, изменение catalog.

### M10. Упростить capture и убрать лишние копии (A08), M

Сначала убрать worker, который только пересылает уже построенный snapshot. Прямой capture/store/submit должен сохранять новые token checks и иметь явный main-thread budget. Измерить количество bytes и copies для одного center+halo, включая moves inline arrays, cache hits и incremental capture.

Затем эксперимент из двух реализаций за одним интерфейсом:

- Immutable/ref-counted chunk pages + сбор immutable snapshot в worker.
- Краткий region read-lock и bulk copy с `try_lock`/reschedule без ожидания main thread.

Выбрать по trace и памяти; не внедрять оба как постоянные production paths. Нельзя читать mutable chunk storage из worker без выбранной схемы ownership. Избежать per-cell world/hash lookups: заранее определить pointers и локальные диапазоны. Проверить parity snapshot, отсутствие зависаний и уменьшение capture CPU p95 на тех же inputs.

### M11. Один owner планирования, независимое состояние публикации (A10, A15), L

Целевая запись, а не новый набор дублирующих bool:

```text
ChunkRecord
  identity / desired inputs / interest
  resident voxel and light data
  published { meshVersion, gpuHandle, boundsVersion }
  pending   { token, stage, dependencies, priority, admittedBytes }
  debt      { reason, createdAt, deadline, lastUsefulProgressAt }
```

`published` и `pending` могут существовать одновременно. Колонка агрегирует версии своих slices; не подменяет их одной глобально монотонной enum. У Render есть read-only snapshot опубликованных handles; у diagnostics — read-only event stream. Только coordinator принимает запрос/отмену/завершение работы и меняет scheduling state.

Мигрировать по пути first mesh → relight replacement → seam repair → eviction, с adapter к существующим API. На первом шаге shadow mode сравнивает старое и новое **решение**, но не запускает две копии дорогостоящих jobs. Для каждой удаляемой recovery policy указать invariant, который теперь делает её ненужной. Не удалять все страховки одновременно.

**Выход G2:** completion принадлежит конкретной инкарнации и версии; old published mesh остаётся валидным при pending replacement; реестр state/debt не требует регулярного полного «восстановления истины» по нескольким maps; out-of-order и world-switch tests зелёные.

## Этап 3 — bounded throughput и frame pacing

### M12. Сквозной admission и общий бюджет workers (A11), L

Измеряемые кредиты: snapshot bytes, estimated result bytes, completed bytes, GPU pending bytes, GPU live/retired bytes. Для результата непредсказуемого размера нужен documented maximum, расширение резерва либо корректный defer/fallback; голый `queue.size()` недостаточен.

Поддерживать отдельный резерв для near first-paint и завершения pipeline, чтобы background work не занял всю память. Credits освобождать через RAII на всех выходах. Не ждать GPU fence, держа mutex/shared CPU resource. Для CPU pool задать общий лимит по hardware и профилю; I/O не должен занимать весь compute pool. Оценить runnable/blocked utilization до выбора окончательного числа потоков.

Очередь исполнения должна видеть priority/age и cancellation token. Дальняя работа перед началом compute перепроверяет актуальность. Не полагаться только на priority в admission queue, если дальше task попадает в длинный FIFO. Для тяжёлых generation/lighting jobs предусмотреть cooperative checkpoints и resumable units, где это необходимо по trace.

### M13. Один frame deadline, bounded drain и инкрементальные индексы (A08, A10, A11), L

Все main-thread стадии получают один deadline; они не суммируют независимые «минимально гарантированные» бюджеты сверх кадра молча. Зарезервированный progress выражается квотой на несколько кадров и priority, а не неограниченной аварийной работой сейчас.

Убрать `DrainAll` из bounded apply пути: неиспользованные completed results остаются в очереди с корректным byte accounting. Не заменять это переносом всей очереди в другой неограниченный vector. Разбить дорогие apply/capture units; отдельно логировать unavoidable atomic unit и превышение deadline.

Заменять полные ring/Y/cache scans инкрементальными индексами только после M11. Событие меняет конкретные counts/sets; редкая полная сверка остаётся debug validator. Измерять wall time и число посещённых элементов; не принимать оптимизацию по уменьшению числа вызовов без сохранения результата.

### M14. GPU/CPU telemetry без скрытого sync (A12), M

GPU timestamp query ring, чтение по готовности с frame/pass id, отдельные CPU submit/driver wait/present. Visibility stats — delayed staging/readback; unavailable sample остаётся unavailable. Не перезаписывать query, пока его результат ещё нужен. На GLES учитывать capabilities и отмечать отсутствие GPU timing, не писать ноль.

HUD on/off должен влиять только на стоимость вывода диагностики в оговорённом диапазоне. Сделать два профиля сбора: минимальные acceptance counters и подробная trace для расследования. A/B проводить с одинаковым профилем. Invariants, budgets и useful progress не должны зависеть от частоты обновления HUD.

**Выход G3:** bounded bytes подтверждены стрессом, нет постоянно растущей очереди при нагрузке ниже измеренной capacity, frame-time tails улучшены без ухудшения visual latency/корректности. Если при заданной скорости игрока спрос выше устойчивой производительности, система сообщает overload и регулирует дальний prefetch/quality, сохраняя защищённую ближнюю область; это не маскируется fog как «всё готово».

## Этап 4 — модульные границы и контролируемые оптимизации

### M15. Закрепить ownership в интерфейсах и сборке (A15), M/L

Выделить compile targets `WorldData`, `WorldGen`, `Streaming`, `Lighting`, `MeshBuild`, `RenderResidency`; конкретную нарезку уточнить по зависимостям, а не только названиям папок. UI не изменяет внутренние очереди; diagnostics не создаёт work; renderer не решает, надо ли relight мира.

Обновить architecture docs и include-check для `.cpp`/`.h`, forward/reverse restrictions. Существующие нарушения перечислить с причиной и задачей удаления; не разрешать новые через широкое исключение каталога. Отдельный compile/integration test подтверждает, что world data и lighting logic можно тестировать без GL context.

### M16. Оптимизировать только подтверждённый bottleneck, отдельные experiments

1. Если доминирует mesh compute — binary greedy/packed quad эксперимент на реальных snapshots, с parity для lighting/AO/material/fluid semantics.
2. Если first-paint latency всё ещё велика, а refined mesh заметно дешевле в draw — fast-first/refine-later после M09/M12, с ограничением общей работы.
3. Если доминирует объём дальнего мира — velocity-aware prefetch и отдельный coarse far representation; заранее решить seams и handoff к полным чанкам.
4. Если доминируют renderer CPU submissions — resident tables/MDI update granularity и минимальный resource graph. GPU cull должен быть дешевле CPU alternative в конкретном профиле, иначе оставить простой путь.

Каждый эксперимент сохраняется только при улучшении end-to-end metrics на нескольких типах terrain без новых visual defects. Переход на Vulkan/mesh shaders/новый engine не входит в обязательный план; для него нужен отдельный доказанный business/performance case.

**Выход G4:** доменные boundaries исполняются тестами; дополнительные техники имеют измеренный эффект и documented fallback; число legacy recovery paths уменьшается по мере замены контрактов.

## Матрица обязательных сценариев

| Сценарий | Инъекция / изменение | Что проверяем |
|---|---|---|
| Неподвижная сцена | Сотни кадров без изменений | Стабильные commands/bounds; нет самопроизвольного repair |
| Камера у границы frustum | Move внутри chunk, yaw/pitch, FOV/resize | Ни один CPU-reference visible object не потерян |
| Проекция | Perspective↔orthographic | Полная cull invalidation |
| Оба render passes | Менять только transparent или opaque | Pass-local bounds/history и неизменность другого результата |
| Маленький GPU pool | Частый remesh, delayed/failed fence, growth | Нет early reuse, old mesh сохранён при defer |
| Обновление приоритета | Дальний ticket становится near/edit | Reprioritise без потери slice demand |
| Queue ABA | Upgrade, drain, re-enqueue old kind | Старый tombstone не удаляет новую работу |
| Координаты | Отрицательные, 0/65536, min/max supported | Нет потери битов/undefined signed packing |
| Light edit | CPU/seed/fallback, block-only, unchanged | Только реальные changes инвалидируют mesh |
| Настоящая темнота | Закрытая пещера / ночной режим | Нулевой корректный свет не запускает бесконечный repair |
| Halo update | Сосед изменён/загружен/выгружен в середине job | Stale result не публикуется; border обновляется |
| Новый мир / reload | Та же координата и повторившийся local revision | World epoch/incarnation отсекают старые results |
| Недостаток CPU/памяти | 1–2 workers, slow I/O, малые credits | Bounded queues и прогресс near work без deadlock |
| Полёт и остановка | Cold/warm, forward/reverse, fast turn | Time-to-visible, debt age и catch-up после остановки |
| Завершение | Активные jobs и обычное закрытие | Join до destruction, нет UAF и потерянных обязательных saves |
| Диагностика | HUD on/off, полный/неполный log | Сопоставимый workload; incomplete run не проходит |

GPU capture и sanitizer instrumentation могут менять timing. Correctness stress и performance acceptance — отдельные профили, оба обязательны; нельзя объединять их числа в один percentile.

## Метрики и критерии завершения

### Жёсткие correctness gates

- Ноль нарушений перечисленных инвариантов во всех deterministic/fault-injection tests.
- Ноль false-negative cull относительно conservative reference для eligible resident geometry.
- Ноль early GPU range reuse; fence timeout/failure покрыты.
- Ноль stale accepts/ghost occupied tickets; stale discards допускаются и имеют reason.
- Ноль исчезновений защищённой уже опубликованной геометрии при replacement. При cold start незагруженная дальняя область учитывается отдельно, а не объявляется мгновенно готовой.
- Light parity в подготовленных эталонных сценах; image black pixel сам по себе не означает failure.
- Ноль потерянных кредитов и callback access после destruction в lifecycle stress.
- Ноль `PASS` на неполном/несопоставимом run.

### Продуктовые performance SLO — зафиксировать после G0

Сначала выбрать целевую hardware-конфигурацию, resolution, render distance и скорость передвижения. Ниже **предложение для профиля 60 FPS**, не измеренный результат и не универсальный отраслевой стандарт:

| Метрика | Предлагаемый ориентир | Как измерять |
|---|---|---|
| Steady frame time | p95 ≤ 16.67 ms, p99 ≤ 33.3 ms | Отдельно cold/warm cruise, не средний FPS |
| Main-thread streaming | p95 ≤ 5 ms; каждый overrun имеет stage/reason | Общий deadline, включая capture/apply |
| Protected-ring first-paint | p95 ≤ 250 ms для готовых voxel inputs | От появления demand до первого корректного draw; cold generation отдельно |
| Остановка после полёта | Нет бесконечно стареющей near debt; время catch-up измеряется | Demand age в миллисекундах и oldest reason |
| A/B regression guard | p95/p99 не хуже baseline более чем на 5% без явного решения | ≥3 повторов, одинаковый manifest; при шуме больше запусков |
| Память | Ни один soft/hard budget не превышается скрытым backlog | Live/pending/completed/retired bytes и reserve accounting |

Не считать временную остановку движения «решением» отставания streaming. Не снижать render distance/скорость/fidelity между A и B без отдельного обозначения changed workload. Если целевая машина не тянет предложенный профиль, выбрать и документировать другой продуктовый профиль; не подгонять отчёт под существующий плохой baseline.

Useful throughput = количество актуальных mesh/light результатов, дошедших до публикации, а не `schedule_ok`. Дополнительно нужны enqueue-to-start, compute, complete-to-apply, upload-to-first-draw, oldest debt по классу, discarded work CPU ms, copies/bytes и доля frame budget, потраченная на recovery. Стадийные p95 нельзя складывать как математический p95 всего pipeline: end-to-end измерять отдельно.

## Порядок коммитов и контрольные точки

```text
G0: M00 + M01
    ├─ M02 + M03 + M08 (CPU correctness)
    └─ M04 + M05 → M06 → M07 (GPU/visibility correctness)
G1: подтверждена локальная корректность
    M09 → M10 → M11
G2: versions / publication / ownership
    M12 → M13; M14 после M04
G3: bounded performance
    M15; M16 только по результатам trace
G4: продуктовая приёмка и сокращение legacy paths
```

Стрелки отражают существенные зависимости, не запрет раннего прототипирования. Каждый merged checkpoint содержит тест, trace/benchmark manifest, выполненные gates и известные ограничения. Для rollback использовать обычный revert своего отдельного коммита; не откатывать пользовательские новые изменения и не переписывать историю базовой ветки.

Начать следует с M00/M01 и первых отдельных regression fixes M02–M04. Крупный coordinator refactor не является предпосылкой для исправления найденных локальных ошибок.

## Исторический статус до исправлений review

Честный статус после пролёта 174657 и remediation gap audit (2026-09-10):

- Существующее исправление camera-position invalidation закоммичено: `b1badb4f`.
- Выполнен архитектурный аудит, исследованы внешние первичные источники.
- **M00 (A13):** scorecard fail-closed — `INVALID_RUN` / `CORRECTNESS_FAIL` / `PERFORMANCE_FAIL` / `PASS`; hard gates в verdict; unit test `tools/test_AnalyzePhase57Scorecard.py`. **Дополнено (F0):** product fail на `mesh_apply_stale` storm; wall_ms alone не закрывает stale/enter fail.
- **M01 (A14):** `enable_testing` + CTest labels `unit;streaming`; CI smoke расширен streaming-тестами; `column_scheduler_audit_repro`; CPU lifetime fixture (`greedy_vertex_pool_lifetime_test`). **Не закрыто:** полный GL scene fixture / A01 GPU proof — mock lifetime ≠ A01 correctness. Не объявлять GPU correctness без fixture.
- **M02 (A05):** full-width `ColumnCoord`, generation tickets, urgency refresh, `LiveCount`; audit repro 5/5 PASS.
- **M03 (A06):** CPU merge выставляет changed через `LightChangeSet`. Heuristics `force_unchanged_relit` могут ещё жить — частично.
- **M04 (A02):** pass-local `CullAabbMaxSsbo` + per-pass cull history — в основном сделано.
- **M05 (A01):** **было неполным** на 174657 (fence at Free-time). **F1:** retire через post-draw fence / `PendingRetire`→`SignalDrawComplete`; `Reserve` ждёт retire queues; orphan via `ReleasePooledBatch`. Приёмка — F5 manual + `pool_fence_timeout` / flicker.
- **M06/M07 (A03/A04):** `CullInputKey` + exact missing-ref delta — в основном; edge cases остаются.
- **M08–M10 (A07–A09):** skeleton stamps (`ChunkIncarnationAt≡0`); main capture + `CaptureDependencyStillValid` parity (F2). Полный incarnation — не закрыт.
- **M09:** не считать закрытым без полного stamp validation на всех path.
- **M11 (A10):** shadow coordinator есть; **не полный cutover**. Telemetry `column_meshing_n` = emerge FSM; F0 добавил `column_job_*`. Enter published∥pending semantics — F3.
- **M12–M14 (A11/A12):** admission/bounded drain / TryEnqueue — частично; F3 clears InFlight on TryEnqueue fail.
- **M13 / GL stress A01:** **не закрыто** в CI.
- **M15 (A15):** include-check allowlist; CMake WorldData/RenderResidency split — отложен.
- **M16:** оптимизации **не внедрялись**.

См. также F0 JSONL поля: `mesh_apply_superseded*`, `mesh_apply_drop_no_active*`, `pool_retired_*`, `enter_lit_gate_active` / `gate_elapsed_ms` / TTF proxies, `column_job_*`.

## Исполнение корректировки R1–R6 — 2026-09-11

### Дополнение: интеграционный дефект удаления demand

`PruneGhostDirty` передавал `!HasChunk` в политику, принимающую `has_chunk` и самостоятельно инвертирующую его. Ошибка присутствует в базе `cc7063ff`: загруженные dirty chunks удалялись, отсутствующие сохранялись. Исправлен вызов. Decor integration теперь проверяет production-путь с resident и absent chunk: удаляется только absent, все resident перестраиваются; затем проверяются cross grass и stone geometry через `RebuildAll` и frustum culling. После исправления тест проходит без ослабления ожиданий. Он добавлен в CTest и обязательный список Windows CI (теперь 16 streaming + 1 driver test); прежние записи о 16/16 и падении decor ниже описывают предыдущую контрольную точку.

Следующий world replay должен включать это исправление вместе с prepared-warmup drain: предыдущий smoke не проверял ни одно из них. Сначала проверяется сохранение demand и выход из прогрева без forced gate, затем streaming correctness и лишь после этого сравнение FPS. Не компенсировать потерю demand увеличением бюджетов или ослаблением visibility gates.

**Результат smoke2:** выполнен на свежем build, прогрев без forced exit, debt=0/underfeet=1, но полёт всё ещё CORRECTNESS_FAIL; подробности в `SMOKE_CHECKPOINT_2026-09-11.md`. Приоритет следующего шага уточнён: M09 — конфликты relight и stale mesh retry (120 записей relight retry и 132 stale mesh apply); M10/M12–M14 — стоимость streaming phase и emerge, общий deadline и capture. Не переходить к FPS acceptance до исправления visible black/missing. Регрессии теперь 17/17 PASS, включая настоящий вызов ghost-prune в decor integration.

Текущая база — `cc7063ff7bcfb3d408185fb23e456e41fb80184a`, ветка `codex/world-streaming-audit-fix`. Продолжение поверх неё; откат к `891844d5` не выполнялся. Этот раздел заменяет исторический статус выше, но не отменяет критерии G0–G4.

| Шаг | Внесённое изменение | Фактическая проверка / незакрытая часть |
|---|---|---|
| R1 | Mesh enqueue возвращает отказ; dirty demand сохраняется. Snapshot/result credits принадлежат payload через RAII, отказ result budget возвращает demand. Worker pool разрушается раньше состояния callbacks. Legacy void enqueue снова lossless | Production job pool: queue cap, cancel/release, lossless legacy, overflow-safe credits. Полного ASan/TSan world-switch replay нет; legacy очереди пока НЕ ограничены глобальным бюджетом |
| R2 | Хранятся pending поколения draw fences; failed/null не разрешают reuse. Reserve не сбрасывает живую арену, growth копирует старое storage, handles обновляются; render-thread wait неблокирующий | Production allocator с управляемыми GL fences; отдельный реальный GL 4.3 тест: 128 кадров draw/replace и pixel oracle, AMD Radeon(TM) Graphics, 0 ошибок. Это НЕ world/culling oracle |
| R3 | GPU command table публикуется транзакционно; удаляются исчезнувшие material batches; OOM оставляет старую геометрию, revisions и dirty retry | Тест вызывает настоящий `PublishPassInputs`: удаление batch, OOM, retry без нового dirty события, сохранение cull state при неизменных входах |
| R4 | Настоящие incarnation/content/light revisions; mesh center+halo stamps и catalog validation в cache/CPU apply/deferred GPU apply. Relight хранит read set и точный retry spec, origin не sentinel; stale demand пересоздаётся | Stamp tests и настоящий capture cache: origin, mutation, neighbor load, halo light, reuse. Relight integration/random completion и starvation под частыми edits ещё нужны |
| R5 | Исправлены fixtures readiness с явным FM demand (без ослабления assertions); отдельный zero-demand case. GPU timer ring читает только готовые старые queries и не перезаписывает pending slots | Все 16 зарегистрированных CTest прошли после сборки targets; scorecard regression script PASS. Из них 15 streaming и 1 real-driver. Полный renderer timing/culling replay ещё не выполнен |

Лог CTest: `build/desktop-msvc/audit-ctest-20260911.log`. Команды: `ctest --test-dir build/desktop-msvc -C Release --output-on-failure` и `python tools/test_AnalyzePhase57Scorecard.py`. Список зарегистрированных тестов не равен всем существующим test executables проекта.

Дополнительно: `RefreshIncrementalShell` теперь проверяет stamps перед частичным обновлением; регрессия подтверждает полный recapture изменившегося interior при прежнем sourceRevision. Windows CI проверяет наличие executable И регистрацию всех 15 streaming tests, отвергает пустой набор и сохраняет CTest logs. `check_include_rules.py` проходит. Запуск CI на сервере ещё не выполнялся.

### Следующий порядок работ и условия перехода

1. **R6.1 / M00–M01:** изолированный world smoke, затем одинаковые manifest/route/config и 3 cold + 3 warm A/B. GL smoke не измеряет FPS мира. Сохранить executable hash, исходные JSONL/INFO и verdict; не использовать старые логи как доказательство текущего build.
2. **R6.2 / M09:** закрыть visual-residency dependency halo (сейчас stamps покрывают voxel/light/incarnation, но не все внешние visual callbacks), immutable catalog reads и relight retry progress. Добавить adversarial completion/unload/reload/world-switch tests, счётчики причин reject/retry и возраста demand. Coalesce/serialise конфликтующие lighting regions только после измерения stale storm.
3. **R6.3 / M10–M13:** snapshot admission ДО capture/allocation; один worker budget, bounded queues с явным отказом/повтором для каждого producer, end-to-end byte credits и общий frame deadline. Сейчас result budget считается после compute; временные allocations и удвоение GPU storage при growth не покрыты строгим общим cap.
4. **R6.4 / M11:** перевести shadow coordinator в authoritative owner отдельной проверяемой миграцией. Удалять старые repair paths только при доказанной parity. Не считать fake GPU handles/зеркальные counters состоянием публикации.
5. **R6.5 / M14–M16:** устранить синхронные HUD readbacks, сделать cull/reference scene oracle, затем профилировать staging command table, линейный free-list и growth/copy. При малом pool cap whole-pass transaction может откладываться бесконечно: нужен progress-safe chunk-granular publish/eviction policy, а не освобождение старого mesh до успешной замены. После доказательства корректности — module boundaries и измеренные оптимизации.

G1–G4 остаются открытыми. Внесённые исправления закрывают конкретные воспроизводимые нарушения контрактов, но не являются заявлением об устранении мигания, чёрных чанков и просадок FPS во всех сценариях.

### World smoke и дополнительная коррекция

См. [SMOKE_CHECKPOINT_2026-09-11.md](SMOKE_CHECKPOINT_2026-09-11.md): 45-секундный пролёт завершился, но получил CORRECTNESS_FAIL; прогрев вышел через `force_ingame_no_uf` после 150 секунд с visibility debt 80. Гипотезу о достаточности R1–R5 этот результат не подтверждает.

В следующую коррекцию включены: возврат evicted completions владельцам при shrink очереди; явный scorecard failure для forced enter без underfeet; opt-in relight queue/dependency diagnostics; замена безусловного `coop_prepared` bypass в GPU-warmup на проверку текущих async work/mesh/visibility/underfeet. Подготовленный spawn сохраняет GPU storage, но не выключает потребителей готовых результатов при оставшейся задолженности. Проверить повторным world run до любого заявления о закрытии progress failure.

Расширенная проверка существующих targets: `completed_job_queue_test` прошёл; `decor_mesh_integration_test` собран, но упал на `cross batches should not be empty`. Причина и принадлежность регрессии пока не установлены; этот тест не входит в зелёный набор 16 CTest и не должен теряться в итоговом статусе.

### Коррекция после 893fb9bf — установка освещения и recovery demand

Текущая ветка `cursor_audit_impl`, база `893fb9bf`. Предыдущие записи о падении decor описывают исторический этап: причина (инверсия HasChunk) исправлена в этой базе.

1. **M03/M09 / A06/A07:** удалена повторная vertical seed из `DrainAsyncRelightResults`. Она заменяла рассчитанный worker skylight упрощённым светом без horizontal flood. Новый `InstallComputedLight` устанавливает только запрошенные домены после существующей validation, сохраняет прочие каналы и изменяет revision ровно один раз только при реальном изменении. Никакой validation не отключено. Production snapshot/compute + install regression: навес с боковым светом (у старого seed там 0), byte-for-byte full install, sky-only/block-only, несимметричные Y/Z, повторный no-op без invalidation.
2. **M12 / A11:** результат mesh overflow больше не теряется из-за нулевого dirty ingress budget при наличии старого drawable mesh. Recovery снимает только orphan ownership; новые CPU/GPU jobs сохраняются. Production cache regression сначала воспроизвёл отказ на предыдущем коде, после исправления подтвердил retry и публикацию replacement без нового dirty события.
3. **M01 / A14:** добавлен `relight_result_install_test` в CTest и обязательный Windows CI; расширен decor integration. Локально 18/18 PASS, scorecard regression и include rules PASS. Это не закрывает world gates.

Следующая проверка: свежий Release в изолированном runtime, без teleport и `CUBATARIUM_RELIGHT_AUDIT`; измерить black/missing, stale retries и стоимость streaming. Затем продолжить M09 (точные зависимости и immutable catalog) и M10/M12–M14 (capture/admission/deadline), опираясь на новый профиль. Старые seed counters после удаления повторного seed из apply не считать throughput завершённого relight.

**Проверка выполнена — smoke3:** 45 секунд/11 чанков без forced enter, но CORRECTNESS_FAIL по black/missing. См. checkpoint. Fly wall median 70.68 ms; это не A/B с smoke2. В текущем профиле light install уже не основной расход: period median 0.05 ms, тогда как `prep_schedule_policy_ms` 17.60 ms. Следующая конкретная задача M14: инструментировать подэтапы schedule policy и исключить eager `IsSpawnMeshRingReady()` вне enter gate; отдельно продолжить M09/визуальный oracle для оставшихся black chunks. Проверять один и тот же no-teleport маршрут, а не увеличивать бюджеты по наблюдаемому долгу.
