# Проверка закрытия архитектурного аудита

Дата: 2026-09-12. Проверенный код: `3d61756f`, ветка `cursor_audit_impl`.
Основание: [исходный аудит A01–A15](E:/Work/Home/Cubatarium/docs/streaming/ARCHITECTURE_AUDIT_2026-09-10.md), [план M00–M16](E:/Work/Home/Cubatarium/docs/streaming/ARCHITECTURE_REMEDIATION_PLAN_2026-09-10.md), [контрольные прогоны](E:/Work/Home/Cubatarium/docs/streaming/SMOKE_CHECKPOINT_2026-09-11.md).

Этот документ — актуальный статус и порядок оставшихся работ. Исторические записи «реализовано», «16/16», «decor падает» в плане не являются текущей приёмкой.

## Вывод

Продолжать текущую ветку; оснований для полного отката к `891844d5` не обнаружено. Исправления сохраняют полезные контракты и имеют воспроизводимые регрессии. Однако исходные пользовательские проблемы **не закрыты**, а результат нельзя принимать как готовый streaming/render pipeline.

- Локально устранены воспроизведённые ошибки scheduler, потери resident dirty demand, повторного затирания skylight и mesh overflow recovery. Усилены GPU lifetime/publication и проверка версий.
- Последний world run, smoke3: 45 секунд, 11 чанков, без teleport и forced enter; **CORRECTNESS_FAIL**, focus missing fraction ≈0.737, visible black focus median 78 по scorecard. Fly wall median 70.68 ms. Эти значения — диагностика, не доказанный A/B и не pixel oracle.
- Повторный запуск зарегистрированных CTest перед коммитом: **18/18 PASS**. Это 17 тестов с label streaming и один real-driver GL smoke. Не все существующие executable tests проекта зарегистрированы; серверный CI в этой проверке не запускался.
- **A13 остаётся fail-open для неполной схемы метрик.** Наличие perf/INFO уже проверяется, полнота их содержимого — нет. Поэтому G0 также не закрыт, не только G1–G4.

Не назначаем процент завершения: локальное исправление одного дефекта, проверка production-path и end-to-end приёмка — разные уровни доказательности.

## Матрица A01–A15

«Локально исправлено» означает только устранение исходного воспроизводимого нарушения. «Частично» — контракт улучшен, но часть реализации или обязательных проверок отсутствует. Ни одна строка сама по себе не означает закрытия пользовательского симптома.

