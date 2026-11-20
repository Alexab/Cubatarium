# S3.04: отмена relight без потери обязательства

Текущий пакет остаётся OPEN. Предыдущие solver/AIR доказательства не повторяются.

## Гипотеза H1 и адресная репродукция

Production owner: `UAsyncRelightBuilder::CancelPending`; потребитель восстановления: `UWorld::CancelAsyncRelightWork`.

Гипотеза: queued closure удаляется до вычисления, `InFlight` очищается, а discarded sources собираются только из completed results. Следовательно, ещё не начатое задание исчезает без возврата долга.

Фиксированные условия: один реальный worker; служебное задание удерживает его promise-барьером; job 9101 поставлен через настоящий `EnqueueJob`; source (4,-40,4), band -48..-33, skylight выключен, finalize выключен, дополнительный AIR target (1,-3,0). Перед отменой проверяются очередь длины 1 и наличие job в in-flight. Отмена выполняется до отпускания барьера.

PASS: после cancel и idle отменённый запрос доступен владельцу восстановления. Начальная проверка существующего API требует хотя бы возврата исходной позиции; это необходимое, но недостаточное условие сохранения полной спецификации. FAIL: список пуст или содержит другой источник. Ошибка предусловий и timeout отделены кодом 2 от нарушения инварианта (код 1).

Команды: Release build target `async_relight_lifecycle_test`; CTest `^async_relight_lifecycle_test$`, Release, timeout 20 секунд. Барьер и idle имеют собственные таймауты 5 секунд. Сборочные логи `s3-cancel-repro-build-20261120.log` (первая ошибка регистрации CTest до компиляции) и `s3-cancel-repro-build-configured-20261120.log` сохраняются отдельно.

Тестовый friend даёт доступ только к scheduling seam; реальная отмена, учёт работы, snapshot capture и возврат долга не подменяются. Проверка не требует GPU или native запуска приложения.

## Следующая реализация при подтверждении H1

H1 подтверждена: Release build PASS; CTest FAIL за 0.08 s, `expected_sources=1 recovered_sources=0`, без ошибки предусловий или timeout. Лог: `build/desktop-msvc/s3-cancel-repro-test-20261120.log`. Это намеренно сохранённая красная regression-проверка незавершённого исправления. Продуктовый fix ещё не внесён; пакет не закрыт. Повтор этой репродукции до исправления не требуется.

Полная спецификация должна переживать queued/running/completed отмену и overflow, возвращаться один раз и не возвращаться повторно при late completion. UWorld должен сохранять точный запрос до штатного возобновления, не запуская worker во время quiesce. После исправления расширить проверку на полный spec и преемника, затем проверить смену world epoch. Только координаты источника не закрывают S3.04.

## Проверенный промежуточный результат: ledger builder

Добавлен `RetryObligations` под `InFlightMutex`: полный spec и submit epoch удерживаются от enqueue до delivery/cancel/overflow. `TakeDiscardedJobs` и `TakeOverflowJobs` передают точные запросы. Late completion старого epoch не создаёт второй долг и не удаляет новый job с тем же ID. Старые position APIs пока сохранены для существующих UWorld callers; они потребляют те же записи, поэтому двойной передачи нет.

Release test PASS (`s3-cancel-late-cached-flag-test-20261120.log`, 0.07 s): queued cancel, completed cancel, completed overflow, все поля spec, однократный take, recapture/compute/validate/install AIR successor и running cancel с реальным worker, остановленным на audit callback перед Compute. Для ABA successor повторно использует job ID 9106 после смены epoch; старый completion отвергнут, новый доставлен.

Промежуточные неуспехи сохранены: `s3-cancel-ledger-build-20261120.log` — неверное имя getter в тесте, исправлено на `GetSectionLightField`; `s3-cancel-late-test-20261120.log` — timeout тестового барьера, потому что environment flag был закеширован выключенным до его установки. Локализация: `Core/Environment.h` задаёт process-constant flags. Фикстура теперь включает audit до первого enqueue; одноразовый atomic claim ограничивает барьер старым worker. Это не engine deadlock.

Остаётся обязательная работа: перевести UWorld с позиции на точные deferred jobs; проверить resume/quiesce и очистку при замене мира; проверить остальные stale/read-set/catalog/profile/incarnation переходы. Полный S3.04 не закрыт. Native app и весь S0..S6 этим unit результатом не сертифицированы.

