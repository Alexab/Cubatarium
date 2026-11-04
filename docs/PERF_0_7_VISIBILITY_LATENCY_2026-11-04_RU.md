# PERF-0.7: сквозная трассировка publish/readiness/relight и edit-remesh

Дата: 2026-11-04. Ветка: `feature/perf-0-7-visibility-latency`.
База: `develop` после PERF-0.6 (`6de37c729`).

## 1. Цель и границы этапа

Требовалось локализовать три независимых наблюдаемых симптома:

- отставание визуала мира при полёте за пределы уже готового кольца;
- задержка готовности света;
- мгновенные дыры под удаляемым блоком.

В коде уже существовали кольцевые `JobStageSpan` и `DemandTransitionSpan`, но подробные
demand transitions сохранялись только для watched visual chunks. Для общего профиля не
хватало агрегатов от запроса до фактической публикации. Кроме того,
`PhysicsTelemetryData.EditToFirstMeshMs` измеряет синхронную часть постановки edit/remesh,
а не момент появления опубликованного mesh.

Этап добавляет измерения и не меняет scheduling, relight, meshing или правила публикации.
Причинная оптимизация по новым данным должна выполняться отдельно.

## 2. Реализованная телеметрия

При `CUBATARIUM_VISIBILITY_AUDIT=1` `UJobStageTrace` дополнительно агрегирует:

- `demand_to_publish`: от `AttemptCreated` до принятого `InstallPublished` той же попытки;
- `demand_to_light_settled`: от создания попытки до первого settlement света;
- `light_settled_to_publish`: от settlement до публикации;
- `edit_to_publish`: от фактического `SetBlock` в place/break до публикации соответствующего
  chunk slice;
- `mesh_created_to_admitted`, `mesh_created_to_started`: путь до worker;
- `mesh_build`: фактический worker build;
- `mesh_built_to_publish`, `mesh_created_to_publish`: ожидание готового результата и полный
  mesh-job latency.

Для каждой метрики сохраняются count, total, median, p95 и max. Отдельно выводятся числа
созданных и завершённых без публикации попыток/job, а также текущие pending attempts/edits.
Сводка имеет схему `visibility_latency.v1` и включается в `movement_diagnostics.json`.

Сопоставление demand выполняется по глобальному `attempt_id`; mesh post-build — по `job_id`;
edit — по координатам изменённого slice. Повторный edit до публикации retarget'ит ожидание
на последнее изменение. Все map ограничены: 8192 attempts, 8192 post-build jobs и 1024 edits.
Без environment flag новый aggregator не выполняет работу. Существующие watched rings не
расширялись и не стали обязательными для этой сводки.

## 3. Протокол

Release `Cubatarium`, 1280×720, `--metrics`, отдельные новые копии
`World_242_perf_baseline_source`. Пользовательский `World_242` не изменялся.

Полёт:

- warmup 20 с;
- 70 с sprint-полёта на запад без телепортации, level-forward;
- 30 с остановки;
- `CUBATARIUM_VISIBILITY_AUDIT=1` и `CUBATARIUM_WORLDGEN_SEAL_AUDIT=1`.

Прогоны: `bin/suite_reports/perf_0_7/trace_1/`, `trace_2/`, `trace_3/`.
`trace_1` и `trace_2` относятся к первой версии сводки без отдельного
`demand_to_light_settled`; остальные перечисленные метрики уже присутствовали. `trace_3`
проверяет финальную схему целиком.

Edit-сценарий: `idle-edit-smoke`, 45 с, отдельная копия мира, результат в
`bin/suite_reports/perf_0_7/edit/`.

Во всех четырёх прогонах приложение завершилось с `exit_code=0`. Runner возвращает
`pass=false` из-за прежних performance/readiness gates; это не авария и не ошибка записи
диагностики.

## 4. Полёт: результаты

