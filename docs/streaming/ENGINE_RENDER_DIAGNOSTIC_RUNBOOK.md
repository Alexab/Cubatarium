# Диагностика стриминга и видимости мира

Обновлено: 2026-10-16. Этот runbook фиксирует повторяемые сценарии и порядок
сопоставления состояния мира с тем, что реально попало в framebuffer. Полный
каталог исторических запусков и вспомогательных анализаторов остаётся в
[`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md); выводы по
рефакторингу и незакрытые критерии — в
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md).

## Порядок доказательств для пропавшей поверхности

Проверять этапы последовательно, сохраняя один и тот же мир, маршрут, камеру и
render config:

1. Сохранённые блоки воды в `.cchunk` подтверждают данные мира на диске.
2. `renderer_pixel_fluid_triangle_match` подтверждает пересечение луча с
   исходным CPU-треугольником воды; сам по себе этот признак не доказывает, что
   GPU выполнил draw.
3. `chunk_render_ready`, ready refs, top-face quads и GPU residency связывают
   исходную геометрию с render cache.
4. MDI-флаги являются предварительной подсказкой. Для конкретной команды нужно
   сравнивать `mdi_command_flags` (readback checked / matches batch / instance
   visible), а также VBO/EBO, их backing-size, привязки и GL errors из того же
   draw witness.
5. Сопоставить shell-depth и color witnesses со stencil/depth/color-write
   состоянием. Для water shell ожидается stencil `ALWAYS/ref=1` с записью
   stencil в depth-shell проходе; цветовые проходы используют stencil `EQUAL`.
6. Сравнить framebuffer RGB до и после прозрачного прохода. Нулевое изменение
   вместе со stencil=1 и цветом, равным fog, означает слияние с fog; stencil=0
   вместе с ошибкой draw или невалидным pool buffer означает, что water
   fragment не дошёл до цветового framebuffer.
7. Сверить участок и время с наблюдением пользователя. Sparse pixel probes
   подтверждают только выбранные экранные точки, а не весь экран.

Счётчики `unfinished_visual`, `readiness_debt`, `visual_holes`, GPU batches и
draw calls полезны для маршрутизации расследования, но не являются доказательством
видимой поверхности. `capture_flight_sim_window.ps1` сохраняет desktop/window
capture для визуального контекста; такой кадр может содержать чужое окно или
compositor overlay и не заменяет app-side framebuffer probes.

## Сценарии запуска

### Короткий target-lane — первый выбор при известной зоне

Собирайте приложение только в Release:

```powershell
cmake --build bin --config Release --target Cubatarium --parallel
```

Для проблемной зоны `camera_x=-6800..-5500` используйте проверенный мир и
небольшой no-teleport маршрут, который пересекает весь диапазон:

```powershell
pwsh -ExecutionPolicy Bypass -File tools/run_m335_water_visibility_ab.ps1 `
  -EndMarginBlocks 0 `
  -EnableFogPullIn `
  -World World_M572_Cold_20261014 `
  -ProbeMinCameraX -6800 `
  -ProbeMaxCameraX -5500 `
  -StartX -5450 -StartY 56 -StartZ 56 `
  -CruiseEyeY 70 -Yaw 180 -Pitch -30 `
  -FlyPhaseSeconds 300 -StopPhaseSeconds 20 `
  -StopAfterBlockedSeconds 8 -IdleSeconds 15 `
  -PhaseId m590_pool_release_fix_short_water_lane `
  -Report bin/suite_reports/engine_refactor/m590_m572_pool_release_fix_water_20261015.json
```

Параметры старта, камеры и длительности теперь задаются у
`run_m335_water_visibility_ab.ps1`; значения по умолчанию сохраняют исторический
30-минутный M335. Для целевого участка стартуйте до его ближней границы и
подбирайте длительность по формуле `длина участка / 5.185 blocks/s` плюс 20–40
секунд запаса. Этот сценарий измеряет только выбранный коридор и не заменяет
полную проверку дальнего стриминга. Окно остаётся видимым; во время маршрута не
перехватывайте управление мышью/клавиатурой.

Скрипт временно применяет fog margin и диагностические environment variables,
сохраняет прежние значения и восстанавливает их в `finally`. Он также
восстанавливает `bin/config.json` побайтно. После штатного завершения проверьте
report и выполните pixel join:

```powershell
python -X utf8 tools/analyze_water_surface_pixel_join.py `
  bin/logs/<perf-файл>.jsonl `
  bin/worlds/World_M572_Cold_20261014 `
  --min-camera-x -6800 --max-camera-x -5500 `
  --json-out bin/suite_reports/engine_refactor/<run>_water_pixel_join.json
```

Pixel и draw-witness события удерживаются в диагностическом кольце и
добавляются в perf JSONL при штатном завершении приложения. До закрытия процесса
отсутствие этих строк в файле не означает, что probes не сработали. Не обрывайте
маршрут до сброса кольца.

### Полный повторяемый M335 — только когда target-lane не отвечает на вопрос

Основной дальний сценарий сохраняет отработанные условия: world
`World_M572_Cold_20261014`, старт `(120,56,56)`, camera eye Y=70, yaw=180,
pitch=-30, обычная скорость около 5.185 blocks/s, no-teleport cruise 1800 s,
stop 20 s, idle 15 s. Длина около 9.2k блоков. Скрипт с параметрами по умолчанию
запускает именно этот маршрут.

Используйте полный M335, если симптом не укладывается в выбранное окно, зависит
от длинного прогрева/выгрузки или target-run дал противоречивые данные. M591
уже выполнил полный post-fix M335 на текущем этапе. Не запускайте длинный
маршрут только для того, чтобы оператор посмотрел на изображение: для этого
используйте короткий target-lane. Дальние прогоны оставляйте для конкретной
гипотезы о стриминге/выгрузке, с заранее сформулированной диагностической целью.

### Новый мир — периодическая вторичная проверка

После значимого изменения генерации, загрузки или mesh publication создавайте
новое имя мира и seed, не подменяя им повторяемый M335:

```powershell
cd bin
.\Cubatarium.exe --console --create-world `
  --name World_<unique-id> --seed <uint32> --radius-chunks 4 `
  --output worlds `
  --report-json suite_reports/engine_refactor/<run>_world_create.json
```

До полёта проверьте `success=true`, seed, `world_path` и `chunk_files` в отчёте.
Считайте начальное заполнение диска частью условий эксперимента; уже летавший
мир не является cold-seed baseline. Используйте короткий целевой маршрут, если
проверяется конкретная зона; новый seed нужен для поиска зависимостей от
истории сохранённых chunks, а не для ежедневного повтора.

## Анализаторы и сохраняемые результаты

| Инструмент | Что отвечает |
|---|---|
| `tools/flight_sim_run.py` | Запускает видимый flight, пишет perf/report и исполняет no-teleport маршрут. |
| `tools/flight_sim_analyze.py` | Сводит perf-метрики и stop/readiness gates; `pass=false` не является само по себе доказательством чёрного пикселя. |
| `tools/analyze_water_surface_pixel_join.py` | Сопоставляет экранные rays с сохранёнными блоками, CPU fluid triangles, GPU-state witnesses, stencil и RGB до/после transparency. |
| `tools/analyze_renderer_pixel_trace.py` | Фильтрует подробные renderer pixel trace записи. |
| `tools/flight_sim_timeline_analyze.py` | Сопоставляет переходы и короткие визуальные/streaming события по timeline. |
| `tools/analyze_framebuffer_pixel_grid.py` | Сводит пространственную сетку framebuffer probes. |
| `tools/analyze_geometry_lifecycle.py` | Join для geometry revision bump → queue schedule → точная публикация по координате и attempt id. |
| `tools/analyze_hole_event_pixels.py` | Разбирает pixel probes вокруг hole/stall событий. |
| `tools/run_with_host_cpu_sampler.py` | Записывает host/process CPU нагрузку; используйте для performance baseline, а не для доказательства изображения. |
| `tools/flight_sim_fixed_day.py` | Фиксирует время суток для воспроизводимых сравнений цвета/освещения. |
| `tools/capture_flight_sim_window.ps1` | Сохраняет визуальные window-captures; смотрите рядом с app-side probes, учитывая compositor и overlay. |

Для каждого run сохраняйте perf JSONL, flight report, pixel-join JSON, Release
commit SHA, world/seed, фактический camera-x диапазон, render distance, fog
margin/pull-in и краткую отметку пользователя о видимой воде. Не объединяйте
разные окна, миры или fog policy в одну статистическую выборку. Инструментированный
perf не используйте как чистую performance baseline: pixel readback и GL
witnesses добавляют overhead.

## Разделение причин

- В блоках воды нет данных: разбирать генерацию, disk lookup, deserialize и
  сохранение; сначала подтвердить этим же миром и координатами.
- Блок есть, source triangle отсутствует: разбирать fluid mesh build и
  publication; не считать far samples ошибкой, если они вне render radius.
- Triangle есть, ready refs/MDI команды нет: разбирать admission, upload,
  culling и GPU publication.
- Pool handle расходится с live pool или `glIsBuffer`/bind/size/GL error плохие:
  проверить ownership, освобождение и cache-handle sync; CPU triangle и batch
  counters в этом случае не подтверждают валидный GPU draw.
- Команда и buffers валидны, но stencil не записан: разбирать depth-shell pass,
  geometry/index buffer и framebuffer state.
- Stencil записан, RGB не меняется и совпадает с fog: сравнить горизонтальную
  fog distance и uniform start/end с дистанцией воды.
- Нулевой RGB change без stencil/fog/draw-state контекста остаётся
  неоднозначным; повторить короткий target-lane с теми же параметрами.

## Пример: pooled-buffer регрессия M589 → M590 → M591

M589 покрывал проблемный X-коридор полным M335 и зафиксировал 6,756 water /
source-triangle pixel matches без единого RGB change. Во всех этих точках
stencil остался 0. Среди 256 draw witnesses EBO не был GL buffer в 130 строках,
VBO — в 63, а после submit записаны 130 `GL_INVALID_OPERATION` и 41
`GL_INVALID_VALUE`. Статический разбор нашёл двойной вызов cleanup при
`Remove`/`RepresentationSwitch`: второй вызов трактовал pooled VBO/EBO aliases
как принадлежащие батчу и удалял backing buffers.

Исправление закоммичено в `c478286f`: общий pool больше не удаляется повторной
очисткой; pooled aliases обнуляются при первом cleanup. После Release-сборки
M590 использовал тот же мир, fog policy и probe window, но только 300 секунд
движения от X=-5450. Запуск покрывал X от -5505 до -6797, завершился без kill.
Из 7,007 water/source-triangle matches RGB изменился в 5,304 (75.7%); все
изменившиеся samples имели stencil=1. Все 256 draw witnesses показали живые
VBO/EBO и отсутствие GL errors. Все 477 точных MDI/payload checks совпали,
включая 468 water samples. M589 и M590 дали прямую проверку причины и
исправления на одном участке мира.

M591 повторил полный no-teleport M335 в том же мире после исправления и
покрыл 9,184 блока, включая 8,192-block checkpoint. Оператор сообщил, что
вода оставалась видимой всю трассу. В целевом X-коридоре 162 pixel captures
дали 9,720 samples: 8,344 water blocks, 8,068 source triangles, 8,031 совпавших
water/triangle samples. RGB изменился в 6,025/8,031 совпадении; во всех 2,640
совпадениях ближе 32 блоков RGB изменился. Все 256 draw witnesses подтвердили
живые pooled VBO/EBO и `GL error=0`; 486 MDI/payload checks совпали (481 на
water samples). Это длинная проверка уже выполнена; не повторяйте M335 ради
операторского подтверждения.

Из оставшихся 1,703 совпадений без RGB change 1,681 были дальше fog end;
1,452 точно совпали с fog RGB, 1,660 отличались не более чем на один 8-bit
канал. Поэтому такие далёкие точки надо отделять от ошибок pool/rendering.
M590 и M591 не прошли все общие streaming/readiness stop-gates. В M591 при
этом post-stop visual-hole rate и mesh-missing count равны нулю, а оператор
подтвердил видимую воду; readiness debt (`post_stop_readiness_debt_max=16`,
`chunk_not_ready_med=30`) и streaming phase median `7.86 ms` остаются отдельным
долгом. Не трактуйте `pass=false` этого составного анализатора как
подтверждение пропажи воды. M591: [flight report](../../bin/suite_reports/engine_refactor/m591_m572_pool_fix_full_m335_20261015.json),
[pixel join](../../bin/suite_reports/engine_refactor/m591_m572_water_pixel_join_20261015.json),
[perf JSONL](../../bin/logs/perf_20261015-212358_30076.jsonl).

Исторические команды и более ранние численные результаты M587–M591 дополнены
в [`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md)
и [`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md). Оставляйте
длинный M335 для тех случаев, когда короткая повторяемая выборка не отвечает
на конкретный диагностический вопрос; операторскую проверку изображения
проводите только коротким target-lane.

## Кратковременный fallback света: M592–M593

Оператор подтвердил, что вода оставалась видимой на всём M592. Это отдельный
факт и не подтверждает корректность освещения.

В M593 был запущен 90-секундный no-teleport target-lane в том же мире и
коридоре. Маршрут завершился штатно, прошёл 464 блока со скоростью 5.18555
блока/с. Составной flight gate завершился `pass=false` из-за near-focus
readiness/visual-hole долгов; это не framebuffer-доказательство дефекта света
или воды. В 6,800 pixel-probe строках обнаружено 34 попадания в opaque chunk с
включёнными vertex preview marker и opaque shader override (9 chunks). Отдельно
surface preview был включён в 30 строках; не смешивайте это поле с opaque
hit-chunk. В 29 из 34 opaque preview-проб освещение точного среза ещё не имело
settled proof. Пять проб в трёх срезах уже имели settled proof, и в каждой из
этих проб совпадали field/published/meshed light revision (`1`). Активной
relight attempt не было. Для всех трёх срезов позднее встретилась проба без
preview; камера за это время прошла 51–69 блоков, то есть примерно 10–13 секунд
при скорости маршрута. Это только границы наблюдения редкими пробами на
движущейся камере, а не точный замер времени обновления конкретного чанка.

Причина видимого перехода локализована до progressive light/mesh handoff:

- `LitReady` является состоянием колонки и намеренно может разблокировать
  focus ring, пока отдельный Y-срез ещё не settled.
- Первый drawable mesh без settled proof получает `ProvisionalLightPreview`;
  fragment shader показывает для него ambient floor `0.42`, чтобы не получить
  полностью чёрный чанк.
- После расчёта света cache сохраняет marker до принятия successor mesh. Только
  settlement света не выключает preview.
- `DrainStaleLightRemeshDebt` обнаруживает settled previews внутри active visual
  ring и ограниченно вводит их в remesh: одна работа за кадр, с резервным
  слотом каждый четвёртый кадр, если исчерпан общий admission budget. За границей
  activation ring fallback сохраняется и не заполняет очередь вдали во время
  длинного полёта.

Таким образом, текущие данные подтверждают два последовательных состояния:
часть чанков ждёт вычисления per-slice света, а часть продолжает отображаться с
preview после settlement до замены mesh. Пока нельзя оценить долю времени,
потраченную на ожидание света, очередь stale-remesh, CPU mesh build и GPU apply.
Схема трассы 2 в коммите `0b63f564` разделяет opaque depth-hit и sea-plane
поля. Pixel trace пока не содержит membership/age для `PendingStaleLightRemeshes_`;
добавьте это перед тем, как менять admission budget.

Для следующего шага используйте короткий детерминированный участок и соберите
по каждому opaque chunk: settled time, stale-remesh queued time, dirty-admission
time и mesh publish time. Затем корректируйте либо горизонт/бюджет relight,
либо ограниченный приоритет уже settled preview chunks в ближнем видимом кольце.
Не enqueue-те все дальние previews; оставьте fallback, чтобы ремонт не создавал
чёрные дыры. Длинный M335 сейчас не нужен.

### M594–M595: проверка владельца для unsettled preview

В M594 (176 блоков) 11 opaque preview-проб в шести чанках были unsettled при
совпадающих field/published/meshed revisions `1/1/1`. В двух чанках в момент
pixel capture не было ни mesh owner, ни relight queue/async owner, ни ColumnFlow
ticket. Stale-light-remesh queue был пуст, что ожидаемо до settlement.
Составной анализатор завершился `pass=false`; маршрут и его pixel trace всё же
пригодны для диагностики этой handoff-ветки.

Причина: одинаковые published и desired revisions дают `DemandResult::AlreadySatisfied`,
хотя точный Y-срез ещё не имеет settlement proof. Старый путь
`AdmitUnfinishedVisualDemand` не запрашивал relight для уже drawable
provisional mesh. В коммите `3668f891` добавлена адресная ветка: при включённом
lighting gate видимый `ShouldDrawProvisionalLightPreview` без slice settlement
передаётся в `ensure_slice_relight`.

M595 повторил короткий участок на том же уже прогретом мире. Единственный
opaque preview-hit был unsettled, но имел `column_flow_ticket_flags=1` и
`relight_owner_flags=195`; позднее тот же chunk попал в sample уже без preview.
Это подтверждает, что новый путь может назначить owner. Снижение количества
preview с 11 до 1 не является контролируемым сравнением: M594 прогрел тот же
мир перед M595. Оба run завершили маршрут, но aggregate gate `pass=false`;
общие readiness/visual метрики не доказывают улучшение производительности или
закрытие рефакторинга.

M594: [flight report](../../bin/suite_reports/engine_refactor/m594_m572_light_owner_trace_20261015.json),
[pixel trace](../../bin/suite_reports/engine_refactor/m594_m572_light_trace_20261015.json),
[perf JSONL](../../bin/logs/perf_20261015-224256_13996.jsonl). M595:
[flight report](../../bin/suite_reports/engine_refactor/m595_m572_provisional_relight_fix_20261015.json),
[pixel trace](../../bin/suite_reports/engine_refactor/m595_m572_light_trace_20261015.json),
[perf JSONL](../../bin/logs/perf_20261015-224636_36248.jsonl).

M596C/M596D выполнили cold-but-repeatable короткую проверку. Следующий шаг —
повторить её после Release-сборки событийных меток; временные интервалы из
новой трассы должны разделить settlement, stale-remesh queue, dirty admission и
mesh publication. Длинный операторский M335 для подтверждения этого локального
пути не запускайте.

### M596C–M596D: короткий холодный фронтир и осторожная интерпретация

Создан `World_M596C_Cold_20261015` (seed `3650482197`, radius 4, 597 chunk
files). Create-world выполняйте из Release `bin` через PowerShell
`Start-Process -PassThru -Wait -WindowStyle Hidden` и проверяйте его report,
`world_path`, seed и число файлов до полёта. Прямой запуск GUI-subsystem exe
без ожидания преждевременно возвращает управление; harness может удалить такой
процесс как orphan. Стартовая позиция для radius-4 мира должна находиться в
сгенерированной spawn области. Попытка начать на X=-6070 откатилась к spawn и
не создала осмысленного движения.

M596D повторил известную камеру на 35 секунд с no-teleport: X=-32 → -214,
12 chunk columns, без столкновения. `run_outcome=success`, `process_rc=0`, но
composite `pass=false`: readiness debt равен 1, `post_stop_readiness_debt_max=15`;
при этом `visual_holes_rate=0`, `post_stop_mesh_missing_max=0`, и pixel analyzer
не нашёл ни одного пикселя ниже dark threshold. Это не следует трактовать как
обнаруженную пустую или чёрную дыру.

Сырые 35 same-camera/same-pixel переходов нужно разделять по позиции. 34 были
на неподвижном spawn view X=-32: median 2.728 s, maximum 9.269 s, на старте
срезы имели unsettled demand и позднее field/meshed/published revisions
дошли до 2. Единственный такой переход на холодной границе — chunk `(-20,2,2)`
при X=-213.784; первый pixel sample уже видел settled light, но mesh/publish
revision был 0, а через 1.539 s тот же pixel/camera увидел revision 1 без
preview. Это ограниченное наблюдение указывает на mesh handoff в данном случае,
но не находит его очередь-виновника.

В старой трассе pixel time относительный, а demand/job события используют
абсолютный steady-clock. Коммит с текущей трассировкой переводит новые pixel
пробы на `renderer_pixel_sample_steady_ms` для event join и добавляет события
`light_calculation_settled`, `stale_light_remesh_queued` и
`stale_light_remesh_dirty_admitted` только для уже watched slices. Analyzer
дополнительно сопоставляет их с mesh job `started/built/published` по chunk,
world epoch и incarnation. M596 artifacts: [create report](../../bin/suite_reports/engine_refactor/m596c_world_create_20261015.json),
[flight](../../bin/suite_reports/engine_refactor/m596d_cold_world_west_frontier_20261015.json),
[pixel lifecycle analysis](../../bin/suite_reports/engine_refactor/m596d_cold_world_west_frontier_pixels_20261015.json),
[perf JSONL](../../bin/logs/perf_20261015-230556_21480.jsonl).

Для следующего холодного повтора используйте новый уникальный world name с тем
же seed/radius и дождитесь завершения Release create-world процесса:

```powershell
$exe = (Resolve-Path .\bin\Cubatarium.exe).Path
$proc = Start-Process -FilePath $exe -ArgumentList @(
  '--console', '--create-world', '--name', 'World_M597A_Cold_20261015',
  '--seed', '3650482197', '--radius-chunks', '4', '--output', 'worlds',
  '--report-json', 'suite_reports/engine_refactor/m597a_world_create_20261015.json'
) -WorkingDirectory (Resolve-Path .\bin).Path -WindowStyle Hidden -PassThru -Wait
if ($proc.ExitCode -ne 0) { throw "create-world failed: $($proc.ExitCode)" }
```

Проверьте report (`success=true`, правильный seed/path, ненулевой `chunk_files`),
затем повторите ту же short lane, начиная с X=-32; дальний spawn override не
задавайте:

```powershell
pwsh -ExecutionPolicy Bypass -File tools/run_m335_water_visibility_ab.ps1 `
  -EndMarginBlocks 0 -EnableFogPullIn `
  -World World_M597A_Cold_20261015 `
  -ProbeMinCameraX -300 -ProbeMaxCameraX 0 `
  -StartX -32 -StartY 56 -StartZ 56 `
  -CruiseEyeY 70 -Yaw 180 -Pitch -30 `
  -FlyPhaseSeconds 35 -StopPhaseSeconds 20 `
  -StopAfterBlockedSeconds 8 -IdleSeconds 15 `
  -PhaseId m597b_cold_light_handoff `
  -Report bin/suite_reports/engine_refactor/m597b_cold_light_handoff_20261015.json
```

После run снова вызовите `tools/analyze_renderer_pixel_trace.py` для указанного
perf JSONL. Сначала проверьте ненулевой
`absolute_steady_clock_pixel_probe_count` и наличие нужных lifecycle event
counts. Если событие settlement predates первую видимую пробу, стадия света
получает левую границу “уже settled”; не приписывайте этот интервал relight.

### M597–M598: opaque identity join and short cold-frontier result

M597 показал, что координаты пиксельного opaque hit нельзя соединять с
`world_epoch/incarnation` generic pixel row: последние могли принадлежать
sea-plane sample. Текущая Release трасса пишет
`renderer_pixel_opaque_world_epoch` и `renderer_pixel_opaque_incarnation`, а
наблюдение включает точный opaque chunk при provisional sample. Analyzer
отбирает соответствующую demand attempt и один mesh job; один chunk может
иметь несколько попыток в том же временном окне.

M598A создал cold мир `World_M598A_Cold_20261015` (seed `3650482197`, radius 4,
597 файлов чанков). Запускайте GUI-subsystem create-world только в
`Start-Process -WindowStyle Hidden -PassThru -Wait`, затем проверяйте
`success`, seed, путь и `chunk_files` в report. Прямой вызов из PowerShell
может вернуть управление раньше завершения процесса и породить orphan.

M598B прошёл X `-32→-214` при `5.19653 blocks/s`, без teleport/collision.
Pixel trace: 4,800 проб, 35 provisional chunks, 0 dark-threshold samples;
all 35 provisional chunk timelines joined lifecycle events. На frontier
`(-20,2,2)` light settled через ~64 ms, matching mesh published через ~112 ms
от первого preview sample; следующий settled sample пришёл через 1.6 s уже с
другой camera position. На stationary startup slice `(-4,2,2)` был виден
deferred-far owner и ожидание до light settlement ~7.45 s, после чего build
занял ~5 ms. Это указывает на очередь до освещения в startup backlog, но не
измеряет точную длину enqueue→admit и не доказывает, что все визуальные
наблюдения имеют ту же причину. Aggregate suite `pass=false` из-за readiness
debt; direct `visual_holes_rate=0`, `dark_pixel_count=0`. Eye proxy не проверил
temporal mid-flight segment, потому что выбранная проверка пространственная.

Сверка с `WorldColumnSource` INFO log показала, что startup column `(-4,2)`
была загружена с диска (`spatial_load`, 22.096 ms); это та же колонка, что
долго оставалась в `deferred_far`. Её pixel owner перешёл в priority FIFO
между выборками около 6.78 и 8.43 s; периодные FIFO `17→11` согласуются с
порогом admit 16, после чего свет установился около 7.45 s. Точный кадр
перехода для отдельного demand не записан. Колонка frontier `(-20,2)` дала
`procedural/disk_miss`, затем worker generation 91.182 ms и commit; соответствующий
opaque chunk закончил световую часть через ~64 ms после preview и опубликовал
mesh через ~112 ms. Поэтому этот запуск показывает два разных пути: задержка
стартового чанка связана с deferred relight service после disk load, а не с
медленным чтением самой колонки; выбранный свежесгенерированный frontier
обработался быстро. Нельзя обобщать эти два наблюдения на все источники.

Команды и артефакты M598 перечислены в
[flight experiment notes](FLIGHT_EXPERIMENT_SCRIPTS.md); summary внесён в
[ремедиационный план](ENGINE_REMEDIATION_PLAN_2026-10-03.md).
M599 добавил bounded age-service (возраст 1.5 s, не чаще 1 admission/250 ms,
occupancy ниже 75% FIFO soft cap): 21 admissions, deferred-far peak 40,
startup light settlement 6.892 s. Сам admission не помог заметно: колонка уже
была в priority FIFO около 3.17 s, но оставалась позади очереди. M600 добавил
поднятие age-selected entry к priority head после underfeet pin. На той же
холодной карте и маршруте startup settlement сократился до 3.983 s; disk load
занял 22.423 ms, deferred-far peak был 37, age-admit count — 11. Near-focus
visual holes остались 0; suite `pass=false` из-за readiness debt. Это частичное
улучшение: при выборке на 3.15 s стартовый chunk всё ещё был на queue index 11,
а aggregate counter не связывает admission с конкретной колонкой. M600
содержал 292 pixel samples ниже luma 96, но 258 из них имели settled light и
все имели MDI depth surface; это не эквивалентно 292 дырам.

Перед дальнейшей сменой scheduler нужно добавить адресный audit для каждого
age admission (`coord`, age, FIFO до/после, queue index после вставки, active
pin), чтобы связать age path с конкретным on-screen chunk. Дальше использовать
тот же короткий контрольный маршрут; 8,192-block acceptance пока отложить.
Сводка и артефакты M599/M600 внесены в [ремедиационный план](ENGINE_REMEDIATION_PLAN_2026-10-03.md).

M601 добавил эту адресную запись и показал, что для startup column `(-4,2)`
age-service действительно сработал: на возрасте 2.498 s колонка была поставлена
в priority FIFO на индекс 0 при occupancy `20→21`, без активного pin. Но далее
каждый новый age-selected столбец снова вставлялся на индекс 0 (19 admissions
за запуск), так что более поздние запросы могли обходить уже ожидающий запрос.
Точный экранный chunk `(-4,2,2)` оставался provisional до settlement около
10.785 s после первого pixel sample; после settlement mesh build занял 3.672 ms,
publish — 8.066 ms. Это снова ограничивает задержку стадией relight до
settlement, но указывает, что перемещение на head не гарантирует обслуживание:
повторные head insertions создают риск starvation.

Пользователь заметил тёмные/чёрные чанки при входе. В холодном M601 были
`enter_unlit_max=32`, 230 pixel samples с unsettled light demand и 28 luma<96
samples; прямой near-focus mesh-hole proxy остался 0. У подвыбранных экранных
поверхностей preview shader override был включён, поэтому эта трасса доказывает
временный unlit preview и низкую яркость части пикселей, но не захватывает
пиксель со значением RGB=0. Нельзя сводить операторское наблюдение к одному
proxy.

M602 повторил маршрут в сохранённом M600 мире: harness распознал warm cache,
pixel analyzer больше не увидел startup chunks `(-4,2,1/2)` в provisional
lifecycle, а количество preview samples упало с 196 до 18. При этом по другим
вновь активированным срезам оставался readiness debt (`enter_unlit_max=13`),
поэтому прогретый мир уменьшает стартовый relight debt, но не устраняет все
временные unlit состояния. Сравнение подтверждает, что тип мира и сохранённые
light-complete сведения влияют на вход; оно не доказывает, что пользовательское
наблюдение имело только эту причину.

M603 применил это ограничение: новый age-selected столбец не допускается, пока
предыдущий другой age-selected столбец ещё находится в FIFO. В одном коротком
cold прогоне точные стартовые срезы `(-4,2,1/2)` settled за 3.985/4.149 s от
первого pixel sample против 10.735/10.785 s в M601; возрастных admissions было
6 вместо 19. Это согласуется с устранением повторного обгона и близко к M600
(3.983 s), но не доказывает общего ускорения очереди: deferred-far peak вырос
до 44, `enter_unlit_max=30`, post-stop debt достиг 23, suite остался `pass=false`
по readiness/eye-proxy gates. Near-focus mesh-hole proxy равен 0. Считать
изменение ограниченным и перспективным; проверять дальнейшие изменения тем же
коротким маршрутом и следить отдельно за ростом deferred-far backlog.

Вывод по вопросу cold/warm: на холодном мире геометрия может быть загружена или
сгенерирована до появления валидного light-complete состояния, после чего
колонка ждёт relight. Поэтому холодный старт может вызвать тёмные/чёрные
временные чанки при входе. Warm M602 сократил именно startup provisional set,
однако readiness debt сохранялся и на других активных срезах; это фактор, а не
полное объяснение.

M604 повторил холодный seed/маршрут после коммита pacing. Процесс завершил
полёт (`process_rc=0`), но suite не прошёл stale-visual / hole-blink gate:
33/35 периодов классифицированы как spikes, fly wall median 36.9 ms, streaming
phase 24.6 ms, emergence 11.9 ms, frontier pressure 0.87. Deferred-far peak
составил 46 и к остановке снизился до 3; оставшиеся колонки находились в
5–7 чанках при service horizon 4. Зафиксировано 2 age admissions. Это не
показывает устойчивый рост очереди в зоне обслуживания.

Pixel trace M604: 52 low-luma samples / 2,880 probes, 306 unsettled opaque
demand samples, 252 provisional-preview samples в 24 чанках, 168 preview shader
overrides. `visual_holes_rate=0` и A24 safety stop-line прошёл, но visible-black
proxy достиг 16, а startup unlit/pending max — 50. M603 был заметно спокойнее
(3 spikes, fly wall median 13.7 ms, frontier pressure 0), поэтому M604 нельзя
использовать как чистое сравнение scheduler. В нём host CPU не измерялся; сразу
после него два кратких idle-process smoke измерили 30.8–65.4% общей host CPU,
что делает внешнюю нагрузку правдоподобной, но не доказывает её уровень в момент
M604.

`tools/flight_sim_run.py` теперь записывает раз в секунду host CPU, CPU самого
процесса игры, оценку остальной загрузки, RSS, число потоков и доступную память
в поле `runtime_resources` отчёта. Следующий контроль — короткий холодный вход
при спокойной host CPU; не запускать длинный acceptance flight до стабильности
короткого cold-start handoff.

### M605/M607: cold-entry и классификация тёмных pixel probes

В M605/M607 использовался только короткий проверенный GUI no-teleport маршрут
(35 s cruise + 20 s stop + 15 s idle). M605 оказался перегружен: host CPU
median/p95 73.9/99.4%. Для M607 свежая карта того же seed/radius создалась за
59.827 s (597 chunk files); cold-run host CPU составил 41.0/85.5%, warm-run
75.5/99.5%. Поэтому пара cold/warm несопоставима, несмотря на одинаковую
траекторию. Новое правило сравнения включает host median <=50%, p95 <=75%,
разницу обеих статистик <=15 процентных пунктов и разницу median app CPU <=15.

M607 cold имел `enter_unlit_max=50`, `enter_pending_max=44` и 16 provisional
preview chunks; warm M600 имел 0/0 и 3 preview chunks. В обоих `visual_holes_rate`
остался 0. Это согласуется с тем, что сохранённый light-complete state снижает
работу при входе, но метрики сами по себе не доказывают, что это причина
конкретного наблюдения оператора.

Важно: `dark_pixel_count` — порог яркости по итоговому composited RGB, поэтому
включает и samples, чей RGB отличается от pretransparent sample. В M605 таких
samples было 349 из 383, а в M607 cold — 29 из 42.
`analyze_renderer_pixel_trace.py` теперь отдельно
показывает `dark_pixel_classification`: unchanged RGB, совпадение с drawable
visible opaque depth surface и состояние light/preview. M605 дал 34 opaque
candidates, все с unsettled/preview light evidence, 16 с preview marker и 7 с
light-revision mismatch; M607 cold — 13 (4 с pending/preview, без revision
mismatch), warm — 6. Это точнее для диагностики, но всё ещё не прямое
доказательство отсутствующего чанка: проверяйте `depth_surface`, MDI visibility,
light revisions и операторский вид отдельно.

Трасса конкретного startup pixel в chunk `(-3,3,4)` локализовала длинное
ожидание M605: preview наблюдался на одной и той же камере не менее 11 s,
light settlement пришёл через 14.854 s от первого sample, mesh build начался
ещё через 0.746 s, занял 2.788 ms, GPU publication наступил через 11.927 ms,
а install event — через 36.292 ms после публикации (51 ms после начала build).
В M607 на той же позиции и chunk свет settled за 231 ms, а
preview закончился к 400 ms. Следовательно, в M605 mesh build не был основным
узким местом; задержка была до/в очереди light service. Но M605 host был
перегружен, а cold/warm M607 не прошёл load gate, поэтому пока нельзя выбрать
между scheduler delay и нехваткой worker CPU. Следующий аудит должен связать
pending note этой near-focus колонки с FIFO age/admission и фактическим началом
relight worker; policy менять только после такого сопоставления.

Для следующего короткого старта снимайте узкий участок вокруг spawn/точно
наблюдавшихся координат и сохраняйте отдельные lifecycle и pixel traces. Не
расширяйте маршрут, чтобы компенсировать невалидную пару; повторяйте только arm,
который не прошёл host-load gate, когда median и p95 снова в заданном диапазоне.

Static event audit for M605: 97 sampled capture rows contained no hot-frame
stop reason 5. Stop reason 8 appeared 14 times, each with an empty relight FIFO;
11 of those samples still had at least two async jobs in flight. Stop reason 3
appeared 66 times and reports the capture cap being consumed, including 25
samples whose cap was zero. It is not the hot-frame skip path. At 10:49:33.746
an aged deferred-far key was admitted after 14.707 s; the next admission audit
reported `age_service_wait_previous=1` while the prior key remained in the
deque. This is a queue-policy lead, not yet a proven cause for the target
column.

For column `(-3,4)`, the void-dark pending note arrived at 10:49:19.038 and its
first capture began at 10:49:35.683 (16.645 s later). The async worker started
1.146 ms after the capture snapshot, so this sample's delay was before worker
service. A wider second capture followed at 10:49:42.763. Existing logs do not
identify the target's owner state throughout the gap. M608 adds an audit-only
`void_dark_owner` record at note acceptance; correlate its FIFO/deferred/flow/
in-flight fields with `capture submit` and `worker_start`. Keep the scheduler
unchanged until that short Release trace identifies the stage.

M608 exercised the owner snapshot under non-comparable load: host CPU
median/p95 was 91.2/100%, the process exited normally, `visual_holes_rate` was
0, while readiness-debt rate was 1 and entry unlit/pending maxima were 50/42.
Pixel analysis found 33 unchanged-RGB drawable opaque samples; all had
unsettled/preview evidence. No geometric hole was reproduced by the near-focus
proxy.

For `(-3,3)`, the first accepted note had a priority FIFO owner at index 0 of 1
and a column-flow owner. First capture came 16.759 s later. Interim perf samples
reported an empty FIFO; a repeated note at 11:17:13.515 had FIFO index 5 of 18
and no flow owner, followed by capture at 11:17:15.035. The run emitted 396
owner snapshots: 346 keyed in the FIFO, 50 deferred-far-only, 387 with a flow
owner (overlapping flags). `relight_false_clear_n` stayed at zero. This points
to an owner transition or requeue before capture, but the overloaded host
precludes timing conclusions. The next trace should log clear, cancel, trim,
and defer transitions with column keys; do not change admission limits yet.

### M609–M612: cold entry, saved-light flags, and queue ownership

Все четыре прогона использовали один короткий проверенный M335 GUI
no-teleport маршрут, seed `3650482197` и одну карту. M609 создавал карту заново;
M610 повторил маршрут по прогретым файлам terrain, но до M611 у карты не было
`column_light.json`. Поэтому M610 — disk-warm terrain, а не saved-light control.
M611 создал 144 complete light flags; M612 стартовал с этим сохранённым
состоянием.

| Run | Host CPU median/p95 | Важные признаки | Интерпретация |
| --- | ---: | --- | --- |
| M609 fresh | 78.8/100% | create 71.756 s; enter unlit/pending 41/41; holes proxy 0; 34 dark pixel candidates | Свет не был помечен complete; нагрузка выше ворот |
| M610 terrain-warm | 72.5/98% | 27/18; holes proxy 0; 5 candidates | Не saved-light control; не сравнивать как парный результат |
| M611 owner trace | 89.5/100% | 25/16; holes proxy 0; 144 flags появились во время запуска | Высокая нагрузка; часть deferred clear ещё не логировалась |
| M612 saved-light | 99.8/100% | старт 144 flags, конец 128; 8/8; holes proxy 0; 32 candidates | Высокая нагрузка; timing и улучшение визуального результата не доказаны |

Пиксельные candidates обозначают неизменившийся тёмный RGB на drawable opaque
depth surface, а не отсутствующую геометрию. Они сохранялись в тех же восьми
chunk columns в M609 и M612; все имели незавершённый light evidence. При этом
число samples сильно менялось (M609 34, M610 5, M612 32), поэтому этот сигнал
нужно использовать для локализации, а операторский кадр и revision trace — для
подтверждения. Нулевой `visual_holes_rate` говорит только об отсутствии hole в
текущем proxy.

Статический кодовый путь `ShouldDrawProvisionalLightPreview` выдаёт preview для
drawable mesh с preview-тегом, устаревшей mesh revision после settlement или
активным light repair. `fshader_greedy.glsl` поднимает освещение до floor 0.42
только для помеченного preview. Не считать каждый тёмный candidate защищённым
этим fallback: отдельно проверять preview marker, field/mesh/published light
revisions и действующего владельца. Полностью тёмный image проходит gate только
при совпадении settled light field, meshed revision и published revision.

M612 добавил недостающие записи для прямых clear-путей из deferred map.
Получено: 50 defer, 37 clear, 5 cancel, 2,012 capture requeue. Колонка `(-3,4)`
получила capture через ~15.1 s после первой dark-owner note. Это подтверждает
видимые handoff-переходы, но не доказывает потерю работы; нагрузка 99.8/100%
делает интервалы непригодными для оценки SLA.

M613 Release build добавляет light-flags lifecycle события под тем же
`CUBATARIUM_RELIGHT_AUDIT=1`: `light_flags event=load` (включая `missing` и
число entries), `event=clear` с причиной (`relight_enqueued` или
`column_unload_with_relight_work`), `event=set`, `event=save_queued` и
`event=save_result`. Это позволяет понять, почему saved-light флаг перестал
действовать, а также отличить отсутствие/ошибку файла от штатного пересчёта.

Результат M613: cold world создался за 61.673 s (597 chunk files), файл флагов
при старте отсутствовал и загрузился как `missing/0`; к концу короткого маршрута
в нём было 116 entries. Входная нагрузка оказалась выше ворот: host CPU
median/p95/max 58.4/97.6/99.5%, хотя одиночный предзапусковой sample был 0%.
Flight process завершился нормально, suite не прошёл. Метрические признаки:
unlit/pending maxima 55/48, near-focus mesh-hole proxy 0, `black_proxy_rate=0`,
no-ticket visible-black stop-line прошёл.

Pixel trace: 55 final RGB samples ниже luma 96; 44 изменились при transparent
composition. 11 unchanged samples присоединились к drawable opaque поверхности
в одной колонке `(-3,3,4)`, luma 67–90; 4 имели preview/unfinished-light evidence,
light-revision mismatch не выявлено. Эти samples описывают затемнённую поверхность,
но не доказывают пользовательский black chunk.

В light-flags журнале: 242 clear, 358 set, 432 save queue/result пар; по отдельным
колонкам видны повторные relight invalidations. Средние writer wait/service —
0.17/6.27 ms, максимальный service 55.42 ms. При таком host load нельзя делать
вывод о performance. Разовый счётчик CPU перед запуском оказался недостаточным:
ресурсный gate принимать только по runtime распределению из flight report.

M614 повторил короткий маршрут в том же мире после появления сохранённых flags:
load сообщил `loaded/116`. `enter_unlit_max/enter_pending_max` составили 9/9
против 55/48 в холодном M613; `fly_visible_black_max` — 6 против 12,
post-stop stalled — 0 против 2. Near-focus mesh-hole proxy оставался 0, но
общий readiness debt не исчез. Pixel trace M614 дал 92 samples ниже luma 96;
31 unchanged RGB совпали с drawable opaque surface и все имели unsettled/preview
evidence. M613 дал 55 dark-threshold samples и 11 unchanged opaque candidates.
Эти разные proxy не позволяют сделать вывод, что warm-визуальное изображение
было хуже или лучше; сохранённый capture не заменяет оператора. В M614 runtime
host CPU median/p95/max составили 65.9/99.4/100%, поэтому и его timing
непригоден. Процесс закончен штатно, но aggregate suite `pass=false`.

M613 stale-dark trace также выявил межчанковую границу. Для consumer chunk
`(-19,2,0)` stale witness указывал на face-light sample в source chunk
`(-20,2,0)`; для `(-19,2,5)` — на `(-20,2,5)`. Собственные consumer revisions
были согласованы. `ChunkHasStaleDarkFaces` проверяет, что baked face light равен
нулю, а текущий world sample на соседней face-air/solid ячейке уже lit. Capture
dependency stamp включает точный radius-2 light halo 26 соседей и защищает
in-flight mesh от изменения входа до apply. Отдельно нужно доказать, что уже
опубликованный drawable mesh получает remesh owner, когда позже меняется
neighbor halo; наличие ticket без прогресса зафиксировано, но источник
задержки по M613 пока не установлен.

M613/M614 дают предварительный сигнал о cold start: при отсутствии файла
light-complete flags первая карта вошла с большим `unlit/pending` debt; warm
карта загрузила 116 flags и имела существенно меньшие стартовые maxima. Оба
прогона перегружены, поэтому причинность по изображению и performance ещё
нужно подтвердить. Для повторной короткой пары требовать host CPU
median <=50%, p95 <=75%, разницу median/p95 пары <=15 п.п. и разницу median app
CPU <=15 п.п. Использовать существующий M335 сценарий и те же параметры; менять
маршрут лишь для точечного короткого кадра по известной координате. Если ворота
не пройдены, сохранить лог и повторить только соответствующий arm позже. Новые
seed-проверки проводить периодически отдельно от повторяемой пары.

M615 меняет только обслуживание подтверждённого stale-light debt:
`DrainStaleLightRemeshDebt` теперь ищет и не снимает с очереди drawable
`ChunkHasStaleDarkFaces`, даже если собственные mesh/light revisions совпали, и
активирует такой ремонт до `kVisualStageFirstMeshRelightApproachHoriz` (один
чанк за пределами LitDrawable ring). Сохраняется имеющийся предел: не более
одного reserved repair за четыре scheduler frames, когда shared dirty budget
исчерпан. Release build успешен; тесты и новый flight в M615 не запускались.

Основание для правки — M613 `dirty_admit denied_far` для stale chunk на
`focus_horiz=5` при backlog 45 и 23 `dirty_admit_denied` retries для
`(-19,2,5)` в M614. При этом `desired_geom=4`, `published_geom=3`, light
revision уже был settled, а dirty queue и mesh owner отсутствовали. Для соседнего
среза `(-19,2,0)` admission прошёл, и новый mesh опубликовался примерно через
117 ms. Следующий короткий M335 на ресурсно-валидном host должен проверить, что
stale debt на approach ring теперь остаётся durable до publish и не копит
повторные безвладельческие отказы. Длинный мониторинговый маршрут для этой
проверки не нужен.

Артефакты M609–M614 (локальные, не включены в исходники):

- M609: `m609a_world_create_20261016.json`, `m609b_cold_owner_drop_20261016.json`,
  `m609b_cold_owner_drop_pixels_20261016.json`,
  `perf_20261016-112812_12632.jsonl`.
- M610: `m610a_warm_revisit_20261016.json`,
  `m610a_warm_revisit_pixels_20261016.json`, `perf_20261016-113458_37732.jsonl`.
- M611: `m611a_queue_handoff_20261016.json`,
  `perf_20261016-114313_23848.jsonl`.
- M612: `m612a_saved_light_revisit_20261016.json`,
  `m612a_saved_light_revisit_pixels_20261016.json`,
  `perf_20261016-115016_20008.jsonl`.
- M613: `m613a_world_create_20261016.json`,
  `m613b_cold_flag_lifecycle_20261016.json`,
  `m613b_cold_flag_lifecycle_pixels_20261016.json`,
  `perf_20261016-120225_41932.jsonl`.
- M614: `m614a_warm_flag_revisit_20261016.json`,
  `m614a_warm_flag_revisit_pixels_20261016.json`,
  `perf_20261016-121314_37496.jsonl`.

Полные выводы, ограничения сравнения и дальнейшие действия находятся в
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md).

