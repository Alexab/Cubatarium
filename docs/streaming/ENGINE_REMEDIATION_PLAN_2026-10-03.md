# План исправления стриминга и отображения мира — обновлён 5 октября 2026

Исходная база: `develop` / `codex_audit2`, commit `185e2f08` (merge
`codex_audit`). На 5 октября текущий чистый Release manifest — M396,
commit `19007cc5`, на закреплённом профиле M335. M395 выявил ошибку сходимости
обхода препятствий; она исправлена и проверена полным M396 по исходному
Z-коридору. Red load cap из M394 откатил commit `820af493`; quota bump не
возвращать без отдельного причинного сравнения.
Связанные документы: [аудит движка](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md),
[архитектурные контракты](ENGINE_REMEDIATION_PLAN_2026-09-22.md),
[каталог flight-экспериментов](FLIGHT_EXPERIMENT_SCRIPTS.md).

## Цель

Устранить тёмные и визуально пустые участки мира на длинных перемещениях,
сохранив повторяемый маршрут World_164 как основную регрессионную базу. Перед
дальним маршрутом измерять загрузку сохранённого мира и создание процедурного
мира. Периодически повторять ключевой сценарий на новых seed/мираx, чтобы
проверять переносимость исправлений.

Профиль M335 зафиксирован: World_164, start `[120,56,56]`, eye `70`, yaw
`180°`, pitch `−30°`, scale `1`, fixed clear day, no teleport, полёт `2 800 s`
и остановка `20 s`. Не подбирать новые условия съёмки. Обход препятствия может
локально изменить траекторию только при предсказанной опасности или контакте.

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
Исторические M335/M368 подтверждают два уже используемых профиля: визуальный
M335 (`start=[120,56,56]`, cruise y=70, yaw=180°, pitch=−30°) и collision
control M368 (`product-174657-far` defaults, cruise y=56, pitch=0°). Не подбирать
новые высоты, углы или Z-коридоры для acceptance. M377/M378 повторили визуальную
линию M335 без контакта, но M378 шёл через меняющееся время суток и не достиг
8 192 блоков. Для следующего визуального повтора использовать M335 без изменений
камеры/маршрута, с закреплённым ясным днём; `tools/flight_sim_fixed_day.py`
временно задаёт `time_of_day=0.25`, clear weather и нулевые облака и восстанавливает
`world_data.json` побайтно. Маршрутное обходное движение разрешено только как
автоматическая временная детур-ветка, после препятствия она возвращается на тот же
курс. Её проверять отдельно на прежнем collision-control M368, не заменяя им
визуальный acceptance.

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

### Дальний World_164 side corridor: M372 — владелец mesh-work теряется на frontier

M372 повторил no-teleport маршрут M371 по z=`224`, cruise y=`70`, pitch `0°`,
Release, speed scale `1`. Видимый процесс завершился с `rc=0`, прошёл `6 960`
блоков, но far checkpoint `8 192` не достиг. Heading/y/z оставались стабильны;
ground-contact и blocked-substep counters нулевые. Это длинный streaming probe,
а не доказательство, что низкий маршрут больше не встречает деревья.

С `CUBA_VISUAL_BLACK_TRACE=1` census был валиден в 898/1 135 periods. В 154
periods были resident solid camera-band срезы без drawable mesh и без владельца
работы; максимум — 25 таких срезов при `x=-4271`. У образца `(-266,3,19)` есть
12 non-air блоков и `desired_geom_rev=1`, но `mesh_revision=0`,
`published_geom_rev=0`, `active_stage=0`, `mesh_work_owner_flags=0`, dirty queue и
ColumnFlow repair ticket отсутствуют. Следующий кодовый шаг — найти, почему
FirstMesh demand/repair не остаётся зарегистрированным для resident non-air
camera-band slices, и закрыть инвариант «mesh ready либо явный owner/terminal
reason с повторной попыткой». Не увеличивать commit budget, пока эта цепочка не
разобрана.

Источник трассы одновременно показывает queue/ready backlog: 2 914 procedural
commits, `queue_ms` p95 13.87 s, `ready_wait_ms` p95 3.70 s, `generation_ms` p95
226 ms и `apply_ms` p95 23 ms. Commit cap во всех событиях равнялся одному
результату на frame. Полный `total_ms` p95 достиг 17.30 s; это не прямое сравнение
с M371 из-за более длинной полосы и другого распределения disk/procedural work.
У M372 median wall frame `159.8 ms`, renderer stage `93.5 ms`; dominant spike
class — `stream`, max spike почти 8 s. Нужны queue age/owner transitions и отдельный
warm/cold возврат к тем же колонкам.

M372 не даёт валидной визуальной оценки поверхности: кадры почти полностью неба,
поскольку `pitch=0°` при высоте y=`70`. В free-move камера перемещается по полному
`Front`, поэтому простой pitch-down вызвал бы снижение траектории и риск
столкновения. Этот профайл больше не является кандидатом на визуальную приёмку.
Пользователь зафиксировал ранее отработанную камеру M335; дальнейшие проверки
сохраняют y=70, yaw=180°, pitch=−30°, start и speed scale 1. Время/погоду
фиксировать отдельно, не меняя маршрут. Нужен участок `>=8 192` блоков с
координатными pixel/ray/source witnesses и конечной stop convergence.

Гейты M372 остались красными: `unfinished_visual` rate `96.65%` (внутренний proxy,
не доля чёрных пикселей), `chunk_not_ready` median `26`, `unlit_max=51`,
`visible_black_focus` median/max `4/68`, stop convergence=false. Анализатор
сообщил `process_rc=0`, `pass=false`; манифест чистый, EXE Release hash совпал с
M371. Подробные данные приведены в разделе M372 аудита.

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
2. Основной визуальный профиль зафиксирован историческим M335: видимый Release,
   no-teleport, `product-174657-far`, World_164, start `[120,56,56]`, cruise y=70,
   yaw=180°, pitch=−30°, speed scale 1. Камеру и коридор больше не калибровать.
   Запускать через `tools/flight_sim_fixed_day.py` с дневным временем 0.25 и clear
   weather; сравнивать commit/EXE/world/config hashes, маршрут, pixel/ray/source
   traces. M377 прошёл 6 640 блоков за 1 800 s, M378 — 7 680 блоков за 2 400 s.
   M379 на fixed daylight достиг 8 192, но render analyzer остался FAIL; далее
   сравнивать отрезки только при неизменных условиях. M380 повторил тот же M335
   маршрут после исправления GPU pool reuse и packed fallback: видимый no-teleport
   Release, скорость 5.19287 blocks/s, без ухода yaw/pitch, пройдено 10 080
   блоков и checkpoint 8 192. Продуктовые ворота остались FAIL (24/39),
   post-stop convergence=false. На историческом дереве около x≈−2 832 обход
   зарегистрирован и завершён; близкий кадр выглядит как удар, но движение
   продолжалось с нулевыми blocked substeps/ground contacts. M335 параметры и
   коридор не менять; если следующий run покажет реальные blocked substeps,
   корректировать именно обход и повторять тот же профиль.
   M368 остаётся отдельным collision-control: прежний default product route
   (y56, pitch=0°) остановился у дерева около x=−2 832. В flight-sim уже
   включён по умолчанию forward hazard probe с поиском свободных боковых
   сегментов, попытками обхода и возвращением на линию; события обхода пишутся
   в отчёт. M380 подтвердил один успешный обход дерева (`hazard=5.25`, right
   offset 3, pass 10.25, completed=1), без collision stop; визуально близкий
   пролёт не считать остановкой без movement/collision evidence. Если контакт
   всё же мешает основному пролёту, исправлять именно этот алгоритм и повторять
   тот же M335 профиль, не подбирать новый визуальный маршрут.
   M369/y96 и M371/M372 z224/pitch0 — только старые stress diagnostics,
   не новые acceptance conditions.
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
маршрут M371 смещён по Z, короче M369 и выполнялся на другой высоте; не
использовать его как новый визуальный коридор. M378 на M335 маршруте показывает,
что незаписанная дальняя территория создаётся процедурно. Сравнивать новые source
traces на M335 при фиксированном дне, не увеличивать общий commit cap вслепую.
Основной незакрытый долг M371 переместился к render readiness:
`chunk_not_ready` median 27, `unlit_max=37`, post-stop convergence=false.
Продолжить метрику oldest-ready age, completed-ready count, effective commit
budget и отброшенных/устаревших результатов вместе с координатным lifecycle.
M378 даёт первые синхронизируемые данные для длинной трассы. Было `2 014`
disk completions и `1 508` disk misses с последующими procedural commits. В
камерном диапазоне примерно x=−6 960…−7 600 новые колонки устойчиво проходили
через disk miss → procedural commit; одновременно census доходил до `60`
not-render-ready и `46` focus slices без готовой геометрии. Pixel probes включали
32 768 экранных точек; screen-ray trace — 8 192. При фиксированном day factor 1 /
night factor 0.22 median probe luminance был 158, а тёмных `<40` не было; при
night factor 0.35 median был около 22, в том числе на валидных поверхностях.
Это подтверждает сильный time-of-day вклад, но не объясняет все отсутствующие
меши: M378 `dirty_med=462`, `chunk_not_ready_med=24`, `post_stop_convergence=false`.
Far checkpoint не достигнут (`7 680` блоков).

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

Каждый patch проверять на одном и том же World_164 профиле M335: видимый
no-teleport Release, y=70/pitch=−30°/yaw=180°, fixed daylight, без смены Z.
M379 уже прошёл checkpoint 8 192 и зафиксировал низколюминансные daylight pixels
при valid voxel hits, но без opaque live-GPU/MDI batch на конкретном witness.
Периоды также показывали около 2 000 aggregated publication OOM retains и
полностью занятые 2 048 mesh slots при отсутствии staging allocation failures.
Это делает pool fragmentation/capacity и условие packed fallback проверяемыми
владельцами, не доказывая их единственной причиной. Commit 656fe5c6 добавил
best-fit/free-range split/coalesce и разрешил packed predecessor до появления
исполняемой MDI batch. M380 на том же M335 daylight маршруте подтвердил
same-coordinate OOM 213–229 → 0 и pool use 257/320 → 90/157 MiB; camera-band
no-drawable остался 0, а draw_oracle_missing_resident не снизился.
Harness autosave выключен, поэтому disk misses новых координат в этих прогонах
не доказывают failure persistence. Инструментированный прогон локализует дефект,
отдельный uninstrumented повтор пригоден для performance comparison. Пустой/чёрный
proxy не считать исправленным из-за меньшего счётчика или более короткого прогона.
Финальный M380 остался FAIL (24/39): median wall `97.83 ms`, render total
`72.91 ms`, stream `43.22 ms`, mesh emerge `23.16 ms`, `chunk_not_ready` median
26, dirty median 265; post-stop convergence=false. Это подтверждает, что pool
fix устранил OOM retain в сопоставимом месте, но не исправил общий визуальный
долг и задержки.

Разбор M380 уточнил два разных источника визуальных задержек.

1. Новый столбец `(-550,0,3)` не нашёлся на диске: источник записал
   `procedural disk_miss`, затем commit с `generation_ms=81.55`,
   `ready_wait_ms=72.29`, `apply_ms=4.31`, total около `177 ms`. Для соседних
   новых столбцов были queue waits до `23.9 s`. По всей M380 source trace было
   1 883 disk completions, 2 987 disk misses и 2 945 procedural commits.
   Генерация отдельного результата была быстрой (p50/p95 `92.7/138.1 ms`), но
   request queue p95/max составил `28.69/62.90 s` (530 commits ждали в этой
   очереди больше 10 s), а generation-finished → apply p95/max —
   `7.30/32.00 s` (61 результат ждал больше 10 s). Apply p95 был `8.13 ms`.
   Эти хвосты важны для дальнего streaming и требуют отдельного causal trace
   по queue priority, ready count и frame admission; не менять общий commit cap
   только на основании одного p95.
2. Renderer trace того же screen-ray свидетеля показал drawable mesh,
   `fully_dark + pending_light + provisional_preview`. После того как field-light
   revision стал актуальным, published light/mesh догнали его, а preview
   выключился. Между точками камеры `x=−8 722` и `x=−8 805` прошло около 83
   блоков (примерно 16 s на скорости M380); sampled voxel находился в 79.8
   блоках впереди при выборе. Это объясняет, как chunk может некоторое время
   выглядеть приглушённым до завершения relight, даже когда его геометрия уже
   рисуется. Начальный source trace видел deferred-far FIFO и существующий Flow
   ticket, но выбранный screen-ray путь сам не вызывал ограниченное visible FIFO
   admission, которое умеет переносить эту работу из deferred-far в видимую
   очередь.

Исправление в работе: для выбранного screen-ray light-debt кандидата внутри
forward horizon вызывать существующий `EnqueueVisibleRelight`, защищая остальные
выбранные ray-колонки. Он остаётся bounded, переносит deferred-far band и
фиксирует admission outcome/victim в `ScreenRayRepair`; за пределами горизонта
остаётся прежний flow. Следующий M335 повтор должен сравнить расстояние/возраст
от screen-ray admission до settled/published light и длительность preview,
плюс source queue tails и основные render gates. Камеру, высоту, Z, pitch, seed
и скорость M335 не менять. M380'овский обход дерева уже завершился успешно по
телеметрии; collision counters и `detours_completed` продолжать проверять, но
отдельные условия полёта ради картинки не подбирать.

Классификация пикселей также уточнена. Из 191 low-luma sample M380 169 имели
depth-source block id `572` (`tree_leaves`), 9 — `573` (`tree_log`), 3 — `597`
(`tree_bark`); все 181 валидные face-light samples имели sky light `1`. Всего
четыре low-luma pixels были помечены provisional preview. Значит, эта выборка
плохо представляет приглушённые участки больших чанков и часто ловит тёмную
текстуру листвы. DDA/depth distance расходился более чем на 2 блока у 121 из
169 leaf surfaces, поэтому cutout surface mapping нужно учитывать; такое
расхождение не доказывает отсутствующую геометрию. У каждого подозрительного
участка по-прежнему сопоставлять материал, depth surface, lighting, readiness и
кадр.

### Результат M381 — M335 повторён, очередь остаётся узким местом

M381 запустил тот же visible no-teleport M335 на World_164, fixed daylight,
Release `b853570a` (EXE SHA256
`75393A032B0AE31471C82E8DBC2CD722FA5DD042C4E0CCCBEC072772EF625FD8`). Маршрут
прошёл 646 чанков до focus `(-639,3)`, а renderer gates остались FAIL (`22/39`),
post-stop demand convergence — false. `wall_ms` median составил `91.49 ms`,
effective fly FPS — `11.0`, dirty median/max — `412/1 330`. На отдельных дальних
отрезках `visual_holes`, `pending_dark` и `not_render_ready` росли вместе с
`stream_ms`/`scene_ms`; замена условий полёта не требуется.

Полный source trace разделяет генерацию и ожидание результата. Из `3 050`
procedural commits queue p50/p95/max составил `26.8 ms / 29.05 s / 71.69 s`
(`576` результатов ждали старта более 10 s). Сам `generation_ms` был
`92.7 / 133.8 / 367.6 ms`; generation-finished → apply `ready_wait_ms` —
`251.6 ms / 7.47 s / 32.04 s` (`96` более 10 s), apply p95 — `8.07 ms`,
total p95/max — `36.42 / 84.75 s`. Все `3 050` commits имели
`max_commits_per_frame=1`, при ready batch до `73` результатов. На новых
колонках около X=−390 и на повторном witness X=−550 disk miss приводил к
быстрой генерации (примерно `77–143 ms`), но request queue занимала до
`6.8–15.4 s`, а ожидание commit — до `8.2 s`; полный срок достигал `20–22 s`.
На тех же отрезках появлялись readiness holes. Следующий эксперимент должен
проверить budget-aware drain готовых near/focus результатов и admission
backpressure, измеряя отдельно кадр-время; общий cap вслепую не повышать.

Disk-backed колонок было `1 851`; их `elapsed_ms` p50/p95/max —
`338 ms / 34.28 s / 516.28 s`, `71` превысили минуту, `55` — две минуты. Этот
таймер начинается при запросе колонки и заканчивается после получения срезов,
decode/apply и финализации, поэтому это **request-to-ready latency**, а не
измерение чистого времени чтения файла. Следующая доработка должна добавить
раздельные метки worker-queue wait, file-open/read, deserialize, main-thread
apply и finalization, вместе с pending/active read/save counts. После разложения
выбрать исправление для I/O очереди или apply budget по фактическому владельцу
задержки.

Новая visible-relight admission была реально вызвана: в M381 записано `397`
ScreenRayRepair строк, `28` screen-ray samples с `light_debt=1` и `17` явных
visible-relight admissions. Для X=−513 outcome `9` (bounded visible-repair
reserve) предшествовал переходу той же колонки `light_debt: 1 → 0` примерно за
18 s; у другого кандидата X=−525 debt очистился примерно за 2 s, но
provisional preview ещё оставался. На самом M380 witness X=−550 в M381
`light_debt=0`, так что там исправление не активировалось. Это подтверждает
путь admission, но не закрывает общую задержку lighting/readiness.

В `22 480` pixel probes было `101` low-luma `<32` sample. Все `101` имели
валидную depth surface, draw-ready drawable и видимый MDI pass; `85` source
faces были `tree_leaves`, `3` — `tree_log`, `2` — `tree_bark`. Это не подтверждает
отсутствующую геометрию в этих пикселях, но sparse low-luma выборка не закрывает
жалобу на большие приглушённые области. Для подозрительного кадра продолжать
pixel/depth/source/lifecycle join, не трактовать низкую яркость foliage как
streaming hole.

Collision telemetry M381: `collision_stop_triggered=false`, один planned и
completed detour, zero plan failures, heading deviation zero. Flight-sim уже
обходит препятствие по существующей логике; M335 остаётся control profile.

M379/M380 same-coordinate pool slice около x=−5 800 по-прежнему подтверждает
только устранение publication OOM (213–229 → 0) и снижение pool use
(257/320 → 90/157 MiB); camera-band no-drawable равен 0, но
draw_oracle_missing_resident не снизился, wall samples перекрываются. Это не
закрывает render holes.

**Gate:** контрольный маршрут проходит far checkpoint, нет необъяснённых
невалидных/неопубликованных поверхностей в проверяемом коридоре, а stop convergence
конечна. Операторская визуальная проверка остаётся отдельным условием.

### Результат M382 — disk I/O быстрый, completion backlog отстаёт от камеры

M382 повторил тот же visible no-teleport M335 на `World_164`; параметры камеры,
скорость, daylight, старт и route hash совпадают с M381. Прогон штатно завершил
временную фазу без `collision_stop`; AppRunner также записал ноль obstacle probes.
При этом оператор сообщил, что на экране камера остановилась у дерева. Это
наблюдение расходится с метриками движения, поэтому его нельзя считать
подтверждённым collision stop: следующий экранный прогон должен сохранять кадры
последних секунд и сопоставлять их с camera position/blocked-substeps.

За 2 800 секунд полёта камера прошла 615 чанков и закончила на x=−608 — на 31
чанк раньше M381. Скорость отображения просела: median wall frame `104.2 ms`,
effective fly FPS `9.62`, Red pressure во всех периодах. Анализатор не прошёл
визуальные/очередные gates: `unfinished_visual` присутствовал в 97.96% samples,
самый длинный непрерывный отрезок — 803 периода; Dirty median/max `657/1265`,
видимый near-void достиг `4462`. При этом pixel/depth join нашёл 101 dark probe
из 21 920; 100 имели валидную глубину и видимый MDI draw, большинство hits были
по листве. Эта выборка не исключает большие приглушённые пятна, но не доказывает
массовое отсутствие draw geometry.

Новая разбивка I/O показывает, что чтение файла не является главным источником
задержки: `file_read_ms` p50/p95 `0.92/1.21 ms`, worker queue p95 `0.65 ms`,
тогда как ожидание применения готовых результатов p95 достигало `70.37 s`, а
очередь у финализации колонки — p95 `408` готовых slices и максимум `1710`.
На маршруте x=−608 последние полностью применённые disk columns находились лишь
около x=−256. Значит, диск уже прочитал результаты, но main-thread FIFO drain не
успевает подавать их в актуальный camera corridor.

Для процедурного frontier в M382 generation p95 обычно оставался около
`100–180 ms`; основную задержку составляли request queue (по полосам p95 до
`54.93 s`) и result-ready wait (до `11.48 s`). Во всех `2840` miss-записях
предел применения оставался `max_commits_per_frame=1`; ready batch достигал 51.
На witness `(-550,0,3)` очередь заняла `15.24 s`, generation `94 ms`, ready wait
`327 ms`; соседний экранный geometry debt имел очередь mesh position `1/595` и
`mesh_scheduled_this_frame=0`. Это усиливает гипотезу об обслуживании очередей,
но увеличение commit cap без frame/apply budget пока не обосновано: median wall
уже около 104 ms, а render занимает 58% кадра.

**Статус G3 после M383:** bounded near-focus drain с aging реализован в
`e9d999d9` и проверен на том же M335. Следующий участок работы — очередь
процедурных запросов/готовых результатов и жизненный цикл приоритизации dirty
mesh. Общий commit cap пока не поднимать: ограничение одного procedural commit
на кадр сохраняется, а медианный frame wall уже выше 100 ms. Любой локальный
drain должен сохранять измеренный wall/apply budget и проверяться повтором M335.
Новый seed остаётся периодической проверкой, не заменой повторяемой базы.

### Результат M383 — disk completion ближе к камере, procedural и mesh debt остаются

M383 собран из чистого Release `5b185435`, EXE SHA-256
`612AE8480BD1AA30BBBF1A3070647F42492A60C34C7134EFF0C280C729AB4A1A`. Это тот
же видимый no-teleport M335: `World_164`, seed `3650471197`, start
`[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`, scale `1`, daylight,
2 800 s fly + 20 s settle. Маршрут завершился штатно (`process_rc=0`), прошёл
checkpoint 8 192, но закончился на focus x=−588 (на 20 чанков меньше M382).
Ни высота, ни камера, ни темп, ни исходный мир не подбирались заново.

Disk completion drain выбирает ограниченную порцию ближайших к focus slices,
добавляя возрастной бонус и оставляя старым далёким завершениям шанс на
обслуживание. По сравнению с M382 p95 result-wait снизился `70.37→26.05 s`, а
самая западная полностью применённая disk column продвинулась примерно с
x=−256 до x=−335. Это перспективный эффект, но не доказанный причинный выигрыш:
самый длинный wait вырос `676.6→706.3 s`, размер ready queue почти не изменился
(max `1710→1720`), analyzer остался FAIL. IO worker и чтение файла быстры;
старый хвост завершений и main-thread application всё ещё не закрыты.

По рендеру M383 также не является исправлением: `unfinished_visual` оставался
в `97.88%` samples, longest run `798`, Dirty median/max `970/1634`, visible-black
max `28`, stop convergence не достигнута. Near-void max снизился `4462→1237`,
но видимый-black max слегка вырос `23→28`; это неустойчивые одноповторные
сравнения. Экранный sample при x≈−383 показывает одновременно очередь mesh
output с 4 slots headroom, 6 запрошенных schedule slots при 4 доступных и
ремонтируемый geometry debt у front slices. В 259 ScreenRayRepair rows западнее
x=−400, 252 имели geometry debt, 231 продвигали visible geometry, но 254 были
сняты до mesh scheduling phase либо в кадре без schedule; median queue position
был 8 при queue size 944. Сам по себе этот snapshot не доказывает starvation:
FirstMesh ticket rejection часто соседствовал с уже существующей Dirty/Remesh
ownership. Следом нужно проследить одного и того же witness через несколько
кадров и установить, почему его queue owner не обслуживается.

Процедурный источник всё ещё создаёт длинные очереди: generation обычно занимает
десятки/сотни миллисекунд, но request-ready и commit queue имеют многосекундные
хвосты; на отдельных route bands p95 queue достигал 75 s. Все commits по-прежнему
ограничены одним на кадр. Это кандидат на focus/age-aware выборку с малым
wall-budget, но сперва требуется отделить время ожидания запроса от ожидания
готового результата и фактического apply.

