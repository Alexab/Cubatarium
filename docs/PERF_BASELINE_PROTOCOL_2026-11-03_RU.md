# PERF-0.1: протокол повторяемого baseline-замера

Дата: 2026-11-03.
Ветка этапа: `feature/perf-0-1-baseline` от актуального `develop`.

Цель — получить сопоставимые static/moving baseline-прогоны и раздельно оценить
стоимость FramePerf-телеметрии. Результаты длинных инструментированных прогонов нельзя
сравнивать с короткими прогонами или прогонами других миров.

## Состояние мира

- Рабочий мир пользователя `bin/worlds/World_242` не изменяется.
- Неизменяемый снапшот:
  `bin/worlds/World_242_perf_baseline_source`.
- Активная копия для каждого прогона:
  `bin/worlds/World_242_perf_baseline_run`.
- Перед каждым повтором активную копию пересоздают из снапшота:

```bash
rm -rf bin/worlds/World_242_perf_baseline_run
cp -a bin/worlds/World_242_perf_baseline_source \
  bin/worlds/World_242_perf_baseline_run
```

## Протокол

1. Для каждого режима выполнить три прогона `--metrics`.
2. Static: 120 секунд полёта/простоя, warmup 30 секунд, камера не двигается.
3. Moving: 120 секунд, warmup 30 секунд, тот же сохранённый старт, `--fly`,
   `--hold-forward`, без teleport-cruise.
4. После шести прогонов выполнить по одному `--no-metrics` контролю для static и moving.
5. Не использовать один прогон как доказательство регресса: сравнивать повторы,
   backend, renderer, route hash, фактическое разрешение и одинаковый стартовый фокус.

## Команды

Static, для N из 1–3:

```bash
python tools/flight_sim_run.py \
  --world World_242_perf_baseline_run \
  --require-existing-world \
  --no-fly \
  --no-hold-forward \
  --seconds 120 \
  --warmup-sec 30 \
  --metrics \
  --phase-id perf_baseline_v1_static_N \
  --report bin/suite_reports/perf_baseline_v1/static_N.json \
  --artifact-root bin/suite_reports/perf_baseline_v1/static_N
```

Moving, для N из 1–3:

```bash
python tools/flight_sim_run.py \
  --world World_242_perf_baseline_run \
  --require-existing-world \
  --seconds 120 \
  --warmup-sec 30 \
  --metrics \
  --phase-id perf_baseline_v1_moving_N \
  --report bin/suite_reports/perf_baseline_v1/moving_N.json \
  --artifact-root bin/suite_reports/perf_baseline_v1/moving_N
```

Контроль без телеметрии повторяет те же маршруты с `--no-metrics`:

```bash
python tools/flight_sim_run.py \
  --world World_242_perf_baseline_run \
  --require-existing-world \
  --no-fly --no-hold-forward \
  --seconds 120 --warmup-sec 30 --no-metrics \
  --phase-id perf_baseline_v1_static_nom \
  --report bin/suite_reports/perf_baseline_v1/static_nom.json \
  --artifact-root bin/suite_reports/perf_baseline_v1/static_nom

python tools/flight_sim_run.py \
  --world World_242_perf_baseline_run \
  --require-existing-world \
  --seconds 120 --warmup-sec 30 --no-metrics \
  --phase-id perf_baseline_v1_moving_nom \
  --report bin/suite_reports/perf_baseline_v1/moving_nom.json \
  --artifact-root bin/suite_reports/perf_baseline_v1/moving_nom
```

## Агрегирование

Static:

```bash
python tools/perf_baseline_summary.py --mode static \
  --output bin/suite_reports/perf_baseline_v1/static_summary.json \
  bin/suite_reports/perf_baseline_v1/static_1.json \
  bin/suite_reports/perf_baseline_v1/static_2.json \
  bin/suite_reports/perf_baseline_v1/static_3.json
```

Moving выполняется с явным общим префиксом 25 чанков — минимальным покрытием
трёх выполненных повторов. Агрегатор дополнительно читает application report: именно он
содержит фактический `start_focus` после загрузки; `focus_start` в metrics относится к
steady-сегменту после warmup/idle и не заменяет его.

