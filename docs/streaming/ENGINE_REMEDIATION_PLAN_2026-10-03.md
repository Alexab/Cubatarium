# План исправления стриминга и отображения мира — 3 октября 2026

База: `develop` / `codex_audit2`, commit `185e2f08` (merge `codex_audit`).
Связанные документы: [аудит движка](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md),
[архитектурные контракты](ENGINE_REMEDIATION_PLAN_2026-09-22.md),
[каталог flight-экспериментов](FLIGHT_EXPERIMENT_SCRIPTS.md).

## Цель

Устранить тёмные и визуально пустые участки мира на длинных перемещениях,
сохранив повторяемый маршрут World_164 как основную регрессионную базу. Перед
дальним маршрутом измерять загрузку сохранённого мира и создание процедурного
мира. Периодически повторять ключевой сценарий на новых seed/мираx, чтобы
проверять переносимость исправлений.

## Текущая позиция и пределы доказательств

### Замер производительности и загрузки 2026-10-03

Для World_164 сравнивали холодный вход до и после передачи стартовой загрузки
terrain columns существующему AsyncChunkIO:

| Замер | До | После | Вывод |
|---|---:|---:|---|
| `spatial_chunks` | 35,28 с | 1,56 с | Фаза ускорилась примерно в 22,6 раза. |
| От начала операции до `prepare_view` | 39,12 с | 5,70 с | Синхронное чтение сохранённых колонок было крупным стартовым тормозом. |
| Источник 121 стартовой колонки | sync disk | 121/121 disk, `disk_light=1` | В этом входе повторной генерации не было; все завершились до mesh warmup. |

После изменения асинхронный worker читает сохранённые срезы, а основной поток
ограниченно применяет результаты: максимум 10 срезов или около 6 мс за update.
Это исправляет измеренный saved-world вход, но не является дальним flight acceptance.
Файл старого замера: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261003-203444.39168`;
нового: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261003-205930.20528`.

Для интерактивного нового мира (World_175, seed `3650471210`) получено:

| Фаза | Время | Наблюдение |
|---|---:|---|
| `generate_columns` | 6,08 с | `async_generation=1`, `generation_workers=4`. |
| relight columns + emissive | 0,54 с | Релайт не является доминирующей фазой этого запуска. |
| `mesh_warmup` | 10,66 с | В начале было 1270 dirty meshes. |
| `prepare_view` | 31,11 с | К концу прогрева ещё оставались dirty/missing mesh и неготовый spawn ring. |
| От начала создания до конца `prepare_view` | 51,29 с | Стартовый визуальный gate — открытая задача G1. |

У `prepare_view` пока нет точного terminal-reason в фазовом логе, поэтому нельзя
утверждать, что все 31 секунду ушли на один конкретный долг. Диагностический
`enter_lit` лог показывает состояние на 28,6 с: visibility debt 64, ring not ready
11, один missing greedy mesh, 7 inflight, underfeet/spawn ring не готовы. В
последующем live entry gate завершился через 1,9 с с `settle_reason=live_blockers`.
Свежий мир сохранён; данные и параметры прогона не удалять.

Этот World_175 замерен в `2026-10-03 21:08`, до Release-пересборки в `21:52`
и фикса async saved-world пути. Тайминги полезны как диагностический срез, но
G1 требует повторить интерактивное создание на актуальном EXE до следующего
дальнего acceptance.

Прямой no-teleport диагностический участок World_164 (visible Release, scale 1,
yaw 180) прошёл от focus `(7,3)` до `(-99,3)`: 106 chunk steps / 1 696 блоков,
медианная скорость `5.999` блоков/с, heading deviation 0. В source trace было
121 `disk/queued` + 121 `disk/complete` при старте и 947
`procedural/committed` вдоль нового фронтира. После стартового окна disk read не
появлялся; обратного прохода по выгруженным координатам не было. Значит, этот
замер подтверждает «старт с диска, новая территория из генератора», но ещё не
проверяет повторную загрузку выгруженного чанка.

У 947 процедурных commit `queue_ms` p50/p95/max = 7/97/15 042 мс,
`generation_ms` = 144/355/862 мс, `apply_ms` = 12/28/50 мс и `total_ms` =
232/531/15 264 мс. Максимальная очередь относится к двум низкоприоритетным
колонкам `(0,-4)` и `(1,-4)` рядом со стартовой областью; основное время создания
колонки — рабочий поток, а apply остаётся коротким. Редкие старые задачи всё же
могут ждать в очереди дольше 15 с.