| ID / план | Состояние кода и доказательства | Что мешает закрытию |
|---|---|---|
| A01 / M05 | Частично. Draw fence после draw, pending fence generations, timeout/failed без разрешения reuse, growth сохраняет storage, allocate→publish→retire. Production mock + реальный draw/replace/pixel тест | Нет world stress со всеми использующими память passes, tiny-cap progress/eviction и multi-driver проверки. При нехватке места whole-pass transaction сохраняет старое изображение, но сама по себе не гарантирует прогресс |
| A02 / M04 | Исправление в коде: AABB max и LastGoodCull в pass cache | Реальный mixed opaque/transparent cull oracle отсутствует; allocator GL smoke его не заменяет |
| A03 / M06 | Частично. Строгий CullInputKey включает pass, revisions, camera, view/projection fingerprint, distance/mode; invalidation в cache | Нет проверенного полного пути move/rotate/FOV/resize/ortho→GPU commands против reference; каждый shortcut должен проходить тот же oracle |
| A04 / M07 | Частично. Missing refs и транзакционная публикация; тесты удаления material batch, OOM и retry с сохранением predecessor | Residency/visibility ещё не единый независимый реестр; нужна последовательность `{A,B}→{B,C}`, возврат в область, eviction и tiny-cap convergence на renderer |
| A05 / M02 | Локально исправлено: full-width coord, generation/live map, urgency refresh, live count; исходный audit repro и scheduler tests PASS, identical demand не плодит heap entries | Расширить model/property tests на длительный reprioritise/cancel и несколько запрошенных slices; это не отменяет подтверждённого исправления исходных probes |
| A06 / M03 | Локально исправлено: LightChangeSet, установка рассчитанных каналов без vertical reseed, unchanged не меняет revision. Production Capture/Compute+install и roof-side-light regression PASS | Общая корректность lighting не доказана: границы, vertical bands, legitimate darkness и invalidation всех потребителей требуют oracle; оставшийся visible_black нельзя списать на этот уже исправленный путь |
| A07 / M09 | Частично. Incarnation/content/light, center+6 halo stamps, catalog equality, relight read set и exact stale retry spec | Visual-residency callback влияет на shell, но не входит в stamps; workers удерживают catalog, однако читают live registry. Нет adversarial multi-region relight/world-switch proof и bounded retry progress |
| A08 / M10 | Частично. Passthrough capture worker выключен по умолчанию | Capture/copies и store находятся на main thread; потокобезопасной worker capture схемы нет. Перед переносом выбрать immutable pages либо краткий read-lock, не читать mutable world без ownership |
| A09 / M08–M09 | Частично. Worker pool уничтожается раньше callback state, epoch/job checks, RAII credits, incarnation при reuse | Полный cancellation/destruction при активных mesh+relight+capture, unload/reload и sanitizer replay не выполнены. Flight harness использует `_Exit`, поэтому его exit 0 не доказывает normal shutdown |
| A10 / M11 | Открыто. ColumnRecord пока shadow; progress timestamp перестал обновляться при каждом неизменном sync | `gpu_handle=1` и token fallback `1` — синтетические маркеры, не реальные владельцы публикации/jobs. Legacy maps/repair producers остаются authoritative; cutover не выполнен |
| A11 / M12–M13 | Частично. TryEnqueue, bounded drain, RAII, overflow retry; отдельные потери demand воспроизведены и устранены | Резерв snapshot получен после создания аргумента, result — после compute; legacy Enqueue unbounded. Нет global compute/byte budget, общего deadline и строгой границы transient GPU growth memory |
| A12 / M14 | Частично. GPU query ring хранит pending slots/pass/sequence и не читает недоступный query | Cull stats всё ещё используют синхронный glGetBufferSubData при включении статистики; HUD/профили сбора не эквивалентны. Нужны availability/sample age и раздельные CPU/driver/GPU timings |
| A13 / M00 | Открыто, приоритет P0 для приёмки. Четыре verdict, missing files, false hard gate, forced-no-underfeet закрывают часть старых дефектов | Отсутствующие поля превращаются в 0; null hard gate игнорируется; manifest opt-in; raw flight report не содержит teleport flag. Исполненный контрпример ниже даёт PASS |
| A14 / M01 | Частично. Непустой ожидаемый CTest registry, production regression tests, real GL fixture, проверка списка в CI | Push filter не включает `cursor_audit_impl`; PR запускается только в перечисленные target branches. Нет dedicated GPU acceptance runner/world oracle и normal-shutdown gate |
| A15 / M15 | Частично. World→Render include audit охватывает .h/.cpp, 40 legacy allowlist, отдельные test targets без GL | Нет целевых domain libraries и reverse restrictions; facade не делает coordinator единственным owner. Не добавлять широкие exemptions вместо переноса ответственности |

## Повторно подтверждённые пробелы

### C01 — неполные метрики получают PASS (A13, исполнено)

Через настоящий `analyze_perf` обработаны три строки вида `{"kind":"period","movement_speed":3}`. INFO summary содержал одну settle-запись и пустые списки ошибок. Затем вызваны штатные validate/fidelity/product/build_verdict. Результат: `periods=3`, `wall_med=0`, `visible_black_focus_med=0`, `verdict=PASS`. Никаких wall/black/missing измерений во входе не было. Даже `evaluate_hard_gates({"visual_holes_rate_le_0_10": None})` возвращает два пустых списка.