#### M615A / M616 geometry admission follow-up (2026-10-16)

The M615A run used the established visible GUI M335 no-teleport profile. Its
preflight host CPU median/p95 was 9.8/39.8%, but runtime rose to 73.7/100/100%
(median/p95/max), so do not use it for performance acceptance. It exited normally.
At focus `(-14,3)`, drawable chunk `(-19,2,5)` had current light revision 1604
but geometry `desired=4..9 / published=3`; the world gate denied 66 attempts at
h5 without creating an owner. A stale-light debt event for that coord was queued
then cleared without admission. The flight log recorded zero `gpu=1` witnesses.

M616 changes only this unowned drawable geometry-debt admission path in the h5
approach ring. To verify, commit the change before launching the runner (its
build-tree hash rejects tracked dirt), then repeat the same short profile and
inspect action `geometry_debt_approach_reserved`, work ownership, published
geometry revision, and post-stop focus debt. Require fresh CPU median <=50% and
p95 <=75% before starting; also judge the run from runtime CPU, since M615A's
preflight did not predict runtime load. Keep GPU-only witness ambiguity separate:
the current chunk-wide proxy may mistake unrelated sky-lit blocks for proof that
a dark cave face is stale. No long operator-monitoring flight is needed.

The post-M616-build preflight on 2026-10-16 sampled host CPU at median 57.5% and
p95 93.6% with the application stopped. A later fresh preflight admitted M616A,
but runtime host CPU rose to median/p95/max 68.6/98.6/100%; this run cannot be
used for performance acceptance. A short process sample showed RustDesk and
OneDrive activity, but that does not attribute the full host load. Keep the
resource gate and judge runtime load separately.

