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