Пользователь сообщил, что на экране полёт упёрся в дерево и остановился. В M383
есть кадр с деревом рядом с камерой и крупными незаполненными областями рендера;
записанная камера затем достигла x=−588. Report не содержит collision summary,
а M383 trace не дал подтверждённой записи о blocked substeps/flight-ground
contact в доступной сводке. Это расхождение не следует списывать на ошибку
наблюдения: следующий такой же M335 должен сохранять последние GUI frames и
сопоставлять движение/blocked counters с frame timestamp. В `5b185435` flight
planner уже повторно планирует после фактически заблокированного substep или
ground contact, меняет предпочитаемую сторону и расширяет отступы после
неудачной попытки; M383 эту ветку не подтвердил как сработавшую.

**G3 выполнена частично:** M385 подтвердил, что повторный запрос теперь доходит до
scheduler: у 801 из 2 705 procedural commits `priority_refresh_n > 0`, у 729
priority изменился до старта генерации, а у 294 — после старта. Проверка token до
смены состояния также включена. Одного повтора недостаточно, чтобы приписать
изменению улучшение очереди или рендеринга; lifecycle грязных mesh witnesses
остаётся открытым.

### Результат M384 — detour подтверждён, priority refresh не доходил до scheduler

M384 использовал чистую Release-сборку `7e1f0607` и неизменный M335: видимый
no-teleport пролёт по `World_164`, seed `3650471197`, старт `[120,56,56]`, eye y
`70`, yaw `180°`, pitch `−30°`, scale `1`, 2 800 s полёта и 20 s settle. Процесс
штатно завершился (`rc=0`), прошёл 9 824 блока до focus `−607` и пересёк
checkpoint 8 192.

Renderer acceptance не улучшился: `unfinished_visual` — `97.96%` samples,
longest run `809`, Dirty median/max `715/1 346`, wall median `106.47 ms`,
near-void max `1 553`, visible-black max `46`, Red pressure `100%`, stop
convergence false. Относительно M383 стали ниже Dirty и wall, но holes остались
на том же уровне, а near-void и visible-black ухудшились. Это неустойчивое
смешанное изменение на одно повторение; исправления renderer нет.

Процедурные `WorldColumnSource` commits: `2 826`; request queue p50/p95/max
`25.5 ms / 35.96 s / 62.25 s`, generation p50/p95 `95.5/180.5 ms`, ready wait
p95 `9.06 s`, apply p95 `9.79 ms`, source-to-apply p95 `43.63 s`. У M383
соответствующие overall queue p50/p95/max были `28.4 ms / 33.33 s / 88.58 s`;
из-за одного повтора и отсутствия priority updates в telemetry изменение нельзя
считать эффектом scheduler patch.

Flight control подтвердил работу обхода: `1` hazard, `1` started/completed
right-side detour, side offset `3`, pass distance `7.25`, plan failures `0`;
blocked substeps и ground contacts — `0`. Значит, обход препятствия теперь
срабатывает в контрольном M335, не изменяя исходные параметры съёмки.

### Результат M385 — приоритет обновляется; очередь готовых commit ограничена одним на кадр

M385 использовал чистую Release-сборку `613296e6` с тем же видимым no-teleport
M335 на `World_164`, seed `3650471197`: старт `[120,56,56]`, eye y `70`, yaw
`180°`, pitch `−30°`, scale 1, 2 800 s полёта и 20 s settle. Процесс завершился
штатно (`rc=0`), focus прошёл `7→−591`, дистанция составила 9 568 блоков,
checkpoint 8 192 пересечён. В этом повторе обход препятствия не понадобился.

Acceptance рендеринга по-прежнему FAIL: `unfinished_visual` ненулевой в 98.0%
периодов (медиана 26, максимум 100), Dirty median/max `596/1 459`, wall median
`105.61 ms`, near-void max `4 285`, visible-black max `28`, Red pressure `100%`,
stop convergence false. `unfinished_visual` — широкий census
`column_loaded_no_mesh_n`, а не доля пиксельных дыр: прямые `near_focus_holes` и
`visual_holes` имели median 0, максимум 1 и были ненулевы в 16.8% периодов;
`focus_missing_mesh` имел median 0 и максимум 1. Эти показатели следует
разделять в оценке дальнейших прогонов.

На 2 705 procedural commits request queue p50/p95/max составил
`27 ms / 33.83 s / 80.85 s`, generation `91.9/128/305 ms`, ready wait
`289 ms / 8.74 s / 65.05 s`, apply `5.38/8.14/34.22 ms`, полный request-to-apply
`448 ms / 42.16/95.82 s`. У всех записей `max_commits_per_frame=1`, хотя
`ready_batch_n` имел p50 3, p95 32 и максимум 55. Это измеряет ограничение слива
готовых результатов; ослаблять его можно только с ограничением суммарной
стоимости `ApplyTo + MarkDirty` на кадр. Priority refresh был активен: 801 commit
имел ненулевой refresh counter, у 729 priority изменился до старта генерации,
у 294 — после старта. Queue p95 оказался ниже M384 на ~2.1 s, но один повтор не
доказывает причинность.

M385 смешивает disk и procedural path: 2 291 disk requests, 1 845 disk
completions, 2 710 disk misses и 2 705 procedural commits. Это не сопоставленный
reload-тест и не объясняет затемнённые области. Среди 21 200 pixel probes было
104 тёмных попадания; все имели depth и видимый MDI, большинство source-face
совпадений относилось к листве с sampled sky light `1`. Это указывает на
опубликованную геометрию в sampled pixels, но не позволяет считать большие
затемнённые пятна нормальными и не исключает lighting/revision проблему.

GPU mesh slots заполнялись до лимита 2 048: mid/late-flight bound median
`2 045/2 046`, свободных слотов было `2/1`; к концу накопительный eviction
counter достиг `2 427`. `gpu_mesh_slot_no_victim_n` оставался нулевым: allocator
находил mesh вне guarded draw radius, поэтому один этот факт не объясняет holes.
`mesh_async` median 12 (max 16), pending GPU median 10, pipeline schedule-skip
median 0/p90 7 — это признаки backpressure; их следует коррелировать с focus
witnesses, а не лечить простым увеличением slot capacity.

**Следующий контрольный шаг G3:** оставить M335 неизменным и испытать
time-budgeted drain уже готовых procedural results: при наличии backlog разрешать
до трёх приоритетных commits за кадр с целевым суммарным main-thread временем
`ApplyTo + MarkDirty` в 12 ms; перед следующим commit остановиться, если цель
достигнута. Один синхронный commit не прерывается и может превысить цель; фактическое
время нужно оценивать по `commit_apply_ms`. Без ready backlog текущий лимит
сохраняется.
Записывать `ready_batch_n`, `ready_wait_ms`, фактические commits/apply ms на кадр,
wall/stream time, near-focus holes и mesh completion. Это проверяет ограничение
`max_commits_per_frame=1` из M385 и удерживает стоимость кадра; GPU slot capacity
в этой итерации не менять. После изменения сделать минимум три одинаковых M335
перед выводом о причинном результате. Новые миры остаются периодической проверкой
переноса, не заменой `World_164`.