Отчёт direct segment не прошёл rendering/stop gates: `holes_rate=1.0`,
`unfinished_visual=27`, `unlit_max=40`, `chunk_not_ready_med=27`,
`dirty_med/max=494/1408`, `post_stop_demand_stop_converged=false`. При этом
`dark_face_stale_near_n=0` и `visible_black_focus_n` median=6. Эти прокси не
объясняют цвет пользовательских затемнённых поверхностей и не привязаны к тому
же chunk/pixel. Итоговый report: `bin/suite_reports/engine_refactor/g3_world164_far_source_trace_20261003.json`;
perf: `bin/logs/perf_20261003-212517_33652.jsonl`.

Сценарий `product-174657-far` раньше шёл 300 с при нормальной скорости и фактически
прошёл только 1 696 блоков, не достигая checkpoint 8 192. Его default продлён до
1 800 с при speed scale 1. Для отдельной проверки unload→disk reload добавлена
контролируемая смена курса: `--reverse-course-after-sec 150`; она не использует
teleport, а `AppRunner` принудительно выставляет заданный курс каждый кадр и
считает отклонения.

Round-trip до и после disk-first исправления подтвердил обход persistence в
старом async streaming пути:

| Версия/участок | Disk complete | Procedural commit | Вывод |
|---|---:|---:|---|
| До исправления, повторный круг | 121 стартовая | 461 | Дальше стартового кольца все запрошенные колонки ушли в генератор, хотя для части координат уже были `.cchunk` файлы. |
| После исправления, тот же круг | 508, из них 387 на маршруте | 46 | Все 508 завершились с `disk_light=1`; для 46 `disk_miss` последовал procedural commit. |

Сопоставление координат двух трасс: 387 координат, прежде созданных процедурно,
теперь действительно прочитаны с диска; ещё 46 старых procedural координат не
имели файла при повторном запросе. Эти 46 не являются доказательством ошибки
читателя: надо выяснить, были ли колонки пусты, не завершили сохранение при
выгрузке или были отсеяны как неполные. В проверенном круге не было disk retry и
incomplete; все disk complete имели valid light flag.