M616A verified the admission repair on the same short M335 route. At focus
`(-14,3)`, drawable chunk `(-19,2,5)` produced two
`geometry_debt_approach_reserved` retries with `owner_after=1`; no
`dirty_admit_denied` remained for that target. It appeared in RemeshQ at `3/4`
then `1/2`. At those snapshots, desired geometry advanced 12→13 while published
geometry remained 2, without an active mesh/capture/GPU/RAA owner. The target
therefore got admitted but publication was not observed before stop. A relight
handoff was tagged `stale_mesh_input=1` while its field light revision was 1 and
the mesh light revision was 0; the stale-light debt later published revision 1.
That audit field is a light/mesh staleness relationship, not an async mesh
capture rejection. Exact geometry revision bump reasons were not enabled, so the
source of the later geometry churn is open.

The target remained drawable; the route had `visual_holes_rate=0`,
`fly_visible_black_max=6`, and `post_stop_visible_black_max=0`. It also had
`post_stop_readiness_debt_max=36`; these counters have different scopes and are
not direct framebuffer evidence. No operator confirmation was captured. The
only `gpu=1` text match was a first-mesh audit row with `enter_gpu=1`, not a
GPU-only stale-face witness. Check event type and source fields instead of
grepping `gpu=1` alone.

M617A repeated the short M335 route with `CUBA_VISUAL_BLACK_TRACE=1`. For the
target, light debt was queued, admitted and published; the consumer's own field
and meshed light revisions both reached 1604. Geometry desired/published snapshots
progressed 3/2 → 4/3 → 5/4 → 6/5. A final CPU stale-face witness sampled light
10 at `(-305,42,80)` in source chunk `(-20,2,5)`, face 3, while the consumer's
own revisions matched. It was already in RemeshQ at position 0/4; repair owner
flags 3 indicate Dirty + active build. The witness was sampled before observing
that build publish, so do not count it as a post-repair persistent defect.