## Интеграция deferred owner в UWorld

UWorld теперь получает полные jobs из builder, удерживает их в `RelightRetryQueue` и подаёт через штатный drain с бюджетом min(apply cap, свободные pipeline slots). Callback подтверждает удаление из deferred queue только после enqueue. Диапазоны, domains, AIR/sample targets и finalize flags не заменяются generic terrain request. HasPending учитывает deferred долг в Full; Flat не ожидает Full-задания, которые сохраняются для последующего recapture.

BeginBackgroundQuiesce и синхронный Quiesce приостанавливают очередь; Drain во время suspend не запускает ни retry, ни stale successor. EnsureStreamingActiveAfterBackgroundQuiesce возобновляет её. Все три найденных `BlockWorld.Clear` пути очищают долг старого мира; cooperative load/create возобновляют пустой owner для нового мира.

Проверка используемого production queue с реальным builder PASS: `s3-world-retry-policy-test-20261120.log` (Release, 0.07 s): suspend не запускает worker; resume завершает точный AIR successor; limit 0 и admission reject удерживают запрос; limit 1 сохраняет хвост; Clear исключает повтор старых запросов. Это unit-проверка owner и builder, не вызов UWorld lifecycle.

Полные сборки приложения PASS: `s3-world-retry-app-build-20261120.log` и после уточнения Flat pending `s3-world-retry-mode-app-build-20261120.log`; используется обязательная Release команда и provenance wrapper. Итоговый provenance: `s3-world-retry-mode-release-provenance-20261120.json`.

Следующий шаг: один адресный native API probe изолированного UWorld для cancel → quiesce → resume → точный AIR successor, включая очистку при замене мира. Затем сверка оставшихся sync/async stale controls. До этого интеграция lifecycle считается непроверенной, S3.04 остаётся OPEN.

## Native H2: фактический UWorld owner

Проверка добавлена в opt-in TerrainSaveFailureProbe, вызываемый штатным runner. Отдельный `UWorld(nullptr,nullptr)`, один worker, доказанный AIR, профиль -48..-17. Job 9199 пишет band -48..-33 и explicit AIR target (1,-2,0) вне этого band; finalize=false, draw repair=true. После completed predecessor реальный Quiesce отменяет работу; drain должен оставить 1 точный запрос, 0 inflight и 0 полей. После ResumeAfterSessionSave штатный drain должен установить certified AIR поля (0,-3,0) и (1,-2,0), sky=15 на (20,-24,4). Затем новый completed request отменяется, AbandonTerrainForWorldReplace должен удалить долг и поля.

PASS: все предусловия и утверждения проходят, case `relight_cancel_quiesce_resume` имеет PASS. FAIL сохраняет точное сообщение require в report.json. Worker waits ограничены 5 s каждый; общий native процесс timeout 600 s. Вызов: engine_smoke_run, World_261, изолированная копия, render distance 4, startup-only, --probe-terrain-save-failure, уникальная папка `s3-world-relight-lifecycle-20261120-native`. Проверяется API lifecycle; не menu UX, реальный GPU свет или общая финальная матрица.

H2 PASS: report.json содержит `relight_cancel_quiesce_resume` PASS и все 10 API cases PASS. Реальный UWorld удержал все поля spec, не запустил работу в quiesce, после resume установил certified AIR поля и sky=15 вне первоначального band, replacement очистил поля/долг. Native exit 0; startup и build provenance PASS; 40 stationary observation samples, drift 0. Wall elapsed 127.079 s.

`s3-world-relight-lifecycle-receipt-20261120.json` фиксирует хеши исходных report/route/verdict/flight report, неизменность source World_261 и побайтное восстановление config. Runtime lease освобождён; EngineSmoke-копия намеренно удерживается штатным runner для воспроизведения.

Исходный общий smoke-verdict сохранён UNTESTED: missing terrain_save_failure/save_error_ui component readers (существующий долг S0) и отсутствующие route/visual/performance acceptance. Положительный API report не переписан в общий PASS. Повтор native H2 не требуется без изменения зависимого кода.

Оставшийся шаг S3.04: конечная таблица sync/async read-set/catalog/profile/configuration/incarnation/epoch с адресными rejection-and-successor controls; переиспользовать существующие certificate и immutable catalog проверки. Quiesce/retire native H2 и builder queued/running/overflow доказательства уже получены.