Время `disk complete` для 508 колонок: p50 220 мс, p95 969 мс, максимум 15,11 с.
Максимум включает старую низкоприоритетную очередь и требует прицельной проверки;
не трактовать его как чистое время чтения устройства. После исправления round-trip
ещё не стал зелёным: `holes_rate=1.0`, `unfinished_visual=23`, `unlit_max=37`,
`visible_black_focus_n` median=1, `dirty_med/max=652/813`, stop convergence=false.
По сравнению с прежним кругом некоторые прокси ниже, некоторые выше; это пока не
доказательство улучшения изображения. Run оставался на нормальной скорости,
вернулся в исходный focus `(7,3)`, `heading_deviation=0`, teleport=false.
Артефакты: `bin/suite_reports/engine_refactor/g2_world164_roundtrip_diskfirst_20261003.json`,
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261003-215238.4768` и
`bin/logs/perf_20261003-215242_4768.jsonl`.

Итог: гипотеза «сохранённые дальние колонки не читаются и генерируются заново»
подтверждена для старого пути и исправлена. Гипотеза «все поздние тёмные участки
вызваны именно этим» не подтверждена: pixel/ray связка остаётся открытой, а
стриминг всё ещё показывает unfinished/unlit debt.

Отдельный CLI `--create-world` дал поколение на одном worker и выключенном async.
Это искусственный headless reference, а не оценка интерактивного создания.
При обычной Release-сборке генерация мира уже идёт на 4 worker-потоках.

Повтор на новом seed `3650471212` и актуальном Release (M370) подтвердил длинные
фазы создания и подготовки обзора в последовательном CLI-режиме: суммарно
`67.962 s`, из них `generate_columns=23.021 s` и `prepare_view=25.346 s`.
Это новый seed sample, но CLI явно использовал `async_generation=0`,
`async_chunk_io=0`, один worker; он не закрывает G1 для пользовательского
интерактивного старта и не измеряет изменение очереди completed results.
К концу `prepare_view` сохранялись `mesh_dirty=290`, `mesh_in_flight=1`, а
EnterLit сообщал `debt=10`, `ring=30`, `mesh_missing=1`. Следующий G1 замер —
интерактивное создание на текущем Release с фазовыми причинами ожидания.

### Дальний World_164 no-teleport run: остановка на дереве (M368)

Видимый Release run начался из контрольной позиции `[120,56,56]`, yaw `180°`,
прошёл на обычной скорости (median `5.99853` блоков/с) и не отклонял курс
(heading deviation `0`). Пользователь увидел столкновение с деревом и остановку.
Инструментальная трасса согласуется с этим: последний движущийся period был
около `x=-2826,y=67,z=56`; следующий зафиксировал `x=-2832,y=56,z=56`, после чего
все оставшиеся `588` из `896` period samples имели нулевую скорость. Итоговый
focus сдвинулся `(7,3)→(-177,3)`, то есть `184` чанка / `2 944` блока — меньше
checkpoint `8 192`. Перелёт не достиг far distance и не является acceptance.

В этих хвостовых samples оставались `chunk_not_ready` median `24` (конец `23`),
`dirty` median/max `1 672/1 792` (конец `1 666`), `pending_light=52` и
`empty_backlog` конец `23`. В целом `holes_rate=1.0`, `fly_visible_black_max=18`,
`unlit_max=19`, `wall_ms_fly_med=73.68 ms`; stop convergence не прошёл. Это
показывает большой незакрытый render debt у остановившейся камеры, но не
характеризует стриминг новых дальних территорий после точки столкновения.

В perf telemetry `camera_flight_ground_contacts` остаётся положительным после
контакта, а `movement_speed=0`; `camera_move_blocked_substeps` при этом не
фиксирует длительную блокировку. В `UCamera::DoMovement` свободный полёт сначала
проверяет `HasGroundSupport`; при контакте вызывает `OnLandedFromFlight` и
пропускает обработку W в этом physics step. Flight-sim повторно включает free
move, но при сохраняющейся ground support камера остаётся на месте. Это объясняет
остановку маршрута, не указывая на ошибку движения мышью. По этому маршруту
`stop_after_blocked_sec=0`, поэтому harness продолжал собирать метрики до таймера.
Для следующих far runs введён default watchdog `8 s`; `--stop-after-blocked-sec`
остаётся явным переопределением.

Коммит `7bfc330c` сохраняет исходный `users.json` и повторно ставит pin перед
каждым `--repeat`, а `19387103` включает watchdog по умолчанию для far-сценария.
M369 проверил y96 как безопасный для препятствий stress-маршрут, но этот уровень
вне eye-level proxy corridor и не достиг дальнего checkpoint. Прямой y70 маршрут
также пересекает лес (M335 около x=-2759), поэтому повторять ту же линию не нужно.
Для следующего acceptance сначала нужен короткий collision probe на y68–70 со
сдвигом стартового коридора по Z; сохранённые чанки предлагают z≈224, но требуют
проверки runtime. Затем можно увеличить фазу движения. Low-eye/y56 остаётся
collision-sensitive diagnostic, а collision response включён во всех профилях.

Отчёт: `bin/suite_reports/engine_refactor/g3_world164_far_diskfirst_20261003.json`;
perf: `bin/logs/perf_20261003-220433_38772.jsonl`; AppRunner report:
`bin/flight_sim_report.json`; INFO log:
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261003-220428.38772`.
Процесс завершился с `process_rc=0`, однако product gates `pass=false`. В manifest
записан `git_sha=aaff26c5` и Release EXE
`1a2420a3ab678ae6669e96a51169f3a04131f4593545bc18eeb7ff93708d087a`; dirty hash
не чистый, так как runner был изменён уже после запуска процесса. Использовать
этот flight только как collision-limited diagnostic, не как чистый source commit
acceptance.

### Дальний World_164 y96 run: M369 — generation frontier и незакрытый долг

Видимый no-teleport Release run прошёл на scale `1` от `[120,56,56]`, yaw
`180°`, удержал `y=96` и не столкнулся. Фокус достиг `(-497,3)` от `(7,3)`:
`504` чанка / `8 064` блока, на `128` блока меньше checkpoint `8 192`. M369 —
длинный диагностический run, но не far-distance acceptance. Adequacy classifier
текущего product proxy допускает eye-level `player_y=45..70`, поэтому M369
провалил `altitude_out_of_corridor`. Симптом затемнения тоже не воспроизведён по
proxy: `visible_black_focus` median `0`, max `18`; `fly_void_near_max=0`.