M617A's host CPU was 65.1/99.5/100% median/p95/max; suite `pass=false`, so it
is not performance acceptance. `visual_holes_rate=0`, fly visible-black max=3,
post-stop visible-black max=1, readiness-debt max=46. These counters are not
framebuffer evidence. Its eye-proxy gate also failed on stale-visual delta 2.0,
stale activity without hole counters, and an untested temporal mid-flight segment;
this is a coverage gap plus a churn signal, not proof of a pixel hole. The report
contains only four renderer rows and no target
`demand_transition_trace` rows: the watch began when the renderer census found
the target, after its revision changes.

Commit `e39de9df` starts the opt-in visual watch when bounded admission first
identifies h5 geometry debt. M618A confirmed this: five target
`mesh_revision_bumped` rows carried reasons and owner state. Two bumps were
`priority_world_streaming_commit` (12→13, 13→14); three were
`priority_dirty_enqueued` (14→15, 15→16, 16→17). Every snapshot showed ordinary
RemeshQ (`kind=3`), Dirty owner only, no active build/capture/GPU, and indices up
to 6/7. Geometry publication reached revisions 13 and 15, but 17 remained in the
ordinary queue at stop.

Commit `7e5a9634` promotes already-owned drawable geometry debt in h5 to priority
RemeshQ in place; it changes queue ordering without adding scheduler budget.
Release build passed. M618A preflight host median/p95 was 35.3/66.9%, but runtime
reached 70.7/99.3/99.6%, so it is not performance acceptance. The suite reported
`visual_holes_rate=0`, fly visible-black max=4, post-stop visible-black max=0,
readiness-debt max=49. Eye-proxy stale delta was 2.5 with no temporal mid-flight
coverage; no framebuffer verdict was captured.

