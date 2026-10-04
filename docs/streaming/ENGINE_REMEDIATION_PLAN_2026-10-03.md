# План исправления стриминга и отображения мира — обновлён 4 октября 2026

Исходная база: `develop` / `codex_audit2`, commit `185e2f08` (merge
`codex_audit`). Последний длинный trace M378 использовал чистый Release commit
`6d06cef2`; отчёт и сценарии перечислены ниже.
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

### M390 interim — M335 retains route; fog horizon masks far surfaces

M390 is an in-progress visible Release run with the exact M335 route and timing:
World_164, start `[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`, 2 800 s
flight plus 20 s settle. It adds relight-owner logging and dense pixel capture;
camera, speed, world, and fixed-day settings remain unchanged. The run has passed
the historical tree corridor near x≈`−2 826` with zero blocked substeps and zero
flight-ground contacts so far; obstacle bypass stays enabled and has not needed
to activate on this M335 line.

Frame inspection finds uniform dark-blue polygons at exactly RGB `(13,38,89)`,
the configured fog color `(0.05,0.15,0.35)`. Existing render settings are
distance fog on, RD 4 chunks, start ratio `0.48`, density `0.85`, end margin 28;
that means fog reaches full blend at 36 blocks and begins at about 17.3 blocks.
The route harness disables adaptive fog pull-in, not distance fog itself. This
explains why far terrain becomes visually empty/blue in those captures without
proving that it is missing. Treat exact fog-color polygons separately from
screen holes, and retain the same M335 route when validating any renderer fix.

Reclassification of M389 `<96` framebuffer probes shows transparent composition
in 306/1,032 samples; 271/281 unsettled-light and 144/177 preview-light samples
are among them. The specific M389 sample at camera `(-7626,70,56)` is sand at
y=46 beneath sea level 48 with visible MDI geometry and pending light, so there
is real underwater light debt as well. In the unmodified-RGB subset, 10/726
samples are unsettled and 33/726 carry the preview marker. These counts do not
measure screen area and do not establish that transparency caused every sample.
The earlier M389 dim-sample table overstated the evidence for broad dry-chunk
light failure; prioritize the raw fog, transparent, depth, and light witnesses.

M390 is not yet a completed acceptance run. After it ends, inspect the flushed
pixel trace and relight-owner log on the distant segment, record the final
movement/collision counters, and only then select a bounded streaming/rendering
change. Do not change M335 camera or route parameters to make its scene easier to
capture.

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

Переносимый вывод для Cubatarium: источник данных — отдельный наблюдаемый результат
до mesh readiness; очередь должна ограничивать дубликаты/запас работы, а disk I/O,
decode/apply, generation, lighting и publication должны иметь отдельные latency и
completion counters. Конкретные лимиты брать из измерений этого движка.
