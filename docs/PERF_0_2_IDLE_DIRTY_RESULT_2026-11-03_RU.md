# PERF-0.2: устранение idle dirty/remesh — результат

Дата: 2026-11-03. Ветка: `feature/perf-0-2-idle-dirty`.
Базовый протокол: `docs/PERF_BASELINE_PROTOCOL_2026-11-03_RU.md`.
План: `docs/WORLDGEN_TAILS_REWORK_PLAN_2026-11-03_RU.md`.

## 1. Подтверждённые циклы

### 1.1. Dark/light requeue

Точечный probe `(24,2,12)` до исправления за 30 секунд записал 34 полных
build/GPU-цикла. Все завершались `dark_mesh_rejected` при неизменных входных
ревизиях: `desired_geom=4`, `source_geom=4`, `published_geom=3`,
`field/published_light=1`, `face_mask=2`.

После отклонения тёмного кандидата при активном column-wide defer-until-lit два
producer снова создавали работу:

- stale-light/halo repair (`PriorityRelitInstallRepair`);
- generic visual repair (`PriorityWorldCoreRepair`).

Артефакт: `bin/suite_reports/perf_0_2/dark_reject_probe/`.

### 1.2. Повторный emerge repair намеренно пустого mesh

Probe `(21,3,9)` до исправления записал 11 нормальных публикаций и 13 повторных
bump-ов `PriorityChunkEmergeRepair` при неизменных content/light-ревизиях.
Срез содержал 4096 непустых блоков, но его полностью скрытая геометрия корректно
публиковалась как GPU-resident zero-quad mesh: CPU/GPU geometry = 0, debt = 0.

`IsPublishedMeshCurrentForInputs` принимал только `HasDrawableGreedyMesh`.
Поэтому намеренно пустой, но удовлетворяющий demand результат не проходил
redundant-repair guard и emerge пересобирал его снова.

Артефакт: `bin/suite_reports/perf_0_2/missing_witness_probe/`.

## 2. Исправления

1. `DrainStaleLightRemeshDebt` сохраняет durable halo debt, но не запускает
   заведомо отклоняемый remesh, пока активен defer-until-lit.
2. `UWorld::EnsureVisualRepairDirtyPriority` не создаёт новый generic Dirty при
   активном lit gate; исполняемый successor остаётся у relight.
3. Redundant-repair guard использует `HasMeshSatisfyingColumnReady`, а не только
   наличие ненулевой геометрии. Намеренный zero-quad результат пропускается
   только при отсутствии более новых geometry/coverage/executor obligations.
   Изменения content, light, halo или незавершённая попытка по-прежнему не
   считаются текущим опубликованным результатом.

Изменённые исходники:

- `src/World/Streaming/MeshLitGate.h`;
- `src/World/Streaming/SoftDeferEmptyPolicy.h`;
- `src/World/Core/World.cpp`;
- `src/Render/Mesh/ChunkMeshCache.h`;
- `src/Render/Mesh/ChunkMeshCache.cpp`.

Регрессионные проверки добавлены в:

- `src/Test/StreamingRenderReadyInvariantsTest.cpp`;
- `src/Test/MissFirstMeshClassTest.cpp`.

## 3. Проверки

Release-сборка `Cubatarium`, `streaming_render_ready_invariants_test` и
`miss_first_mesh_class_test` завершена успешно. Проверка статического Windows EXE:
PASS. Оба теста: PASS/OK. `git diff --check`: без ошибок.

После исправлений probe `(21,3,9)` содержит только одну начальную публикацию;
повторных bump-ов `PriorityChunkEmergeRepair` нет. В конце и во всех последующих
периодах probe dirty = 0.

Артефакт: `bin/suite_reports/perf_0_2/missing_witness_after/`.

## 4. Static A/B

Одинаковые параметры: копия `World_242_perf_baseline_source`, static camera,
1280×720, 120 секунд, warmup 30 секунд, `--metrics`. Перед каждым прогоном рабочая
копия мира создавалась заново. Все процессы завершились штатно (`process_rc=0`).
Общий `pass=false` относится к оставшимся универсальным flight-gates, включая
требование движения на static-маршруте; это не признак сбоя приложения.

| Прогон | Wall median, мс | FPS | Dirty median/max/end | Missing end |
|---|---:|---:|---:|---:|
| Baseline 1 | 49,2990 | 20,2844 | 271 / — / — | — |
| Baseline 2 | 49,5189 | 20,1943 | 226 / — / — | — |
| Baseline 3 | 59,8353 | 16,7125 | 237,5 / — / — | — |
| После фиксов, `static_2` | 28,4448 | 35,1558 | 0 / 0 / 0 | 15 |
| После фиксов, `static_3` | 22,9834 | 43,5097 | 0 / 0 / 0 | 8 |
| После фиксов, `static_4` | 28,4804 | 35,1119 | 0 / 1 / 0 | 14 |

Медиана трёх итоговых прогонов: 28,4448 мс и 35,1558 FPS. Относительно медианы
исходного baseline frame time уменьшился на 42,56%, FPS вырос на 74,09%.

В последних 15 периодах `static_2` и `static_3` dirty стабильно равен нулю. В
`static_4` был единичный транзиентный dirty=1, после которого очередь снова
сошлась к нулю. Постоянного dirty/remesh давления после startup больше нет.

Артефакты:

- `bin/suite_reports/perf_0_2/static_2.json` и `static_2/`;
- `bin/suite_reports/perf_0_2/static_3.json` и `static_3/`;
- `bin/suite_reports/perf_0_2/static_4.json` и `static_4/`.

Промежуточный `static_1` после исправления только dark/light цикла сохранял
dirty median 234 и end 220; он не входит в итоговый A/B после обоих исправлений.

## 5. Границы результата

PERF-0.2 принят по заявленному критерию: устойчивое состояние после startup,
отсутствие повторного remesh без смены входных ревизий и улучшение static FPS.

Остаточный классификатор `focus_visual_missing_mesh_n` стабилизируется на
разных значениях (8–15) при нулевом dirty. Само по себе это не доказывает
видимые дыры; классификация и визуальные/correctness gates остаются отдельной
работой. Данный этап не утверждает, что остальные универсальные performance или
readiness gates уже пройдены.