Точная команда M385:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'; $env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'; $env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m385_world164_m335_priority_refresh_live'; python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m385_world164_m335_priority_refresh_live --report bin/suite_reports/engine_refactor/m385_world164_m335_priority_refresh_live_20261004.json --process-timeout 3000
```

Artifacts: [M385 analyzer report](../../bin/suite_reports/engine_refactor/m385_world164_m335_priority_refresh_live_20261004.json),
[pixel analysis](../../bin/suite_reports/engine_refactor/m385_renderer_pixel_trace_20261004.json),
[perf trace](../../bin/logs/perf_20261004-122227_40432.jsonl),
[source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-122223.40432),
[GUI frames](../../bin/logs/m385_world164_m335_priority_refresh_live).

### Результат M386 — ready drain не включился; pixel trace видит unloaded ray witnesses

M386 повторил тот же видимый Release/no-teleport M335 на `World_164`, seed
`3650471197`: старт `[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`, scale
`1`, 2 800 s fly + 20 s settle. Сборка `74be842f` завершилась штатно, камера
прошла 9 920 блоков от focus x `7` до `−613`, пересекла checkpoint 8 192 и не
остановилась на коллизии. Детур в этом круге не потребовался.

M386 не проверил запланированный drain: несмотря на `ready_batch_n` p50/p95/max
`3/41/57`, все 2 814 commits по-прежнему записали
`max_commits_per_frame=1` и `max_apply_budget_ms=0`. Причина — guard зависел от
`GetCompletedReadyCount()` до `Tick()`, а между этим snapshot и
`Completed.DrainAll()` результаты могли перейти в ready. Следующая версия
разрешает лимит до трёх только во время быстрого движения и при общем backlog
запросов/worker/ready; scheduler сам применяет не более трёх и останавливается
перед следующим результатом после целевых 12 ms суммарного `ApplyTo + MarkDirty`.
Один синхронный apply остаётся непрерываемым и может превысить цель. M387 должен
подтвердить `max_apply_budget_ms=12` и фактические commits на кадр; до этой
проверки M386 считается невалидным измерением эффекта drain.

Процедурные source commits: 2 814; очередь p50/p95/max
`37.2 ms / 38.0 s / 74.6 s`, генерация `99/197/733 ms`, ожидание ready
`338 ms / 10.59 s / 62.61 s`, apply `5.36/11.34/81.73 ms`, полный
request-to-apply `633 ms / 48.61/88.96 s`. Было 2 268 disk queued,
1 767 disk complete и 2 857 disk miss. Большой ready wait остаётся основной
измеренной задержкой источника; увеличение GPU slot capacity не является
обоснованным следующим шагом.

Renderer acceptance остался FAIL: `unfinished_visual` медиана 25, longest run
801, Dirty median/max `229/1 212`, wall median `103.07 ms` (на пролёте
`102.57 ms`), stream phase median `47.69 ms`, Red pressure `100%`, near-void max
`5 854`, visible-black max `31`, `chunk_not_ready` median 25, stop convergence
false. Прямые near-focus holes обычно были нулевыми, однако 221 период из 1 377
имел ненулевой near-focus hole census. В срезе x≈−7 465 соответствующий
camera-band trace одновременно показывал 13 drawable-missing residents и 12
opaque drawables; near-focus hole gate этого не отражал. Это подтверждает
недостаточность focus-centered gate, но само по себе не называет виновный этап
mesh lifecycle.

Из 720 pixel probes в узких окнах вокруг x≈−3 898, −4 663, −7 465 и −8 000
видимый RGB не был чисто чёрным. CPU voxel ray сообщал неизвестный unloaded
chunk перед opaque hit в 160/160, 240/240, 80/160 и 72/160 выбранных пикселей
соответственно. Эти лучи включают небо и могут продолжаться за фактической
поверхностью; результат доказывает наличие unloaded участков на части sampled
лучей, но не является pixel-level доказательством, что именно они создают
видимые пустоты. Требуется связать ray distance, framebuffer depth, nearest
opaque surface, draw residency и sample position в одном пикселе.

M335 параметры не менять. Сначала проверить M387 фактический ограниченный drain
и collision replan на том же профиле; сравнить source ready-wait, commit apply,
frame wall, missing-resident slice, pixel/ray witness и post-stop convergence.
Затем повторить изменённую версию ещё дважды до вывода о влиянии. Случайные
новые seed остаются отдельной периодической проверкой переноса.

Команда M386:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'; $env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'; $env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m386_world164_m335_apply_budget'; python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m386_world164_m335_apply_budget --report bin/suite_reports/engine_refactor/m386_world164_m335_apply_budget_20261004.json --process-timeout 3000
```

Артефакты: [M386 report](../../bin/suite_reports/engine_refactor/m386_world164_m335_apply_budget_20261004.json),
[perf trace](../../bin/logs/perf_20261004-134106_19096.jsonl),
[source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-134102.19096),
[GUI frames](../../bin/logs/m386_world164_m335_apply_budget).

### Результат M387 — fast-flight backlog guard всё ещё пропускает ready batch

M387 использовал чистый Release `64d09ce3` и те же visible/no-teleport M335
условия на `World_164`: seed `3650471197`, старт `[120,56,56]`, eye y `70`, yaw
`180°`, pitch `−30°`, scale `1`, 2 800 s fly + 20 s settle. Полёт завершился
нормально: speed median `5.193` блока/с, focus `7→−626`, пройдено 10 128 блоков,
checkpoint `8 192` пересечён. Flight analyzer получил `process_rc=0`, но
renderer gates остались FAIL. Пролёт не встретил hazard: `attempts=0`,
detours/replans `0`, `collision_stop_triggered=false`. Тем самым новый recovery
для уже заблокированного обхода в M387 не проверялся; старые M380/M384 по-прежнему
являются подтверждением обычного обхода.

Из 2 946 procedural commits у 2 051 `ready_batch_n>1`; p50/p95/max batch был
`3/31/59`. Однако все 2 946 событий записали
`max_commits_per_frame=1`, `max_apply_budget_ms=0`. Даже после замены готового
счётчика на общий `gen_backlog_total` snapshot остаётся слишком ранним/неполным:
worker results успевают накопиться к моменту `Completed.DrainAll()`. M387 не
проверил batching patch и устанавливает место для следующего исправления:
передавать scheduler признак fast-flight, а максимальный drain до 3 результатов
и целевые 12 ms включать внутри `UChunkLoadScheduler::Tick()` после
`Completed.DrainAll()`, только когда локальный `ready.size()>1`. Так лимит
определяется точным batch этого кадра и не увеличивает работу, когда результат
один. Один синхронный `ApplyTo + MarkDirty` может превысить target.

Source timings: queue p50/p95/max `34.6 ms / 31.86 s / 66.76 s`, generation
`93/150/370 ms`, ready wait `288 ms / 9.07 s / 59.25 s`, apply
`5.46/10.54/41.30 ms`, total `506 ms / 39.33/80.72 s`. Red pressure был `100%`,
unfinished visual — `97.96%` периодов (longest run `804`, это широкий
loaded-column/no-mesh census, не процент пустых экранных пикселей), Dirty
median/max `430/1 086`, wall median `93.39 ms` (`92.81 ms` на пролёте),
`chunk_not_ready` median `26`, near-void max `4 217`, visible-black max `31`.
Прямой near-focus holes census был ненулевой в 222/1 377 periods, но лишь в двух
periods visual corridor. Stop convergence не прошёл: pending/light/dirty debts
сохранились после settle. Сравнение M386/M387 не причинное: реальная очередь
saved/procedural колонок изменилась между повторениями.

Кадры 148 и 167 показывают наблюдаемые тёмные угловатые участки/резкие границы.
В sample `frame_epoch=30840`, camera x `−9 359`, trace имел только 4 scanlines
на 20 колонок. Две scanlines, попавшие на поверхность, показывали drawable/MDI
геометрию, opaque hit block `549`, sky light примерно `0.53–0.87` и не-чёрные
RGB; две остальные были depth 1 / sky. В PNG тёмные участки лежат между этими
scanlines, поэтому этот pixel join не попал в сам дефект и не позволяет объявить
его ни корректно затенённой водой, ни пустым chunk. Для M388 включить штатный
диагностический `CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS=1` (8 рядов вместо 4),
сохранив M335, мир, seed, скорость и освещение. Это меняет плотность диагностики,
а не условия пролёта. Сопоставить ближайшие pixel/depth/ray данные для тёмных
пятен с draw residency, source block, light value/revision и mesh demand.

Artifacts: [M387 analyzer report](../../bin/suite_reports/engine_refactor/m387_world164_m335_backlog_drain_20261004.json),
[flight report](../../bin/flight_sim_report.json),
[perf trace](../../bin/logs/perf_20261004-144425_7548.jsonl),
[source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-144420.7548),
[GUI frames](../../bin/logs/m387_world164_m335_backlog_drain).

### Результат M388 — пиксель попал в поверхность; scheduler gate не совпал с порогом M335

M388 собран и запущен на Release `0978a7a5`, с тем же visible/no-teleport M335
на `World_164`: старт `[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`,
scale `1`, 2 800 s fly + 20 s settle. Изменена только плотность диагностики:
`CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS=1` (8×20 вместо 4×20 probes), сохранены
source trace и 189 кадров GUI. Полёт завершился с `process_rc=0`, прошёл 9 104
блока (`focus 7→−562`), median speed `5.18555` блока/с, checkpoint `8 192`
пересечён. `collision_stop_triggered=false`; obstacle attempts/detours/replans
все равны нулю. `world_data.json` восстановлен byte-for-byte, исходный SHA-256
`0ade4041…`.

Результат не удовлетворяет render gates: analyzer `pass=false`, 1 364 periods,
5 049 spikes, wall median flying `109.825 ms` (`9.11 FPS`), stream `23.22 ms`,
mesh emerge `27.10 ms`, `unfinished_visual=97.65%` периодов (широкий census,
не экранная доля дыр), visible-black max `34`, near-void max `7 317`, `unlit`
max `41`, stop convergence false. В M388 реально применилось не более одного
column commit за кадр: `stream_gen_commit_n` равен 1 в 255/1 364 periods, 0 в
остальных и никогда не превышает 1. Из 2 402 procedural commit events у 1 486
`ready_batch_n>1` (max 40), однако все события по-прежнему записали cap 1 и
budget 0 ms. Причина найдена в настройке именно этого мира: `MovementSpeedBoostThreshold=6.0`,
тогда как контрольный M335 держит median `5.18555`; поэтому fast-flight bool
ложен. `MovementPrefetchThreshold=1.5` уже распознаёт это перемещение. Drain
переведён на этот существующий сигнал движения; сам маршрут и скорость не меняются.

Плотный trace записал 32 768 экранных samples; 165 имели luma `<32` (это доля
выбранных точек, не screen-pixel hole rate). У всех 165 был валидный depth surface,
drawable chunk и видимая opaque MDI команда; у 148 source-face witness совпал с
поверхностью в пределах 0.1. Все dark sample block-light значения были 0; у 100
sky-light был 1, у остальных 0/1. 38 samples помечены `light_preview=1`, и у
47 demand ещё не считался settled. Настройки shader показывали day factor 1,
ambient 0.12 и sky scale 0.972. Это свидетельствует, что sampled тёмные места
содержат отрисованную геометрию с низким/предварительным освещением; само по себе
не объясняет все видимые полигоны и не является прямым pixel-area измерением.

Для одного совпавшего с тёмной областью места (`camera x=−8 347`, depth chunk
`(−522,4,3)`, column `(−522,0,3)`) persistence trace зафиксировал `disk_miss`,
а затем procedural commit через `24.63 s`: очередь `24.50 s`, генерация `84 ms`,
ready batch 6, apply `5.62 ms`. Значит, для этого свидетельства колонка была
создана заново, а не прочитана с диска. Соседние sampled columns `(−353,0,2/3)`
тоже дали disk miss перед генерацией. M389 повторил тот же маршрут, но
`(-522,0,3)` снова оказался disk miss; данный run не устанавливает поведение
тёплой disk загрузки. Автосохранение в flight harness выключено, а выгрузка
каждой целевой колонки до её повторного посещения не подтверждена. Сохранение и
повторное чтение проверять только при наличии column file и явного `disk_hit`.

Artifacts: [M388 analyzer report](../../bin/suite_reports/engine_refactor/m388_world164_m335_scheduler_drain_20261004.json),
[pixel/light join](../../bin/suite_reports/engine_refactor/m388_renderer_pixel_trace_20261004.json),
[perf trace](../../bin/logs/perf_20261004-154622_31036.jsonl),
[source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-154618.31036),
[GUI frames](../../bin/logs/m388_world164_m335_scheduler_drain).

M389 проверил активацию ready drain и pixel/demand light revisions; результаты
приведены ниже. Для отображения использовать также offline luma thresholds 64 и
96, поскольку `<32` измеряет near-black и пропускает большую часть приглушённых
поверхностей. Следующая итерация G3 должна связать видимые dark samples на этом
широком диапазоне яркости с материалом, light field revision, mesh publication
revision и точной колонкой; условия M335 остаются фиксированными.

### Результат M389 — batching сработал, но warm disk повтора не было

M389 повторил M335 без изменения старта, камеры, скорости и daylight: visible,
no-teleport `World_164`, `[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`,
2 800 s fly + 20 s settle. Пройдено 9 168 блоков (`focus 7→−566`, median
`5.18555 blocks/s`), checkpoint `8 192`, `process_rc=0`. Scheduler применил
cap до трёх колонок и budget `12 ms` к 1 542 multi-ready records; telemetry
зафиксировала 98 multi-commit periods с максимумом 3.

Результат по графике смешанный, acceptance остаётся `FAIL`: flying wall median
`104.60 ms`, stream `26.28 ms`, mesh emerge `29.16 ms`, visible-black max `20`,
near-void max `3 032`, unlit max `45`, post-stop convergence false. Плотный
pixel trace содержит 115/32 768 samples с luma `<32` (M388: 165), 8 provisional
light samples и 2 unsettled demand. Большинство dark probes имеют валидную
drawn surface и settled light revision, поэтому они не доказывают пустой или
сломанный chunk. `unfinished_visual=97.51%` — широкий loaded-column/no-mesh
census, не процент экрана. В отчёте dominant schedule blocker=`empty_fm_queue`,
deferred far relight pending max=`678`, FIFO dropped delta=`1 313`; приоритет
следующего кода — проверить owner/retention visible relight debt до mesh/GPU
publication и трассировать выбранные screen pixels до material/light source.

Порог `<32` недооценивает приглушённые поверхности. Offline пересчёт тех же
M388/M389 dense traces на `<96` дал `1 112→1 032` samples; provisional-light
`219→177`, но unsettled-demand `257→281`. На `<64`: `700→657`, provisional
`147→131`, unsettled `173→193`. Это одни и те же сохранённые пробы без смены
камеры, но не строгий A/B из-за различий worker timing/cache. В M389 `<96`
пробы `tree_leaves` 504, `sand` 244, `grass` 96, `tree_log` 77; 973 имели
depth surface, 966 — видимую opaque MDI команду. Следовательно, набор содержит
как нормальные тёмные материалы, так и видимые provisional/unsettled поверхности.
См. [offline luma-анализ M389/M388](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md#M389-follow-up-wider-luma-thresholds-capture-dim-surfaces).

M389 не проверил тёплый disk path, несмотря на имя `persisted_drain`: точная
колонка `(-522,0,3)` снова дала `disk_miss` и procedural commit через
`247.66 ms`; flight-sim отключает periodic autosave, а wrapper восстанавливает
`world_data.json`. Перепроверить warm persistence только после подтверждения
наличия сохранённого column file и явного `disk_hit`; unload-save paths пока
не считать неисправными или исправленными. Ускорить/перенаправлять M335 ради
этого не нужно.

Оператор также сообщил, что видел столкновение с деревом и остановку. Flight
report M389 показывает нулевые obstacle attempts и `collision_stop=false`;
связь наблюдения именно с этим run не установлена. Harness уже имеет forward
hazard sweep, двусторонний bounded detour и replan после blocked movement или
ground contact. Следующий M335 run должен сохранять кадры и collision counters
одного process/session; менять для проверки маршрутные условия нельзя.

Подробные метрики и артефакты: [аудит M389](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md#M389-bounded-drain-активен-повторы-дальних-колонок-всё-еще-cold).

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

### M390 completed — M335 exposes stale generation work and distant visual debt

M390 completed the same visible Release/no-teleport route: World_164, start
`[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`, fixed clear day, scale 1,
2 800 s flight plus 20 s settle. It covered 8 560 blocks to focus x `−528`;
median flight speed was `5.18555 blocks/s`. The obstacle detour was enabled, but
no obstacle was encountered (`attempts=0`, blocked substeps `0`, ground contacts
`0`).

This run is diagnostic and fails renderer acceptance. The suite report records
flight wall median `109.06 ms`, renderer median `60.06 ms`, dirty median `723`
(max `1 313`), visible-black max `21`, void-near max `1 228`, and unlit max `41`.
Its `unfinished_visual` rate is a loaded-column/mesh-readiness census, not a
screen-area measurement. The distant route must still be treated as a real
streaming/rendering stress case: the report has 210 periods with near-focus
holes somewhere along the route, and post-stop convergence did not clear all
stalled visible-black debt.

Source tracing identifies cold generation plus a long tail of stale requests.
There were 1 706 procedural disk misses. Across the 6 000–8 000 block route
band, procedural generation itself had median `108.6 ms` (`p95 152.9 ms`), while
request queue wait had median `222.8 ms`, `p95 34.9 s`, and max `60.2 s`; total
miss-to-commit reached `60.7 s`. Several requests were first admitted near the
camera, then their priority aged from `−435` to `400`, and they were committed
when their columns were 47 chunks behind the final focus. The scheduler retained
these queued/generating/ready owners after they left the streamer keep ring.
Those late commits add obsolete terrain and first-mesh work while the cold
frontier is still being filled.

The source now has a bounded cancellation pass for procedural requests outside
`max(visual render distance, keep distance + unload margin)`. It invalidates the
generation token so workers can stop cooperatively and drops late ready results.
The matching M335 Release rerun is the validation gate; compare stale-cancel
events, queue/commit latency, focus missing meshes, dirty debt, and post-stop
convergence. Camera, route, speed, and time-of-day remain unchanged.

Pixel evidence separates genuine dark samples from fog. M390 retained RD 4,
distance fog start ratio `0.48`, density `0.85`, and end margin `28`: fog reaches
full blend at 36 blocks and begins at about 17.3 blocks. The uniform blue patches
match the configured fog color `(13,38,89)` exactly. Of 32 768 sampled pixels,
119 were below luma 32; all 119 had settled demand and visible opaque MDI, with
3 preview markers and 9 voxel-pending witnesses. At luma `<96`, 994 samples were
selected; 900 had settled demand, 94 unsettled, 110 preview markers, and 153
voxel-pending witnesses. These are sampled pixels, not screen-area rates. Depth
and voxel-ray witnesses often refer to different surfaces, so retain per-pixel
joins before assigning a dark patch to light, transparency, fog, or geometry.

The prior M389 transparent-composition result remains: 306/1 032 `<96` samples
changed RGB during transparent composition; the pending underwater sand sample
at y=46 is below sea level 48 and has visible MDI geometry. This does not make
transparency the cause of every dark sample.

Artifacts: [M390 analyzer report](../../bin/suite_reports/engine_refactor/m390_world164_m335_relight_owners_20261004.json),
[pixel trace luma 32](../../bin/suite_reports/engine_refactor/m390_renderer_pixel_trace_l32_20261004.json),
[luma 64](../../bin/suite_reports/engine_refactor/m390_renderer_pixel_trace_l64_20261004.json),
[luma 96](../../bin/suite_reports/engine_refactor/m390_renderer_pixel_trace_l96_20261004.json),
[perf trace](../../bin/logs/perf_20261004-182722_29104.jsonl),
[GUI frames](../../bin/logs/m390_world164_m335_relight_owners).

### M391 completed — disk-load owners also outlive the moving retention ring

M391 repeated the identical visible Release/no-teleport M335 conditions on
World_164: start `[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`, fixed clear
day, scale 1, `2 800 s` flight plus `20 s` settle. It reached focus x `−543`
(`8 800` blocks) at median `5.18555 blocks/s`. Collision and ground-contact
counters remained zero; the flight harness's default predictive avoidance was
enabled, but no detour was needed. The harness restored `world_data.json`
byte-for-byte. The same profile stays the acceptance condition.

Renderer acceptance remains red: `1 363` periods, fly wall median `119.46 ms`,
dirty median/max `543/1 289`, visible-black max `53`, void-near max `9 362`, and
post-stop recovery false. `unfinished_visual` remains a loaded-column/mesh
readiness census, not a screen-area rate. Relative to M390 this is not a clean
A/B: M391 encountered a different persisted chunk-file population after prior
flights.

M391's source trace recorded `2 209` unique disk-column requests and `1 779`
matching `complete` outcomes. The remaining `430` queued coordinates had no
`complete` event in the log; by the final focus every such coordinate was
188–379 chunks behind it, far outside the five-chunk retention radius. The log
does not expose the final in-memory pending map, so treat this as a stale-work
signature rather than a direct count of live owners. For disk completions in
the 4 000–6 000 block band, per-column elapsed time had median `25.1 s`, p95
`619.8 s`, max `789.7 s`; aggregated `result_wait_ms` had median `102.1 s`, p95
`2 479 s`. Worker queue p95 was `0.68 ms`, and file-read p95 `1.74 ms`. This
points to delayed admission/application of completed slices, not slow storage
reads. The earlier unload callback only erased the owner map entry; it did not
cancel worker reads or remove already completed results.

Commit `47b9ab04` adds cancellation flags for disk-column owners, checks them in
the I/O worker, removes canceled completions from the ready queue, invalidates
generation tokens, and cancels owners outside the existing streamer retention
radius. `M392` is validating this change on the unchanged M335 route. Compare
`disk/cancelled_out_of_range`, outstanding ready work, disk result wait, focus
missing/dark witnesses, frame wall, and stop convergence before changing the
near-focus apply budget.

Pixel traces still do not assign one cause to every dim patch. At luma `<32`,
M391 recorded `137/32 768` selected samples: all had visible opaque MDI geometry,
`105` had settled demand, `7` carried a preview marker, and `51` had a pending
voxel-light witness. At luma `<96`, `954` samples included `716` settled demand,
`166` preview markers, `316` pending voxel-light witnesses, and `186` whose RGB
changed during transparent composition. Most voxel-ray and depth witnesses
refer to different surfaces. A GUI frame near x `−8 186` also shows sharp blue
triangular patches across a sand/water shoreline; the current sparse probe grid
does not localize those triangles to a responsible layer. Keep them as a
separate renderer symptom, and correlate exact screen pixels with opaque and
transparent depth/source witnesses on the same M335 pass.

Artifacts: [M391 analyzer report](../../bin/suite_reports/engine_refactor/m391_world164_m335_cancel_stale_loads_20261004.json),
[pixel trace luma 96](../../bin/suite_reports/engine_refactor/m391_renderer_pixel_trace_l96_20261004.json),
[perf trace](../../bin/logs/perf_20261004-193054_24660.jsonl),
[source logs](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-193050.24660),
[GUI frames](../../bin/logs/m391_world164_m335_cancel_stale_loads).

### M392 completed — stale disk owners are canceled, but result application still falls behind

M392 used the same visible Release M335 on `World_164`: start `[120,56,56]`,
eye y `70`, yaw `180°`, pitch `−30°`, fixed clear day, movement scale 1,
no teleport, `2 800 s` flight plus `20 s` settle. It reached the `8 192`-block
checkpoint and covered `9 968` blocks at median `5.19287 blocks/s`. The route
manifest passed, process return code was zero, and the harness restored both
`users.json` and `world_data.json` byte-for-byte. No obstacle bypass or collision
stop was recorded. The analyzer still fails, so M335 remains the acceptance
route without parameter changes.

The cancellation change in `47b9ab04` accounts for all queued disk coordinates:
`2 292` unique requests produced `1 859` complete outcomes and `433`
`cancelled_out_of_range` outcomes, with zero queued coordinates lacking a
terminal trace event. Worker queue p95 was `1.482 ms` and file-read p95
`1.834 ms`; completed results still waited a median `2.51 s`, p95 `100.84 s`,
and max `175.06 s`. The ready-load queue had median `16`, p95 `180`, and max
`458` slices while the I/O workers were idle. Per-column deserialize/apply was
median `7.48 ms` across its slices. This isolates the remaining disk path to
main-thread result selection/application, not file-read time or abandoned
out-of-range owners.

Procedural work also has queue-tail pressure: `2 919` disk misses produced
`2 870` commits and `22` out-of-range cancellations. Generation itself was
median `94.93 ms` and p95 `132.90 ms`, while time waiting before a worker began
was median `35.68 ms`, p95 `25.14 s`, max `42.81 s`. Ready-result wait was
median `61.61 ms`, p95 `447 ms`. Keep producer-queue delay separate from the
worker generation cost when tuning admission.

The flight remained under Red streaming pressure for the whole route. There
were `1 372` periods, median fly wall `91.99 ms` (`10.87 FPS`), dirty median/max
`835/1 484`, and world-streaming phase median `49.52 ms`. The dominant wall
stage was streaming; scheduling most often reported `empty_fm_queue`, with
completion stall `gpu_not_ready`. `unfinished_visual` and `void_near` are
readiness censuses, not screen-area measurements. Post-stop convergence failed:
pending work and focus-dirty debt did not fall, and near-focus work remained.

Pixel analysis collected `32 768` probes: `129` below luma 32, `658` below 64,
and `1 076` below 96. M391 had `137/608/954` at the same thresholds; these are
not strict A/B results because persisted chunk populations differ. Of M392's
129 lowest-luma samples, every sample hit drawable opaque MDI geometry and none
changed through transparent composition; `91` had a mesh revision newer than
the published geometry revision and `14` had a provisional-light marker. One
blue sample (`RGB 13,38,89`) had `sky=0`, provisional preview, and geometry
revision `2` versus published `1`. Across all `<96` samples, transparent
composition changed `256` pixels (M391: `186`), while stale geometry samples
rose from `642` to `741`. This supports both stale publication/light debt and a
separate transparent/shoreline symptom; it does not prove one cause for every
dim region.

Next implementation step: service a small, explicitly time-bounded slice of
completed disk results even when the near-streaming budget is exhausted. Keep
normal per-frame limits when the budget is available; measure whether a
one-slice reserve lowers ready-result age without worsening frame wall. Repeat
the exact M335 flight, then correlate screen probes, mesh publication revisions,
light preview, disk/procedural source, and post-stop convergence before changing
the renderer's preview policy.

Artifacts: [M392 flight report](../../bin/suite_reports/engine_refactor/m392_world164_m335_cancel_stale_disk_20261004.json),
[pixel trace luma 96](../../bin/suite_reports/engine_refactor/m392_renderer_pixel_trace_l96_20261004.json),
[world-column source trace](../../bin/suite_reports/engine_refactor/m392_world_column_source_trace_20261004.json),
[perf trace](../../bin/logs/perf_20261004-205500_34968.jsonl),
[GUI frames](../../bin/logs/m392_world164_m335_cancel_stale_loads/frame_127.png,
../../bin/logs/m392_world164_m335_cancel_stale_loads/frame_145.png).

### M393 completed — disk-result latency falls, but renderer acceptance regresses

M393 validated commit `e186c63c` with the unchanged visible Release M335 on
`World_164`: start `[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`, fixed
clear day, movement scale 1, no teleport, `2 800 s` flight plus `20 s` settle.
The manifest and route passed, the app returned code 0 without a hang, and the
harness restored the world data with SHA-256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`. It crossed
the `8 192`-block checkpoint and covered `9 312` blocks at median
`5.18555 blocks/s`; camera height stayed 70 and z stayed 56. There were no
blocked movement substeps, ground contacts, collision stop, or detour. The
runner itself exits 1 because renderer stop-lines fail; this is not an app
crash. No route, camera, speed, or filming parameter changed.

The reserved disk drain improved its target signal. All `2 556` queued disk
coordinates completed with no unmatched requests. Completed-result wait fell
from M392 median/p95 `2.51/100.84 s` to `1.92/48.21 s`; ready-load queue p95
fell `180→104` slices, while the maximum remained `458` and one wait outlier
rose to `194.07 s`. File read p95 was `3.30 ms`; deserialize/apply median/p95
was `7.16/12.77 ms`. This confirms a main-thread result-consumption backlog,
not slow file reads, but leaves a long tail.

Procedural work remains delayed before generation starts: `2 670` disk misses,
`2 613` commits, and `2` out-of-range cancellations. Request-to-worker-start
wait was median `28.66 ms`, p95 `25.44 s`, max `47.38 s`; actual generation was
median/p95 `91.91/121.60 ms`, followed by ready wait `68.73/280.75 ms` and
apply `5.32 ms` median. `queue_ms` measures request-to-worker-start, so this
large tail is admission/scheduler or worker-queue wait, not generator CPU time.
The generation pool has four workers and starts are additionally limited by the
frame's `MaxLoadOps` budget. A coordinate join near x `−7 355` found
`disk_miss` for columns `(−459,0,3)` and `(−460,0,3)`, then commits after
`20.53 s` and `14.31 s` queue waits with generation near `102–103 ms`. Pixel
and source events are coordinate-correlated; the current logs do not prove an
exact same-frame ordering.

Overall rendering did not pass and was slower than M392: `1 372` steady
periods, fly wall median `110.93 ms` (M392 `91.99 ms`), world-streaming phase
median `52.25 ms` (M392 `49.52 ms`), Red pressure rate `69.1%` (M392 `100%`),
dirty median/max `556/832` (M392 `835/1 484`), and `unfinished_visual` median
`27` in both runs. The dominant wall stage remains streaming; the classified
schedule blocker on spikes changed from `empty_fm_queue` to `zero_fm_cap`, and
completion stalls remain `gpu_not_ready`. Stop convergence still fails, though
post-stop missing max improved `35→18`. The route report has `22/39` gates
passing, same as M392.

The dense pixel trace is a regression signal under identical route conditions:
of `32 768` probes, `<32/<64/<96` luma counts rose from M392
`129/658/1 076` to `268/1 025/1 590`. Of M393's `<32` samples, all `268` hit
drawable opaque MDI geometry, none changed through transparent composition,
`155` had mesh revision newer than published geometry, and `31` carried a
preview-light marker. Across `<96`, `1 559/1 590` hit drawable opaque MDI,
`935` were newer than published geometry, `277` had preview light, and `157`
changed during transparent composition (M392: `1 027/1 076`, `741`, `166`,
and `256`). This shifts attention toward stale GPU publication and preview-light
debt; transparent composition is a secondary, still separate symptom.

The mesh/output path needs the next investigation: M393's `pending_gpu_queued_n`
was median/p95 `9/12`, `gpu_finish_not_ready` owns the dominant completion
stall, and the output pool reached its cap in four periods. Across all periods,
first-mesh schedule cap was median/p95 `6/12` and dirty FirstMesh queue
`12/137`; zero FM cap was not a whole-flight condition, but it dominated the
classified spike set. The renderer spends median `61.96 ms` per fly period
while the world-streaming phase spends `52.25 ms`, so optimization must respect
both producer and GPU consumer budgets.

**Updated next steps:**

1. Trace the same stale pixel through CPU mesh revision, queued/kicked GPU apply,
   output-pool capacity, fence readiness, and published revision; distinguish
   a delayed producer from a GPU consumer that cannot publish on time.
2. Review the final admission/backpressure decision for preserving current
   visible FirstMesh work under output-pool pressure. Do not simply raise
   generation or mesh quotas: M393 shows both main-thread wall cost and GPU
   completion pressure.
3. Keep the one-slice disk-result drain only as a measured partial improvement:
   its queue-age and dirty-debt gains coexist with worse wall and pixel counts.
   Revisit its place in the shared frame deadline before expanding it.
4. Make one pipeline change, rebuild Release only, and repeat the exact M335
   route. After a stable World_164 comparison passes its gates, run the planned
   periodic fresh-world check without replacing the repeatable baseline.

Artifacts: [M393 flight report](../../bin/suite_reports/engine_refactor/m393_world164_m335_reserved_disk_apply_20261004.json),
[pixel trace luma 96](../../bin/suite_reports/engine_refactor/m393_renderer_pixel_trace_l96_20261004.json),
[world-column source trace](../../bin/suite_reports/engine_refactor/m393_world_column_source_trace_20261004.json),
[perf trace](../../bin/logs/perf_20261004-220021_37424.jsonl),
[GUI frame near x −7 000](../../bin/logs/m393_world164_m335_reserved_disk_apply/frame_128.png),
[source logs](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-220017.37424,
../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-221153.37424,
../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-223419.37424).

### M394 — профиль M335 сохранён, тест увеличения Red load cap не прошёл renderer gates

M394 проверил commit `69f9d856` на том же видимом Release/no-teleport M335.
Manifest подтверждает World_164, start `[120,56,56]`, eye `70`, yaw `180°`,
pitch `−30°`, fixed clear day, scale `1`, без телепорта; маршрут достиг
checkpoint `8 192` и прошёл `9 232` блока (`577` chunks) с медианной скоростью
`5.18555 blocks/s`. Приложение завершилось с `process_rc=0`, без timeout/hang;
runner вернул exit `1` из-за проваленных acceptance gates. Сохранённые данные
мира восстановлены побайтно (SHA-256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`).

Flight-sim зафиксировал 3 предсказанные опасности; все 3 обхода начаты и
завершены, `plan_failures=0`, `detour_replans=0`, `collision_stop_triggered=false`.
Время событий: `592.4 s`, `600.2 s`, `2 412.4 s`. M395 позднее показал, что эта
удачная последовательность не гарантирует сходимость бокового шага при другом
межкадровом перемещении. См. M395 ниже; алгоритм обхода остаётся открытой задачей.

Renderer acceptance ухудшился по числу gates: `14/39` против M393 `22/39`.
При этом несколько агрегатов разнонаправленные: dirty median/max `486/1 092`
(M393 `556/832`), visible-black max `28` (M393 `48`), но Red pressure `100%`
(M393 `69.1%`), `unfinished_visual` median `28` (M393 `27`), а fly wall
median практически не изменился: `110.39 ms` против `110.93 ms`. `holes_rate`
остался `1.0`; stop recovery не сошёлся.

Pixel trace на `32 768` probes дал `<32/<64/<96` luma
`269/1 069/1 673` (M393 `268/1 025/1 590`). Из `<32` samples все `269` попали
в drawable opaque MDI surface, `151` имели CPU mesh revision новее опубликованной
GPU geometry revision, `19` имели preview-light, ни один не менял RGB в
transparent pass. Среди `<96`: `1 628/1 673` попали в drawable opaque MDI,
`956` имели отставшую опубликованную geometry, `186` — preview-light,
`159` менялись при transparent composition. Таким образом, одни и те же
«тёмные чанки» нельзя свести к отсутствию данных или к прозрачному проходу.
В срезах остаются отдельно unsettled voxel light/demand; required next step —
связать точный surface с CPU mesh revision, GPU upload/fence и publish revision.

Disk trace: `2 556/2 556` queued columns завершились; worker queue p95 `0.175 ms`,
file read p95 `1.20 ms`, но result-wait median/p95/max `1.90/55.51/135.24 s`,
ready-load p95/max `116/458`. Это очередь применения результатов, а не медленный
диск. Procedural trace: `2 630` disk misses, `2 609` commits и `8` отмен вне
retention. Request-to-worker-start wait median/p95/max `43 ms/29.03 s/51.88 s`,
сама генерация `88/112 ms` median/p95. Длинный хвост находится перед выполнением
генератора. Увеличение Red `max_load_ops_cap` с `2` до `4` не устранило его и не
улучшило acceptance; оставлять этот quota bump как исправление оснований нет.

Pool trace также не доказывает жёсткое исчерпание MDI slot capacity: в периоде
максимального fill `gpu_mesh_slot_bound_n=146/2 048` и
`publication_oom_retain_n=0`; медианный vertex-pool fill был `0.675`. Пик
`used=cap=176.79 MB` показывает заполнение текущего выделенного arena, но не
отказ публикации: настроенный med-tier ceiling — `256 MB`. Не увеличивать слот
или memory limits без подтверждённого отказа и same-pixel причинной связи.

Координатное соединение pixel/source trace показывает один и тот же симптом
при обоих источниках данных. При камере `[-2804,70,55]` тёмные drawable pixels
на slice `(-176,4,3)` видели CPU mesh revision `5` при GPU published geometry
`4`; ground column `(-176,0,3)` был прочитан с диска (`file_read_ms=1.16`, весь
source event `466.99 ms`). Рядом колонка `(-176,0,2)` была disk miss, но
procedural request ждал `23.04 ms`, generation заняла `99.02 ms`, apply —
`5.03 ms`. На камере `[-8213,70,55]` drawable slice `(-514,3,3)` снова отставал
на одну geometry revision (`6/5`); ground column `(-514,0,3)` был создан с
`queue_ms=29.04`, `generation_ms=90.74`, `ready_wait_ms=106.39` и
`apply_ms=5.84`. Это не доказывает причину всех тёмных областей, но показывает,
что медленные disk read или terrain generation не объясняют эти конкретные
surface witnesses: источник данных уже был применён, а GPU-геометрия отставала.

**Обновлённые следующие шаги:**

1. Сохранить Red `max_load_ops_cap=2` (восстановлен commit `820af493` после
   M394); не возвращать увеличение без нового причинного замера.
2. Для колонок `(-176,0,3)` и `(-514,0,3)` проследить те же вертикальные
   surface slices от source revision через dirty admission, mesh job и geometry
   revision к GPU batch upload/fence/resident-table swap. Найти стадию, где
   образуется наблюдённый разрыв в одну revision; не менять preview-light policy
   до проверки тех же координат.
3. Для процедурных и disk очередей отдельно определить, где заявки ждут до
   worker start и почему ready disk results ждут до `55 s` p95. Не увеличивать
   число workers/load ops по одному общему `queue_ms`.
4. После одного bounded pipeline fix собрать только Release и повторить точный
   M335. Новые миры оставить периодическим переносимым исследованием после
   улучшения repeatable World_164 gates.

Artifacts: [M394 analysis report](../../bin/suite_reports/engine_refactor/m394_world164_m335_red_generation_cap_20261004.json),
[flight/obstacle report](../../bin/suite_reports/engine_refactor/m394_flight_sim_20261005.json),
[pixel trace](../../bin/suite_reports/engine_refactor/m394_renderer_pixel_trace_l96_20261005.json),
[world-column source trace](../../bin/suite_reports/engine_refactor/m394_world_column_source_trace_20261005.json),
[perf JSONL](../../bin/logs/perf_20261004-231440_30796.jsonl),
[capture directory](../../bin/logs/m394_world164_m335_red_generation_cap).

### M395 — боковой обход не вернулся на M335 линию; pixel owners не попали в compact trace

M395 использовал тот же World_164 Release/no-teleport M335: start `[120,56,56]`,
eye `70`, yaw `180°`, pitch `−30°`, fixed clear day и scale `1`. Скорость при
движении сохранилась (`median 5.19287 blocks/s`), deviation по yaw/pitch была
нулевая, процесс завершился без hang (`process_rc=0`), а `world_data.json`
восстановлен с прежним SHA-256. Но камера ушла с ожидаемой линии: focus изменился
`(7,3)→(-424,129)`, а Z позиции — `56→2065`; из 1 368 period samples только 306
остались в исходной полосе `z=3±5`. На нескольких сотнях samples X почти не
двигался, пока боковой шаг продолжал набирать Z. Это route-control регрессия,
не изменение скорости или ручной сдвиг мыши.

Обход был включён: `attempts=5`, `detours_started=5`, `detours_completed=4`,
`detour_replans=1`, `plan_failures=0`, `collision_stop_triggered=false`. В первом
событии waypoint находился всего в 3 блоках, а `MoveAside` удерживал A/D, пока
горизонтальная дистанция не становилась меньше `0.45` блока. При пропуске такого
узкого допуска fixed-direction шаг не корректировал знак боковой ошибки, поэтому
камера продолжала идти в прежнюю сторону. `ReturnToRoute` использует тот же
узкий distance test. Исправление должно рулить по фактической знаковой боковой
ошибке, считать пересечение waypoint успешным и возвращаться к исходной линии;
профиль съёмки, скорость и длину M335 менять нельзя. Для последующих flight
reports добавить фактическое максимальное и конечное отклонение от исходной линии.

Renderer report дал `22/39` gates при `holes_rate=1.0`; `fly wall median` —
`114.33 ms`, dirty median/max — `609/1 023`, `visible_black_max=35`, а stop
convergence не сошёлся. Pixel trace содержал 32 768 probes; при luma `<96`
получено 2 643 dark samples, из них 2 581 имели drawable visible MDI depth-hit.
У 1 545 таких samples CPU mesh revision превышал опубликованную geometry
revision. Это наблюдение сделано в полосе, куда ошибочно ушёл обход, и само по
себе не является M335 acceptance. Compact serializer не включал generic dirty
owner и queue поля для exact opaque depth-hit, поэтому ownership этого разрыва
нужно корректно сериализовать перед следующим same-route сравнением.

Source trace разделил исходную полосу и ошибочно достигнутую полосу. Для
`z=3±5` было 262 procedural commits с scheduler queue `median/p95/max`
`7.7 ms/0.853 s/4.32 s`, worker-pool wait p95 `0.050 ms`, generation p95
`108 ms`. В достигнутой полосе `z=129±5` было 2 178 commits с scheduler queue
p95 `30.26 s` и max `57.51 s`; worker-pool wait p95 остался `0.053 ms`, а
generation p95 — `120 ms`. Disk file-read p95 был `1.19 ms`, но result-wait p95
составил `5.60 s`. Длинное ожидание находится до worker generation и в очереди
готовых disk results, но M395 попал в этот коридор из-за ошибки обхода.

Следующие шаги:

1. Закрыть waypoint overshoot и измерять cross-track offset. Сначала собрать
   Release и пройти прежний полный M335 до конца; принять полёт только если
   detours завершаются, камера возвращается на исходную линию и достигает
   checkpoint.
2. В compact `renderer_pixel_probe` добавить work-owner, dirty-queue и relight
   поля именно для framebuffer depth-hit chunk (generic fields уже заполняются
   для него, но не сериализуются в dense pixel row). Rerun exact M335 и
   сопоставить dark witnesses со стадией remesh/upload/publication.
3. Разделять исходные z=3 данные и source/queue метрики любого обходного
   участка; только после route-closure повторять выводы о дальнем render debt.
4. Затем продолжить G3 по causal evidence в dirty admission → mesh result → GPU
   upload/fence → resident draw table, без изменения камеры и условий света.

Артефакты: [M395 renderer report](../../bin/suite_reports/engine_refactor/m395_world164_m335_generation_queue_split_20261005.json),
[flight control](../../bin/suite_reports/engine_refactor/m395_flight_control_report_20261005.json),
[pixel trace](../../bin/suite_reports/engine_refactor/m395_renderer_pixel_trace_l96_20261005.json),
[z=3 source trace](../../bin/suite_reports/engine_refactor/m395_world_column_source_z3_20261005.json),
[z=129 source trace](../../bin/suite_reports/engine_refactor/m395_world_column_source_z129_20261005.json).

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
- Geometry clipmaps поддерживают стабильный дальний terrain working set: вложенные
  сетки центрируются на камере и сдвигаются инкрементально, давая steady render
  rate и graceful degradation. Это применимо как отдельный terrain LOD/proxy слой,
  а не как замена редактируемым voxel chunks:
  [Asirvatham & Hoppe, GPU Gems 2](https://hhoppe.com/proj/gpugcm/).
- Sparse voxel octrees показывают иерархическое хранение, ray traversal и
  управление voxel data в памяти/на диске; для Cubatarium это ориентир для
  дальнего volumetric LOD или sparse cache, но не аргумент заменять текущие chunks
  без измерений и совместимого mutation path:
  [Laine & Karras, NVIDIA Research](https://research.nvidia.com/sites/default/files/pubs/2010-02_Efficient-Sparse-Voxel/laine2010tr1_paper.pdf).
- Для изменяемых voxel worlds mesh является производным кешем: базовый разбор
  greedy meshing исходит из того, что блоки меняются существенно реже, чем
  рисуются, поэтому geometry rebuild и draw следует измерять отдельно:
  [Meshing in a Minecraft Game, 0 FPS](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/).
- Khronos описывает implicit synchronization при записи в GPU buffer ranges,
  которые ещё могут использоваться draw-командами. Для текущего GL 3.3 backend
  изменения upload path нужно проверять вместе с fence lifetime и безопасностью
  повторного использования диапазонов:
  [OpenGL Buffer Object Streaming](https://wikis.khronos.org/opengl/Buffer_Object_Streaming),
  [OpenGL Synchronization](https://wikis.khronos.org/opengl/Synchronization).

Переносимый вывод для Cubatarium: источник данных — отдельный наблюдаемый результат
до mesh readiness; очередь должна ограничивать дубликаты/запас работы, а disk I/O,
decode/apply, generation, lighting и publication должны иметь отдельные latency и
completion counters. Конкретные лимиты брать из измерений этого движка.

### M396 — исходный маршрут и детуры восстановлены; render debt подтверждён

M396 собран на чистом Release commit `19007cc5` (`Cubatarium.exe` SHA-256
`3f78eaf3617edbc8c9039ecafee423d9c9513d05afe15efdf7fb1efc7543cb20`) и прошёл
те же World_164/M335 условия: start `[120,56,56]`, eye `70`, yaw `180°`, pitch
`−30°`, fixed clear day, scale `1`, no teleport, 2 800 s полёта и 20 s остановки.
Условия камеры и съёмки не менялись. Обход включался только на двух
предсказанных препятствиях: оба детура завершены, перепланирований и collision
stop нет. Фокус прошёл `(7,3)→(−584,3)` / `9 456` блоков; максимальный уход от
исходной линии составил `2.50` блока, конечный — `0.14` блока. Значит, результат
можно сопоставлять с прежним M335 маршрутом.

Renderer acceptance всё ещё красный: `15/39` gates, `holes_rate=1.0`, максимум
`fly_visible_black=25`. На точке остановки focus miss сохранялся до `70 s`, в
конце было `57` not-ready slices и `152` dirty focus slices; post-stop convergence
не достигнута. Это измеренный render debt, а не доказательство, что каждый такой
slice занимает большую часть экрана.

Пиксельная трасса содержит `32 768` проб. Из `1 605` проб с luma `<96` на `1 532`
нашёлся видимый opaque MDI depth-hit; `851` из них имели CPU mesh revision выше
опубликованной geometry revision. У `822/851` был dirty owner: `792` стояли в
priority-remesh очереди и `30` в обычной remesh очереди. Возраст этих dirty
записей: median `24`, p95 `166`, max `212` кадров. Это локализует значительную
часть stale drawable поверхности до новой сборки меша, но не доказывает причину
всех затемнённых пикселей. Порог `<96` захватывает нормальные тёмные материалы:
среди source-face samples `1 160/1 445` имели sky-light `1`; из `284` проб `<32`
`220` также имели sky-light `1`, а preview-маркер был только у `35`. Поэтому эти
пороговые количества — диагностическая выборка, не доля чёрных/пустых чанков.

Coordinate join дал два разных примера:

- Opaque pixel hit на `(-583,2,2)` относится к колонке `(-583,0,2)`. Для неё был
  `procedural/disk_miss`; от запроса до worker start прошло `12.527 s`, сама
  генерация заняла `94.37 ms`, apply — `5.05 ms`. Через `11.7 s` после commit
  slice уже был drawable, но помечался `provisional_light_preview=1` при
  `field_light_rev=0`, settlement `1:0` и публикации geom `1`. Это доказывает
  новый procedural source для одного видимого sample и показывает отставание
  admission/generation queue; само по себе не объясняет его цвет.
- Колонка `(-578,0,3)` получила procedural commit за `178 ms` total (`73.6 ms`
  генерация). Примерно через `25 s` её slice имел `4096` non-air блоков и
  FirstMesh-ticket в начале dirty очереди (`index=0/17`), но ещё не имел drawable
  mesh, capture, async build или GPU owner; dirty age была `16` кадров. Это
  конкретное пустое покрытие после быстрого получения voxel data. На уровне всего
  run miss stuck достигал `70 s`.

В source trace маршрута: `2 719` procedural commits; request→worker scheduler
wait median/p95/max `33 ms/30.90 s/64.71 s`, worker-pool wait p95 `0.049 ms`,
generation p95 `120 ms`, ready-wait p95 `400 ms`, apply p95 `7.47 ms`. Для disk
path завершились `2 554/2 556` queued columns; file-read p95 `8.19 ms`, но
result-wait p95 `59.67 s`. Следовательно, на медленном procedural хвосте узкое
место до worker, а на disk path — выдача/применение готового результата, не
скорость самого файла. При этом конкретные `(-578,0,3)` и `(-583,0,2)` показывают,
что быстрый источник всё ещё может оставлять FirstMesh/light publication debt.

**Следующий этап рефакторинга:** сначала добавить bounded per-slice trace решения
FirstMesh admission для ближних loaded/no-drawable slices: фактический lane cap,
dirty queue index/age, focus distance, snapshot refresh/time budget, defer reason,
и наличие pipeline owner. Нужен именно ответ, почему ticket у головы очереди не
перешёл в capture, а не ещё один глобальный queue count. Затем разделить и
исправлять два потока по измерению: source admission/ready-result apply и
FirstMesh capture→worker→GPU publication. Не повышать Red load cap и не менять
камеру. После одного source/render изменения повторить M335; затем, уже после
стабилизации World_164, проверить переносимость на новом seed.

Артефакты: [M396 renderer report](../../bin/suite_reports/engine_refactor/m396_world164_m335_detour_closed_loop_20261005.json),
[flight control](../../bin/suite_reports/engine_refactor/m396_flight_control_report_20261005.json),
[pixel/depth trace luma 96](../../bin/suite_reports/engine_refactor/m396_renderer_pixel_trace_l96_20261005.json),
[z=3 source trace](../../bin/suite_reports/engine_refactor/m396_world_column_source_z3_20261005.json),
[perf JSONL](../../bin/logs/perf_20261005-020105_13792.jsonl),
[GUI captures](../../bin/logs/m396_world164_m335_detour_closed_loop).

## M397 — одинаковый M335, очередь FirstMesh и трассировка видимых пикселей (2026-10-05)

Перед изменением политики рендера выполнен полный GUI-пролёт M335 на `World_164` с прежним стартом `[120,56,56]`, eye `70`, yaw `180°`, pitch `−30°`, fixed clear day, no teleport, speed scale `1`, 2 800 s + 20 s остановки. Release executable зафиксирован manifest; рабочее дерево на момент запуска было чистым, commit `fcf34c1e`. Фактическое движение осталось в диапазоне профиля: медиана `5.19 блок/с`, камера сохранила eye y=70, пройдено `9 200` блоков (`575` чанков по X). Столкновений/остановки не было.

Renderer acceptance снова не пройден: `22/39` основных gates, `holes_rate=1.0`, медиана wall `109.5 ms`, dirty median/max `681.5/1 099`, miss-stuck до `216 s`; post-stop demand convergence=false. Перф-агрегат `holes_rate` не является визуальным подсчётом пустых чанков. Pixel/depth trace даёт более точную картину: все `211/211` выборок с luminance `<32` попали в depth и видимую opaque MDI геометрию; `199/211` source faces имели sky-light `1`, `196/211` — settled demand light. При этом у `122/211` CPU mesh revision опережала опубликованную геометрию и `117/211` оставались только с Dirty queue owner (медиана возраста `14` кадров). Значит, выборка строгих тёмных пикселей подтверждает задержку обновления/публикации мешей на существующей геометрии; она не подтверждает отсутствие структуры мира или целых пустых чанков.

Новая трасса очереди FirstMesh удержала `512` последовательных последних сэмплов, охватывающих только frame epoch `29563–30532` из всего маршрута. Для `63` same-frame joins с `mesh_schedule_trace` все решения имели `cause=3`: превышен общий tick budget mesh-emerge. В этих joins FirstMesh cap не был нулевым, snapshot time budget не был исчерпан и pipeline не был у предела. Это подтверждает, что общий wall-budget guard отсекал часть уже состарившихся заявок при доступном локальном snapshot/pipeline capacity. Однако короткое кольцо трассы закончилось за `330` scheduler frames до последнего scheduler события и не содержало hotspot `cx=-529`; вывод относится только к 63 точным joins, а не ко всем отсутствующим пикселям/чанкам.

Source-stage данные отделены от mesh publication. Зафиксировано `2 556/2 556` disk completions; file-read p95 `1.18 ms`, но result-wait p95 `61.10 s`. Procedural worker-pool wait p95 `0.053 ms`, generation p95 `123.74 ms`, scheduler queue p95 `25.01 s`. Следовательно, остаётся отдельная проблема producer admission/готового результата; её нельзя объяснить медленным чтением диска или генерацией worker.

**Следующее изменение:** в `ChunkMeshCache` оставить общий tick budget как ограничитель, но разрешить максимум одну дополнительную over-budget подачу только для ближней FirstMesh-заявки, ожидающей не менее `32` scheduler frames и при свободном async pipeline и snapshot budget. Базовый резерв остаётся одним на кадр, новый резерв — жёстко ограничен вторым слотом. Это не повышает общие лимиты, не меняет world/camera/flight conditions и не трогает light/producer policy. Сравнить на точном M335: долю aged FirstMesh `cause=3`, время mesh-emerge/FPS, чёрные пиксели с depth/source joins и post-stop convergence. Если кадр станет тяжелее без уменьшения задержки, откатить резерв и исследовать стоимость до schedule loop. После этого отдельно разбирать disk-result/procedural scheduler tails и повторить baseline на новом seed только после стабилизации World_164.

Артефакты: [полный компактный M397 report](../../bin/suite_reports/engine_refactor/m397_world164_m335_firstmesh_frontier_20261005.json), [FirstMesh frontier trace](../../bin/suite_reports/engine_refactor/m397_firstmesh_frontier_trace_20261005.json), [pixel/depth trace](../../bin/suite_reports/engine_refactor/m397_renderer_pixel_trace_20261005.json), [source trace z=3](../../bin/suite_reports/engine_refactor/m397_world_column_source_z3_20261005.json). Сырые JSONL и изображения остаются локальными из-за размера.

## M398 — bounded FirstMesh reserve: очередь улучшилась, видимый remesh остаётся заблокирован (2026-10-05)

M398 собран в Release на commit `bc32466b54e553474373d86f754f168946ec4fff` и прошёл тот же M335: World_164, start `[120,56,56]`, eye `70`, yaw `180°`, pitch `−30°`, ясный день, no teleport, scale `1`, 2 800 s + 20 s остановки. Средняя фактическая скорость — `5.186 блок/с`; фокус прошёл `x=7…−563` (`9 120` блоков). Стандартный obstacle avoidance был включён, но блокировок движения и детуров в M398 не было.

Изменение M398 имело точный локальный эффект. В сопоставленной полосе focus X `−563…−537` возраст FirstMesh очереди снизился с M397 `75` кадров median / `259.45` p95 / `279` max (512 samples) до M398 `11` / `33.65` / `40` (408 samples). Это доказывает, что дополнительный ограниченный слот обслуживает состарившиеся FirstMesh заявки. Однако общее качество не восстановилось: acceptance `22/39`, `holes_rate=0.9993`, `visible_black_blink_rate=0.0467`, dirty median/max `463.5/1 117`, wall median `111.99 ms`; miss-stuck достиг `68 s` около `cx=−518`. Через 20 s остановки осталось `22` not-ready среза и `45` dirty focus slices; demand convergence не достигнута.

Depth-linked пиксельная трасса разделяет отсутствие геометрии и старую геометрию. Все `257/257` проб ниже luma `32` попали в существующую поверхность с видимым opaque MDI draw; у `159/257` depth-срезов CPU geometry revision была выше опубликованной, и `152/159` имели только Dirty owner. Из `240` валидных source-face выборок `237` имели sky light `1`, `249` opaque chunk observations имели settled demand light, а preview marker был у `23`. При более широком пороге `<96`: `1 741/1 817` проб имели видимый MDI, `1 094` среза были геометрически stale, `1 042` stale samples были Dirty-only. Эти пороги включают тёмные материалы, поэтому это не классификатор «чёрного чанка».

Отдельная scheduler-трасса обнаружила измеряемый путь задержки: среди 512 watched schedule samples было 49 отказов `cause=3` (общий mesh-emerge tick budget). Все 49 drawable priority-remesh заявок стояли на индексе `0`, были owned только Dirty, и имели возраст `30…2 519` кадров; 40 таких samples находились в пределах пяти чанков от focus `cx=−563`. Эта трасса сама по себе не связывает каждую заявку с тёмным пикселем, но показывает, что текущий over-budget escape hatch обслуживает FirstMesh, а видимая устаревшая геометрия всё ещё может оставаться у головы очереди без сборки.

В source-stage M398 завершились `2 556/2 556` disk requests. Чтение файла быстрое (p95 `1.18 ms`), но result wait p95 составил `73.75 s`; procedural scheduler queue p95 `27.95 s`, worker-pool wait p95 `0.055 ms`, генерация p95 `117.68 ms`. Это независимый producer/admission хвост; не смешивать его с подтверждённой задержкой mesh remesh.

**Следующий шаг:** дать ровно один дополнительный over-budget schedule slot для aged (`≥32` scheduler frames), drawable priority-remesh заявки из renderer screen-ray repair, только в focus radius и при свободных pipeline/snapshot лимитах. Не менять общий schedule cap, flight profile, light policy или source quotas. В M399 повторить точный M335 и проверить: прошли ли screen-ray remesh из Dirty-only к capture/build; сократились ли их queue age и пиксельно связанная stale geometry; не ухудшились ли wall/FPS, FirstMesh service и stop convergence. Долгие disk/procedural queue tails оставить отдельной следующей веткой исследования.

Артефакты: [run report](../../bin/suite_reports/engine_refactor/m398_world164_m335_aged_firstmesh_reserve_20261005.json), [сравнение FirstMesh age](../../bin/suite_reports/engine_refactor/m398_firstmesh_frontier_trace_20261005.json), [пиксельный summary](../../bin/suite_reports/engine_refactor/m398_renderer_pixel_trace_20261005.json), [priority-remesh scheduler evidence](../../bin/suite_reports/engine_refactor/m398_mesh_schedule_trace_20261005.json), [z=3 source trace](../../bin/suite_reports/engine_refactor/m398_world_column_source_z3_20261005.json). Сырые perf JSONL, GUI кадры и полные pixel arrays остаются локальными из-за размера.

## M399 — screen-ray reserve не изменил сходимость (2026-10-05)

M399 собран в Release на `a624dc5fcdf453d8eef838bc7bb0cc760f6c3a3e` и повторил без изменений M335: World_164, старт `[120,56,56]`, eye `70`, yaw `180°`, pitch `−30°`, ясный день, no teleport, scale `1`, 2 800 s + 20 s остановки. GUI-прогон завершился с `process_rc=0`, фактическая скорость была около `5.19 блок/с`, дистанция `9 440` блоков; фокус прошёл `x=7…−583`. Предиктивный obstacle avoidance был включён, но препятствий, остановки или детуров не было.

Renderer acceptance не сошёлся: `23/39` gates, `holes_rate=1.0`, `visible_black_blink_rate=0.05535` (хуже M398 `0.04668`), miss-stuck `60 s` около `cx=−560`; остановочный demand convergence не достигнут. Dirty median снизился до `217` (M398 `463.5`), wall median — до `107.00 ms` (M398 `111.99 ms`), но эти улучшения не закрыли визуальный долг.

В depth-linked выборке все `286/286` пикселей с luma `<32` попадали в валидную поверхность и opaque MDI draw. У `213/286` CPU geometry revision опережала опубликованную; у `206` из них Dirty оставался единственным владельцем работы. В sample `217/269` source faces имели sky-light `1`, а `197/286` opaque-demand записей были settled. Это подтверждает смесь старой опубликованной геометрии и тёмных/слабо освещённых реальных поверхностей; luma `<32` не является счётчиком пустых чанков.

ScreenRayRepair за run записал `522` событий; `437` promoted drawable priority-remesh заявок, median queue age `4`, max `221`, `62` наблюдались на голове очереди. Пример `(-554,3,1)` был promoted с возрастом `2`, имел geometry debt и Dirty-only ownership без build/GPU owner. Over-budget reserve M399 требует возраст `≥32`, поэтому такие свежие прямые renderer witnesses не подходят под новый escape hatch. В bounded tail scheduler trace нет зафиксированных screen-ray reserve flags, но это окно ограничено последними `1 024` событиями и не доказывает, что reserve не срабатывал раньше в маршруте.

**Следующее изменение:** для точной drawable ScreenRayRepair заявки убрать возрастной порог из единственного over-budget reserve. Условия остаются узкими: живой screen-ray pin, drawable, текущий focus radius, свободны pipeline/snapshot budget; суммарно не более двух bounded escapes за кадр с FirstMesh и не более одного ScreenRayRemesh. Общий schedule/pipeline/snapshot cap и все M335 условия сохранить. M400 должен показать сработавшие reserve flags, переход заявок из Dirty в build/GPU stages, пиксельно связанную stale geometry и stop convergence. Если flags не появляются, диагностировать, почему видимый repair не доходит до `try_schedule`, вместо расширения budget.

Producer задержки остаются отдельной веткой: M399 завершил `2 556/2 556` disk requests, file-read p95 `1.14 ms`, result-wait p95 `49.21 s`; procedural scheduler-queue p95 `29.30 s`, worker-pool wait p95 `0.048 ms`, generation p95 `124.73 ms`.

Артефакты: [run report](../../bin/suite_reports/engine_refactor/m399_world164_m335_screenray_remesh_reserve_20261005.json), [pixel/depth summary](../../bin/suite_reports/engine_refactor/m399_renderer_pixel_trace_20261005.json), [screen-ray repair](../../bin/suite_reports/engine_refactor/m399_screen_ray_repair_trace_20261005.json), [scheduler trace](../../bin/suite_reports/engine_refactor/m399_mesh_schedule_trace_20261005.json), [source trace](../../bin/suite_reports/engine_refactor/m399_world_column_source_z3_20261005.json). Полные pixel arrays, raw perf JSONL и GUI captures остаются локальными.

## M400 — свежий ScreenRay reserve срабатывает, но дальний прогон короче (2026-10-05)

M400 собран в Release на `471e2aa279c3e622944a22254f463cf2ebfc61cc`, executable SHA-256 `9ca2581ca92c797ac5a0d7d6122f55ed685fbcb8cabb402e05c2ab6128aebad4`. Повторён тот же M335/World_164: start `[120,56,56]`, eye `70`, yaw `180°`, pitch `−30°`, ясный день, no teleport, scale `1`, 2 800 s + 20 s остановки, видимый GUI. Route hash совпадает с M399; predictive obstacle avoidance оставлен включённым.

Прогон завершился без блокировки движения (`camera_move_blocked_substeps=0`), но не достиг прежнего far checkpoint: focus завершился на `x=−501` (`8 128` блоков от старта) против M399 `x=−583` (`9 440` блоков). Far-distance gate failed. Медиана активной скорости осталась `5.18555 blocks/s`, camera Y весь полёт была `70`, Z оставался в коридоре `54…58`; заметного увода или collision stop нет. Это указывает на нестабильную фактическую производительность/продвижение за фиксированное время, а не на изменение параметров профиля. Дальний диапазон западнее `x=−501` M400 не проверил.

Точечное изменение M400 сработало: сохранённые bounded scheduler samples содержат `62` уникальные frame/coordinate ScreenRay reserve admissions с outcome `cause=15` (schedule accepted), возраста `0…8` кадров. ScreenRayRepair за полный run записал `506` событий, включая `417` promoted drawable priority-remesh witnesses; median age `5`, p95 `51`, max `172`. Это подтверждает переход свежего renderer witness в async build, но не полное закрытие последующего publication debt.

Для общей зоны, измеренной обоими pixel traces (`camera X=−8 192…−1 024`, по spatial bins; camera Y одинаковая, camera Z различалась не более чем на несколько блоков), доля проб с luma `<96` снизилась с M399 `5.34%` до M400 `4.45%`; доля depth samples с CPU geometry новее опубликованной снизилась `69.49%→60.11%`. При этом у `96.61%` оставшихся stale M400 samples Dirty был единственным owner. Во всей M400 pixel выборке все `174/174` пробы `<32` попали в действующую opaque MDI depth-поверхность; `102/174` имели CPU mesh revision выше published. Эти пороги не классифицируют пустой chunk: `<96` включает тёмные материалы и туман, а источник-face часто имеет sky-light `1`.

Общий run acceptance ухудшился: `21/39` gates против M399 `23/39`, `holes_rate=1.0`, stop convergence=false. Dirty median/max выросли `976/1 583` против `217/925`, wall median `128.26 ms` против `107.00 ms`, streaming phase `68.43 ms` против `50.42 ms`, mesh-emerge `33.86 ms` против `26.22 ms`. `fly_visible_black_max` снизился `28→23`, но blink rate остался около `5.6%`. Значит, ScreenRay reserve даёт локальное улучшение stale pixel geometry, но M400 не доказывает общее исправление и одновременно показывает дорогой незакрытый streaming хвост.

В source trace завершились все `2 556/2 556` disk requests, но disk file-read p95 вырос до `12.35 ms`, result-wait p95 до `78.94 s`; procedural scheduler-queue p95 — `33.84 s`, worker-pool wait p95 `0.080 ms`, generation p95 `131.12 ms`. Узкое место остаётся в admission/ожидании готовых source results, а не в длительности генерации на worker. Renderer по run по-прежнему чаще блокируется `zero_fm_cap`, completion stall — `gpu_not_ready`, wall-stage — `stream`.

**Следующий шаг:** сохранить bounded свежий ScreenRay reserve как локально подтверждённую оптимизацию и разобрать загрузку/создание мира как отдельный producer path: кто держит 33–79 секундные очереди, как disk result и procedural request делят admission/ready-apply бюджеты, и почему фиксированное flight-time даёт разный west displacement при одинаковой активной скорости. Не менять M335 camera/world conditions и не поднимать глобальные schedule/pipeline caps. После source fix повторить M335 до полного far checkpoint; затем вновь проверить post-stop convergence и тот же пространственно совпадающий pixel corridor.

Артефакты: [run report](../../bin/suite_reports/engine_refactor/m400_world164_m335_fresh_screenray_reserve_20261005.json), [pixel/depth summary](../../bin/suite_reports/engine_refactor/m400_renderer_pixel_trace_20261005.json), [M399/M400 matched corridor](../../bin/suite_reports/engine_refactor/m399_m400_matched_route_pixel_comparison_20261005.json), [ScreenRayRepair](../../bin/suite_reports/engine_refactor/m400_screen_ray_repair_trace_20261005.json), [scheduler trace](../../bin/suite_reports/engine_refactor/m400_mesh_schedule_trace_20261005.json), [source-stage trace](../../bin/suite_reports/engine_refactor/m400_world_column_source_z3_20261005.json). Raw perf JSONL, full pixel arrays and GUI captures остаются локальными.

## M401 — same M335 after disk-slice apply fast path (2026-10-05)

`UChunkBuffer::ApplyToChunk` теперь принимает уже известную координату чанка,
находит его один раз и применяет блоки/жидкость/свет без поиска чанка на каждый
элемент среза. Путь сохраняет семантику `SetBlock` и CaptureBuffer mirror.
Release build: commit `1d0bcff1`, executable SHA-256
`3BE599300B1D31C142953B7B324F03819D48AA75DB43D087B8EB95BDDCA83208`.

M401 повторил M335 без изменения тестовых условий: World_164, start
`[120,56,56]`, eye `70`, yaw `180°`, pitch `−30°`, ясный замороженный день,
no teleport, scale `1`, 2 800 s полёта + 20 s остановки, видимый GUI и
predictive avoidance. Run завершился `process_rc=0`, прошёл дальность 8 192
блока, focus X достиг `−515` (8 352 блока). Acceptance рендера остался FAIL;
изменение движения/коллизии не потребовалось.

В пространственно общей полосе X `−8 192…−1 024` M401 улучшил медиану
`async_io_ms` `37.90→36.37 ms`, streaming phase `73.03→70.81 ms`, mesh emerge
`35.16→33.43 ms`, wall `134.46→127.54 ms` и dirty count `1 010→949` против
M400. `render_total_ms` остался практически неизменным (`52.04→52.59 ms`), а
общий визуальный gate не сошёлся. Это небольшая producer-side оптимизация, не
исправление пустого/старого кадра.

Source trace уточняет эффект fast path: disk `deserialize_apply_ms` median
снизился `7.97→7.19 ms`, p95 `11.66→11.28 ms`; `file_read_ms` p95
`12.35→3.79 ms` (cache state confounds this comparison). Ключевая задержка
осталась: ready-result wait p95 `78.94→72.13 s`, готовая очередь p95
`112→118`, max `462`. Procedural scheduler queue p95 улучшился лишь
`33.84→32.13 s`, при worker-pool wait p95 `0.080→0.317 ms` и generation p95
`131.12→151.63 ms`. Следовательно, надо разделять admission/backlog, полезность
готового результата для текущего focus и время apply; нельзя сводить проблему
к диску или количеству worker threads.

Дальние pixel/depth пробы тоже не подтверждают «пустой чанк»: все `223/223`
проб с luma `<32` имели валидную opaque depth surface и MDI draw. Во full-run
pixel samples количество low-luma проб выросло против M400: `<32` `174→223`,
`<96` `1 244→1 458`; доля revision-stale surfaces также стала выше.
Pixel counts здесь относятся ко всему маршруту, не к точному corridor.
`renderer_pixel` aggregate по всему run и endpoint различаются, поэтому перед
выводами о spatial регрессии нужно пересчитать оба perf JSONL одинаковыми X-бинами.
Для этого сохранён [`compare_renderer_pixel_routes.py`](../../tools/compare_renderer_pixel_routes.py).

Стартовый gate также нужно считать отдельно от flight time. M401 и M402
получили первый correct proxy за `21.69/17.18 ms`, около `491` resident chunks;
enter-lit завершился за `197/181 ms` с `settle_reason=live_blockers` и
`underfeet_present_ready=0`. Первая картинка появляется быстро, но стартовый
readiness gate завершается при живом долге.

**Следующий шаг:** оставить все M335 условия фиксированными; сопоставить M400 и
M401 pixel witness по X-бинам и source-stage coordinates. Сначала отделить
backlog колонок позади focus от колонок впереди/внутри focus; затем выбрать одно
узкое изменение для ready-result admission или priority refresh. Не повышать
общие quotas без доказательства, что отстаёт именно ближний frontier. После
изменения повторить дальний M335, сравнить одни и те же X-бин и остановочную
сходимость. Новый seed остаётся периодической переносимостной проверкой.

Артефакты M401: [run](../../bin/suite_reports/engine_refactor/m401_world164_m335_chunk_apply_fastpath_20261005.json),
[matched route metrics](../../bin/suite_reports/engine_refactor/m400_m401_matched_route_comparison_20261005.json),
[source-stage report](../../bin/suite_reports/engine_refactor/m401_world_column_source_z3_20261005.json),
[pixel luma 32](../../bin/suite_reports/engine_refactor/m401_renderer_pixel_trace_l32_20261005.json),
[pixel luma 96](../../bin/suite_reports/engine_refactor/m401_renderer_pixel_trace_l96_20261005.json).
Raw perf/capture data remain local.

## M401 spatial pixel comparison — no visible improvement (2026-10-05)

The saved [`compare_renderer_pixel_routes.py`](../../tools/compare_renderer_pixel_routes.py)
was run over the shared camera-X corridor `[-8192,-1024)` in 512-block bins.
It joins samples by camera position so different route endpoints do not change
the measured region. Across that corridor M400 had 27,520 probes and M401 had
28,320. The low-luminance rates changed only slightly: `<32` was `0.632% →
0.689%`, and `<96` was `4.455% → 4.555%`. Geometry revision newer than the
published mesh was `9,904/27,520` (`36.0%`) in M400 and `10,297/28,320`
(`36.4%`) in M401. These results show no material route-level rendering
improvement from the disk-slice apply fast path. Low luminance remains a
pixel/material/light witness, not a count of empty chunks.

The compact bin report is
[`m400_m401_pixel_x_bins_20261005.json`](../../bin/suite_reports/engine_refactor/m400_m401_pixel_x_bins_20261005.json).
Its per-bin distribution is useful for selecting the next diagnostic joins;
the aggregate differences do not justify a global quota increase or another
camera/route change. Continue from the M401 source-stage plan: split source
backlog by whether its columns are ahead of, at, or behind the current focus,
then inspect admission and ready-result apply ownership separately.

## M402 — partial M335 flight, zero-sized framebuffer, then hung app (2026-10-05)

M402 used the exact M335 World_164 route and collision-safe flight-sim build
from Release commit `5d8a091f`; executable SHA-256 was
`C98F0F180873F0F4F640A38D42FD3258330B3384C938C54B757FE4C2F3C26BD6`. It
reached player X `−6949` (about 7,069 blocks from the start; focus chunk
`−435`) and did not reach the far checkpoint. Movement was not blocked and had
no ground contacts at the last valid period, so collision was not the cause of
this interruption.

The run is not valid visual-render acceptance evidence. The INFO log recorded
5,601 `RenderFrame skipped viewport: zero framebuffer size` messages between
12:03:38 and 12:10:38. The final successful framebuffer capture before this
interval was `frame_100.png` at 12:03:32; 28 scheduled captures then failed,
and `frame_129.png` at 12:11:19 is fully black. M400/M401 had zero such
warnings. Why GLFW returned a non-positive framebuffer dimension is unknown;
the render path skips `RenderFrame` when either width or height is non-positive. This
means the visible-render workload changed even while the simulation and
streaming metrics continued. The Windows Application log had no matching
Application Hang/Error entry. The process stopped responding, WM_CLOSE did not
recover it, and the game process was force-terminated; no dump was collected,
so the hang's root cause is not established. The wrapper report's generic
`run_outcome=crash`/unsigned exit value reflects that forced termination, not
evidence of an engine crash. Its final JSONL record is truncated; use only the
939 complete period rows for partial diagnostics.

Those rows still provide a useful frontier witness. At the final valid period
the route had `40` solid slices in the focus band, `16` without drawable mesh,
`15` with pending work and `1` without an owner; the visual terrain census
reported `111` incomplete slices. Nearby `RelightAudit` entries for chunk
`(-436,0,3)` showed non-air data with no installed drawable, and a stale-light
debt backlog of `464`. This points to unresolved first-mesh/publication and
admission debt in loaded content at the frontier; it does not show that the
world data was absent or that storage alone caused the dark appearance.

Before the next long visible acceptance run, reject the visual sample if the
framebuffer remains zero across a capture interval and preserve the first
zero-size transition in the report. Keep any continuing simulation metrics
separately labeled as non-visible workload. For the engine investigation,
inspect the M402 frontier's ownerless/no-drawable slices alongside M400/M401
source traces; avoid changing flight conditions or broadening global budgets.
On another unresponsive app, capture a process dump and wait-chain snapshot
before forced termination so the application hang can be diagnosed.

Artifacts: [curated M402 postmortem](../../bin/suite_reports/engine_refactor/m402_hang_postmortem_20261005.json),
[partial M402 report](../../bin/suite_reports/engine_refactor/m402_world164_m335_safe_detour_hold_20261005.json),
[M402 INFO log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-115345.36848),
[M402 captures](../../bin/logs/m402_world164_m335_safe_detour_hold),
[M402 perf JSONL](../../bin/logs/perf_20261005-113820_36848.jsonl).
The fixed-day runner restored `world_data.json` byte-for-byte (SHA-256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`).

## Source-of-column map — repeated disk misses and unresolved save fate (2026-10-05)

To answer whether the late M335 route loads old columns or creates a new area,
[`compare_world_column_sources_by_x.py`](../../tools/compare_world_column_sources_by_x.py)
groups `[WorldColumnSource]` events into 16-chunk X bins. An X bin aggregates
multiple Z coordinates, so it is a route-position summary rather than a full
spatial map. Inspecting the exact coordinates in `cx=[−440,−424)` shows that
M400 and M401 each encountered the same 144 columns at `cz=[−1,8)` as
`procedural/disk_miss`, then committed them procedurally; neither run recorded
a disk completion for those coordinates. The current
`bin/worlds/World_164/chunks` contains files at similar X values on other Z
lanes, but no slice files for those exact 144 `(cx,cz)` pairs. M400 and M401
ended much farther west, at focus `cx=−501` and `cx=−515` respectively, so
this band was well behind the camera by the end of each route. For these cells,
the evidence establishes repeated procedural regeneration rather than loading
from disk; it does not establish why the first run left no disk result.

The harness disables periodic autosave, then takes the fast shutdown path and
calls `_Exit`, so it does not run a final cooperative session snapshot. Normal
streaming unloads are supposed to enqueue async column saves. The current
Release source now records `WorldColumnSave` queue results, per-slice write
success/failure, target paths, and pending-I/O counts at fast shutdown whenever
`CUBA_WORLD_COLUMN_SOURCE_TRACE=1`. Async save worker failures also return a
completion instead of silently leaving the column permanently marked pending.
The source-bin analyzer includes those events. A same-route Release run must
now show whether the strip was skipped as incomplete, queued, fully written,
or still pending at process exit. The M335 dark-pixel symptom still cannot be
attributed to source choice alone: illumination and mesh publication remain
independent checks.

The transition is mixed rather than a single clean boundary. In
`cx=[−360,−344)`, M400 and M401 each recorded 57 disk completions and 87
procedural disk misses/commits across the 144 column coordinates. The stored
columns have their own queue problem: disk `result_wait_ms` in that bin was
median/p95 `95.1/109.7 s` in M400 and `52.0/177.8 s` in M401. File reads were
milliseconds, not tens of seconds. Across the full runs, disk result-wait p95
was `78.94→72.13 s`, while procedural scheduler-queue p95 was
`33.84→32.13 s`; procedural worker-pool wait p95 remained `0.080→0.317 ms`
and generation p95 was `131→152 ms`. Both stored-result admission and
procedural scheduling need work, but they are separate bottlenecks.

M402 provides a direct partial join at the frontier. For column
`(-436,0,3)`, disk lookup missed at 12:10:29; procedural generation committed
at 12:10:37 with `scheduler_queue_ms=7,794`, `worker_pool_queue_ms=0.041`,
`generation_ms=113.6`, and `apply_ms=6.1`. The schedule snapshot had 61 live
requests, 63 heap entries and a start cap of two. About one second later the
RelightAudit still saw 4,096 non-air voxels with no installed drawable mesh.
So the delay had two stages: the source request waited in the game-thread
scheduler, then mesh publication had not yet made the loaded content drawable.
M402 had an invalid/black display interval, so this is frontier-state telemetry,
not a visual causation claim.

This changes the next implementation focus: first add a bounded per-column
save lifecycle trace (unload candidate/veto, request, queue, worker result,
file path, and shutdown-pending count) and prove whether the exact band is
evicted and persisted. Keep the M335 route and world fixed. Then trace and
reduce age of the highest-priority near-frontier source request without raising
a global quota; verify that same column progresses through FirstMesh capture,
worker, GPU publication, and a screen/depth witness. Separately, audit the disk
ready-result queue in the mixed boundary bin. The source-bin report is
[`m400_m401_m402_world_column_sources_x_20261005.json`](../../bin/suite_reports/engine_refactor/m400_m401_m402_world_column_sources_x_20261005.json);
M402's underlying long INFO logs remain local.

## M403 — exact M335 source/unload baseline (2026-10-05)

M403 completed the established visible fixed-day M335 route on `World_164`:
553 chunks traveled, end focus cx=−546, no teleport, no collision stop, and two
obstacle-avoidance attempts both completed. Windows reported the process
responsive while it ran and the perf log continued to grow; the user's “frozen”
observation matches a severe slowdown rather than a confirmed message-loop hang
in this run. The process exited 0 and the fixed-day wrapper restored
`world_data.json` byte-for-byte (SHA-256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`).

The final resident count reached 20,648 chunk slices (M402 had 16,170 near
x=−6,949; M403 had 17,099 near x=−7,172). M403's median frame was 113.036 ms
(8.87 FPS), median dirty count 1,098, and peak near void debt 1,228. The
analyzer failed its gates. Its `unfinished_visual` key was nonzero in every
steady period; that is a readiness/debt metric, not a pixel-level claim that
every frame looked empty. M403 did not enable dense pixel capture, so use it as
a lifecycle/performance baseline, not visual acceptance.

The decisive unload evidence is that the run emitted **zero**
`[WorldColumnSave]` events and ended with 20,648 resident slices. Runtime mode
was U-A (1): the caller zeroed unload operations during movement, hitch frames,
and dirty counts above 64; U-A also lacked the cursor. The deferred-save queue
was only drained while stopped and calm. Therefore, the long flight kept
loading/generating columns while never saving and evicting the far side. This
explains repeated disk misses without assuming a disk corruption or image
decoder fault. It is a confirmed resident-set leak in the established flight
policy, but not proof that it was the only cause of M402's separate hung window.

M403's source trace saw 2,556 disk completions and 2,537 procedural disk
misses, followed by 2,527 generation commits; no save events were recorded.
Physical reads were quick (median 3.56 ms), while disk result wait was 2.20 s
median / 21.44 s p95. In the focus Z band, the procedural scheduler queue was
69 ms median / 25.83 s p95; worker-pool queue was 0.038 ms median and terrain
generation was 103 ms median. The waits sit in result/scheduler service, not
the storage-device read itself. These queues and the permanently large dirty
and unfinished-visual debts remain separate work after residency is bounded.

## M404 — bounded unload failed: repeated save before veto (2026-10-05)

The first bounded-unload change (`e9519c36`) did not pass M404. M404 used the
exact visible, fixed-day M335 profile for 2,800 seconds plus its 20-second stop
phase. The analyzer failed: 1,353 periods (1,351 steady), 163.0 ms median wall
frame (6.13 FPS), 146.1 ms median world-streaming phase, and 136.9 ms in the
stream metric. Streaming accounted for about 84% of wall time. Resident chunk
slices reached 4,240; `unfinished_visual` stayed nonzero in every steady
period (median 17), and the peak near-void proxy was 723. These counters show
persistent readiness debt, not that voxel data was absent. The report recorded
up to 18 visible dark/stale focus columns; sampled captures showed terrain as
well as visually disconnected/low-detail regions, but the report's operator
visual gate remains untested.

The save/unload ordering explains the severe hitch. The run queued 17,365
column saves across 1,189 unique columns; 16,176 queue events repeated a prior
column, one column was queued up to 103 times, and 70,343 slice writes
completed with no recorded write failures. Only a small number of perf records
showed nonzero `stream_unloads` (period/spike/blink records overlap), while
resident count kept growing. In `e9519c36`, saving ran before the active
`ColumnRecord` eviction check, and a veto did not consume the unload attempt
budget. The cursor could therefore serialize more candidates after an
eviction refusal. Seven period summaries account for 25 removed slices;
spike/blink records overlap those summaries and are not added as separate
unloads. This is the direct source of the repeated work; it does not by itself
explain every visual debt.

The follow-up in the worktree now checks eviction before saving, counts a veto
against the bounded attempt budget, and revalidates deferred candidates against
the current keep ring and camera capsule. Async-load cancellation and token
invalidation happen only after eviction is allowed, so a vetoed active visual
job can finish. Per-frame JSONL now records `stream_unload_candidates` and
`stream_unload_vetoes`. Next: build only the Release `Cubatarium` target, commit
the fix and M404 finding, then run M405 with the same M335 route and capture
settings. M404 persisted many columns, so M405 uses the same route and
settings but a world whose farther columns are now present on disk; compare
source traces explicitly rather than treating the two runs as a cold-state
pair.

Artifacts: [M403 perf analysis](../../bin/suite_reports/engine_refactor/m403_m335_source_unload_baseline_20261005.json),
[column source trace](../../bin/suite_reports/engine_refactor/m403_world_column_source_trace_20261005.json),
[M400–M403 X-bin comparison](../../bin/suite_reports/engine_refactor/m400_m401_m402_m403_world_column_sources_x_20261005.json),
and local raw logs `perf_20261005-131808_9428.jsonl` / `Cubatarium.exe*.INFO.*.9428`.

M404 artifacts: [analyzer report](../../bin/suite_reports/engine_refactor/m404_world164_m335_bounded_unload_20261005.json),
perf JSONL `perf_20261005-143028_11792.jsonl`, rotated INFO logs
`Cubatarium.exe*.INFO.*.11792`, and sampled frames in
`bin/logs/m404_world164_m335_bounded_unload`. The fixed-day wrapper restored
`world_data.json` byte-for-byte (`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`).
Windows briefly marked the process unresponsive during the final
`renderer_pixel_probe`; the process then exited and the analyzer completed.

## M405 — save storm reduced, pending work still pins resident columns (2026-10-05)

M405 used the same visible fixed-day M335 route on `World_164`, with no
teleport, for 2,800 seconds plus the 20-second stop phase. It ran the Release
binary from `927dafb1`. This run did not hang: Windows reported `Responding=True`,
camera movement and captures continued through frame 188, and the process exited
with code 0 (`hang_killed=false`). The low and variable frame rate (about 4–10
FPS in sampled intervals) can look frozen; this does not rule out M402's
separate zero-framebuffer/unresponsive incident.

The analyzer failed its visual/performance gates: 1,373 periods (1,371 steady),
114.751 ms median wall time (8.72 FPS), 63.13 ms median world-streaming phase,
dirty median 972, and `unfinished_visual` nonzero in every steady period
(median 27, maximum 91). `visible_black` reached 29 and the near-void proxy
reached 7,836. The final resident count was 13,779 chunk slices (peak 13,841).
These readiness and draw-state metrics do not establish that voxel data was
missing. Stream accounted for 4,283 spike rows and emerge for 1,625; spike and
period rows are separate observations and must not be summed as operations.

M405 confirms that the M404 repeated-save bug was reduced: the INFO trace
recorded 1,827 queued saves for 1,827 unique columns, 7,352 slice writes, no
repeated queued columns, and no slice-write failures. Yet unload was still
starved by the active-work veto: across `period` rows there were 1,346 unload
candidates and 1,241 vetoes. The remaining 105 successful candidate rows
removed 405 chunk slices. Thus the fix reduced redundant serialization, but a
pending visual token still acts as a residency pin; it is not a safe long-flight
eviction policy. M405 also reported 1,551 relight FIFO drops, 42 false clears,
and 24 seconds where relight drain was near zero while visible-black work
remained. Streaming/unload and light/mesh convergence are distinct remaining
problems.

The source trace recorded 2,591 disk-load completions and 2,608 disk misses
followed by 2,590 procedural commits. This route therefore exercised both
persisted columns and columns created from scratch; the manifest's `cold` label
does not mean that no terrain was read from disk. `disk_light=1` means a saved
light payload exists, not that the column is eligible for trusted-light reuse:
that decision also checks the separate `column_light.json` completion flag.
The data proves mixed provenance, not that either source caused the dark
rendering. The fixed-day wrapper restored `World_164/world_data.json` to the
pre-flight SHA-256 `0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`;
newly persisted terrain slice files are expected flight output.

M405 artifacts: [analyzer report](../../bin/suite_reports/engine_refactor/m405_world164_m335_unload_veto_budget_20261005.json),
perf JSONL `bin/logs/perf_20261005-154651_38480.jsonl`, rotated INFO logs
`bin/logs/Cubatarium.exe*.INFO.*.38480`, and sampled frames in
`bin/logs/m405_world164_m335_unload_veto_budget`.

### M406 implementation and acceptance criteria

The next change treats the streamer's existing keep-ring/capsule decision as
the residency boundary. When a candidate leaves that set, it invalidates its
generation, mesh/GPU, collision, ColumnFlow, disk-load, and queued relight
owners, then saves/removes the column. If relight work is interrupted, it
clears the persisted light-complete flag so a later disk load recomputes light
instead of trusting an incomplete map. M406 records the number of evictions
that invalidated active work. This replaces indefinite pending-token residency
with explicit cancellation at the far boundary; in-flight relight results
remain guarded by world epoch and chunk read-set/incarnation validation.

Build only Release, then run M406 with the exact M335 route, day, camera,
duration, and GUI capture profile used by M405. Compare unload candidates,
vetoes, active-work invalidations, resident-set peak/end, disk/procedural
source events, dark/unfinished visual debt, and frame/stream/emerge timing. Do
not call this visually fixed until sampled pixels improve and visual debt
converges during the post-flight stop phase. Keep the periodic new-world check
as a later secondary gate; it does not replace the repeatable M335 route.

## M406 — residency recovered; streaming latency and visual debt remain (2026-10-05)

M406 ran the same visible no-teleport M335 flight on Release commit
`2f249a5a`. It completed successfully (`run_outcome=success`, `process_rc=0`,
`hang_killed=false`) and restored `bin/worlds/World_164/world_data.json` to its
baseline SHA-256 `0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`.
At the time of the user's freeze report, Windows still reported the window
responsive and captures/logs advanced. The process had frame spikes up to
1.55 s, so the visible stop was real, but a Windows-level hang was not
confirmed then. Windows briefly marked the window not responding only near
the planned stop/exit phase; the process was not killed and exited with code
0. This late signal is distinct from a permanent hang.

The unload/residency change worked: across period rows M406 had 289 unload
candidates, zero vetoes, 201 candidates that invalidated active work, and
1,173 removed chunk slices. The resident chunk-count series ranged from 248 to
573 (median/end 432), versus M405's 13,779 final and 13,841 peak slices. Save
logging recorded 6,152 unique queued columns, 25,147 successful slice writes,
no `slice_failed` events, and no repeated queued column. This closes the
specific unbounded-residency/save-veto failure observed in M405.

The visual/performance gates still fail. The analyzer saw 1,375 periods
(1,373 steady), 79.082 ms median wall time, and a 67.36 ms median streaming
phase. `unfinished_visual` remained nonzero in every steady period. The near
void proxy peak fell from 7,836 in M405 to 3,603, and the visible-dark proxy
peak moved from 29 to 26, but the stop gate still reported missing visual
work and effective holes. These are readiness and draw-state signals; they do
not prove that voxel data is absent.

The source trace shows both persisted and new terrain: 3,382 disk loads were
queued and completed, 2,884 requests missed disk, and 2,878 procedural
columns committed. Physical reads were fast (median 1.04 ms, p95 1.50 ms,
maximum 7.43 ms). Queue/application work was not: disk-result wait for one
column was 400 ms median and 8.38 s p95 when summed across its slices; a
single-slice wait had 2.09 s p95 and 24.90 s maximum. The completed-load queue
had 24 entries at p95 and peaked at 458. Combined deserialize/apply cost was
6.35 ms median, 11.63 ms p95, and 230.71 ms maximum (one slice reached
224.68 ms). Thus “disk load” is not equivalent to slow physical reads: ready
results can wait for main-thread consumption, and applying even one result can
occasionally exceed a frame budget by a wide margin.

Two separate long-frame signatures require work. A 1.093 s spike spent
1.037 s in `TickAsyncChunkIo`; another 1.552 s spike attributed only 280 ms to
the streaming phase (191 ms to unload) and left 1.265 s unaccounted by the
current phase telemetry. The latter cannot be assigned to the renderer or
storage from these logs. The Release report is
[`m406_world164_m335_out_of_keep_cancel_20261005.json`](../../bin/suite_reports/engine_refactor/m406_world164_m335_out_of_keep_cancel_20261005.json);
the raw perf stream is `bin/logs/perf_20261005-172424_2536.jsonl`, with rotated
INFO logs for PID 2536 and captures in
`bin/logs/m406_world164_m335_out_of_keep_cancel`.

### M407 — bound and attribute the streaming pipeline

1. Split load telemetry per frame into discovery/format lookup, file worker
   queue, read, completion-queue wait/depth, decode, world apply, column
   finalize, and light-flag persistence. Keep a separate wall-time residual;
   do not infer a cause from overlapping aggregate fields.
2. Bound completed work without losing ownership: cap or prioritize ready
   results and define how evicted/canceled results retire their column token
   and are retried. Replace repeated full-queue scans with a bounded priority
   structure or another measured policy. Preserve near-focus ordering and
   fairness for older work.
3. Move chunk decode off the frame thread. M406 shows that current async I/O
   workers read bytes, but `TickAsyncChunkIo` deserializes and mutates world
   chunks on the main thread. Keep world mutation on the owning thread, with
   per-frame admission measured in milliseconds and a safe plan for a single
   expensive slice that exceeds the budget.
4. Audit unload serialization separately. `RequestAsyncTerrainColumnSave`
   serializes each chunk before queuing file writes, so the disk write is
   asynchronous while part of the save cost is still synchronous. Capture an
   immutable chunk snapshot safely, then serialize/write it in the worker;
   validate that edits and eviction cannot race the snapshot.
5. Trace the 1.265 s unexplained stall at finer granularity (including waits
   outside the named streaming/render phases). Do not suppress it by labeling
   it as I/O or GPU time without evidence.
6. Build only Release and rerun the exact M335 route and GUI/capture settings.
   Require lower long-frame tails, bounded completion depth/wait, continued
   unload progress, and improving visual debt through stop. Keep a new-world
   run secondary and periodic after the repeatable route passes.

Only after frame-time and queue behavior are stable should the next change
target the remaining lighting/mesh convergence debt; M406 still had 1,403
relight FIFO drops and 49 false clears. Do not combine a queue fix with a
lighting policy change in one experiment, or the repeated route will no longer
isolate the cause.

## M407b — full-route source and pixel evidence; batch completion selection (2026-10-05)

M407's apparent 34-minute in-frame gap was caused by Windows sleep/lock; the
user confirmed that sleep or lock occurred. M407 ended after only about 13 minutes
of active movement and did not reach the far-flight acceptance region. Do not
classify it as an engine hang or a completed long-route run. M407b repeated the
same visible, no-teleport M335 route on Release commit `fc71fa13`, with
`flight_move_speed_scale=1`. It ran 2,800 flight seconds, produced 1,392 steady
periods, exited successfully, and was not killed. No system-sleep-sized frame
gap appeared. The fixed-day wrapper restored `world_data.json` to its baseline
SHA-256 `0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`;
terrain slices generated beyond the previous route extent are expected saved
output and make the next replay a mixed/warm-source run.

M407b did not pass acceptance: median flight wall time was 64.37 ms
(15.53 effective FPS), median streaming phase was 56.69 ms, and stop-phase
visual work did not converge (25 unresolved/not-ready items remained; dirty
mesh work ended at 138). The 20-second stop phase is too short to clear this
debt. The source trace counted 6,153 completed disk slice loads and 1,332
procedural commits after disk misses. Physical file reads were fast (3.01 ms
median / 7.56 ms p95); decode was 3.41 ms median and apply 2.80 ms median.
The slow part was service delay: completed-result wait was 692 ms median and
5.55 s p95, with a 70.17 s maximum; the ready-load queue peaked at 318. New
terrain generation took 90.68 ms median / 138.19 ms p95, while its scheduler
queue reached 176 ms p95 and 9.77 s maximum. The worker-pool queue remained
small (0.03 ms median / 0.15 ms p95) with four workers. This separates disk
throughput from delayed result service and procedural request scheduling.

The long flight's sampled framebuffer probes do not establish that the dark
appearance is under-lighting: 303 of 32,768 samples were below luminance 32,
but every one had a valid depth surface and visible MDI draw. 273 had a nearby
source-face light join; most low-luminance voxel hits were grass or tree logs
with sky light present. The ten opaque-DDA misses corresponded to cutout leaves,
which `TraceOpaqueVoxelRay` intentionally skips. These are material and probe
semantics, not evidence of ten missing chunks. No PNG capture directory was
enabled for M407b, so the sampled points could not be reviewed against full
frames. The analyzer's `holes_rate=1.0` uses `unfinished_visual` as a readiness
proxy; it does not mean every frame or chunk was literally blank. New reports
now identify that signal's semantics explicitly. Do not accept the visual
issue as fixed from these samples.

The route also recorded 963 relight-FIFO drops and 51 false-clear increments,
but dark-face-near counters were zero and the sampled dark faces were mostly
lit. Keep those light-policy counters as a separate lead; do not combine a
light-policy change with the next load-queue change.

### M408 — rank ready disk results once per tick

M407b confirms the M407 issue with repeated completion-queue scans: the ready
queue reached 318 entries while `TickAsyncChunkIo` repeatedly selected one
best item at a time, rebuilding near-focus/age rank calculations on each scan.
M408 computes one rank key per queued result, partially orders a maximum of
four near-focus results in one locked batch, applies only within the existing
per-frame slice and millisecond budgets, and requeues any unconsumed results.
World mutation remains on the owning thread. Compare streaming-phase p50/p95,
ready queue and result-wait distributions, and stop-phase debt against M407b.

M408 used the same visible, no-teleport M335 route and capture settings, with
`CUBA_FLIGHT_CAPTURE_DIR` set before app launch. Source logs, rather than the
manifest's `cold_warm_mode`, determine the actual disk/procedural mix. A
separate new-world run remains necessary to measure first-generation
scheduling and stays secondary to the repeatable route.

### M408 results — faster streaming phase, visual debt remains (2026-10-05)

M408 ran Release commit `273c796f` for the full 2,800-second route and exited
successfully (`process_rc=0`, `hang_killed=false`). The keep-awake helper ended
with the app. The user confirmed that the earlier M407 34-minute pause came
from system sleep/lock, not the engine. The route covered 13,264 blocks
(about 4.74 blocks/s), close to M407b's 13,104 blocks, so the newer run did
not reproduce the earlier super-fast-flight concern. It saved 189 PNGs.

Batch ranking improved the measured frame path: median flight wall time fell
from M407b's 64.37 ms to 56.11 ms (15.53 to 17.82 effective FPS), and the
streaming phase fell from 56.69 ms to 47.33 ms. Mesh-emerge time also fell
from 22.82 ms to 18.36 ms. These gains did not pass visual acceptance:
`unfinished_visual` remained nonzero for every steady period, median
`chunk_not_ready`/missing-resident debt was 27, and the 20-second stop ended
with 25 unresolved items. The stop did drain debt (not-ready −53 and dirty
mesh work −136), but did not converge to zero. `holes_rate=1.0` remains the
readiness proxy documented above, not a literal claim that every framebuffer
frame was blank.

The source trace counted 7,377 disk slice completions and 198 procedural
commits after disk misses. Disk reads were not the limiting stage: file-read
time was 0.98 ms median / 2.49 ms p95; decode was 3.15 ms median and apply
2.53 ms. Completed disk results still waited 585 ms median / 4.15 s p95, and
the ready-load queue reached 446 entries. This is a mixed/persisted replay;
the manifest's `cold` label does not supersede the source trace. The measured
result-wait maximum (68.1 s) is accumulated per-column service delay, not one
frame stall. The next change should not target file reads or decoder speed.

Full-frame captures did not show a large all-black view on the sampled route
segments. The pixel trace found 277/32,768 probes below luminance 32 (303 in
M407b); every probe had a valid depth surface and visible MDI draw, and 257
had sky light 1. At the broader 96 threshold, M408 had 1,840 dim probes versus
1,781 in M407b; all still mapped to visible MDI surfaces. Near dark-face
counters stayed at zero. This does not dismiss the reported dim appearance:
these are sparse probes, several dark pixels do not join to a source face,
and block material IDs have not yet been mapped to names.

The next investigation is the gap between broad visual-debt counters and the
actual camera band. In M408, `focus_visual_missing_mesh` had median 27 while
`focus_data_camera_band_solid_no_drawable_n` had median 0 (maximum 2); the
broader vertical band had median 90 solid slices without a drawable mesh.
Before tuning another queue or light budget, M409 should record the coordinates
and vertical slices behind the missing-resident census and correlate them with
screen depth/coverage. Determine whether the persistent debt is in the camera
band or in below-camera/occluded slices, then change the owning stage. Keep
the exact M335 route, speed, visible GUI and stop parameters.

Artifacts (ignored `bin/` outputs): [M408 acceptance report](../../bin/suite_reports/engine_refactor/m408_world164_m335_batch_ranked_disk_results_20261005.json),
[pixel trace, luma <32](../../bin/suite_reports/engine_refactor/m408_renderer_pixel_trace_20261005.json),
[pixel trace, luma <96](../../bin/suite_reports/engine_refactor/m408_renderer_pixel_trace_l96_20261005.json),
[source trace](../../bin/suite_reports/engine_refactor/m408_source_trace_20261005.json),
and perf stream `bin/logs/perf_20261005-210152_32396.jsonl`. M408 used INFO
logs rolled under PID 32396. The matching M407b reports are above.

### M408 deep trace review — readiness is not draw evidence (2026-10-05)

The raw perf stream contains 8,192 screen-ray rows: 5,258 hit resident opaque
voxels, 2,534 reached an unloaded slice, and 400 found no opaque hit in range.
The selector marked 1,171 resident-hit rows as repair candidates and selected
816. Candidate creation requires an opaque hit (`hit.state == 1`); unloaded
ray results are skipped. This trace measures repairs for known solid geometry.
It cannot tell whether an unloaded region should contain terrain.

The reported `draw_oracle_missing_resident_n` is not an independent rendering
oracle. `AccumulateDrawOracleFromVbCensus()` assigns it directly from
`unfinished_visual`, and `WorldStreaming.cpp` copies that value into
`PhysicsTelemetryData.DrawOracleMissingResidentN`. Comparisons of those two
fields repeat the same loaded-column/readiness census. Keep framebuffer,
screen-depth, mesh-ownership, and readiness evidence separate until a real
object-ID or expected-surface oracle is connected.

The bounded frustum trace captured 256 examples, including 122 `no drawable`
candidates and 126 ready refs sampled for their post-cull MDI state. These are
candidate examples, not counts of missing screen pixels: chunk AABBs can
intersect the frustum while their contents are occluded, and accepted-empty
interior slices need no draw command. The 9 camera-band unowned peak samples
are more actionable: at frame epoch 52,523 they were resident solid slices at
`x=-821..-819`, `y=3..4`, with 1–32 non-air blocks, mesh revision 0, no dirty
queue entry, no demand geometry revision, and no ColumnFlow ticket. They were
lit-ready, but the one-frame peak trace does not establish that these
coordinates caused a visible pixel defect.

Reviewing captures also changes how empty-looking frames should be read.
Frames 88 and 148 show mostly sky/ocean and a hazy horizon; frame 188 shows a
nearby forest canopy, sand, and water. M335's established RD4 fog reaches full
blend at roughly 36 blocks, so the distant blue field is consistent with the
configured fog and is not by itself evidence of unloaded terrain. Dark foliage
and sparse low-luminance probes need material/source-light correlation before
they can be classified as a dark chunk.

The reusable `tools/analyze_visual_coverage_trace.py` summarizes focus,
screen-ray, frustum, and camera-band peak rows without loading the 500+ MiB
JSONL file into memory. M409 keeps the exact visible/no-teleport M335 route and
uses a compact serializer for sample-kind-10 screen rays. Functional
remediation remains gated on an exact visible surface/depth mismatch; do not
raise queue or lighting budgets based only on `unfinished_visual`.

Reproduce the M408 deep-trace summary after shutdown with:

```powershell
py tools/analyze_visual_coverage_trace.py bin/logs/perf_20261005-210152_32396.jsonl --json-out bin/suite_reports/engine_refactor/m408_visual_coverage_trace_20261005.json
```

The helper's source and trace artifacts remain in ignored `bin/` paths; only
the reusable analyzer and interpretation are tracked.

### M409 results — trace volume fell, acceptance still fails (2026-10-05)

M409 completed the same visible Release/no-teleport M335 route on
`World_164`: 13,472 blocks, median speed 5.19653 blocks/s, 189 captures,
`process_rc=0`, and no forced termination. The user later confirmed that the
34-minute M407 pause was caused by system sleep/lock. It is not an engine-hang
observation and must not be used as evidence for a streaming deadlock. M409
itself completed normally. The saved `world_data.json` was restored byte for
byte.

M409 did not pass renderer acceptance (`27/39` gates). Median flight wall time
was 58.12 ms, streaming phase 49.67 ms, and mesh emerge 19.49 ms. The
readiness proxy remained nonzero in all steady periods (median 27); the stop
phase drained pending/not-ready/dirty counts by 6/3/98 but did not reach zero.
This is a real unfinished-work signal, not a measured fraction of blank
pixels. M409's wall and streaming medians were slightly higher than M408's
56.11/47.33 ms. This is not a policy A/B: M409 only changed trace
serialization, and the persisted disk mix/system cache differ between runs.

The compact screen-ray serializer reduced raw perf output from 490.55 MiB in
M408 to 403.67 MiB in M409 (−86.88 MiB, 17.7%). Screen-ray rows alone fell
from 81.61 MiB to a compact form. Dense `renderer_pixel_probe` rows still use
193.63 MiB (48% of M409's log); `view_draw_gate_trace` adds 39.0 MiB. This is
an instrumentation cost, not a rendering improvement. Preserve dense pixel
coverage while making future serialization smaller or explicitly measuring
the overhead.

M409 recorded 7,467 queued disk slices and 7,467 completions, with no
out-of-range cancellations. Typical storage work remains inexpensive:
file-open median/p95 2.10/3.38 ms, file-read 2.92/7.02 ms, deserialize
3.21/4.09 ms, and apply 2.66/4.59 ms. Worker-queue wait was 9.64/32.33 ms
median/p95; completion-to-apply wait was 587 ms/3.09 s, with a 61.79 s
maximum and a ready-load high-water of 292. Those tails show admission and
result-service delay, not a generally slow disk. The maximum is accumulated
column service time, not one 61-second frame stall. Compare the ready queue,
result wait, and cancelled/stale work in the next repeat before changing the
four-result ranking or apply budget.

For the 225 procedural disk misses, terrain generation stayed stable at
84.06 ms median / 118.88 ms p95. Worker-pool wait was only 0.029/0.053 ms,
while request-to-scheduler-start wait reached 9.36 ms median, 255.89 ms p95,
and 3.06 s maximum. The tail is before execution in the frame-driven
`ChunkLoadScheduler`, not a shortage of generator worker threads. Do not raise
worker count or generation concurrency based on the frame wall alone; first
measure how often requests age while admission is throttled and whether they
remain inside the moving retention ring.

The source trace also caught one synchronous cold-index build: the first disk
column request at `(7,0,3)` spent 286.18 ms in `GetHighestChunkSliceOnDisk()`
while it enumerated the world's chunk directory. Only 2 of 15,159 column
discovery calls exceeded 1 ms; p95 was 0.0288 ms. This is a one-time entry
hitch, not the long-flight cause, but directory enumeration still runs on the
world thread and should be moved into the existing asynchronous world-load
phase (or replaced by a persisted/incremental index) before expanding the
far-flight workload. Preserve correct handling of old worlds without an
index.

Dense pixel evidence does not connect M409's sampled dim pixels to absent
terrain. All 269 probes below luma 32 had a valid depth surface and visible
MDI draw; 245 joined to a source face. Of those dark probes, 200 had settled
light with sky=1 and no preview marker, 18 had settled sky=0, and 41 used a
preview marker. A representative far-route sample at camera `x=-13,346`
showed RGB `(25,33,23)`, a depth surface in chunk `(-836,3,3)`, matching mesh
and published geometry revision 11, settled light revision 1, sky light 1,
and no preview. The voxel ray's hit was more than two blocks from the depth
surface, so it cannot explain that pixel. This points the next investigation
toward the exact surface material, shader albedo/light/fog factors, and
same-pixel depth join; it does not prove that every operator-reported dim
region is expected. At the same time, focus/frustum/camera-band traces retain
unready or no-drawable samples and the long-flight stop still fails. Keep
streaming debt as an open issue and do not turn the dim-pixel count into a
missing-chunk count.

The cold directory-index scan is now warmed asynchronously before world use
(see the M410 result below). The next work remains evidence-led:

1. Verify the asynchronous index warmup on a second existing world and on a
   world without a prebuilt index; keep legacy discovery correctness intact.
2. M410 maps most sampled low-luminance depth surfaces to the actual
   `tree_leaves` material. Do not relax light settlement or alter shader
   lighting based on those samples. Compare leaf texture/albedo, fog and
   neighboring opaque terrain against operator reports to distinguish
   expected dark foliage from the remaining dim-region symptom.
3. Streaming still has a long result-to-apply tail and stop convergence fails.
   Instrument the age and ownership transitions from disk completion through
   the main-thread apply and first drawable mesh, along with retention and
   stale-result reasons. Make a queue-policy change only after that chain
   identifies where work stops progressing; do not raise quotas from
   `unfinished_visual` alone.
4. Keep the exact visible/no-teleport M335 route on `World_164` as the primary
   repeatable gate. Run the cold new-world generation case periodically as a
   secondary workload.

### M410 results — index hitch removed; streaming readiness still open (2026-10-06)

M410 used the unchanged visible Release/no-teleport M335 route on `World_164`.
It reached 13,488 blocks at 5.19653 blocks/s, saved 189 frames, exited with
`process_rc=0`, and was not force-killed. The world metadata was restored
byte-for-byte. The earlier M407 34-minute pause was confirmed by the user to
be system sleep/lock, so it is not an engine-hang observation.

The M409 cold directory-index hitch is fixed for this path. M409's first
`GetHighestChunkSliceOnDisk()` call took 286.18 ms; after asynchronous index
warmup M410 measured 0.0141 ms on first discovery. Across 7,578 discoveries,
the median was 0.0135 ms, p95 0.0286 ms, max 0.2061 ms, with no calls over
1 ms. This validates removal of the scan from the live request path; a second
world and an unindexed-world case remain to be checked.

The long-flight acceptance still fails 12/39 gates. Median flight wall time
was 56.90 ms, streaming phase 48.07 ms, mesh emerge 20.22 ms, and median
`unfinished_visual`/`chunk_not_ready` was 27. The stop phase ended with 26
not-ready items and 140 focus-dirty chunks; pending/not-ready/dirty deltas
were -5/-18/-122. The 10 stop samples did not converge, so the unchanged M335
route remains a failing renderer/streaming gate. These counters describe
unfinished readiness and work; they do not alone prove visible blank pixels.

All 7,578 disk slices queued in M410 completed, with no cancellations. File
read median/p95 was 0.98/1.32 ms; deserialize 3.18/4.02 ms; apply 2.66/5.12
ms. Completed-result wait remained high at 627 ms median, 12.76 s p95 and
62.18 s maximum; the ready-load queue peaked at 376. These are accumulated
queue/service ages, not single-frame stalls. The 123 procedural misses had
84.81 ms median generation and negligible worker-pool wait (0.03/0.084 ms
median/p95); request-to-scheduler-start wait was 10.53 ms median, 155.18 ms
p95 and 549 ms maximum. This points follow-up toward result ownership,
admission and main-thread apply progress, not simply more worker threads.

M410 sampled 261 pixels below luma 32. Every sample had a valid depth surface
and visible MDI draw; 243 joined to a source face. Source-face IDs mapped 229
to `tree_leaves` (`leaves_opaque.png`), 22 to `tree_log`, 2 to `tree_bark`,
and 8 were unknown. The leaf samples had median luma 28.65. Many had settled
sky light 1 and no preview marker. At luma below 96, 1,634 samples included
1,492 source-face joins, led by leaves (1,138), grass (203), and logs (172).
The sampled low-luminance signal therefore does not establish a missing chunk
or unsettled lighting. It also does not disprove every operator-observed dim
region; material identity needs to be compared with the affected views.

Coverage traces recorded three unowned no-drawable slices at peak. Their X
coordinates were five chunks behind focus, outside the five-chunk retention
ring, so they are not evidence that the current camera view lacked terrain.
Camera-band no-drawable peak was 17, mostly dirty queue-owned work. Frustum
candidate rows remain samples, not counts of screen holes. Keep these
evidence types separate from screenshot/pixel witnesses.

The Release target build succeeded with
`cmake --build bin --config Release --target Cubatarium --parallel 8`.
No tests were run. The M410 route and analysis artifacts are linked below.

Artifacts (ignored `bin/` outputs): [M410 acceptance report](../../bin/suite_reports/engine_refactor/m410_world164_m335_async_disk_index_20261005.json),
[pixel trace <32](../../bin/suite_reports/engine_refactor/m410_renderer_pixel_trace_20261005.json),
[pixel trace <96](../../bin/suite_reports/engine_refactor/m410_renderer_pixel_trace_l96_20261005.json),
[visual coverage](../../bin/suite_reports/engine_refactor/m410_visual_coverage_trace_20261005.json),
[source trace](../../bin/suite_reports/engine_refactor/m410_source_trace_20261005.json),
and raw perf `bin/logs/perf_20261005-235830_42324.jsonl`. Captures are in
`bin/logs/m410_world164_m335_async_disk_index/`.

### M411 low-trace replay — diagnostics affect frame cost; readiness unchanged (2026-10-06)

M411 repeated the same visible/no-teleport M335 route with pixel/source trace
flags disabled, retaining ordinary flight metrics and 15-second GUI captures.
The process completed 864 chunks (13,824 blocks), returned `process_rc=0`,
was not killed, and saved 189 captures. `world_data.json` was restored to the
same SHA256 as before the run. The GUI screenshots show nearby forest at the
start and a blue, foggy ocean/distant silhouette at the mid-route and end;
these selected frames contain no full-screen black render. This does not
exclude a transient hole or explain every dim-region report.

Median flight wall time was 52.94 ms, streaming phase 44.33 ms, and mesh
emerge 17.99 ms. Relative to traced M410, these were lower by 3.96, 3.74 and
2.23 ms respectively. The raw perf log was 50.25 MB versus M410's 424.48 MB,
and the recorded spike count was 336 versus 924. The lower median and 8.4x
smaller log are consistent with measurable dense-trace overhead, but do not
isolate it from the changed persisted chunk mix and system file cache.

The M335 acceptance still failed 12/39 gates. Median unfinished/not-ready
debt stayed at 27, dirty median at 158, and stop convergence failed: stop-end
not-ready was 27, focus-dirty 143, pending median 13, with pending delta +6.
Lower trace overhead therefore did not resolve the streaming/readiness
problem. Keep the lightweight replay for baseline performance, and run dense
pixel/source traces only when a specific renderer attribution question
requires them. Do not equate the readiness gate with a blank-pixel oracle.

Artifacts (ignored `bin/` outputs): [M411 report](../../bin/suite_reports/engine_refactor/m411_world164_m335_low_trace_20261006.json),
raw perf `bin/logs/perf_20261006-010419_12320.jsonl`, and 189 captures in
`bin/logs/m411_world164_m335_low_trace/` (representative: [start](../../bin/logs/m411_world164_m335_low_trace/frame_000.png),
[mid-route](../../bin/logs/m411_world164_m335_low_trace/frame_094.png),
[end](../../bin/logs/m411_world164_m335_low_trace/frame_188.png)).

### Readiness-count semantics exposed by the M410/M411 review

`unfinished_visual` is a count of focus-ring columns classified by
`ClassifyFocusColumnVisual`, not a count of missing screen pixels. In
particular, `MissingMesh` remains unfinished until every resident solid slice
in the presentable band has a first mesh. The render state is progressive: a
drawable Y slice can keep part of a column visible while another resident
slice still carries this first-mesh obligation. Such a column can therefore
contribute to the readiness debt without being an all-blank column in the
frame. Keep this invariant; report camera-band no-drawable slices separately
and join only exact depth/pixel witnesses when assessing a visible hole.

Artifacts (ignored `bin/` outputs): [M409 acceptance report](../../bin/suite_reports/engine_refactor/m409_world164_m335_compact_screen_rays_20261005.json),
[pixel trace <32](../../bin/suite_reports/engine_refactor/m409_renderer_pixel_trace_20261005.json),
[pixel trace <96](../../bin/suite_reports/engine_refactor/m409_renderer_pixel_trace_l96_20261005.json),
[visual coverage](../../bin/suite_reports/engine_refactor/m409_visual_coverage_trace_20261005.json),
[source trace](../../bin/suite_reports/engine_refactor/m409_source_trace_20261005.json),
and raw perf `bin/logs/perf_20261005-224948_35096.jsonl`. Captures are in
`bin/logs/m409_world164_m335_compact_screen_rays/`.

### M412 peak-synchronized probe — readiness debt is not a screen-hole count (2026-10-06)

M412 replayed the exact visible/no-teleport M335 route on `World_164` using
the Release executable from `0a3ae7b0`. It completed 861 chunks, saved 189
captures, returned `process_rc=0`, and was not force-killed. The world file
was restored to SHA256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`.
The user confirmed that M407's earlier long gap was caused by system
sleep/lock; keep-awake was enabled for M412.

The long-flight gate remains open: M412 passed 28/39 checks, with median
frame wall time 52.87 ms, streaming phase 44.24 ms, mesh emerge 18.65 ms,
median unfinished readiness 27, and no stop convergence. One focus-column
miss remained for 32 seconds (16 frames) at `(-706, 3)`. The report recorded
zero `visible_black_focus_n`, but this is not a full-frame pixel oracle.

The new epoch trigger captured both camera-band peaks on the same frames:
17 no-drawable rows at epoch 46,021 and 8 unowned rows at epoch 38,725. Each
frame had 80 pixel samples, 59 depth surfaces, and 53/54 voxel-ray hits. The
frames had 11/2 samples below luma 32, all backed by sampled geometry; none
of those dark samples had both valid depth and a voxel-ray hit. None of the
target chunk coordinates had a depth-surface or voxel-ray hit. Most
depth surfaces were in the camera/focus chunks. This is consistent with
camera-band readiness samples that do not intersect the sampled screen area;
it does not rule out holes between the sparse rays or explain every dim
region.

Neither peak frame produced a `view_frustum_coverage_trace` row, despite the
same-frame pixel capture finding opaque depth surfaces in the scene. This is
an unresolved diagnostic inconsistency, not proof that the view had no
geometry. Before using frustum/camera-band counts to change queue policy,
emit an explicit frustum-probe summary even when its candidate list is empty,
then compare its AABB/plane decisions with the depth-surface chunk set. Keep
first-mesh readiness, camera-band debt, geometric frustum results, and
sampled pixels as separate signals.

M412's `CUBA_VISUAL_BLACK_TRACE` log was 412.60 MB, while low-trace M411 was
50.25 MB. Median frame wall time was similar, but the runs are not a clean
trace-overhead A/B because their persisted chunk/cache mix differs. Use
low-trace M335 for performance baselines and enable detailed probes only for
targeted attribution. M412's report says `visible_black_focus_n=0`; this and
the selected PNGs do not close the operator-reported dim-chunk issue.

Artifacts (ignored `bin/` outputs): [M412 acceptance report](../../bin/suite_reports/engine_refactor/m412_world164_m335_peak_sync_20261006.json),
[same-frame camera-band/pixel join](../../bin/suite_reports/engine_refactor/m412_camera_band_pixel_join_20261006.json),
raw perf `bin/logs/perf_20261006-022416_22260.jsonl`, and 189 captures in
`bin/logs/m412_world164_m335_peak_sync/` (representative: `frame_141.png`).

### M413–M415: exact-frustum membership still needs screen-space attribution (2026-10-06)

M413 repeated M335 with the first explicit per-frame frustum summary. The
flight itself completed normally (860 chunks/13,760 blocks, 189 captures,
5.19653 blocks/s, `process_rc=0`, no forced kill, and byte-for-byte world
restore), but only one summary epoch survived. The summary had been written
to the high-rate generic visual ring and was overwritten. M414 moved summaries
to their own bounded ring; its run retained 499 summaries and 43 frames with
sampled candidate rows. M414 completed 860 chunks/13,760 blocks at the same
speed, with median wall/stream/mesh-emerge times 52.06/42.83/18.01 ms. It
still failed the M335 acceptance report (27/39 gates, stop convergence
failed).

M415 added exact-geometric-frustum membership counts for each camera-band
high-water snapshot and replayed the same visible Release M335 route. It
completed 861 chunks/13,776 blocks at 5.19653 blocks/s, returned
`process_rc=0`, saved 189 captures, and restored the original world-data hash
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`.
The long-flight report remains FAIL (27/39 gates): median wall/stream/
mesh-emerge was 52.08/43.42/18.38 ms, median unfinished readiness was 27,
and stop convergence did not pass. The traced perf log was 392.63 MB, so its
timings are diagnostic-run measurements, not a low-trace performance control.

At the no-drawable peak (18 slices, epoch 50,110), all 18 target AABBs
intersected the exact camera frustum. At the unowned peak (8 slices, epoch
46,306), all 8 intersected it. Each matching frame had 80 sampled pixels;
the target chunks had zero sampled opaque-depth hits and zero voxel-DDA hits.
There were 5 dark pixels in the first frame (all with depth and DDA hits) and
none in the second. The view contained 506/428 resident non-air chunks and
214/173 exact-frustum candidates respectively. This rules out the simple
“all these peak slices were outside the camera volume” explanation, but does
not prove a visible hole: an AABB can be occluded, and the 4-by-20 sampler can
miss the projected area.

Next, emit one compact target-specific trace row for each peak slice, including
its projected screen rectangle and same-frame drawable, render-ready, CPU /
packed reference, MDI resident/visible, and runtime-cull state. Compare the
rectangle with the captured pixels or a target-directed screen probe. Use that
evidence to separate absent mesh, draw submission/culling, occlusion, and
sparse-sampler miss before changing streaming quotas, retention, or lighting.
Keep the exact M335 route as the primary repeatable gate and continue periodic
cold new-world checks.

Artifacts (ignored `bin/` outputs): [M413 report](../../bin/suite_reports/engine_refactor/m413_world164_m335_frustum_summary_20261006.json),
[M414 report](../../bin/suite_reports/engine_refactor/m414_world164_m335_frustum_ring_20261006.json),
[M415 report](../../bin/suite_reports/engine_refactor/m415_world164_m335_peak_frustum_membership_20261006.json),
[M415 camera-band/pixel join](../../bin/suite_reports/engine_refactor/m415_camera_band_pixel_join_20261006.json),
raw perf `bin/logs/perf_20261006-054508_8136.jsonl`, and 189 captures in
`bin/logs/m415_world164_m335_peak_frustum_membership/`.

### M416 — projected peak chunks overlap pixel probes but have no target mesh (2026-10-06)

M416 kept the established visible/no-teleport M335 route and added a bounded
per-target projected-AABB probe. The Release build and static executable check
passed. The 2,800-second flight completed 863 chunks / 13,808 blocks at
5.19653 blocks/s, saved 189 captures, returned `process_rc=0`, was not killed,
and restored the world-data hash to
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`. The
acceptance report still failed 11/39 gates and post-stop convergence failed.
Median wall / streaming-phase / mesh-emerge times were 51.65 / 43.13 / 17.65
ms, unfinished visual readiness was 27, dirty count was 157 (maximum 404),
and the miss-stuck maximum was 28 seconds. The approximately 412 MB detailed
trace is forensic evidence, not a low-trace performance control. No tests were
run. The user confirmed M407's long pause was system sleep/lock.

The join retained 91 target-specific render probes across the route. All 91
targets were resident, intersected the exact geometric frustum, and had a
valid projected AABB rectangle. The 4-by-20 pixel sampler landed inside 79
rectangles; 39 contained at least one opaque depth sample, but zero samples
were attributed to the target chunk and there were zero exact target voxel-ray
hits. At the latest no-drawable peak, 15 slices had 58 sampler hits and 26
depth surfaces in their rectangles; at the latest unowned peak, 5 slices had
21 hits and 10 depth surfaces. These measurements show that the sampler looked
inside most projected rectangles, but neither AABB overlap nor a neighboring
depth surface proves that a target surface should be visible there.

The ownership trace narrows the streaming/rendering failure. At the latest
no-drawable peak, every target had non-air data; 11/15 had a FirstMesh dirty
queue owner, including a representative at queue index 73, while 4/15 had no
dirty queue or ColumnFlow mesh ticket. At the latest unowned peak, all 5/5 had
no dirty queue, no ColumnFlow ticket, and mesh revision zero. Some rows still
reported the broad `work_pending` state because it includes relight or
post-dispatch repair cooldown; that state is not proof of an active mesh owner.
The bounded `first_mesh_frontier_trace` did not include these exact target
coordinates. This points to an ownership/admission or service-lifecycle gap
alongside a FirstMesh backlog, rather than a frustum/culler rejection. It does
not yet prove that each rectangle is an exposed visual hole. Captures showed
fog/sea and faint terrain at the start and endpoint; frame 120 also shows
large triangular shoreline/water artifacts, which should remain a separate
renderer defect to investigate.

The v8 pixel join also checks later probe frames for the same chunk coordinate.
For `(-173,3,2)`, 13 later CPU voxel-ray samples hit the target chunk: in 11,
GPU depth belonged to a nearer surface (by 3.1–21.7 blocks); in 2, the depth
hit matched the target chunk and DDA distance within 0.003 blocks; none had a
farther depth surface or a renderer gap. A `ScreenRayRepair` row at epoch
16,500 reported that same slice drawable and mesh-satisfying (mesh revision 5,
published geometry revision 4) with geometry debt promoted into priority
RemeshQ at index 13/252, age 41 frames. The earlier missing drawable therefore
resolved at least to a drawable predecessor; that observed period does not
prove an exposed missing pixel.

Photometric evidence is separate. Of 32,768 same-route pixel samples, 287
were below luma 32 and 1,815 below 96. Every one had opaque depth and a
draw-ready drawable surface; none had pending light. All 251/251 dark samples
and 1,670/1,670 dim samples with a valid face-light witness had matching
published/field light revisions; median sampled sky light was 1.0. There were
zero voxel-ray gaps among the 280 dark-sample ray hits and 2 among 1,764 dim
sample ray hits. The low-luma source IDs were led by `tree_leaves` (572: 229
very dark, 1,283 dim) and `tree_log` (573: 18 very dark, 159 dim). This agrees
with earlier M380/M381 pixel audits: most sampled dark foliage is drawn, lit
geometry, not a missing mesh or outstanding light repair. M416 did not record
the actual per-fragment fog factor, so texture-versus-fog attribution remains
open; add fog state/factor, preview, precipitation, and wetness to the pixel
trace before changing visual policy.

**Next:** trace the same target coordinates through demand creation, concrete
owner acquisition, scheduler dequeue, snapshot admission/defer, build attempt,
completion validation, GPU apply, and owner release. Include queue age/index,
admission rejection reason, active attempt/stage, relight cooldown state, and
world/incarnation/revision identity so a temporary cooldown cannot masquerade
as a live owner. Compare the queued and ownerless targets before changing
quotas. If ownership disappears without publication, repair that lifecycle;
if queue entries remain starved, make one narrowly scoped fairness change and
repeat M335. Keep repeatable `World_164` flights as the primary gate and retain
periodic cold-world checks.

Artifacts (ignored `bin/` outputs): [M416 acceptance report](../../bin/suite_reports/engine_refactor/m416_world164_m335_peak_screen_coverage_20261006.json),
[M416 camera-band/pixel join](../../bin/suite_reports/engine_refactor/m416_camera_band_pixel_join_20261006.json),
raw perf `bin/logs/perf_20261006-065527_31132.jsonl`, and 189 captures in
`bin/logs/m416_world164_m335_peak_screen_coverage/`.

## M418 update — fog explains far fade; mesh service remains the readiness bottleneck

M418 repeated the established 13,584-block M335 route on `World_164` after
M417 was interrupted by Hibernate/reboot. It completed normally, restored the
world file byte-for-byte, and retained all 189 captures and the full pixel
trace. The report's process outcome is success; visual acceptance still fails
12/39 gates and stop convergence fails 5/12. Do not read `unfinished_visual`
or `effective_holes_rate` as direct pixel evidence: the report explicitly
defines that signal as readiness debt.

### What the new evidence separates

- **Pale blue far views:** M335 disables adaptive FogPullIn, but the base
  distance fog remains. The captured fog range was start 17.28 / end 36 blocks,
  consistent with 4 effective render chunks and the 28-block end margin. Of
  17,608 depth-valid pixel samples, 7,807 had fog factor above 0.9 and were
  close to the captured fog color (median distance 8.58 RGB units; factor/color
  distance correlation -0.837). Treat the distant pale/empty appearance as
  fog-dominated on this route, not proof of missing chunks. First verify the
  product render-distance target and effective-distance policy before changing
  fog or streaming behavior.
- **Sampled dark patches:** all 280 pixels below luma 32 and all 1,781 below
  luma 96 had opaque depth and draw-ready geometry, with no pending-light work.
  All available light witnesses matched revisions (256/256 dark, 1,679/1,679
  dim). The 280 darkest probes had fog factor 0 and zero voxel-ray gaps; 244
  were leaves (block 572). This does not reproduce a
  dark missing chunk or stale-light patch; the low-luma sample is chiefly
  rendered foliage/material color. Pre-fog base RGB is still not captured.
- **Transient mesh readiness:** 18 no-drawable peak rows had resident non-air
  data, a FirstMesh dirty owner, and exact-frustum intersection; 7 unowned peak
  rows had no work owner. Same-epoch renderer probes found 17 still
  non-drawable and one already drawable/satisfying with two visible MDI
  commands, so peak and rendered state can straddle publication. Pixel
  rectangles had samples in 119/144 peak probes, but no same-frame target-depth
  sample or exact target voxel hit. Later route rays yielded 29 target hits:
  11 matched target depth, 18 were occluded by nearer surfaces, and none showed
  a farther-depth gap. These rows are readiness/ownership debt, not proof of an
  exposed pixel hole.
- **Long-route throughput:** first/middle/last 400-period median wall time was
  40.19 / 56.98 / 67.72 ms. Stream phase rose 29.16 / 46.57 / 59.94 ms and
  mesh emergence 10.87 / 19.14 / 26.85 ms, while GPU render time stayed near
  5–7 ms. `column_loaded_no_mesh_n` stayed near 27 median, ColumnFlow deferred
  about 15 items while draining about 1, and async mesh work stayed near 10 of
  a 14-item peak. The dominant completion stall was `gpu_not_ready`, and the
  dominant wall stage was stream. This points to mesh/stream service and
  publication pressure as the current throughput work.
- **Disk versus procedural generation:** all 1,393 repeated-route period rows
  had zero disk-load completions and zero generator commits. M418 therefore
  does not test cold persistence or generation. Retain `World_164` as the
  primary deterministic gate, and run the same profile periodically on a
  cold/new world to exercise those paths.

### Revised work order

1. **Pin the visibility contract.** Record configured and effective render
   distance, fog end margin, and fog range in the report. Confirm whether four
   chunks is intentional for product visuals. Do not increase view distance
   until mesh/stream throughput is understood; a larger radius multiplies
   mesh work and can worsen the measured late-route slowdown.
2. **Make long-flight evidence crash-resilient.** M417 lost its in-memory
   pixel/fog ring when Hibernate recovery ended in an unclean reboot (Windows
   logged Kernel-Power 41). Add bounded, deduplicated periodic trace
   checkpoints; avoid rewriting the entire ~445 MB ring on each checkpoint.
3. **Trace ColumnFlow and mesh publication throughput.** For the same camera
   band, record queue age/kind, drain/admission result, async build stage,
   GPU-ready wait, owner transfer, and completion/apply. Explain why the
   no-drawable ownerless slices lack an owner and why stream/mesh medians rise
   with distance. Compare first, middle, and late route windows before changing
   budgets or fairness.
4. **Repair confirmed ownership lifecycle defects.** If a resident non-air
   slice loses all FirstMesh/ColumnFlow/active-attempt owners before publication,
   restore exactly one durable owner and verify it through GPU apply. Do not
   change lighting based on the sampled foliage pixels.
5. **Run the repeatable route, then cold-world controls.** Keep the M335
   coordinates, yaw, pitch, time/weather, and speed unchanged for regression
   comparisons. Add periodic new-world runs with the same flight parameters;
   report disk-completion and generation-commit counts so persistence and
   procedural creation are actually covered.

M418 artifacts: [flight report](../../bin/suite_reports/engine_refactor/m418_world164_m335_fog_attribution_20261006.json),
[fog/pixel join](../../bin/suite_reports/engine_refactor/m418_camera_band_pixel_join_20261006.json),
raw trace `bin/logs/perf_20261006-102124_22924.jsonl`; see the M418 section in
`FLIGHT_EXPERIMENT_SCRIPTS.md` for command, full evidence, and captures.
### M420 follow-up and revised order

M420 used the pinned M335 visible no-teleport route for 2,800 seconds and
covered 13,472 blocks at a median 5.19653 blocks/s. It confirms the long-run
frame-cost trend (56.29 ms median wall, 47.40 ms streaming phase, 20.33 ms mesh
emergence, 7.06 ms render), but recorded no disk-load or generation completion.
Thus it is the repeated-world throughput gate only. `gpu_not_ready` and
`stream` remain dominant; raising fog/view distance would hide the service
problem and increase work.

The per-kind trace found a live ColumnFlow queue median of 26 (Relight median
21), while Relight dispatch median was 0. Treat this as a fairness/service
risk to investigate with queue age and same-frame visible-light evidence, not
as proof of a black chunk. The raw `unfinished_visual` gate counts readiness
debt, not framebuffer holes; effective hole blink rate and mid-corridor
visual-hole median were both 0, and sampled frames show nearby geometry.
Do not relax deadlines or increase budgets until age, class, visible demand,
and main-thread cost are separated.

The existing `async_io_ms` sample was overwritten by the complete
`TickAsyncChunkSystems` duration. Preserve it as a compatibility alias, and
report explicit `async_chunk_systems_ms` plus
`async_chunk_io_drain_ms` (main-thread time applying ready chunk-I/O results,
not worker disk latency). Add both fields to the throughput analyzer. The
M420 report is a valid Release baseline pinned to commit `afe4192f`, but its
clean-tree manifest gate failed because this instrumentation edit was in the
working tree after flight launch; future baselines must start after committing
and building.

Before another long repeated-world flight, run a 600-second M335 segment in a
fresh seeded world to measure load/generation performance. Build it from the
same `World_164` settings but omit `chunks/` and `chunks.json`: the marker alone
can classify an empty folder as persisted and would muddle the cold-path
control. Enable `CUBA_WORLD_COLUMN_SOURCE_TRACE=1` and screenshots. If that run
shows material startup, generation, or frame-time problems, address them first.
If the route is saved on exit, repeat the same segment on that world to measure
disk reads. Then run the full 2,800-second repeated-world baseline on the
committed Release build before changing ColumnFlow service policy.

### M421/M422 cold-start follow-up - prioritize entry convergence

M421 did not use its requested fresh world: the product replay path overwrote
`args.world` with `World_164`. M422 corrected and verified this in the report
manifest. The updated Release run loaded a genuinely cold metadata-only world
and generated 2,310,487 non-air blocks in memory, but spent about 891 seconds
inside `EnterLit` and ended before the route or first flight telemetry row.
The GUI remained responsive; this is an entry convergence stall, not a frozen
process or far-flight result.

During M422, total mesh dirty count peaked at 208 and remained roughly 85-121.
The 150-second fallback was intentionally blocked at dirty residual 106 because
the safety threshold is 32. At the last samples, underfeet was present and
visibility debt was zero, but the spawn mesh ring was still not ready; no
presentable timestamp was ever recorded. The repeated queue/GPU activity did
not make measurable convergence. Do not raise the cap or force InGame as a
standalone fix: that can hide actual missing near-camera geometry.

Revised immediate order:

1. **Instrument the stalled owner lifecycle in EnterLit.** Capture the dirty
   coordinate set or a bounded histogram by chunk Y/distance and age, plus
   FirstMesh/ColumnFlow ticket, active build, GPU publication, soft-defer, and
   validated-empty outcome. Record the same set at regular checkpoints so
   growth, drain, and re-enqueue can be separated. Add a terminal reason and a
   bounded checkpoint even when the world operation never enters gameplay.
2. **Find why the camera ring never becomes presentable on a fresh seed.**
   Correlate M422 `dirty_n`, `ring_not_ready`, `gate_miss_*`, `gate_done_n`, and
   relight/GPU counters with `CountPostLoadRingNotReady`,
   `IsSpawnMeshRingReady`, `HasMeshSatisfyingColumnReady`, and the column work
   owner. Specifically verify whether resident solid slices producing a
   validated zero-quad mesh are treated as completed empty results or remain
   FirstMesh dirty forever. Do not infer a render hole from readiness debt
   alone.
3. **Repair one proven lifecycle defect at a time.** Preserve the dirty-residual
   safety check until the ring has a correct presentability contract. After a
   fix, run a short visible cold-world M335-start control first, verify the
   manifest's world/seed and that EnterLit exits with a measured
   `first_presentable_ms`, then run a cold route segment. Only after that should
   the full 2,800-second `World_164` repeated route resume as the primary
   streaming/rendering regression gate.
4. **Keep the two evidence lanes separate.** M421 is a valid short repeated
   `World_164` route control (the report manifest's `cold` label is not a cache
   state claim). M422 is a cold world-start failure with zero route periods.
   Neither substitutes for the other; retain periodic fresh-seed checks after
   each established-baseline pass.

The harness and period-timer correction is committed as `afbd2572` and built
Release-only. Its new timer fields now use the same interval-mean aggregation
as `world_streaming_phase_ms`, so future phase comparisons are temporally
aligned. M422 artifacts and exact counts are recorded in
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m421m422---selecting-a-genuinely-cold-world-and-cold-start-stall-2026-10-06).

### M423-M426 follow-up - split presentability debt from retained remesh work

The new EnterLit samples show why neither global fast-exit nor simply waiting on
all queued work is adequate. M424/M426 have persistent camera-band no-drawable
slices with active/pending owners and dirty residuals near 100; the 150-second
fallback correctly remains closed under the current safety rule. M425 reaches a
different state: the sampled 52 solid camera-band slices are drawable and
satisfying, no camera-band mesh work is pending, yet the ring stays unready with
26-27 total dirty entries and an intermittent near-async blocker. Its oldest
dirty entry is itself drawable/satisfying while a priority remesh is active.
M425 ended before the 150-second fallback, so do not claim that it would exit
or that the cap would be the only blocker.

The next work is diagnostic plus a narrowly scoped readiness review:

1. **Identify exact near async blockers.** Extend the EnterLit checkpoint to
   record the coordinate, chunk Y, owner type (in-flight versus completed),
   dirty/queue trace, demand stage/revisions, and whether an existing drawable
   satisfies the slice for every async item that blocks the radius-2 spawn
   ring. Current `async_mesh_pending` is only a boolean; in M425 it toggled
   while the camera-band census reported no unpresentable or pending slice.
2. **Explain persistent no-drawable work in M424/M426.** For the exact
   camera-band no-drawable chunks, persist demand transition/attempt ids,
   queue age/index, `desired_geom_rev - published_geom_rev`, and terminal or
   rejection reason at checkpoints. Compare the oldest dirty owner with the
   current gate miss; they are not guaranteed to be the same chunk.
3. **Review the ring predicate against the existing retained-image contract.**
   `HasMeshSatisfyingColumnReady()` deliberately accepts an already drawable
   image while a replacement is being built, but the near-ring code also
   checks aggregate async and raw dirty state. Determine whether those raw
   blockers include out-of-band or already-satisfying slices. If so, narrow
   only those blockers to actual presentable unsatisfied slices; keep pending
   GPU/underfeet safety and keep no-drawable slices fail-closed. Do not change
   the 32-item residual fallback until the revised presentability predicate is
   proven on both the M425 and M426 shapes.
4. **Validate in stages.** First replay a fresh world beyond 150 seconds so a
   dirty residual below 32 can exercise soft settle naturally. Then use a fresh
   seed with residual above 32 to verify that unresolved camera-band slices
   still block exit and converge. After that, run the unchanged visible
   no-teleport M335 `World_164` route as the primary streaming/rendering gate;
   continue periodic fresh-seed checks as a separate lane.

M423-M426 traces were collected with visible GUI and no camera movement while
EnterLit was closed; they are startup diagnostics, not long-flight acceptance.
M423 omitted `CUBA_VISUAL_BLACK_TRACE` and is occupancy-incomplete. M424-M426
have valid census traces. See the experiment archive for exact reports, seeds,
limits, and logs. The M422/M423-M426 evidence and new ordering are reflected in
[`ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md`](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md#m423-m426---classify-cold-start-missing-meshes-versus-retained-dirty-work-2026-10-06)
and [`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m423-m426---cold-enterlit-owners-and-soft-settle-boundary-2026-10-06).

### M427-M428 update - return to the primary repeated-world gate

Commit `47088487` narrowed EnterLit's mesh blockers to unsatisfied,
camera-presentable work, while retaining fail-closed handling for missing
drawables and pending GPU/underfeet work. M427 was not at the M335 locus because
it lacked a per-world player file. M428 pinned the known start and stayed
stationary by telemetry, but its empty scenario and multiple EnterLit traces
make it a startup diagnostic rather than route acceptance. One trace records
first presentability at 5.17 seconds; a later trace still reports live
blockers and visibility debt, so the gate needs confirmation under the normal
route lifecycle.

Next actions:

1. Replay the unchanged visible, no-teleport M335 route on `World_164`, using
   the committed Release binary. Preserve the exact route, speed, start,
   yaw/pitch, capture configuration, and collision-detour defaults.
2. Classify any dark/empty-looking regions by aligned route position, black
   pixel census, visible-black candidates, relight state, chunk source
   (disk/generation/resident), and mesh readiness. Do not label the
   `unfinished_visual` counter as a framebuffer hole by itself.
3. If the route confirms this gate and the existing presentation is stable,
   retain the fix and periodically run a separate cold seed. If M335 regresses,
   correlate the exact coordinate and lifecycle counters before changing
   scheduling or startup safety policy again.

M427-M428 limitations and exact metrics are recorded in
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m427-m428---validate-the-presentability-gate-at-the-established-locus-2026-10-06).

### M429 readiness checkpoint - visual evidence is better; service and cold-start work remain

M429 has now completed the planned primary repeatable lane: visible Release,
no teleport, unchanged M335 route on `World_164`, 2,800 seconds and 12,880
blocks. Route adequacy, speed, process exit, and artifact capture all passed.
The user reports that the world currently looks sufficiently good. Sparse
framebuffer evidence supports that impression: none of 32,768 sampled points
was near-black (<16 mean RGB); all 294 points below luminance 32 hit a valid
depth surface on a drawable mesh with a visible MDI pass. This is positive
visual evidence, but the pixel ring samples only the late part of the route and
does not prove full-screen or continuous coverage.

| Plan area | State after M429 | What still blocks closure |
|---|---|---|
| Repeatable long route | **Complete for this checkpoint.** M335 route and expected speed passed on the clean Release binary. | Repeat after any service-policy change; keep the route parameters fixed. |
| Perceived rendering | **Promising, provisional.** User observation and sampled captures/pixels show no obvious black/empty regions. | Preserve route-wide pixel/capture evidence; samples are sparse and late-route only. Do not equate dark foliage/material with missing terrain. |
| Disk streaming | **Measured, generation not covered.** 2,175 disk columns completed; file-read median/p95 was 1.09/1.68 ms; no procedural commit occurred. | The result queue had long worker-finished-to-main-thread waits (655 ms median, 9.60 s p95, 60.61 s max); shutdown showed 28 ready slices and 7 pending columns. Diagnose queue service and ensure route-relevant work drains. |
| Cold-world entry/generation | **Open blocker.** M422 still has a reproducible `EnterLit` non-convergence; M427/M428 did not constitute a passing cold-route control. | Re-run fresh-seed entry after the `47088487` presentability change, preserving the no-drawable and underfeet safety gates; cover generation separately from saved-world reads. |
| Performance | **Open, current M429 value is diagnostic-only.** Median wall 61.63 ms and streaming phase 51.91 ms; `async_chunk_io_drain_ms` median 9.97 ms. | Dense pixel readback and source logging perturb this run. Get a low-instrumentation baseline, then profile queue ranking, result application, and finalization before changing budgets. |
| Plan readiness | **Ready to continue, not ready to close.** The next intervention target is specific enough to investigate. | Resolve post-worker result service and cold `EnterLit`; then re-run the established route and a periodic fresh seed. |

#### Updated next actions

1. **Separate queue wait, selection, and main-thread apply cost.** The source
   trace times result wait from worker completion to application; disk reads are
   fast, but p95 result wait is 9.60 seconds. `TickAsyncChunkIo` ranks the
   completed queue and currently takes at most four results per tick. For frames
   over 24 ms it uses a four-slice/4 ms target, and the near-stream-over-budget
   fallback uses one result/2.5 ms. M429's per-column `result_wait_ms` sums
   waits across slice results (655 ms median, 9.60 s p95, 60.61 s max); the
   worst individual slice wait was 195 ms median, 2.40 s p95, 15.20 s max.
   Median drain wall was 9.97 ms. Capture queue depth/oldest-ready age,
   selected/applied slices, budget hits, rank time, per-result apply/finalize
   time, and near/far priority at period boundaries. A single result or
   finalization may exceed the time target.
2. **Use the measurements to choose a bounded service design.** Check whether
   full ready-queue ranking, per-slice application, or column finalization
   dominates. If ranking is the cost, use a persistent priority structure or
   bounded candidate set rather than rescanning the whole ready queue. If apply
   dominates, split large column commits/finalization into incremental main-
   thread work. Preserve near-camera priority and stale-token cancellation;
   don't simply raise a global budget from one diagnostic run.
3. **Get a low-instrumentation timing control.** Replay the same M335 route and
   settings with dense pixel readback and verbose per-column source tracing
   disabled. This is a performance control, not a replacement for pixel-level
   visual diagnosis. Keep Release and collect the same phase timers.
4. **Return to the cold-start lane.** Use a new metadata-only world/seed with no
   `chunks/` directory or `chunks.json`, visible GUI, and the established M335
   start/camera settings. Confirm `EnterLit` leaves with an actual
   `first_presentable_ms`, then run a cold 600-second route to cover generation.
   Do not bypass the ring or dirty-residual safety checks unless all
   camera-presentable slices have a satisfying drawable or validated-empty
   result.
5. **Acceptance after a change:** Release-build, run the unchanged visible
   M335 repeated-world route, check route/speed and collision response, compare
   low-instrumentation frame phases, inspect captured visual samples, and then
   run a separate periodic fresh seed. The run report must not use
   `unfinished_visual` as a framebuffer-hole verdict.

M429's full evidence and limitations are recorded in
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m429---m335-long-run-visual-and-disk-result-audit-2026-10-06).

### M430 update - isolate instrumentation cost before changing streaming policy

M430 ran the unchanged visible Release route for 600 seconds with all optional
tracing and captures disabled. It completed normally at the configured
5.19653 blocks/s, reached focus X `-185` / 3,072 blocks, and had no blocked
movement substeps or ground contacts. Its median frame wall was 34.76 ms and
world-streaming phase 23.93 ms. `async_chunk_io_drain_ms` was still 7.97 ms
median, 11.62 ms p95, and 14.46 ms maximum. This verifies that a material
main-thread I/O-drain cost persists without M429's pixel readback or per-column
source logging.

M430 is deliberately only a near-route timing control: it stops before the
8,192-block checkpoint, has no pixel captures, and cannot establish late-route
streaming behavior. Its route and speed gates pass, but report `pass=false`
because product readiness/performance gates remain red; `holes_rate` still
means `unfinished_visual` debt. M429 remains the long visual-evidence run.

Updated next actions:

1. **Keep the policy unchanged and obtain a full low-instrumentation M335
   control.** Repeat the same route for 2,800 seconds with optional pixel and
   source traces disabled. This will show whether the drain cost and frame-time
   growth persist at far checkpoints without expensive forensic capture. Keep
   Release, speed, camera and world constant.
2. **Then instrument the result-service phases.** Add period counters for ready
   queue depth before/after drain, selected/applied slices, time-budget hits,
   queue-ranking time, result application and column finalization. Include the
   worst ready-result age if it can be sampled without scanning/locking the
   queue on the hot path. Use these measurements to choose between reducing
   queue-rank overhead and splitting expensive main-thread application.
3. After a bounded service change, compare both the unchanged long M335 route
   and a separate fresh-seed entry/generation run. Keep the M422 cold `EnterLit`
   blocker and current user-positive visual assessment as separate evidence.

M430 artifacts and the full caveats are in
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m430---low-instrumentation-m335-near-route-control-2026-10-06).

### M431 readiness checkpoint - the late-route cost is real; disk drain is not the whole cause

M431 completed the full visible, no-teleport M335 Release route on
`World_164` with optional visual/source traces and screenshots disabled. Route
speed and coverage passed: median `5.19653 blocks/s`, focus X `7 -> -864`,
`13,936` blocks, and the `8,192`-block checkpoint was crossed. The app exited
normally (`process_rc=0`, `run_outcome=success`, `hang_killed=false`); the
runner returned nonzero because product gates remain red (`27/39`, stop gates
`10/12`). Movement telemetry showed no blocked substeps or ground contacts.

The user's current positive visual assessment remains useful evidence. M431
itself had no screenshots or pixel probes, so it cannot confirm appearance at
the additional westward segment. Its `visible_black_focus_n`, `visual_holes`,
`unfinished_visual`, and `chunk_not_ready` fields are internal readiness or
candidate signals, not framebuffer measurements. M429's sparse pixel samples
remain the direct pixel evidence, and only cover late-route probes through a
shorter segment. Keep those evidence types separate.

The low-instrumentation phase breakdown shows that late-route cost is not a
dense-trace artifact. Grouped by focus X (`near >= -200`, `mid -600..-201`,
`far <= -600`), median frame wall was `34.95/50.50/67.78 ms`; world-streaming
phase was `23.85/42.86/61.58 ms`. The two largest growing components were
`async_chunk_systems_ms` (`12.10/21.36/30.36 ms`) and `mesh_emerge_ms`
(`8.82/17.00/26.10 ms`). By comparison, async chunk-I/O drain rose only from
`7.85` to `10.29 ms`; `update_streaming_ms` rose from `2.37` to `2.86 ms`.
This rejects the narrow claim that the late frame growth is explained mainly
by disk-result application. It does not identify the unaccounted work inside
the two larger phases or prove that performance caused a visible defect.

M431 also crossed into terrain absent from M429's route extent. The INFO log
contains 171 worker-side `ChunkPopulate` records at X `-850..-868`, with
`total_ms` median/p95/max `95.27/128.32/250.12`. The recorded costs are on
generation workers and cannot be added directly to main-thread frame time.
Because M431 did not enable `CUBA_WORLD_COLUMN_SOURCE_TRACE`, it cannot join
those calls to queue age, source selection, commit timing, or exact frame
pressure. The route also persisted new world state, so the next `World_164`
repeat may follow a different disk/generation mix.

Plan readiness is **ready for a targeted phase-breakdown change, not ready to
close**. The repeatable long route, speed, normal process exit, and collision
telemetry are now controlled. User-observed appearance is positive, but the
farther segment lacks pixel evidence; cold-world entry/generation remains a
separate open lane; and product readiness/post-stop convergence still fail
with 26 items not ready at the end. The `holes_rate=1` gate is based on
`unfinished_visual`, not an image-hole rate.

#### Next work, in order

1. **Split the unexplained main-thread phase costs.** Add low-overhead
   cumulative timers and interval aggregates for the major subphases inside
   `UWorldStreaming::TickAsyncChunkSystems` and `UWorldStreaming::TickMeshEmerge`.
   Existing counters already isolate `TickAsyncChunkIo`, relight drain/capture,
   selected mesh snapshot/scheduling/GPU stages, but those subtimers do not
   account for the far-route medians: `async_chunk_systems_ms` grows by about
   18 ms and `mesh_emerge_ms` by about 17 ms from near to far, while their
   currently exposed individual stages grow by much less. Preserve a residual
   bucket so timer sums can be checked against the enclosing phase.
2. Build **Release only**, then repeat the same full M335 route with optional
   dense pixel/source traces off to verify that the breakdown itself remains
   low overhead. Compare near/mid/far bands against M431; don't tune budgets
   until a dominant subphase is identified.
3. Use a separate fresh-seed world for the cold 600-second startup/generation
   lane with source tracing. Record metadata and chunk-store state before and
   after; do not let the newly generated M431 west edge stand in for a cold
   world. Repeat a saved-data segment separately to compare persistence.
4. If the user's dim/empty observation returns, capture the same established
   route with the sparse pixel/source diagnostics around both the M429
   previously sampled segment and M431's new west edge. Do not change route or
   camera conditions to improve presentation.
5. After a measured implementation change, rerun full M335, inspect the image
   evidence, check the post-stop residual, and periodically repeat the separate
   fresh-seed lane. Close the plan only when the image evidence, readiness,
   far-route performance, and cold entry are independently acceptable.

M431 artifacts and exact run settings are documented in
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m431---full-m335-low-instrumentation-control-and-generation-frontier-2026-10-06).

### M432 readiness checkpoint - route controlled; far hot-path census identified

M432 repeated the complete visible M335 route on the same world/build class.
The app and flight completed normally at `5.19653 blocks/s`, covering 13,824
blocks with zero collision blocks or ground contacts. The operator currently
reports that visuals look sufficiently good. This makes the repeatable route a
usable control and puts immediate effort on performance and readiness debt;
M432 had no pixel capture, so this flight does not independently certify the
newest route segment.

The new near/mid/far-east/far-west split shows steady late-route cost growth:
frame-wall medians are `37.06/53.65/64.84/72.83 ms`; async post-scheduler
medians are `12.28/21.12/26.41/30.85 ms`; mesh post-telemetry medians are
`2.13/10.20/16.33/19.99 ms`. I/O drain changes only `8.94 -> 10.40 ms`, and
the actual chunk scheduler tick remains near zero. The likely repeated census
inside `SampleColumnEmergeStageTelemetry()` is now a bounded target for
measurement and cleanup. It is invoked twice per frame and performs growing
map/record scans; preserve its two bounded demand maintenance passes while
avoiding duplicate diagnostic census. This is still a hypothesis for the
reported subphase growth until an explicit census timer confirms its cost.

Current plan status: **ready to implement and evaluate the bounded census
change; not ready to close**. The long repeatable flight, expected speed,
collision control, and positive operator visual observation are established.
Still open are late-route frame cost, the post-stop convergence/readiness
gates, and whether cold world creation/loading has different bottlenecks. The
flight analyzer's `holes_rate=1` remains a readiness proxy based on
`unfinished_visual`, not a pixel-hole verdict.

#### Remaining work, in order

1. Split demand-store maintenance from the telemetry census. Keep the current
   maintenance cadence, sample the full column/job/demand census once, and add
   a low-overhead per-period timer for that sample and its residual. Do not
   change streaming, lighting, or mesh budgets in this diagnostic change.
2. Build **Release only** and repeat the unchanged full M335 `World_164` route.
   Compare four distance bands (near, mid, far east, far west), route speed,
   zero-collision counters, user-visible output, phase timing, and post-stop
   readiness against M432. Commit the source change before the run so the
   manifest is attributable to one revision.
3. Run the planned separate 600-second fresh-seed startup/generation lane with
   source tracing, recording world/save metadata before and after. Compare with
   saved-world evidence; do not interpret M431/M432 `World_164` as cold
   generation because the west edge has been persisted and source tracing was
   off.
4. Keep pixel/source capture out of routine timing flights while the operator
   sees no visual defect. If dim/blank geometry returns, capture sparse samples
   on the same established route at the M429 probes and the M431/M432 western
   segment, then separate pixel evidence from readiness counters.
5. Close only after the route remains visually acceptable, far-route frame
   costs no longer show unexplained growth (or have an explicit accepted
   budget), the fresh-world path is understood, and stop convergence has a
   justified pass/fail criterion.

The M432 raw values, source finding, and caveats are recorded in
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m432---full-m335-timing-resample-with-phase-timers-2026-10-06).

### M433 readiness checkpoint - duplicate removed; one-per-frame census remains costly

M433 repeated the same visible M335 profile after commit `5b6cce71`. Process
and route succeeded at the expected median speed with no blocked movement or
ground contacts. Across matched distance bands, wall medians improved by
`7/13/14/16%` from near through far west; the far-west wall median fell from
`72.83` to `61.05 ms`. Async post-scheduler fell from `30.85` to `21.32 ms`
without a corresponding I/O-drain reduction. This supports the duplicate
census removal as a real, useful perf fix.

The new timer shows that the remaining single census consumes `19.48 ms`
median in far west, almost exactly the complete mesh post-telemetry phase. The
sample is diagnostic-only for production behavior and scans growing column and
demand state. Preserve both demand-store maintenance calls every frame, but
reduce the full census to a 250 ms time cadence and expose sample count/age.
Async post-scheduler still has roughly `21 ms` far-west median after the
duplicate was removed, so a separate async-policy breakdown remains an open
follow-up if it stays material after the census cadence change.

Readiness remains **ready for another bounded perf step; not ready to close**.
The route and movement confounders are controlled; the operator's visual
assessment is positive; and frame cost is measurably better. Remaining work is
the single-sample hot path, residual async-phase work, failed post-stop
convergence, and a fresh-world source-traced startup/generation lane. M433 did
not capture pixels, and `holes_rate=1` still means `unfinished_visual` debt.

#### Next work

1. Rate-limit only the diagnostic column/job/demand census to one sample per
   250 ms. Keep the existing two bounded demand reconciliation/orphan-cancel
   calls every frame. Log sample count and snapshot age; do not change streaming
   or mesh budgets.
2. Build **Release only**, commit before running, and repeat full M335 on
   `World_164`. Compare the same four distance bands, speed/collision control,
   census amortized and per-sample cost, async post-scheduler, and stop-tail
   readiness against M433.
3. If far-route cost remains high, add targeted subphase timing to the
   non-census work inside `TickAsyncChunkSystems()` before changing its policy.
4. Run a separate fresh-seed 600-second lane with column-source tracing and
   metadata/store-state checks; do not treat persisted `World_164` as cold
   generation evidence.
5. Keep dense pixel/source capture disabled while appearance remains positive;
   if a dim/blank symptom returns, probe the established route at the M429
   locations and the western edge.

M433 evidence is documented in
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m433---remove-duplicate-census-and-measure-remaining-cost-2026-10-06).