| Метрика | trace_1 | trace_2 | trace_3 |
|---|---:|---:|---:|
| Пройдено чанков | 21 | 21 | 21 |
| Attempts created | 41 572 | 42 133 | 43 892 |
| Attempts terminal without publish | 28 304 | 28 060 | 29 062 |
| Demand→publish count | 12 789 | 13 595 | 14 327 |
| Demand→publish median | 245,18 мс | 273,76 мс | 256,05 мс |
| Demand→publish p95 | 2 528,41 мс | 2 615,52 мс | 2 439,22 мс |
| Demand→publish max | 23,28 с | 21,50 с | 24,93 с |
| Demand→light settled median | н/д | н/д | 238,29 мс |
| Demand→light settled p95 | н/д | н/д | 1 533,87 мс |
| Light settled→publish median | 82,87 мс | 82,02 мс | 80,40 мс |
| Light settled→publish p95 | 1 373,76 мс | 1 488,82 мс | 1 372,56 мс |
| Mesh created→started median | 0,029 мс | 0,022 мс | 0,022 мс |
| Mesh created→started p95 | 2,90 мс | 1,77 мс | 1,73 мс |
| Mesh build median | 0,142 мс | 0,141 мс | 0,143 мс |
| Mesh build p95 | 5,71 мс | 6,08 мс | 6,10 мс |
| Mesh built→publish median | 24,98 мс | 23,45 мс | 23,58 мс |
| Mesh built→publish p95 | 92,38 мс | 80,94 мс | 82,42 мс |
| Pending attempts в конце | 479 | 478 | 503 |

Созданные attempts относятся к slice-level demand, а не к числу пройденных XZ-колонок.
Примерно 66–68 % наблюдаемых попыток завершаются без публикации. Это сильный сигнал churn,
но агрегат сам по себе не различает retain/reject/cancel и не доказывает, что каждая такая
попытка обязана была стать видимой.

Свет и publish дают секундные хвосты, тогда как ожидание старта worker и сам build на два–три
порядка короче по медиане. Следовательно, дальнейшая микрооптимизация mesh builder не
адресует основную измеренную задержку. Даже после settlement p95 до публикации остаётся
около 1,37–1,49 с: одной только оптимизации relight calculation недостаточно.

После остановки во всех полётах `post_stop_focus_visual_pending_light_max=0`, но
`post_stop_focus_visual_missing_mesh_max=25–26`, а общий readiness debt остаётся 47–49.
В этих прогонах relight FIFO overflow dropped составил 138 / 180 / 173. Это направляет
следующее расследование в admission/retry/ownership и FIFO eviction, а не в terrain populate.

## 5. Редактирование: результаты

`idle-edit-smoke` выполнил 20 измеренных edit-to-publish:

| Метрика | Значение |
|---|---:|
| count | 20 |
| median | 10,67 мс |
| p95 | 28,73 мс |
| max | 30,70 мс |
| pending edits в конце | 0 |

Следовательно, существующий synchronous `EditToFirstMeshMs` нельзя больше использовать как
доказательство фактической видимой готовности. Новый путь показывает, что первичный
изменённый slice в этом сценарии публикуется быстро и без оставшихся pending edits.

Это не опровергает визуальный witness мгновенной дыры. Публикация является lifecycle
событием, а не pixel-проверкой; метрика первичного slice также не описывает отдельно каждый
neighbor/seam. Поэтому по этим данным нельзя утверждать, что дыры устранены или что их причина
находится в async queue. Для следующего mesh-расследования нужен кадровый witness до/после
break и сопоставление exposed face с публикациями всех участвующих соседей.

## 6. Проверки

- `chunk_render_demand_test`: PASS, включая детерминированные интервалы
  demand/light/edit/publication и mesh stages.
- `streaming_render_ready_invariants_test`: PASS.
- Release `Cubatarium` и проверка статического Windows EXE: PASS.
- Два полных пролёта первой схемы, один полный пролёт финальной схемы и edit-сценарий:
  приложение `exit_code=0`.
- `git diff --check`: PASS.

## 7. Решение и следующий этап

PERF-0.7 принят как диагностический этап. Он локализует многсекундные хвосты вокруг
demand/light/publication и подтверждает массовый attempt churn; код scheduling в этом этапе
не изменён.

Следующий отдельный этап должен разделить terminal attempts по
retain/reject/cancel/supersede, связать FIFO overflow с потерянными XZ-колонками и проверить
повторные admission пути. Только после такой атрибуции выбирать исправление очереди или
владения. Дыры при break исследовать раздельно через pixel/face witness и neighbor publish.