M619A completed the same short M335 route with both
`CUBATARIUM_RELIGHT_AUDIT=1` and `CUBA_VISUAL_BLACK_TRACE=1`. For target
`(-17,2,2)`, all 20 geometry revisions 3…22 eventually published exactly; 15
had a watched schedule row in priority RemeshQ (`kind=2`). The final revision 22
was scheduled at frame 2126 and published at frame 2127. This confirms promotion
and publication on the observed target; missing schedule rows are bounded-trace
coverage gaps, not evidence that those revisions failed.

After M618A, two samples (63.8/75.8% and 59.0/86.9% median/p95) deferred the
flight. A later sample was 46.8/63.1%, so M619A ran. Its runtime host CPU rose
to 71.6/93.0/100.0% median/p95/max: useful for queue diagnosis, not performance
acceptance. The saved Windows-API sampler avoids the missing localized
`Get-Counter` set on this machine and may be used for optional context:

```powershell
.\tools\sample_host_cpu_gate.ps1 -SampleCount 10 -IntervalMs 1000
```

Do not require a CPU threshold before a diagnostic flight. Host CPU includes
Codex and other system work, so it is advisory only; retain samples for context
and record runtime host/app CPU separately. CPU readings do not block a run or
determine its result. M619A had zero mesh-coverage
holes and zero post-stop mesh missing, while readiness debt reached 50 and did
not converge; suite `pass=false`. Those proxies do not establish the displayed
framebuffer. Eye-proxy stale-visual delta was 31 and temporal mid-flight coverage
was untested. No operator visual verdict was captured. Next investigate the
repeated `priority_world_core_repair` / relight revisions and cold-start
readiness debt with a resource-qualified trace; keep the short established route.