### M434 checkpoint — 250 ms census cadence validated; remaining gates stay open

M434 completed the unchanged visible, no-teleport M335 route using Release
source commit `a5ae29b3`. The runner exited successfully, the manifest was
clean, and the established speed and collision controls passed. The route
covered 14,288 blocks at a median 5.19653 blocks/s, held eye height at 70,
and recorded zero blocked movement substeps and zero ground contacts. The
operator's current visual assessment is acceptable.

The 250 ms rate limit materially reduced the diagnostic census cost without
changing streaming, lighting, or mesh budgets. In the far-west band,
`column_emerge_stage_sample_ms` is 1.842 ms median per frame at a sample rate
of 0.186 per frame; the snapshot age median is 116 ms. This is about a 90%
reduction from M433's 19.48 ms per-frame census median. The enclosing
`mesh_emerge_post_telemetry_ms` median fell from 19.48 to 11.39 ms. Across
matched bands, M434 wall medians improved over M433 by approximately
2/9/12/15% from near through far west. The far-west wall median is 52.06 ms,
down from 61.05 ms. Full-route wall median is 43.06 ms, and spike count is
37, down from M433's 47.63 ms and 87 spikes.

This change did not resolve all streaming cost. Far-west
`async_chunk_post_scheduler_ms` remains 21.43 ms median, effectively
unchanged from M433's 21.32 ms; I/O drain also remains about 10.83 ms. Total
far-west streaming phase is 45.74 ms median. The dominant spike class remains
stream, with a maximum wall sample of 342.06 ms. The next performance change
must first identify the work inside the remaining async/post-scheduler and
world-streaming phases; do not change their budgets or admission policy based
on phase totals alone.

