# S3.05: сертификаты сохранённого света

Статус OPEN. Существующие codec, storage worker и certificate доказательства переиспользуются; общий аудит не возобновляется.

## H1: холодное чтение не наследует несовместимый свет

Production путь: `UChunkStorageService::WriteColumnLight/ReadColumnLight`, `LoadTerrainColumn`, `SectionLightRestoreQueue`, `RestoreSectionLight`, actual geometry solver и `RecordLightCalculation`.

Фиксированная сцена существующего TestColumnLightPersistence: root (2,-3,-3), upper (2,-2,-3), source в соседней секции с emission 14, terrain stone на Y=-64, AIR field-only outputs, два padded receipts. Terrain и metadata реально записываются в изолированную временную папку. Каждый случай использует новый UBlockWorld и новый registry/catalog; loaded terrain проверяется до cache restore. Никаких native/GPU stubs для disk path не требуется.

Конечные случаи: compatible; algorithm light-v5 с пересчитанным checksum; materials emission 14→6; profile SeaLevelY+1; configuration 1→2; inputs source removal после cold read; legacy terrain без column light файла.

PASS: compatible восстанавливает оба receipt и все padded bytes под новыми runtime identities. Остальные не создают target field/readiness; после recapture/compute/install получают complete certificate. Аналитический blocklight на границе: 13 исходно, 5 после emission=6, 0 после removal. Исходные stone и source block сохраняются (кроме намеренно удалённого source). Исходный валидный metadata fixture восстанавливается после матрицы.

Регрессии: target `negative_height_storage_test`; существующие `light_cache_certificate_test` (algorithm/material/global signature) и `chunk_lighting_propagation_test` (typed certificate, profile/input/footprint/checksum/unknown/corruption). Release only. CTest timeout для этого запуска 120 s; один repro/verification на гипотезу, при сбое сначала точный assertion.

Границы: H1 доказывает физический storage/restore/recalculate путь и terrain block preservation. Полная cooperative UWorld загрузка, fluid payload сохранность и глобальный cache signature owner требуют отдельной сверки существующих доказательств перед закрытием S3.05. Global column signature содержит min/max; typed section receipt дополнительно сравнивает все поля профиля. Совпадение глобального signature само по себе не подтверждает section readiness.

## H1 результат и расширение fluid payload

Release build и 3/3 tests PASS: `s3-persisted-matrix-build-20261120.log`, `s3-persisted-matrix-test-20261120.log` (1.57 s). Для дополнительного требования сохранности fluid fixture дополнена настоящим water block на Y=-63 со state Flowing(level=3, falling=true, kind=Water), packed=27. Во всех семи cold cases проверяются block ID и точный packed state после восстановления/пересчёта света; terrain stone/source также проверяются.

Расширенная Release проверка PASS: `s3-persisted-fluid-build-20261120.log`, `s3-persisted-fluid-test-20261120.log`; 7 строк `persisted_disk_matrix ... PASS`, block 13/5/0, fluid=27 во всех случаях. Test 1.23 s. Уточнение timeout: команда передавала --timeout 120, но существующее CTest TIMEOUT property имеет приоритет, фактический лимит в verbose log — 300 s. Оба запуска завершились менее чем за 2 s; timeout не был причиной успеха.

После достаточного PASS повтор H1 прекращён. Следующее конкретное действие: сопоставить реальный UWorldPersistence async load/restore owner и cooperative load с уже полученными disk receipts; установить недостающий upper-level контроль совместимого/legacy cold reload. S3.05 остаётся OPEN; GPU/backend, renderer и общая S6 матрица не сертифицированы.