Долг остался высоким: `holes_rate=0.93596` (`unfinished_visual` proxy, не доля
чёрных пикселей), `dirty_med/max=1 071/1 779`, `wall_ms_fly_med=91.11 ms`,
pressure red `75.8%` periods, `unlit_max=31`. `post_stop_convergence=false`:
missing/effective holes не обнулились, pending/not-ready/focus-dirty не сошлись.
Empty-world proxy прошёл с median `47` opaque draw commands, однако это не
доказывает полноту геометрии или корректное освещение.

Source trace зарегистрировал `1 917` disk completions, каждый с `disk_light=1`,
затем `1 324` disk misses и `1 145` procedural commits. Для commit:
`generation_ms` p50/p95/max `94.6/141.7/945` мс, `apply_ms`
`5.5/9.5/28.6` мс, `queue_ms` `182.6/11 345/61 961` мс, `total_ms`
`994/78 083/397 694` мс. Остаток `total - queue - generation - apply` оценивает
интервал от завершения worker generation до применения: p50 `366` мс, p95 `49.94` с,
max `389.74` с; `154` commits ждали более 10 с, `56` более 60 с и `9` более
300 с. `gen_backlog_total` p95/max `51/67`, `gen_q` `12/36`. Значит, стоимость
самой генерации и `ApplyTo` не объясняет большие latency; отдельно исследовать
готовые результаты, лимит commits, priority aging и отбрасывание задач за камерой.

