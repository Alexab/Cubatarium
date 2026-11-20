# S4.01: секционные owners и агрегаты

Статус OPEN. Продолжение существующего аудита; S0..S3 доказательства не повторяются без изменённых зависимостей. Остальные очереди и atomic geometry/field publication остаются S4.02/S4.03.

## Текущая таблица переходов

| Домен/переход | Production owner | Проверяемый контракт |
|---|---|---|
| Geometry requested/coalesced | UChunkRenderDemandStore::NoteDemand | coord + world epoch + incarnation, typed desired revisions, active attempt |
| Geometry running/computed progress | NoteStageProgress и реальные queue/worker callbacks | чужой attempt и регресс стадии не подтверждают текущий demand; trace диагностический |
| Geometry validated/published | WorldMeshService/mesh install + publication contract/cache | immutable capture inputs, epoch/incarnation/revisions; publication отдельно от compute |
| Light requested/running/computed | World persistence/edit demand → AsyncRelightBuilder либо sync pipeline | полный spec удерживается до outcome; CPU/GPU вычисление само не даёт readiness |
| Light validated/installed | ValidateRelightInputs → InstallComputedLight → UBlockWorld::RecordLightCalculation | SectionLightField и его current calculation certificate — физическое доказательство |
| Section light settlement | UWorld::NoteChunkSliceLightCalculationSettled + demand store | только identity/revision witness; LightSettlementMatchesCurrentCalculation дополнительно проверяет физический сертификат |
| AIR light demand | LightSampleLease в demand store | lease не является physical readiness; поле/сертификат принадлежат field owner |
| No surface/zero mesh | geometry proof/current capture inputs | отсутствие требуемого surface допускается только с актуальным proof; не по отсутствию меша |
| Column scheduling | ColumnEmergeStates, mirrored ColumnRecords, pending light bands | агрегат/очередь, не самостоятельный сертификат секции |
| Persisted hints | column flags и optional section receipts | несовместимость не теряет relight obligation; flags не дают section completion |

Существующие проверки: ChunkRenderDemandTest (identity/attempt/settlement), MeshPublishContractTest (accept/reject/retain), S3.04 actual cancel/stale/retire и S3.05 certificate/recovery. Сначала source review и актуальный Release запуск, затем только недостающие controls.

## H1: coarse column не должен подтверждать stale/new section

Source suspicion: IsColumnLitReady сначала проверяет pending map, затем возвращает true для coarse LitReady/Meshing/RenderReady/Empty без physical coverage. При raw terrain/physics receipt между mutation и relight callback старый coarse state может пережить потерю сертификата. Публикационный gate строже, но scheduling consumers также должны агрегировать актуальные секции.

Фиксированная native API сцена: отдельный UWorld, profile -48..-33 (одна секция), spanned stone floor Y=-48, emitter14, known upper AIR, Full domain из actual pipeline. CPU snapshot устанавливается и сертифицируется штатными owners; NoteChunkSliceLightCalculationSettled записывает witness; coarse LitReady. Затем один AIR voxel (2,-40,2) становится stone напрямую через BlockWorld, моделируя receipt до handoff. Должны закрыться physical coverage и section settlement; IsColumnLitReady также должен стать false, хотя coarse state не изменён. Recompute должен вернуть true. Remove/recreate той же geometry с новым incarnation снова должен дать false до нового расчёта; повторное settlement не меняет identity/field revision. Отдельный Unknown/Empty не ready.

PASS требует всех переходов и положительных предусловий. FAIL сохраняет physical/section/coarse/pending значения в checkpoint. До репродукции production не меняется. Native Release startup-only, copy World_261, rd4, --probe-terrain-save-failure, process timeout 600 s; каталог `s4-section-owner-repro-20261120-native`. Worker/render queues не подменяются, тест не является framebuffer acceptance.

Перед потенциальной правкой нужно проверить recursion и demand band semantics: HasCurrentColumnLightCoverage не вызывает IsColumnLitReady. No-op admission должен проверять именно свой band, чтобы aggregate false из другого band не создавал бесконечный повтор уже текущей работы. Performance/full matrix остаются S6.

H1 подтверждена native FAIL: physical=false, section_settlement=false, pending=false, coarse_state=LitReady(4), column_ready=true; ошибка `coarse column cannot certify geometry-invalidated section`. Положительное ready предусловие прошло. Native process exit 0; failure API report сохранён, общий smoke UNTESTED не переписан. Начальный compile preflight не имел явного RelightResultInstall include, исправлен до native; это не runtime failure.

Адресная правка: IsColumnLitReady после pending gate агрегирует HasCurrentColumnLightCoverage по профилю вместо coarse state. Coarse labels остаются scheduling/diagnostic mirror. В обоих no-op admission guards physical coverage проверяется только по запрошенному Y-band. Дополнительный контроль: расширить profile до -17, сделать верхнюю секцию незавершённой, пересчитать нижнюю; global aggregate false не должен поставить нижний уже-current band в очередь. Проверочный native каталог `s4-section-owner-verified-20261120-native`; критерии прежнего H1 сохранены, добавлен конкретный anti-churn band check.

H1 verification PASS: stale geometry/replacement закрывают aggregate, compute возвращает readiness, повтор settlement не меняет field revision, Unknown/Empty не ready, current lower band не переотправляется из-за незавершённого upper. Native API probe 20/20 PASS, runtime exit 0, startup/provenance PASS, source/config retained и lease released. До/после checkpoint отличается column_ready=true→false при одинаковых physical=false/section_settlement=false/pending=false/coarse=4. Receipt `s4-section-owner-h1-receipt-20261120.json` связывает immutable artifacts. Общий smoke UNTESTED не переписан.

Current Release reuse verification: `chunk_render_demand_test`, `mesh_publish_contract_test`, `capture_token_validation_test`, `capture_worker_integration_test` — 4/4 PASS, 0.34 s (`s4-section-owner-capture-test-20261120.log`). Capture test выполняет production CaptureAndStore/TryGet с fixed source revision, world epoch bump, neighbor residency/light mutation и recycle; worker integration исполняет реальный worker. Publication policy test не считается доказательством atomic geometry/field cutover (S4.03).

## Остаток S4.01: конечные адресные проекции

1. `UColumnFlowExecutor::SyncColumnJobStageFromWorld` сейчас выводит lit_ready из отсутствия pending/meshing/gpu, не из physical proof. Нужно проверить actual executor после stale section и после восстановления; заменить competing readiness projection каноническим агрегатом, сохранив отдельные queue/worker факты.
2. `ChunkEmergeCoordinator` on-publish callback допускает RenderReady при IsColumnLitReady **либо** coarse Meshing/LitReady. Проверить, что metadata не обходит канонические секционные факты при частичной публикации.
3. Уточнить контракт `IsColumnRenderReady` (сейчас GetColumnRenderableState.draw_ok): permission/provisional draw и completion publication не должны смешиваться. Это проверка single-authority projection; atomic predecessor/successor pair остаётся S4.03.

Это source findings, ещё не runtime FAIL. До их проверки S4.01 OPEN; H1 больше не повторяется без зависимой правки. MayMeshColumn — разрешение вычислять geometry/preview, а не certificate readiness; его coarse policy не превращается автоматически в light gate. JobStage trace и column metadata остаются диагностикой/проекциями. Новая aggregate query проходит диапазон profile; стоимость и bounded maintenance учитывать в S4.05/S6.04, performance PASS здесь не заявлен.
