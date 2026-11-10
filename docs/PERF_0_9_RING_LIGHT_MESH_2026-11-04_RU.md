# PERF-0.9: отставание мира за исходным кольцом, свет и MissingMesh

Дата: 2026-11-04

## Результат этапа

Проведён диагностический профиль полёта за пределы исходного кольца и два независимых проверочных профиля после исправления. Исправлена ложная атрибуция стабильной пустой публикации: такой срез ожидает покрытия граней соседями (`FaceDebt`), а не потерянную задачу `FirstMesh` и не отсутствие владельца mesh-работы.

Это **исправление классификации и диагностики**, а не ускорение конвейера. Фактический визуальный хвост после остановки сохраняется и требует отдельной оптимизации GPU extract/output backpressure. Лимит FirstMesh вслепую не увеличивался: измерения показывают свободные слоты планирования при downstream backpressure.

## Исходные наблюдения

Профиль `bin/suite_reports/perf_0_9/trace` получен на копии `World_242_perf_baseline_source` с включёнными `CUBATARIUM_VISIBILITY_AUDIT=1` и `CUBA_VISUAL_BLACK_TRACE=1`.

- Приложение завершилось с `exit_code=0`; общий gate runner вернул неуспех из-за существующих quality/performance gates.
- Пройдено 35 чанков; эффективный FPS полёта — 24,33.
- После остановки `pending_light=0` во всех финальных периодах.
- `missing_mesh`: максимум 77, конец 61.
- `terrain_incomplete`: конец 29.
- В финальных `focus_slice_trace` все исследованные unresolved-срезы имели актуальный settled light. Следовательно, финальный хвост нельзя объяснять текущим relight.
- В восьми финальных census-epochs наблюдались примерно 10–12 Dirty FirstMesh и 12 кажущихся «ownerless» срезов на выборку.
- Все 12 финальных «ownerless» имели `stable_published_face_debt=1`, запись mesh cache, нулевой CPU vertex count и совпадающие desired/published geometry/light revisions. Это опубликованная пустая геометрия, ожидающая peer coverage.

## Причина ложной классификации

Колонка сочетала missing solid slices с `face_debt`, но классификатор сначала выбирал `MissingMesh`. Data/mesh census также учитывал stable published face debt как `solid_unowned`.

Такой срез уже имеет актуальную пустую публикацию. Повторный FirstMesh не создаст отсутствующие грани до появления покрытия соседних срезов. Поэтому прежняя классификация создавала ложный сигнал потерянного владельца и могла направлять расследование к увеличению FirstMesh budget вместо реального bottleneck.

## Изменение

В `ColumnRenderableState` добавлен признак `stable_face_debt_only`. Он выставляется только когда:

1. в колонке есть missing solid slice;
2. есть column face debt;
3. нет нестабильного missing solid slice;
4. `HasStablePublishedFaceDebt` подтверждает актуальную пустую публикацию.

Чистая политика `ShouldClassifyMissingMeshAsFaceDebt` переводит только этот случай в `FaceDebt`. Любой нестабильный solid slice сохраняет класс `MissingMesh`. Stable published face debt больше не увеличивает `band_solid_unowned_n` и `camera_band_solid_unowned_n`.

Дорогая проверка стабильной публикации вызывается только для missing solid в колонке с face debt; для остальных срезов дополнительного сканирования нет.

## Проверочные профили

Оба прогона использовали новые независимые копии `World_242_perf_baseline_source`, тот же маршрут и те же diagnostic env. Оба приложения завершились с `exit_code=0`. Общий pass остаётся `false` из-за прежних quality/performance gates; это не скрывается и не считается доказательством исправления визуального lag.

| Метрика | Исходный trace | after_1 | after_2 |
|---|---:|---:|---:|
| Пройдено чанков | 35 | 36 | 37 |
| FPS fly | 24,33 | 24,66 | 33,98 |
| MissingMesh, max post-stop | 77 | 78 | 69 |
| MissingMesh, end post-stop | 61 | 64 | 61 |
| FaceDebt, end post-stop | 3 | 10 | 4 |
| PendingLight, end post-stop | 0 | 0 | 0 |
| TerrainIncomplete, end post-stop | 29 | 27 | 28 |
| Camera-band solid unowned, конец | 5 в period; 12 в census-выборке | 0 | 0 |

В последних восьми периодах after_1 и after_2 `focus_data_camera_band_solid_unowned_n=0`. Прежние stable-empty срезы перешли из MissingMesh/unowned в FaceDebt. Нестабильные срезы остаются MissingMesh: unit-инвариант явно запрещает их скрывать.

FPS и latency между сериями несопоставимы как доказательство ускорения: различаются машинная нагрузка, пройденная дистанция и фактический состав работы. Патч меняет классификацию, а не throughput.

## Почему не увеличен FirstMesh cap

В финальных периодах исходного профиля эффективный FirstMesh cap колебался примерно от 3 до 12, а фактическое schedule соответствовало доступному cap. При этом:

- output slots оставались 16;
- output headroom составлял примерно 4–12;
- pipeline outstanding составлял примерно 2–12;
- `pending_gpu_applies_n` составлял примерно 9–15;
- `mesh_admission_mode=3`;
- доминирующий completion stall — `gpu_not_ready`.

В after_1/after_2 картина сохранилась: schedule не упирался в единый жёсткий верхний FirstMesh cap, а downstream GPU/output backpressure оставался активным. Дополнительный FirstMesh parallelism увеличил бы очередь перед ограниченным выходным этапом, но не доказанную пропускную способность публикации.

## Проверки

- Release `streaming_render_ready_invariants_test`: `PASS`.
- Полная Release-сборка цели `Cubatarium`: успешно.
- Статическая проверка EXE: `PASS`; 6 системных DLL.
- `after_1`: `exit_code=0`, 36 чанков.
- `after_2`: `exit_code=0`, 37 чанков.
- `git diff --check`: без ошибок.

Добавлены unit-проверки трёх случаев: stable face debt классифицируется как FaceDebt; unstable solid сохраняет MissingMesh; отсутствие face debt не даёт FaceDebt.

## Оставшаяся проблема и следующий этап

Радикальное отставание мира после выхода за кольцо не устранено этим этапом. Текущие данные локализуют следующий участок расследования: GPU extract/completion и output publication backpressure, включая `gpu_not_ready`, pending GPU applies и повторные FirstMesh/Remesh dirty passes.

Следующий этап должен измерить стоимость и очередь GPU extract/output, возраст публикаций и причины повторного dirty. До такого измерения нельзя обоснованно увеличивать FirstMesh cap или объявлять визуальный хвост исправленным.
