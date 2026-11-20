# S3.04: конечная матрица установки

Статус: COMPLETE в пределах S3.04. Предыдущие solver, AIR lifetime и native quiesce доказательства переиспользуются. Родительский S3 остаётся OPEN.

## H3: реальный async install после изменения входа

Пять изолированных UWorld, по одному worker, отрицательный профиль -48..-17, write band -48..-33, AIR target (0,-3,0), resident AIR neighbor (1,-3,0), исходный job 9399. Ожидается полностью известный текущий sync snapshot и completed async result до изменения. Затем изменяется ровно один вход:

| Изменение | Ожидаемая причина отказа | Контроль |
|---|---|---|
| Содержимое прочитанного соседа | ReadSet | Block (16,-40,4) AIR → stone |
| Каталог | Catalog | Новый immutable catalog, emission stone 0 → 7 |
| Профиль | Profile | MaxY -17 → -1, остальные компоненты неизменны |
| Конфигурация | Configuration | lighting identity → 99 |
| Incarnation | ReadSet | Remove/Ensure соседа с теми же AIR bytes, другой incarnation |

Для каждого случая: общий production validator sync результата обязан вернуть точную причину; фактический UWorld drain(max=1) должен применить 0 результатов, не создать target field и поставить job 9401. После worker idle следующий drain применяет 1 результат и устанавливает current certified AIR field; долга не остаётся. Это проверяет реальный stale retry owner, не имитацию очереди. Смена epoch проверена отдельно реальным running cancel и same-ID successor в async_relight_lifecycle_test.

Таймаут worker: 5 s на predecessor/successor. Native процесс: 600 s, Release, копия World_261, startup-only, render distance 4, --probe-terrain-save-failure; уникальная папка `s3-stale-install-matrix-20261120-native`. PASS требует всех пяти cases; причина остановки сохраняется в report.json. После FAIL сначала анализ точного require и логов job 9399/9401, без обзорного повторного полёта.

## Sync owner и границы доказательства

`ChunkLighting.cpp::RelightChunkCoords` выполняет capture → pinned catalog → Compute → ValidateRelightInputs → install → RecordLightCalculation в одном main-thread вызове. `SceneLightRefresh` также валидирует до установки. Native H3 проверяет тот же immutable snapshot и production validator, а async часть — реальный UWorld owner. Конкурентная мутация live UBlockWorld во время синхронного вызова не является разрешённой моделью доступа и не вводится ради теста.

Существующие `TestRelightInputContextAndSeparateChannels`, `TestUnknownGeometryAndPinnedCatalog`, `TestWorldOwnedLightCertificates`, `TestBatchAbsenceEvidenceIsFreshAndExact`, `TestVersionedLightInputMemoInvalidatesImmediately` покрывают причины, pinned catalog, Unknown/proof revoke, разделение domains и смену incarnation/manager. Перед закрытием требуется актуальный Release запуск соответствующего executable и source review результата. CPU проверки не доказывают реальный GPU dispatch; он остаётся S3.06.

## Полученные результаты

Актуальная Release сборка `relight_result_install_test` и `async_relight_lifecycle_test` PASS (`s3-stale-unit-build-20261120.log`). CTest `relight_result_install_test`, `light_calculation_certificate_test`, `async_relight_lifecycle_test`: 3/3 PASS, 0.57 s (`s3-stale-unit-test-20261120.log`). Source review подтверждает, что полный executable вызывает перечисленные context/channel/pinned-catalog tests, а certificate selector вызывает world/absence/manager tests. Полная Release app build с H3 probe PASS (`s3-stale-matrix-build-20261120.log`); provenance сохранён отдельно.

Pinned catalog control меняет emission 14 → 6 между capture и Compute: immutable старый snapshot выдаёт block=13 на соседнем voxel, отвергается Catalog, fresh computation выдаёт block=5. Таким образом, проверяется не только сравнение pointer, но и фактическое использование замороженных свойств solver.

Native H3 PASS: все пять stale cases вернули ожидаемые причины (ReadSet=5, Catalog=4, Profile=2, Configuration=3, incarnation ReadSet=5). Во всех случаях old_applied=0, successor job=9401 установил актуальное сертифицированное AIR поле. Полный probe 15/15 PASS, включая ранее закрытые API cases. Процесс exit 0; startup/build provenance PASS; исходный World_261 не изменён, config восстановлен побайтно, lease освобождён. Receipt `build/desktop-msvc/s3-stale-install-matrix-receipt-20261120.json` связывает хеши исходных артефактов. Обзорный native повтор не требуется.

Общий smoke-verdict остаётся UNTESTED из-за отсутствующих общих component readers и внепакетных route/visual/performance доказательств; оригинальный файл не переписан. Эти ограничения не подменяются scoped PASS и остаются в исходном S0..S6.

Итог S3.04: общий sync validator и pinned input semantics проверены; фактический async UWorld отвергает пять видов stale и завершает преемника; queued/running/completed cancel и overflow возвращают полный spec ровно один раз; late epoch не удаляет same-ID successor; quiesce/resume и retire проверены реальным UWorld. Persisted cache compatibility — следующий S3.05; реальный GPU solver — S3.06; остальные очереди/owners — S4.01/S4.02.