M615A artifacts:

- `suite_reports/engine_refactor/m615a_stale_debt_approach_ring_20261016.json`
- `logs/perf_20261016-124231_41960.jsonl`
- `logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-124224.41960`

M616A artifacts:

- `suite_reports/engine_refactor/m616a_outer_geometry_debt_repair_20261016.json`
- `logs/perf_20261016-125253_29904.jsonl`
- `logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-125249.29904`

M617A artifacts:

- `suite_reports/engine_refactor/m617a_geometry_revision_cause_20261016.json`
- `logs/perf_20261016-130222_31232.jsonl`
- `logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-130218.31232`

M618A artifacts:

- `suite_reports/engine_refactor/m618a_proactive_revision_watch_20261016.json`
- `logs/perf_20261016-131122_38316.jsonl`
- `logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-131118.38316`

M619A artifacts:

- `suite_reports/engine_refactor/m619a_promote_owned_geometry_debt_20261016.json`
- `logs/perf_20261016-132510_14976.jsonl`
- `logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-132505.14976`

Повторить целевой lifecycle join можно без ручного разбора JSONL:

```powershell
python tools/analyze_geometry_lifecycle.py `
  bin/logs/perf_20261016-132510_14976.jsonl `
  --coord=-17,2,2
```

### M620A: boundary stale-light trace