```bash
python tools/perf_baseline_summary.py --mode moving \
  --common-prefix-chunks 25 \
  --output bin/suite_reports/perf_baseline_v1/moving_summary.json \
  bin/suite_reports/perf_baseline_v1/moving_1.json \
  bin/suite_reports/perf_baseline_v1/moving_2.json \
  bin/suite_reports/perf_baseline_v1/moving_3.json
```

Контроль без метрик оценивается по application report, wall elapsed и результату маршрута;
он не должен смешиваться с FPS-агрегатами инструментированных прогонов.

## Минимальные критерии приёмки PERF-0.1

1. Все reports загружены; `runtime_world_matches_requested == true`.
2. Static-прогоны имеют одинаковый start/end focus и не перемещают камеру.
3. Moving-прогоны имеют одинаковый application `start_focus`; если разный FPS меняет
   конечный фокус за фиксированное wall-time, сравнивается общий префикс маршрута, а
   полный маршрут помечается как неэквивалентный сегмент.
4. В каждом report заполнены resolution, seed, backend, renderer и route identity.
5. Разброс повторов по wall/FPS/streaming quantified; его причина объяснена явно.
6. Если неподвижная сцена не достигает стабильного состояния, это фиксируется как
   отдельный вывод PERF-0.1 и становится входом PERF-0.2.

## Результат выполнения PERF-0.1 v1

### Прогоны

Выполнены три static, три moving и два `--no-metrics` контроля. Каталог результатов:
`bin/suite_reports/perf_baseline_v1`. Все инструментированные reports загружены,
`runtime_world_matches_requested == true`, world seed `3266652616`, renderer
`AMD Radeon(TM) Graphics`, backend `gpu_greedy`/`mdi_vertex_pool`/`gpu_frustum`,
разрешение `1280x720`.

### Static

- Application report во всех трёх прогонах: start `(20,10)`, end `(20,10)`,
  `chunks_traveled == 0`.
- Полный маршрут: wall median **49,30–59,84 мс**, FPS **16,71–20,28**.
- Population CV: wall **9,30 %**, FPS **8,72 %**.
- Первые два прогона отличаются менее чем на 0,5 % по wall median. Третий является
  выбросом; при отсутствии host resource sampling его системная причина не
  установлена, поэтому выброс сохранён в baseline, а не отброшен.
- `dirty` в steady-фазе остаётся в диапазоне **226–271** после 120 с. Неподвижная
  сцена не пришла к нулевому dirty/remesh состоянию — это прямой вход в PERF-0.2.

### Moving

- Application start во всех трёх прогонах: `(20,10)`; yaw и route identity совпадают.
- Полные маршруты за 120 с: конечный фокус `(-13,10)`, `(-19,10)`, `(-5,10)`;
  пройдено 33, 39 и 25 чанков. Различие длины маршрута вызвано разным FPS и не
  является различием входных данных.
- Сравнение по общему префиксу 25 чанков: wall median **60,48–107,83 мс**,
  FPS **9,27–16,53**; streaming phase **31,73–71,23 мс**, mesh-emerge
  **15,64–34,12 мс**, dirty **196,5–483**.
- Разброс объясняется асинхронным streaming/mesh backlog и performance feedback:
  худший прогон одновременно имеет максимальные streaming, mesh-emerge и dirty.
- Полные маршруты разной длины не используются для FPS-агрегирования; приёмочное
  сравнение выполнено только по общему префиксу.

### Контроль без метрик

- Static: 2353 кадра за 131,568 с, start/end `(20,10)`.
- Moving: 1746 кадров за 131,5 с, start `(20,10)`, end `(-13,10)`, 33 чанка.
- Контроль подтверждает выполнимость протокола, но не показывает стабильной
  системной разницы от FramePerf: инструментированные static-прогоны 1–2 быстрее
  контроля, а третий медленнее. Отдельная стоимость телеметрии в этом наборе не
  выделена из-за более сильной системной вариативности.

### Вердикт

**PERF-0.1 v1: PASS как этап измерения и фиксации baseline.** Протокол воспроизводит
идентичность мира/маршрута, корректно сравнивает moving по общему префиксу и явно
количественно описывает разброс. Диагностические flight-gates в отчётах остаются FAIL
и не считаются провалом PERF-0.1. Основной функциональный вывод: статическая сцена не
достигла стабильного состояния за 120 с; следующий этап — PERF-0.2.