M434 is **ready for the next diagnostic step, not ready to close or merge as
rendering-ready**. Process and route adequacy passed, but analyzer
`pass=false`: stop recovery still does not converge; the dual-lane readiness
gate fails at max unlit 41; and the eye-proxy gate reports stale-visual and
blink-proxy failures. Route-wide `unfinished_visual` is 27 median, but the
report explicitly uses it as readiness debt, not as framebuffer truth.
`visible_black_focus_n` has median 0 and maximum 18, with short nonzero
intervals that return to zero; no pixels were captured. Current user feedback
is positive, so do not treat those internal counters as proof of a visible
defect or spend timing runs on dense captures unless the symptom persists or
returns. Conversely, do not claim visual acceptance from these counters.

The cold-world lane remains independent and unproven on this revision.
M422-M426 documented a repeatable cold EnterLit/presentability problem, while
M434 reused persisted World_164. The census cadence affects diagnostic work
only and is not expected to repair startup. Use a new isolated metadata-only
seed to check whether the cold convergence problem still reproduces and to
record generation/source latency before the next long route.

#### Current remaining work

1. Run a visible, source-traced cold-seed startup lane on a unique world name
   with the current Release binary. Preserve the established M335 camera
   locus; use a 600-second flight only if the enter/presentability gate opens,
   and bound the process at 900 seconds. Record metadata, source lifecycle,
   resident chunk/block counts, EnterLit snapshots, and resulting save state.
   Keep this result separate from the World_164 repeatable-route control.