Manifest чистый: `git_sha=d3be1310`, Release EXE SHA-256
`1a2420a3ab678ae6669e96a51169f3a04131f4593545bc18eeb7ff93708d087a`,
`teleport=false`; процесс завершился `rc=0`, но harness `pass=false`.
Артефакты: `bin/suite_reports/engine_refactor/g3_world164_far_y96_diskfirst_20261003.json`,
`bin/logs/perf_20261003-225116_32248.jsonl`, `bin/flight_sim_report.json`,
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261003-225112.32248`.

M369 выполнен как продолжающаяся диагностика до повторного закрытия G1; он не
заменяет повторное измерение загрузки и создания мира на актуальном Release.

**G1 статус: частично закрыт.** Синхронный saved-world read измерен и заменён на
bounded async apply; disk-first работает также на движущемся frontier.
Интерактивный новый мир измерен на более раннем Release (World_175, M367), а M370
добавил чистый одно-worker CLI срез на новом seed. Ни один из них не заменяет
повтор интерактивного создания на текущем Release. Для закрытия G1 нужны такой
повтор, warm/cold вход и устранение либо обоснование длинного `prepare_view` с
остаточным mesh/readiness debt.
Дальний acceptance остаётся за этой проверкой и должен пройти 8 192 блока без
speed multiplier; короткие round-trip/source probes допустимы как диагностика.

- На M367 отрисованные почти чёрные пиксели совпали с валидными opaque-поверхностями,
  ненулевыми GPU face indices и видимой MDI-командой. Следовательно, низкая
  яркость сама по себе не доказывает отсутствие voxel data или mesh.
- Тот же прогон оставил product gates красными: `holes_rate=1.0` — внутренний
  `unfinished_visual` proxy, `fly_visible_black_max=22`, `dirty_max=215`,
  post-stop convergence=false. Маршрут прошёл 1 792 блока вместо дальнего
  checkpoint 8 192. Эти значения не являются полным framebuffer-аудитом.
- Код содержит и disk reload, и generation пути. Текущие отчёты не связывают для
  одной координаты источник данных, его light revision, mesh publication и пиксель.
  Поэтому причина появления затемнения после долгого полёта остаётся открытой.
- Текущий perf отчёт M367 измеряет полёт, а не фазовую длительность начальной
  загрузки сохранённого мира или создания нового мира.

## План работ и ворота

### G0 — Воспроизводимая и сохраняемая база

1. Хранить все runner/analyzer сценарии, относящиеся к flight-исследованиям, в
   Git; каталог должен указывать на входы, назначение, ограничения и основной
   поддерживаемый запуск.
2. Контрольный сценарий: видимый Release, no-teleport,
   `product-174657-far`, World_164, одинаковые пользовательские настройки,
   освещение, стартовая позиция и версия мира. Всегда сохранять commit/EXE hash,
   конфигурационные hashes, траекторию и метрики. M369 показал, что y96 обходит
   деревья, но находится вне eye-level proxy corridor `y45..70` и не воспроизводит
   продуктовый dark symptom. Прямой y70 маршрут проходит через лес; для acceptance
   коротким no-teleport probe проверить верхнюю границу `y68..70` на сдвинутой по Z
   полосе. M371 подтвердил z=224/y70 как collision-clear до `3 424` блоков, но
   render gates не прошёл и не достиг far checkpoint. Low-eye y56 оставить
   collision-sensitive control, а y96 — отдельным high-altitude stress diagnostic.
3. После каждого изменения повторять контрольный отрезок до задетой области;
   дальний acceptance не объявлять до достижения checkpoint 8 192 без collision
   shortfall или искусственного ускорения.
4. Зафиксировать мышиный ввод/heading в run manifest или trace. Если heading
   уходит от сценария, этот прогон не сравнивать как повторяемый.

**Gate:** маршрут и пробы дают координатные данные, окно видимо, `teleport=false`,
скорость/направление соответствуют сценарию; артефакты и параметры восстановлены.

### G1 — Вход в мир и создание мира до дальних полётов

Сначала добавить phase wall-time для `WorldCooperativeSession` и сохранять
времена начала/завершения фаз с числом chunk/column, объёмом loaded data и режимом
async. На каждый сценарий собирать cold saved-world load, повторный warm load и
генерацию нового seed. Не сравнивать headless `--create-world` с интерактивным
созданием без оговорки: CLI специально выставляет `AsyncChunkGeneration=false` и
`AsyncChunkIo=false`.

Затем сравнить фазы `scan_chunks`, `spatial_chunks`, `generate_columns`, relight,
mesh warmup и prepare view. Отдельно измерить main-thread время: async file read
не означает async deserialize/apply. Исправлять выявленный доминирующий участок,
сначала с ограничением работы по времени кадра и явными progress counters, затем
проверить, что initial world readiness и видимый spawn не регрессировали.

**Gate:** есть фазовые cold/warm/new-world отчёты; известны p50/p95 и максимумы,
main-thread hitch contribution, worker count и очередь; найденные блокирующие
участки исправлены или доказано, что они не мешают загрузке/созданию. До
следующего дальнего acceptance повторить G1 на текущем Release; M369 остаётся
документированной диагностикой и не является базой сравнения улучшений.

### G2 — Разделить disk reload и procedural generation

Трасса уже покрывает стартовый disk load и commit процедурной генерации. На
дальнем маршруте использовать `CUBA_WORLD_COLUMN_SOURCE_TRACE=1`; текущая
инструментация фиксирует disk hit/miss, pending save, highest Y, valid disk light,
retry, очередь/latency/apply для async disk load и очередь/generation/apply для
procedural commit. Пока это не полная трасса до GPU publication и пикселя.
M369 показал отдельный неразмеченный интервал между worker generation и
main-thread apply: у 1 145 commits вычисленный p50/p95/max был
`0.366/49.94/389.74` с. После инструментирования ready queue и fix
FocusIngressBudget M371 записал точный `ready_wait_ms` для 1 633 commits:
p50/p95/max `190/1 196/15 016` ms, `10` ожиданий выше 10 s, ни одного выше 60 s.
В том же прогоне `queue_ms` p95 был 1.89 s, generation p95 182 ms, apply p95
23 ms, full total p95 3.29 s. Это обнадёживающий результат, но не чистое A/B:
маршрут M371 смещён по Z, короче M369 и выполнялся на другой высоте. Следующий
повтор должен сохранить этот коридор и длинную дистанцию; не увеличивать общий
commit cap вслепую. Основной незакрытый долг M371 переместился к render readiness:
`chunk_not_ready` median 27, `unlit_max=37`, post-stop convergence=false.
Продолжить метрику oldest-ready age, completed-ready count, effective commit
budget и отброшенных/устаревших результатов вместе с координатным lifecycle.
Продолжить координатную трассу жизненного цикла колонки:

`request → persisted high-water/file hit → save-pending guard → disk read result
→ deserialize/apply → generation request/commit → relight → mesh source revision
→ GPU publication → pixel probe`.

Источник отмечать явно: `resident`, `disk`, `procedural`, `mixed/repaired`,
`unknown`. Записывать world/session epoch, chunk XYZ, incarnation, file format,
наличие pending save, token/sequence, timestamps и точный отказ/повтор. Trace должен
позволять сравнить в одном мире две группы: уже пройденная и выгруженная колонка
при возврате к ней; и новая колонка без файлов впереди маршрута. Для каждого
затемнённого probe проверять block id, voxel/light validity, baked vertex light,
mesh/source manifest, texture readiness, MDI visibility и итоговый цвет.

**Gate:** для выбранных тёмных участков доказана цепочка источника и причина
задержки/неверного света; если статус `unknown`, gate остаётся открытым.

### G3 — Исправить найденный дефект рендеринга/стриминга

Изменения выбирать по G2, а не по одной корреляции яркости. Возможные владельцы
работы: data I/O и deserialize, generation queue/admission, lighting, dependency
debt/seam invalidation, mesh build/publication, MDI/texture state, shader lighting.
Сохранять ранее подтверждённые контракты identity/manifest, конечную сходимость,
владение demand и безопасное retired-resource lifetime из архитектурного плана.

Каждый patch проверять на одном и том же World_164 отрезке видимым no-teleport
прогоном с framebuffer/ray/lifecycle evidence. M371 показал, что focus-data census
остаётся выключенным без `CUBA_VISUAL_BLACK_TRACE`; следующий длинный y70/z224 run
должен включить его, `CUBA_WORLD_COLUMN_SOURCE_TRACE` и низкочастотные framebuffer
captures. Достичь 8 192 блока, проверить real voxel/mesh/light state в кадре и
дождаться stop convergence. Этот инструментированный run использовать для
локализации; отдельный uninstrumented повтор — для performance comparison. Пустой/чёрный proxy не считать
исправленным только из-за меньшего счётчика или более короткого прогона.

**Gate:** контрольный маршрут проходит far checkpoint, нет необъяснённых
невалидных/неопубликованных поверхностей в проверяемом коридоре, а stop convergence
конечна. Операторская визуальная проверка остаётся отдельным условием.

### G4 — Новые миры как периодическая проверка переноса

Основной цикл остаётся на детерминированном World_164: не менее трёх повторов на
контрольных изменениях на каждый один новый seed cohort. После каждого крупного
вехового изменения (смена владельца очереди, persistence/light контракт,
публикационный путь) запускать отдельный новый мир с записанными seed, generator,
preset и настройками. На fresh-world run проверять создание, первое появление
мира, короткий no-teleport участок и возврат к ранее выгруженным колонкам в той же
сессии. Эти запуски дополняют контрольную базу, но не заменяют её.

**Gate:** исправление работает на исходном мире и как минимум на новом seed;
параметры свежего мира фиксированы и результат можно повторить.

### G5 — Сборка Release с параллельной компиляцией

Текущая конфигурация использует Visual Studio 17 2022. Ранее запуск
`cmake --build bin --config Release --target Cubatarium --parallel 8` ограничивал
параллельность MSBuild, однако generated `Cubatarium.vcxproj` не задавал `/MP`,
поэтому единый MSVC compile task не компилировал translation units параллельно.
Для основного приложения включён `/MP`; Release target собран командой
`cmake --build bin --config Release --target Cubatarium --parallel 8`. Generated
Visual Studio project подтверждает `MultiProcessorCompilation=true`, а сборка
использовала несколько `cl.exe` процессов. Продолжать собирать только Release
target; Debug и тестовые targets в этой работе не запускать.

## Исследовательская основа

- Godot Voxel описывает persistent chunk streams: загружать данные по блокам,
  сохранять изменённые при выгрузке, а disk load выполнять в рабочих потоках:
  [Streams](https://github.com/Zylann/godot_voxel/blob/master/doc/source/streams.md).
- Его генераторные API рассчитаны на независимую работу блоков в нескольких
  потоках; пользовательский generator должен быть thread-safe:
  [Generators](https://github.com/Zylann/godot_voxel/blob/master/doc/source/generators.md),
  [Performance](https://github.com/Zylann/godot_voxel/blob/master/doc/source/performance.md).
- В Luanti очередь emerge объединяет запросы блока и ограничивает очередь; worker
  сначала проверяет память, затем диск, затем generation:
  [Emerge implementation](https://github.com/luanti-org/luanti/blob/master/src/emerge.cpp).

Переносимый вывод для Cubatarium: источник данных — отдельный наблюдаемый результат
до mesh readiness; очередь должна ограничивать дубликаты/запас работы, а disk I/O,
decode/apply, generation, lighting и publication должны иметь отдельные latency и
completion counters. Конкретные лимиты брать из измерений этого движка.