The short repeated M335 run completed without process timeout but failed the runtime resource gate: host CPU median/p95/max was 82.0/99.8/100.0%. It is diagnostic, not performance acceptance. Proxy counters were `visual_holes_rate=0`, `fly_visible_black_max=3`, `post_stop_mesh_missing_max=0`, `post_stop_visible_black_max=1`, readiness debt max 30; there is no operator framebuffer verdict.

Join `visual_black_trace` rows to renderer revisions before calling a witness persistent. In M620A, 185/194 rows had desired geometry newer than the published geometry and are therefore in-flight observations. Eight rows had matching geometry and own-chunk light revisions, and all eight were CPU chunk-seam witnesses that sampled nonzero light in an adjacent source chunk. This isolates neighbor input invalidation as a plausible missed lifecycle event, not a confirmed pixel defect.

Static review: the relight result apply path queues dependencies when light values change; the procedural commit path can skip neighbor dirty fanout for far/pressured columns, and persistence applies each slice before its finalization dirty step. The code now queues 26-neighbor drawable mesh dependency debt at procedural commit and on every successful disk slice apply. The normal drain remains deduplicated, skips work already owned or in flight, and obeys its existing per-frame budget.

Next validation: build the Release `Cubatarium` target only. If a fresh CPU preflight passes, repeat one short M335 route with `CUBATARIUM_RELIGHT_AUDIT=1` and `CUBA_VISUAL_BLACK_TRACE=1`; inspect whether seam dependency entries precede the repaired publication and whether exact-revision witnesses recur after publication. Do not use the M620A runtime as a performance baseline.

M621A rebuilt Release and ran the same visible no-teleport M335 route. Preflight passed at 15.0/20.2% host CPU median/p95, but runtime rose to 61.9/97.3/100.0%; the suite failed and this is not performance acceptance. Proxy metrics: in-flight visual mesh coverage 0.0303, stop mesh missing 0, stop visible-black max 0, readiness debt max 37. No operator framebuffer verdict was captured.

The shared dependency queue recorded 158 queued / 73 applied counters, but these totals include relight invalidations. The flight log had 18 exact geometry + own-light-current CPU seam witness signatures sampling nonzero light from an adjacent chunk. M620A had 8; these cross-run counts alone cannot establish regression because the runs differ in cache state, runtime load, and trace coverage. The fix is not yet validated as a resolution.

A target `(-19,2,-2)` had exact seam witnesses at frames 2474 and 2611 and subsequent geometry bumps at 2479 and 2612. The next diagnostic should trace, only for watched chunks: changed source coordinate and cause, dependent queue insertion, drain outcome (deferred / already owned / applied / rejected), snapshot halo signature at capture, and published geometry/light revisions. This distinguishes a missed notification from a snapshot/commit validation gap without lengthening the operator flight.

### M622: use the halo stamp to classify a seam witness

For a stale-light witness emitted through either instrumented path, `stale_mesh_halo_state` in JSONL classifies the last published mesh's neighbor halo: 0 means no valid retained signature, 1 means it still matches current neighbor light cells, and 2 means it differs. `stale_mesh_cache_entry_present` and `stale_mesh_halo_stamp_valid` distinguish an absent cache entry from an entry lacking a stamp. These fields are not pixel-level verdicts. Note: M622A/M622C predate full coverage of the `sample_kind=0` visible-black attribution path; their halo zeros there are default/unmeasured, not state 0 evidence.