2. Add low-overhead subphase timing around the non-census work in
   `TickAsyncChunkSystems()` and its adjacent async-I/O/commit-result drain.
   Time scheduler versus worker-queue wait, ready-result dequeue/apply,
   relight/result reconciliation, and bounded demand maintenance separately;
   keep these timers off the policy path. Then run an unchanged Release M335
   repeat and compare the same four focus bands.
3. Use those measurements to choose one owner/policy change. Preserve current
   every-frame bounded demand reconciliation, do not loosen EnterLit or
   post-stop safety gates, and retain old drawable geometry until replacement
   publication is proven safe.
4. After any world-generation, I/O, admission, or mesh-publication policy
   change, repeat the established World_164 route and add a fresh-seed
   startup check. Continue fresh-world checks periodically; they cannot
   replace matched saved-world routes.
5. Define post-stop convergence evidence from actual owners and monotonic
   progress, then address the false-clear, dirty/unfinished and unlit gates.
   Keep operator appearance, pixel evidence, render-readiness counters, and
   route adequacy as separate acceptance dimensions.
6. Close only after the repeated route has acceptable user-visible output,
   unexplained far-route growth is removed or explicitly budgeted, the cold
   startup path is understood, and stop convergence reaches a justified
   pass/fail criterion.

The M434 matched-band results, report semantics, and artifact paths are in
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md#m434---validate-250-ms-census-cadence-on-full-m335-route-2026-10-06).
