# PERF-0.11 — FirstMesh frontier и TerrainIncomplete

Дата: 2026-11-05.

## Цель

Продолжить расследование заметного отставания геометрии и света при вылете за исходное кольцо после PERF-0.7…PERF-0.10. На этом этапе требовалось отделить реальный production bottleneck от стоимости диагностических framebuffer readback и получить конкретные координаты/владельцев долгоживущего долга.

Этап не является доказательством устранения визуального отставания. Production-лимиты FirstMesh, capture и relight FIFO не изменялись.

## Изменения

1. Добавлен opt-in режим `CUBA_VISUAL_BLACK_TRACE_CPU_ONLY=1`. Вместе с `CUBA_VISUAL_BLACK_TRACE=1` он сохраняет CPU trace потокинга, mesh scheduling, demand и lifecycle, но отключает выбор framebuffer probes в `GeometryEngine`. Без переменной прежнее поведение сохраняется.
2. Добавлен ограниченный `focus_terrain_incomplete_trace`: не более 12 ближайших TerrainIncomplete-колонок на обновлённый data/mesh census. Записываются координаты, focus, `ColumnRecord.emerge`, desired/pending stage, pending token и базовые record flags.
3. Для выборки выделено независимое bounded ring на 256 записей (`sample_kind=20`), чтобы она не вытеснялась focus slice и fluid traces. Это диагностический путь; при выключенном visual trace новых обходов нет.

## Метод

Использовались независимые копии исходного мира и только Release `bin/Cubatarium.exe` через `tools/flight_sim_run.py`.

Основной полёт:

- мир `World_242_perf_0_11_cpu`;
- 116 с: 8 с idle, 100 с движения, 8 с stop;
- маршрут focus `(20,10)` → `(-16,10)`;
- JSONL: `bin/suite_reports/perf_0_11/cpu_trace/logs/perf_20261105-113525_39652.jsonl`;
- `renderer_pixel_probe` отсутствует, то есть framebuffer-readback ветка действительно отключена.

Проверка TerrainIncomplete:

- мир `World_242_perf_0_11_terrain2`;
- 50 с: 8 с idle, 34 с движения, 8 с stop;
- маршрут focus `(20,10)` → `(7,10)`;
- JSONL: `bin/suite_reports/perf_0_11/terrain_trace2/logs/perf_20261105-124415_7192.jsonl`.

Оба wrapper-запуска завершились с кодом 1 на существующих performance/readiness gates. Это ожидаемый отрицательный результат теста готовности, а не сбой запуска приложения.

## Наблюдения FirstMesh

Основной CPU-only полёт:

- 1544 записи `first_mesh_frontier_trace`, 921 уникальный срез;
- dirty age: median 26 кадров, p95 64, max 94;
- median `frontier_scheduled_this_tick` = 6;
- snapshot dependency/publication/store/pipeline defers = 0;
- soft-defer у выбранных FirstMesh не доминирует;
- самый старый выбранный срез `(-4,1,13)`: age 94, queue index 0, owner flags = dirty, desired geom 2 при published geom 1; он не даёт доказательства ownerless/inflight deadlock.

Сокращённый повторный полёт:

- 517 frontier-записей, 335 уникальных срезов;
- dirty age: median 29, p95 48, max 62;
- 232 выборки при `scheduled >= max_schedule`, 57 при `scheduled >= first_mesh_cap`, остальные без насыщения этих двух пределов;
- capture reserve иногда исчерпан, но одновременно насыщается общий schedule cap, поэтому это не изолирует capture как причину;
- `mesh_async_stuck_sec=0`, конкретные срезы получают попытки и продвигаются.

Вывод: подтверждён перегрузочный хвост очереди при поступлении новых срезов, но не подтверждён отдельный дефект ownership, snapshot/capture scheduling или GPU output/readback. Простое увеличение cap может лишь перераспределить main-thread/GPU нагрузку и потому не вносилось.

## Наблюдения TerrainIncomplete

В сокращённом полёте получено 256 записей для 83 уникальных колонок.

- 195 выборок находятся на горизонтальном расстоянии h=5, 52 — h=4, 9 — h=3.
- 219 выборок не имеют `ColumnRecord` (`flags=0`, все stage = 0).
- На конечном stop/focus `(7,10)` долгоживущие колонки преимущественно образуют trailing edge x=2, то есть h=5.
- Небольшая часть колонок проходит стадии Lighting/Meshing/RenderReady; одиночные pending records не образуют доказанного постоянного generation deadlock.
- Прежний `focus_slice_trace` действительно не покрывал этот класс: его выборка строится по resident solid slices, а TerrainIncomplete может быть колонкой без такого slice witness.

Вывод: большая часть счётчика TerrainIncomplete в этом маршруте описывает внешнее trailing ring, а не ближний зависший chunk. Нельзя складывать этот счётчик с MissingMesh как будто это один production bottleneck. Для следующего этапа следует измерять возраст/время до completion по конкретной XZ-колонке и отделять ожидаемый внешний радиус от реально видимого near-FOV долга.

## Производительность и границы вывода

Основной полёт: fly FPS 18.355, frame median 53.835 мс; `world_streaming_phase_ms=29.16345`, `mesh_emerge_ms=14.02445`, `scene_ms=19.8921`. После stop оставались MissingMesh 51, TerrainIncomplete 31, FaceDebt 23, PendingLight 0.

Сокращённый полёт: fly FPS 18.201; post-stop MissingMesh снизился до 38, dirty — до 132, но TerrainIncomplete остался 31. Разная длина маршрута не позволяет считать эти величины A/B-сравнением производительности.

Диагностика CPU-only убирает один известный источник возмущения, но сама остаётся тяжёлой opt-in инструментацией. Полученные FPS не являются чистым пользовательским baseline.

## Решение и следующий шаг

Production fix на этом этапе не вносился: причинный дефект, который можно безопасно исправить, не подтверждён. Сохранены только диагностические изменения.

Следующий измеримый шаг — профиль темпа ingress FirstMesh против завершений по расстоянию/FOV и отдельный column-age trace для TerrainIncomplete. До такого измерения не увеличивать FirstMesh cap, snapshot reserve или relight FIFO.

## Проверка

- Release-сборка `Cubatarium`: успешно.
- Статическая проверка EXE: PASS, только 6 системных DLL.
- Полёты выполнены через prescribed wrapper; gates намеренно остаются FAIL, поэтому визуальная сходимость мира не заявляется.
