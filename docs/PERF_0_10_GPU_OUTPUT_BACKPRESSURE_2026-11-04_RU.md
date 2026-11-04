# PERF-0.10: проверка GPU extract/output backpressure

Дата: 2026-11-04

## Итог

Гипотеза о том, что радикальное отставание видимого мира после выхода за исходное кольцо вызвано GPU extract/output backpressure, **не подтвердилась**.

Детальный существующий профиль `CUBA_GPU_PROCESS_PROFILE=1` показал короткое ожидание focus FirstMesh и наличие свободных readback slots. Многсекундный возраст общей очереди относится преимущественно к фоновым Remesh и не доказывает задержку видимых FirstMesh.

Изменять `kReadbackRing`, FirstMesh cap или output slots по этим данным нельзя: это добавило бы работу без устранения доказанной причины. Production-код в этом этапе не менялся.

## Метод

Новый прогон выполнен на независимой копии `World_242_perf_baseline_source` с маршрутом PERF-0.9:

- 8 с начального idle;
- 100 с полёта с yaw 180, pitch -2;
- 8 с остановки;
- `CUBATARIUM_VISIBILITY_AUDIT=1`;
- `CUBA_VISUAL_BLACK_TRACE=1`;
- `CUBA_GPU_PROCESS_PROFILE=1`;
- отдельный `gpu_process.jsonl`.

Артефакты: `bin/suite_reports/perf_0_10/trace`.

Приложение завершилось с `exit_code=0`. Общий gate runner вернул неуспех из-за существующих quality/performance gates; прогон не объявляется визуально здоровым.

Пройдено 36 чанков, эффективный FPS полёта — 31,51. После остановки `pending_light=0`, финальные `MissingMesh=61`, `FaceDebt=7`, `TerrainIncomplete=31`. Значения FPS нельзя напрямую сравнивать с другими сериями как A/B: production-код не менялся, а машинная нагрузка не контролировалась.

## Детализация GPU-очереди

Для финальных 251 sampled process calls (примерно последние 2000 исходных sequence numbers):

| Метрика | Медиана | p95 | Максимум |
|---|---:|---:|---:|
| Pending GPU applies на входе | 13 | 15 | 16 |
| Queued на входе | 9 | 13 | 14 |
| Queued FirstMesh | 4 | 7 | 8 |
| Queued Remesh | 4 | 8 | 9 |
| Возраст самого старого pending, мс | 5389,72 | 10402,70 | 10946,60 |
| Возраст самого старого focus missing Queued, мс | 15,24 | 25,63 | 103,58 |
| Возраст самого старого Dispatched, мс | 15,31 | 29,55 | 121,99 |
| Kicked за вызов | 2 | 5 | 7 |
| Finished за вызов | 4 | 7 | 8 |
| Полная CPU-стоимость process, мс | 0,714 | 1,264 | 1,883 |

Свободный readback slot присутствовал в **96,41 %** замеров. Медианы `counter_not_ready_n` и `quad_not_ready_n` равны нулю. Таким образом, агрегированный `dominant_completion_stall=gpu_not_ready` описывает отдельные периоды, но не постоянную блокировку финального focus FirstMesh.

Старые pending до 10,9 с сосуществуют с focus missing Queued медианой 15,24 мс. Следовательно, нельзя использовать `oldest_pending_ms` или общий pending count как возраст видимого FirstMesh. Приоритизация ближних missing срезов фактически работает.

## Что это меняет в расследовании

1. GPU readback/output capacity не является доказанным доминирующим bottleneck для focus FirstMesh.
2. Backpressure в основном ограничивает накопление фоновой работы; это не то же самое, что многсекундная задержка публикации видимого нового terrain.
3. Сохраняющиеся `MissingMesh` и особенно `TerrainIncomplete` требуют прослеживания более ранней цепочки: terrain residency → peer coverage → snapshot → FirstMesh schedule.
4. Повторные dirty следует классифицировать по причине и ревизии. Нужно отличать полезную смену входных данных от повторного enqueue неизменного содержимого.
5. Следующий профиль должен выбирать конкретные долгоживущие focus missing/terrain-incomplete координаты и строить timeline по ним, а не делать вывод по агрегированной очереди.

## Решение

Этап закрыт как отрицательный диагностический результат. Никаких бюджетов и GPU-путей не изменено. Следующий этап — timeline долгоживущих focus missing/terrain-incomplete срезов и причин повторных dirty.