With both `CUBA_VISUAL_BLACK_TRACE=1` and `CUBATARIUM_RELIGHT_AUDIT=1`, parse `RelightAudit` lines matching `mesh_dependency`. Each queued event names `cause`, changed `source`, dependent coordinate, insertion status, and backlog. Drain events say whether that dependent was deferred behind a live owner, covered by existing dirty/remesh work, applied, rejected, or no longer drawable. Correlate them to the `visual_black_trace` frame and coordinate:

- Halo state 2 + no applicable post-change dependency lifecycle points to an invalidation gap.
- Halo state 2 + deferred/applied events points to queue fairness, owner, or publication completion.
- Halo state 1 with a current nonzero stale sample points to mesher sampling or witness-to-mesh attribution.
- Halo state 0 cannot distinguish those cases.

M622 instrumentation Release build passed. Its preflight was 60.3/87.7% median/p95 and failed the flight gate; no flight was started. Re-sample after CPU load falls before any run.

GPU-path correction: `CommitGpuMeshResult` is a separate publication route from `ApplyMeshResult`. Commit `a3493579` retains the captured `PendingGpuApply.snapshot.lightHaloSignatures` after each successful GPU commit (including validated empty output), and Release rebuilt successfully. This makes `stale_mesh_halo_state` meaningful for `gpu_greedy` rather than returning unavailable for all GPU meshes.

Historically, the latest preflight before M622A measured host CPU median/p95
56.4/72.0%. This no longer blocks diagnostic flights; the measurement is
retained as context only.

### M622A trace outcome

The route completed (`process_rc=0`, 35 periods, 12 chunk columns). Runtime host CPU was 83.2/100.0/100.0% median/p95/max; suite `pass=false`, so this is not performance acceptance. Proxies: in-flight visual mesh coverage 0, fly visible-black max 4, stop mesh missing and visible-black max 0, readiness debt max 31. No operator framebuffer verdict was captured.

The run recorded 340 `visual_black_trace` rows, 158 with nonzero CPU stale samples, and seven with equal desired/published geometry plus aligned own-light revisions. The focus-slice query was incorrectly guarded by pre-existing watch membership, while the separate per-column attribution writer did not query halo state at all. `d2869ef0` removes the watch guard only from the focus-slice writer; M622C later showed that most stale samples came from the uninstrumented attribution path. None of the M622A zero values establishes signature availability.

`RelightAudit` confirms source-aware dependency events are emitted for watched chunks. The log contains procedural and relight causes, and drain outcomes including `covered_by_existing_owner` and `applied`. The old run cannot prove those owners captured the current halo because the halo state was omitted. M622D later repeated the same route after the trace fixes; CPU preflight is advisory only.

### M622C finding and M622D gate

M622C completed 35 periods normally. It was diagnostic only: host CPU median/p95/max 66.2/88.9/97.1%, `visual_holes_rate=0`, fly visible-black max 3, post-stop missing and visible-black max 0, readiness debt max 35, suite `pass=false`. No operator screen verdict was collected.

Of 144 nonzero stale-sample records, all were `sample_kind=0` per-column attribution rows. Five had exact geometry and own-light revisions, but their halo fields were not populated; the query added after M622A only covered focus-slice rows. Commit `57016e90` adds the query to the attribution writer and separates cache-entry presence from stamp validity. A fresh post-run preflight failed (70.4/89.9% median/p95), so wait for a passing sample before one same-route M622D run. Do not lengthen the route or use these proxies as a framebuffer verdict.

### Open-sky invalidation side path

Static review after M622C found `ApplyEnterOpenSkyBoundary` mutates light in more chunks than the initiating column: its horizontal BFS crosses into loaded neighbors. Before `2f95b9cc`, callers did not receive that changed-chunk set. The function now returns only coordinates whose light-field revision actually changed. All three call sites remesh those source slices and queue their neighboring mesh dependencies with `cause=enter_open_sky_boundary`; the bounded deduplicating drain remains unchanged. Release and static executable verification passed. This is a code-path correction, not yet tied to a visible seam.

M622D completed the documented 35-period route (`process_rc=0`; suite
`pass=false`) without a CPU preflight gate. Runtime host CPU was
69.9/100/100% median/p95/max; app CPU was 22.85/52.43/56.56% of machine
capacity. Treat both as context, not as a blocker or acceptance verdict.

The attribution trace measured 165 nonzero stale-light records across 56
chunks. All 165 had a mesh-cache entry and valid retained halo stamp, and all
165 stamps differed from current neighbor light. Of these, 22 samples across
15 chunks had matching desired/published geometry and own meshed/field light
revisions. Sixteen of those 22 had a matching successful publication in the
bounded lifecycle trace within 30 frames; six had no such publication event in
that trace. The log recorded 3,353 dependency enqueues, 1,381 applied,
6,542 deferred behind a live owner, and 1,312 covered by existing ownership.
Dependency events shared an exact coordinate with 55 of the 56 stale chunks.
These counts show the invalidation path is active, but do not prove the
operator-visible result or that every changed halo is repaired promptly.

The observed dependency causes were `relight_light_change`,
`procedural_chunk_commit`, and `disk_slice_apply`; no
`enter_open_sky_boundary` event occurred. M622D therefore does not exercise the
M623-specific path. Commit `2f95b9cc` remains a statically justified fix whose
runtime effect is unverified.

The INFO dependency events previously lacked a frame identifier. Commit
`45f04f6a` adds `frame_epoch` to queue and drain events; M622E confirmed the
field can be joined directly to stale-witness frames. Its short route completed
with `process_rc=0` and 35 periods. The analyzer returned `pass=false` on
non-CPU checks; CPU remained advisory. Runtime host CPU was 81.7/100/100%
median/p95/max; app CPU was 21.36/50.16/59.86% of machine capacity.

M622E produced 178 nonzero stale-light records over 51 chunks: 177 halo state
2 and one state 1. The latter had stale own-light revision and `frame_epoch=0`,
so it is not a seam witness. Nine rows over eight chunks had current geometry
and own-light revisions; all nine had same-coordinate/same-frame dependency
queue and drain events, then a matching publication within 8–31 frames. Six
drains were `applied`, three `covered_by_existing_owner`. Three coordinates
later became stale again alongside new relight activity, with subsequent
publications in the lifecycle trace. This looks like repeated invalidation and
repair, not a demonstrated permanently stale mesh. No operator framebuffer
verdict was captured; no M623 open-sky cause occurred.

The detailed JSON report is
`bin/suite_reports/engine_refactor/m622e_dependency_frame_join_20261016.json`;
raw perf and INFO traces are indexed in `FLIGHT_EXPERIMENT_SCRIPTS.md`. Next
review the repeated light-change schedule for those three coordinates and
exercise the open-sky path only in a scenario where that mutation actually
occurs. Keep the diagnostic route short; no host CPU threshold is required.

### M622F: open-sky snapshot call accounting

M622F repeated the same short visible route after adding callsite accounting
to `ApplyEnterOpenSkyBoundary`. The process completed (`process_rc=0`, 35
periods), while suite acceptance remained false on non-CPU quality checks.
Host CPU was 100/100/100% median/p95/max, while the application used
15.91/30.58/33.09% of machine capacity. This confirms that host CPU can be
saturated by work outside the game; CPU samples are context only and must not
gate route execution or be described as engine performance acceptance.

The trace observed six `enter_lit_snapshot` calls: four changed no chunks, and
two changed one loaded slice each at `(3,3,2)` from world column `(48,32)`.
The later mutation queued dependencies `(3,3,3)` and `(3,4,3)` with
`enter_open_sky_boundary`; both were drained as `applied`. These startup events
had `frame_epoch=0`, before the first streaming epoch was published. This
verifies execution and dependency transfer at the snapshot callsite, but the
perf lifecycle trace did not capture a matching publication for these exact
coordinates, and no operator framebuffer verdict was collected. The
`enter_visual_repair` and `relight_terrain_column` callsites remain untested.

Proxy results were `visual_holes_rate=0.0303`, fly visible-black max 4,
post-stop visible-black max 0, and post-stop readiness debt max 57. These are
diagnostic proxies, not evidence that the operator saw blank chunks. Report:
`bin/suite_reports/engine_refactor/m622f_open_sky_call_accounting_20261016.json`;
raw perf and INFO paths are listed in `FLIGHT_EXPERIMENT_SCRIPTS.md`. The next
short diagnostic should correlate the epoch-zero dependency drains with mesh
publication, or add a startup-safe correlation key, then exercise the other
two callsites only if a compact route naturally reaches them. Do not add a
host-CPU admission threshold.
