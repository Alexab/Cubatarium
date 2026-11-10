# PERF-0.8: атрибуция attempt churn и relight FIFO eviction

Дата: 2026-11-04. Ветка: `feature/perf-0-8-attempt-attribution`.

## Цель

Разделить ранее смешанные terminal transitions, определить назначение relight FIFO eviction и исправлять scheduling только после подтверждения конкретного источника churn.

## Диагностика

В opt-in схему `visibility_latency.v1` (`CUBATARIUM_VISIBILITY_AUDIT=1`) добавлены:

- `attempts_retained`, `attempts_rejected`, `attempts_cancelled`, `attempts_reminted`;
- `attempts_orphan_cancelled`, `attempts_superseded_cancelled`;
- `relight_soft_cap_deferred`, `relight_visible_replacement_deferred`, `relight_farthest_trim_deferred`, `relight_other_deferred`;
- `relight_unique_deferred_columns`.

`AttemptReminted` завершает именно `previous_attempt_id`, а не новый attempt. Повторный deferral той же XZ-колонки не увеличивает число уникальных колонок. Счётчики очищаются вместе с остальной visibility-телеметрией.

## Что показали исходные прогоны

Оба приложения завершились с `exit_code=0`; `pass=false` у runner относится к старым quality/performance gates.

| Метрика | `before` | `cancel` |
|---|---:|---:|
| Пройдено чанков | 20 | 9 |
| FPS fly | 33,23 | 5,91 |
| Attempts created | 42 892 | 13 929 |
| Terminal без publish | 29 720 | 10 835 |
| Retained | 254 | 63 |
| Rejected | 17 | 3 |
| Cancelled | 29 430 | 10 728 |
| Orphan cancelled | не разделялось | 6 955 |
| Superseded cancelled | не разделялось | 3 773 |
| Reminted | 19 | 41 |
| Visible replacement deferred | 152 | 70 |
| Уникальные deferred XZ | 81 | 29 |

Во втором прогоне 64,8 % cancellations составили ownerless `Created`. Все зарегистрированные FIFO evictions были `visible_exact_replacement`; `soft_cap_overflow` и `farthest_trim` не наблюдались. Жертвы централизованно проходят через `PreserveRelightFifoVictimAsDeferred` и сохраняются в `DeferredFarRelightColumns`, поэтому счётчик dropped не является доказательством безвозвратной потери.

## Подтверждённая причина и исправление

Несколько легальных производителей вызывают `NoteDemand` без frame clock. Раньше такой attempt получал `attempt_created_ms=0`, а `ReconcileMaintenance` считал ноль немедленно просроченным (`past_grace=true`). Это создавало цикл: новый `Created` → первая maintenance-отмена → повторный `NoteDemand`.

Теперь при отсутствии переданного времени attempt получает монотонную process-wide метку через `DemandEventTimeMs`. Реальный grace 250 мс сохраняется; ownerless attempts по-прежнему отменяются после его истечения. Изменение не отключает watchdog и не создаёт replacement из maintenance.

## Проверка исправления

| Метрика | `after_1` | `after_2` |
|---|---:|---:|
| `exit_code` | 0 | 0 |
| Пройдено чанков | 9 | 8 |
| FPS fly | 6,11 | 5,41 |
| Attempts created | 7 689 | 9 722 |
| Terminal без publish | 4 490 | 4 768 |
| Orphan cancelled | **0** | **0** |
| Superseded cancelled | 4 352 | 4 592 |
| Retained / rejected / reminted | 66 / 7 / 65 | 122 / 7 / 47 |
| Visible replacement deferred | 93 | 85 |
| Уникальные deferred XZ | 47 | 39 |

Немедленный orphan churn устранён в обоих прогонах. Оставшиеся cancellations относятся к superseded и не являются тем же дефектом.

Абсолютные latency/FPS нельзя использовать для утверждения регресса или улучшения относительно первого `before`: маршрут и скорость машины несопоставимы (20 чанков при 33 FPS против 8–9 при 5–6 FPS). В медленных сериях доминирует `swap`; telemetry ресурсов машины недоступна. Demand→publish p95 составил 10 135,12 / 8 529,28 мс, поэтому общая проблема отставания мира не закрыта этим локальным исправлением.

Проверки:

- `git diff --check` — PASS;
- `chunk_render_demand_test` Release — PASS, включая clockless creation timestamp, явно просроченный orphan и раздельную атрибуцию;
- `streaming_render_ready_invariants_test` Release — PASS;
- полная Release-сборка `Cubatarium` — PASS;
- проверка статического Windows EXE — PASS, только 6 системных DLL;
- четыре профильных приложения — `exit_code=0`.

Артефакты: `bin/suite_reports/perf_0_8/{before,cancel,after_1,after_2}`. Исходный `World_242` и baseline source не изменялись; каждый прогон использовал отдельную копию.

## Остаточные задачи

1. Разделять superseded по производителю/изменению target только если последующие сопоставимые прогоны покажут, что этот churn ограничивает throughput.
2. Не увеличивать relight FIFO cap вслепую: eviction сохраняет работу, а текущий механизм требует отдельного измерения deferred re-admission.
3. Отставание после выхода за кольцо, готовность света и кратковременные дыры при удалении блока остаются отдельными задачами; edit→publish latency сам по себе не доказывает отсутствие framebuffer-дыр.
4. Следующие performance-сравнения выполнять на сопоставимом пройденном маршруте и при стабильном состоянии машины; `swap`-доминированные серии маркировать как несопоставимые.