Причины: [g()/analyze_perf](E:/Work/Home/Cubatarium/tools/AnalyzePhase57Scorecard.py:113) подставляют нули; [validate_run_inputs](E:/Work/Home/Cubatarium/tools/AnalyzePhase57Scorecard.py:527) проверяет counts, но не обязательную схему; [evaluate_hard_gates](E:/Work/Home/Cubatarium/tools/AnalyzePhase57Scorecard.py:563) пропускает None. Тест «complete and clean» проверяет конструктор verdict с пустыми fails, не полный путь разбора достаточных данных. Это не обесценивает красный smoke3, но исключает использование будущего зелёного verdict как приёмки до Q0.

### C02 — CI не покрывает текущий push (A14, подтверждено конфигурацией)

[Windows workflow](E:/Work/Home/Cubatarium/.github/workflows/windows-release-smoke.yml:5) разрешает push в main/master/thread_chunks/codex/**/perf/**/feature/**. Ветка `cursor_audit_impl` не совпадает. PR в main/master/thread_chunks или workflow_dispatch могут запустить workflow; отсутствие push-trigger не означает отсутствие всех способов запуска CI.

### C03 — вход capture шире, чем validation (A07, подтверждено кодом)

[ChunkMeshSnapshot::Capture](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshSnapshot.cpp:107) меняет shell occlusion по `neighbor_visually_drawable`. [ChunkInputStamp](E:/Work/Home/Cubatarium/src/World/Chunks/ChunkInputStamp.h) проверяет только incarnation/content/light. Смена visual publication соседнего чанка без этих изменений не представлена в stamp; отдельные revision/dirty side effects ещё могут вызвать recapture, но замкнутого snapshot-контракта нет.

Кроме того, [AsyncMeshBuilder](E:/Work/Home/Cubatarium/src/Render/Mesh/AsyncMeshBuilder.cpp) и [AsyncRelightBuilder](E:/Work/Home/Cubatarium/src/World/Lighting/AsyncRelightBuilder.cpp) сохраняют `catalogKeep`, но Compute/mesher вызывают через указатель на live registry. Сохранение старого catalog lifetime не равнозначно чтению именно этого catalog на протяжении всей job.

### C04 — shadow record не заменил владельца (A10, подтверждено кодом)

[SyncFromWorldTruth](E:/Work/Home/Cubatarium/src/World/Streaming/ColumnRecordCoordinator.cpp) восстанавливает поля из внешнего truth и использует синтетические handle/token. Поэтому наличие `ColumnRecord` не означает выполнение M11; требуется реальный переход владения, а не ещё одно зеркало readiness.

### C05 — оставшийся горячий участок измерен только агрегатно (A11/A12)

В smoke3 по всем period samples `prep_schedule_policy_ms` median 17.60 ms/max 27.56 ms. Это весь участок, **не** измерение одного вызова. В [ChunkEmergeCoordinator](E:/Work/Home/Cubatarium/src/World/Streaming/ChunkEmergeCoordinator.cpp) аргумент `IsSpawnMeshRingReady()` вычисляется eagerly даже при выключенном enter gate, хотя policy в этом случае от него не зависит. Это кандидат на безопасное устранение лишней работы; приписывать ему все 17.60 ms нельзя. Light install после исправления: period median 0.05 ms; нового основания оптимизировать его вместо schedule segment нет.

## Новый порядок исполнения Q0–Q10

Пункты ниже детализируют, но не заменяют инварианты и M00–M16. Каждый implementation commit должен содержать regression и документированные ограничения. Результат — не увеличение числа repair policies.

### Q0. Закрыть измерительный контракт — P0, M00/A13

Файлы: scorecard/parser, его tests, AppRunner report, flight harness manifest.

1. Версионировать схему обязательных raw period fields и report для каждого режима. До агрегирования валидировать наличие, тип, конечность числа, диапазоны и counts. None/NaN/Infinity/отсутствующее поле не превращать в 0; legacy aliases разрешать явно по версии.
2. Acceptance по умолчанию требует manifest и ожидаемый набор hard gates. Неизвестный/missing/null gate — INVALID_RUN, false — соответствующий FAIL. Диагностический режим может выдавать метрики, но не acceptance PASS.
3. Записывать из executable фактические teleport/route/start pose/speed/render distance, frame count, exit/shutdown mode. Связать report, INFO, perf одним run id; проверить hashes и совпадение baseline-кандидата, не только существование строковых полей.
4. Убрать baked historical baseline из acceptance fallback; при отсутствии парного baseline явно ограничить тип verdict. Разделить валидность run, абсолютные correctness gates и сравнительный performance verdict.

Приёмка: end-to-end parser/CLI tests для C01, пустых/обрезанных файлов, пропавшего одного поля в середине run, null/NaN, нет INFO/manifest/gate, другой build/config/route, недостаточно cruise samples. Все возвращают INVALID/FAIL и ненулевой exit. Полностью валидная фиксированная fixture проходит. Реальные красные smoke1–3 нельзя превратить в PASS ослаблением gates.

### Q1. Обязательные CPU/GL gates — P0, M01/A14

Зависимость: можно начать независимо от Q0, acceptance job включает результат Q0.

- Покрыть текущую ветку в workflow или согласованно убрать ограничивающий branch filter, сохранив path filters. Проверять и реальный push, и PR target, не только синтаксис YAML.
- Build каждого ожидаемого target до CTest; сохранять executable hashes, список тестов, CTest/scorecard artifacts. CPU integration отделить labels от pure unit; GPU driver label не заменять CPU mock.
- На выделенном GPU runner skip/unavailable не является PASS. Hosted CPU runner не обязан иметь GL4.3; его успех не закрывает GPU gate.

Приёмка: CI запущен на актуальном SHA; отсутствующий test/executable, нулевой набор и failed regression ломают job; GPU acceptance имеет фактическую модель/driver и не пропущен. Список 18 — текущий минимум, не инвентаризация всех тестов.

### Q2. Разделить причины missing/black и добавить renderer oracle — P0/P1, M01/M04–M07

Зависимость: Q0 и локальный GL harness; CI GPU часть Q1 нужна для постоянной приёмки.

- Зафиксировать малую сцену: opaque+transparent, decor/fluid, навес с боковым светом, тёмная пещера, сосед на границе чанка. Камера/свет/анимация детерминированы.
- Для каждой проблемной координаты сохранять identity, desired/meshed light revision, pending token, published allocation, pass/bounds/table version, CPU conservative visibility, GPU inclusion и пиксель/depth/object-id маску.
- Различать отсутствие resident geometry, missing command, false-negative cull, stale vertex light и законную темноту. `visible_black` — триггер расследования, не причина автоматически создавать relight.
- Production MdiVertexPoolStore/Geometry path: move/rotate/FOV/resize/projection; менять только один pass; `{A,B}→{B,C}`, material removal, delayed/failed fence, tiny pool/OOM, возврат камеры. Reference сохраняет те же материалы/меши/свет, а не переключается на неэквивалентный legacy renderer.

Приёмка: ни одного reference-visible необходимого draw не потеряно; GPU не пишет live range; old publication сохраняется при retry; legal-dark scene не запускает бесконечный repair. Несовпадение классифицировано и воспроизводится отдельной fixture. Нужен normal shutdown test, поскольку flight `_Exit` его обходит.

### Q3. Разделить schedule cost и убрать доказанно ненужные вызовы — P1, M13/M14

Можно инструментировать до Q2, но performance acceptance — только после correctness.

- Раздельные timings/call counts/elements visited для spawn-readiness, cancellation, DropRemesh, dirty scans и остальных частей `schedule_policy`; помечать, какие подзамеры вложены, чтобы не складывать их дважды.
- Short-circuit spawn-ring query вне активного enter gate и при независимом от него решении; callback-count regression проверяет отсутствие вызова, таблица enter cases — неизменность решения.
- Дорогие действия выводить из pure policy evaluation, передавать им общий remaining deadline; неначатая работа остаётся у owner. Не просто занулять repair ради низкого CPU времени.

Приёмка: исходный и новый код на одной сцене/маршруте дают одинаковые решения/coverage; отсутствует лишний query вне gate; стоимость атрибутирована; нет роста oldest-demand age и новых пропусков. Не обещать конкретное ускорение до парного replay.

### Q4. Полные immutable inputs и progress relight — P1, M09/A07/A09

- Включить version/identity visual boundary в mesh dependencies либо устранить влияние visual residency на геометрию явно выбранным контрактом. Тест меняет только drawable state соседа, без voxel/light edit.
- Все worker reads идут через захваченный immutable catalog/view; live registry lookup после submit исключён. Catalog change invalidates commit; тест перезагружает catalog между submit/compute/apply.
- Для relight записывать реальные read/write regions и propagation settings; проверить vertical band/roof выше захваченного диапазона. Тесты block/sky-only, сосед load/unload, edit во время compute, world switch, случайный порядок completion.
- По retry reason/age выбрать coalesce latest-target и/или сериализацию конфликтующих regions. Нельзя принимать stale результат ради FPS; отменённая версия не блокирует новую, обязательный near demand не голодает.

Приёмка: результат совпадает с синхронным reference на одинаковом immutable input; stale не публикуется; после прекращения edits конечный demand сходится без orphan ticket и без повторной потери domain/Y/finalization spec.

### Q5. GPU publication progress при ограниченной памяти — P1, M05/M07/A01/A04

Зависимость: Q2 oracle.

Сохранять транзакционность, но определить минимальную progress unit и защитный memory reserve. Whole-pass staging при tiny cap не должен бесконечно ждать невозможного свободного места. Выбрать chunk-granular publish/eviction в рамках interest policy; учитывать live+retired+staging+growth bytes. Не освобождать нужный predecessor до успеха replacement.

Приёмка: delayed/failed fences, repeated material changes, cap меньше временной whole-pass копии; нет early reuse или holes, а допустимая по минимальному footprint замена достигает publish. Невозможная конфигурация возвращает явный overload/reason, не «готово» и не вечный silent retry.

### Q6. Единственный owner: поэтапный cutover — P1, M11/A10

Зависимость: Q4 identities/dependencies и Q2 coverage oracle.

Перенести реальные tokens, GPU handles и debt lifetime в record; сначала first mesh, затем relight replacement, seam, eviction. Shadow сравнивает решения, не выполняет вторые jobs. На каждом этапе перечислять удаляемые legacy writers/recovery producers, инвариант замены и маршрут отката. Отдельные published и pending обязательны; pending не означает потерю drawable predecessor.

Приёмка: на активной стадии один producer/owner; нет синтетического handle в authoritative state; oldest debt age сбрасывается только при полезном progress; repeated reconcile не меняет семантику и не нужен для штатного восстановления потерянной работы.

### Q7. Сквозной admission и capture ownership — P1, M10/M12

Учёт ресурсов можно уточнять до Q6; смена scheduling authority интегрируется через Q6.

Reserve-before-allocation для snapshot и conservative result capacity, RAII на всех ветвях, отдельный completion/near reserve; сохранить lossless legacy до миграции каждого caller на retry-aware API. Ограничить сумму compute workers, отделить blocking I/O. Удалить неиспользуемый capture hop; выбрать одну модель worker capture (immutable pages либо краткий try-lock+bulk copy), измерив bytes/copies/lock time.

Приёмка: memory cap stress учитывает все стадии и transient storage; cancel/reject/throw/destruction возвращают credits; worker wait не блокирует main thread; рост очередей ограничен при спросе ниже измеренной capacity, обязательный near demand прогрессирует при перегрузке.

### Q8. Общий frame deadline и несинхронная telemetry — P1, M13/M14

Зависимость: Q3 measurements, Q6 owner, Q7 accounting.

Один deadline передаётся capture/apply/admit/upload/repair; локальные floors расходуют многокадровую progress quota, а не суммируются сверх кадра. Большие atomic units дробятся или явно учитываются как неизбежный overrun. HUD cull stats переводятся на delayed staging; unavailable/старый sample имеет validity/age, не выглядит текущим нулём.

Приёмка: trace объясняет budget overshoot; p95/p99 и max измерены вместе с time-to-first-correct-paint/oldest debt. HUD on/off не меняет correctness/workload; ни один pending GPU query/readback не перезаписывается/не читается синхронно ради статистики.

### Q9. Формальная world acceptance — M00/M01, G0–G3

Зависимость: Q0–Q8 в нужных для маршрута путях. Фиксировать immutable save copy и полный manifest. Сделать 3 cold + 3 warm до/после на одинаковом no-teleport route, speed, render distance, HUD, driver и resolution. Включить остановку после полёта, разворот/возврат, edits, unload/reload и normal shutdown. Сохранить raw logs и oracle captures, не только summary.

Приёмка: исходные correctness gates соблюдены; нет false-negative draw/битой геометрии/stale light; demand после нагрузки сходится. Performance считается только на валидном correctness run. Абсолютные SLA и разрешённые overload modes фиксируются в manifest до запуска; запрещено менять пороги по полученному результату. Отсутствие GPU/environment возможности означает «не проверено», а не PASS.

### Q10. Закрепить границы и рассматривать оптимизации — M15/M16, G4

Domain build targets, forward/reverse include rules и план удаления каждого allowlist exception. Diagnostics read-only; renderer не создаёт lighting work. Binary greedy/LOD/fast-first/refine и другие техники — только по оставшемуся профилю, отдельные сравнения с parity и fallback. Переписывание API/движка не обязательная часть аудита.

## Implementation progress 2026-09-12

| Item | Статус |
|---|---|
| Q0 schema / fail-closed verdict | done |
| Q1 CI gates | done |
| Q2 renderer oracle / attribution | done — attribution + census mismatch + normal_shutdown_test; GL pixel oracle remains `greedy_vertex_pool_*` / driver (G1 open) |
| Q3 schedule cost split | done |
| Q2 renderer oracle / attribution | done — attribution + census + DrawOracle/SmallOracleWorld CPU gate (`draw_oracle_gate_test`). GL pixel/object-id remains `greedy_vertex_pool_* --driver` (G1 evidence still needs product_anchor F5) |
| Q4 visual boundary stamps | Strategy A geom-only stamp; MeshInputs types; WorkerCompute **pinned catalog for faces+liquid+movement+cross** (GPU extract eligibility still registry residual) |
| Q6 ColumnRecord cutover | ShadowCompare default; keep-until-replace Enqueue. **100100**: mismatch ~251 flat (cruise delta≈0) — FirstMesh cutover *candidate* after Q4 F5 |
| Q9 F5 acceptance flight | **100100** drawable PASS (stale 36.5, holes 0, enter~2.8s, VB 70). Full G0/G1 open |
| Q5 tiny-cap publication progress | **landed** chunk-granular retain + `publication_progress_unit_n` |
| Q8 frame deadline / async cull stats | **R2** — `UFrameDeadline` + soft-defer Relight/Seam; **FirstMesh never hard-killed** |
| Q7 admission reserve-before-allocate | **R3** — CaptureAndStore + CaptureAndCommitOnMain snapshot reserve |
| Q9 F5 acceptance flight | **product_anchor PASS: 192015**; **095318** drawable PASS (stale 24, holes 0.06, enter~99ms, VB 73). Full G0/G1 vs 141350 open |
| Q10 domain boundaries | include allowlist burn-down note; domain CMake libs open |

G0–G4 остаются **не закрытыми** (см. матрицу gates ниже).

## Статус gates и следующий коммит

| Gate | Статус | Блокирующее доказательство/проверка |
|---|---|---|
| G0 | Не закрыт | Manifest/CI улучшены (Q0/Q1); парный world acceptance flight ещё нужен |
| G1 | Не закрыт | 100100 holes 0; VB med **70** still red vs 141350 |
| G2 | Частично | 100100 Decide cruise plateau (~251 total, late delta 0); FirstMeshOwner not enabled yet |
| G3 | Частично | ColumnFlow DrainBudget/RemeshSeam soft-defer by Exhausted; FirstMesh floor; apply/upload still local budgets |
| G4 | Не закрыт | Domain CMake/include reverse burn-down не завершён |

Следующий шаг: F5 after Q4 catalog liquid/movement pin; then enable FirstMeshOwner if drawable holds; Relight/Seam/Eviction cutover; Q9 3+3. Default remains ShadowCompare until FirstMesh flip.
