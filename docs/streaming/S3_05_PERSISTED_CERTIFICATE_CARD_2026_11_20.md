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

## H2: верхний async persistence owner

Три изолированных UWorld/каталога на диске: compatible/materials/legacy. Профиль -48..-17, AIR target (0,-2,0), источник (16,-24,4) emission 14, stone и water state packed=27 на нижнем этаже. Оригинальное поле вычислено реальным solver, затем обычный SaveTerrainColumn записывает terrain и optional light metadata. Materials меняет emission на 6 после сохранения; legacy удаляет только optional cache-файл.

Настоящие RequestAsyncTerrainColumnLoad для двух колонок и TickDrainAsyncChunkIo принимают worker results под текущими tokens. Compatible должен восстановить все padded bytes в AIR target. Materials/legacy не получают ложной readiness и должны сохранить обычный terrain relight ticket. Затем явный sample-target запрос через реальный async builder/UWorld install проверяет достижимость актуального AIR результата (это отдельное исполнение sample demand, не доказательство автоматической обработки всего terrain FIFO). Уровень blocklight на target edge 13 либо 5. Все три случая сохраняют terrain IDs и packed fluid state=27.

Worker/drain deadline 10 s, successor idle 5 s. Native Release запуск через engine_smoke_run, startup-only, World_261 copy, render distance 4, process timeout 600 s, --probe-terrain-save-failure; уникальный каталог `s3-persisted-world-load-20261120-native`. PASS требует всех трёх cases; при FAIL сохраняется точный require и сначала локализуется последний переход, без повторного обзорного запуска.

Source review: cooperative async spatial load использует тот же RequestAsyncTerrainColumnLoad; synchronous branch вызывает проверенный LoadTerrainColumn. Async receipt offer проверяет token и cancellation; bounded restore queue вызывает NoteChunkSliceLightCalculationSettled только для текущего/восстановленного поля. Finalize terrain создаёт обычный relight obligation независимо от optional receipt. Состояние всего cooperative startup/menu этим API probe не сертифицируется.

H2 первый запуск FAIL (`s3-persisted-world-load-20261120-native`): compatible field не восстановлен. Локализация до повторного запуска: WorldColumnSource root demand 9502 получил retry complete=0; TerrainColumnNeedsFill требует terrain в каждом XZ, если весь профиль не имеет decoded frames. Fixture содержала один stone на колонку и не удовлетворяла контракту complete procedural terrain. Source column, у которой decoded обе секции, завершилась. Receipt не мог подтвердить незавершённую root колонку. Это не доказательство дефекта cache restore.

Адресное изменение только fixture: сплошной floor Y=-48 на двух колонках и явный require IsTerrainChunkComplete для обоих оригинальных столбцов до save. Остальные profile/backend/acceptance неизменны. Проверочный native каталог `s3-persisted-world-load-floor-20261120-native`; исходный FAIL остаётся неизменным. Если тот же assert снова падает, следующая работа — диагностировать offer/owner/restore, а не третий обзорный запуск.

## H2 второй результат и обязательная карта локализации

`s3-persisted-world-load-floor-20261120-native`: compatible PASS, точные padded bytes восстановлены. Materials отвергнут с `PersistedLight rejected=1 reject_materials=1`, target (0,-2,0); terrain/fluid assertions до отказа прошли. Новый FAIL: `upper persistence incompatible/legacy cache retains ordinary relight obligation`. Legacy и successor не выполнены: их PASS не заявляется. Native процесс exit 0, исходный общий smoke UNTESTED; report probe FAIL сохранён.

Карта последнего доказанного перехода:

| Звено | Доказательство/состояние |
|---|---|
| Producer | SaveTerrainColumn записал terrain и optional receipt; source completeness PASS |
| Worker/доставка | RequestAsyncTerrainColumnLoad → completed disk columns, current token; terrain/fluid прочитаны |
| Restore owner | PendingSectionLightRestores: compatible restored=1; materials rejected=1 |
| Terrain relight owner | Не достигнут: FinalizeAsyncTerrainColumnLoad возвращается при отсутствии GetStreamer |
| Successor/consumer | Пока UNTESTED для upper H2; lower disk H1 уже PASS |

Source review уточняет прежнюю формулировку: relight obligation создаётся в finalize только при наличии streamer. Фикстура UWorld(nullptr,nullptr) его не создавала. Кроме того, ProceduralTemplate.SeaLevel оставался 63 вне профиля -48..-17; fallback band max(MinHeight,SeaLevel-32)..MaxHeight был бы пуст. В existing else для disk_light present but incomplete уже есть TryEnqueueTerrainColumnRelight; отсутствие этого кода не подтверждено.

Следующий строго ограниченный шаг: инициализировать реальный streamer через EnsureStreamer, установить SeaLevel=-33 одновременно в procedural settings и профиле, добавить require наличия streamer и допустимого fallback band до запроса. Сцена/acceptance остаются теми же; это исправление integration fixture prerequisites. Следующий запуск разрешён этой новой локализацией, а не повтором той же гипотезы. До его результата production recovery не исправляется, S3.05 OPEN. Никаких дополнительных обзорных полётов.
