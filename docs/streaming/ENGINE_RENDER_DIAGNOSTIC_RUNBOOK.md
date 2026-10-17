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
| `tools/analyze_renderer_pixel_trace.py` | Сводит подробные renderer pixel trace записи; `--watch-chunk X,Y,Z` сохраняет framebuffer-пробы для указанного чанка по depth surface или voxel-ray hit. |
| `tools/flight_sim_timeline_analyze.py` | Сопоставляет переходы и короткие визуальные/streaming события по timeline. |
| `tools/analyze_framebuffer_pixel_grid.py` | Сводит пространственную сетку framebuffer probes. |
| `tools/analyze_geometry_lifecycle.py` | Join для geometry revision bump → queue schedule → точная публикация по координате и attempt id. |
| `tools/analyze_hole_event_pixels.py` | Разбирает pixel probes вокруг hole/stall событий. |
| `tools/run_with_host_cpu_sampler.py` | Записывает host/process CPU нагрузку; используйте для performance baseline, а не для доказательства изображения. |
| `tools/flight_sim_fixed_day.py` | Фиксирует время суток для воспроизводимых сравнений цвета/освещения. |
| `tools/capture_flight_sim_window.ps1` | Сохраняет визуальные window-captures; смотрите рядом с app-side probes, учитывая compositor и overlay. |

Host CPU is context only. Never block a diagnostic flight, mark it invalid, or
set its PASS/FAIL from total host load: Codex, a compiler, and other processes
share that number with the game. Record host and app CPU samples when useful,
but interpret engine performance from the application's own frame/stage
measurements and compare only runs with adequate measurement conditions.

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
с фиксацией startup, runtime app CPU, frame-time и визуальных lifecycle joins.
Host CPU остаётся контекстом: запуск не откладывается из-за Codex или фоновой
системной работы.

### M605/M607: cold-entry и классификация тёмных pixel probes

В M605/M607 использовался только короткий проверенный GUI no-teleport маршрут
(35 s cruise + 20 s stop + 15 s idle). M605 оказался перегружен: host CPU
median/p95 73.9/99.4%. Для M607 свежая карта того же seed/radius создалась за
59.827 s (597 chunk files); cold-run host CPU составил 41.0/85.5%, warm-run
75.5/99.5%. Поэтому пара cold/warm несопоставима, несмотря на одинаковую
траекторию. В то время для сравнения пары предлагались пороги host CPU
(median <=50%, p95 <=75% и разница arm-ов <=15 п.п.). Эти пороги сняты:
системный sampler включает Codex и другие процессы. Сохраняйте host CPU только
как контекст, не откладывайте диагностический запуск и не объявляйте результат
непригодным только из-за общей загрузки.

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
прогона имеют разную общую системную нагрузку, поэтому timing нельзя считать
строгим performance A/B. Старые host CPU thresholds для пары отменены: брать
существующий M335 сценарий и те же параметры, записывать host/app CPU как
контекст, а вывод строить по runtime app/frame metrics и совпадающим pixel,
queue и publication witnesses. При необходимости повторять только arm с
нехваткой диагностических данных. Новые seed-проверки проводить отдельно от
повторяемой пары.

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
проверки не нужен. Host CPU не является условием запуска; M615A показал, что
низкий preflight не предсказывает загрузку во время маршрута.

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
geometry revision, and post-stop focus debt. Start without a host-CPU threshold
and record runtime host/app CPU for context; M615A showed that preflight does not
predict runtime load. Keep GPU-only witness ambiguity separate:
the current chunk-wide proxy may mistake unrelated sky-lit blocks for proof that
a dark cave face is stale. No long operator-monitoring flight is needed.

The post-M616-build preflight on 2026-10-16 sampled host CPU at median 57.5% and
p95 93.6% with the application stopped. A later fresh preflight admitted M616A,
but runtime host CPU rose to median/p95/max 68.6/98.6/100%; this run cannot be
used for performance acceptance. A short process sample showed RustDesk and
OneDrive activity, but that does not attribute the full host load. The former
host resource gate is retired; judge the engine from its own runtime timings and
queue/publication evidence while retaining host CPU as context.

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
readiness debt with a short trace; host CPU does not gate the run.

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

The short repeated M335 run completed without process timeout. Under the former runtime resource gate it was marked non-acceptance because host CPU median/p95/max was 82.0/99.8/100.0%; that host-only gate is retired. Keep its runtime and proxy metrics as diagnostic evidence: `visual_holes_rate=0`, `fly_visible_black_max=3`, `post_stop_mesh_missing_max=0`, `post_stop_visible_black_max=1`, readiness debt max 30. There is no operator framebuffer verdict.

Join `visual_black_trace` rows to renderer revisions before calling a witness persistent. In M620A, 185/194 rows had desired geometry newer than the published geometry and are therefore in-flight observations. Eight rows had matching geometry and own-chunk light revisions, and all eight were CPU chunk-seam witnesses that sampled nonzero light in an adjacent source chunk. This isolates neighbor input invalidation as a plausible missed lifecycle event, not a confirmed pixel defect.

Static review: the relight result apply path queues dependencies when light values change; the procedural commit path can skip neighbor dirty fanout for far/pressured columns, and persistence applies each slice before its finalization dirty step. The code now queues 26-neighbor drawable mesh dependency debt at procedural commit and on every successful disk slice apply. The normal drain remains deduplicated, skips work already owned or in flight, and obeys its existing per-frame budget.

Next validation: build the Release `Cubatarium` target only, then repeat one short M335 route with `CUBATARIUM_RELIGHT_AUDIT=1` and `CUBA_VISUAL_BLACK_TRACE=1`; host CPU does not gate launch. Inspect whether seam dependency entries precede the repaired publication and whether exact-revision witnesses recur after publication. Do not use the M620A runtime as a performance baseline.

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

M622 instrumentation Release build passed. The earlier workflow recorded a
60.3/87.7% host CPU preflight and withheld M622A; that admission gate was too
strict because host usage includes Codex and other system work. M622A was later
run without a CPU threshold. Treat sampler output as context only; do not wait
for a host CPU preflight to pass before a diagnostic flight.

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
verifies execution and dependency transfer at the snapshot callsite. The INFO
audit later records stale-light debt closed at field/meshed/published light
revision 19 for `(3,3,2)` and revision 2 for `(3,3,3)`. Concurrent relight
invalidations mean this does not isolate open-sky as the sole cause. For
`(3,4,3)`, slice/handoff state is recorded but no explicit matching
publication or settled-debt closure appears. The perf lifecycle ring did not
retain these startup coordinates, and no operator framebuffer verdict was
collected. The `enter_visual_repair` and `relight_terrain_column` callsites
remain untested.

Proxy results were `visual_holes_rate=0.0303`, fly visible-black max 4,
post-stop visible-black max 0, and post-stop readiness debt max 57. These are
diagnostic proxies, not evidence that the operator saw blank chunks. Report:
`bin/suite_reports/engine_refactor/m622f_open_sky_call_accounting_20261016.json`;
raw perf and INFO paths are listed in `FLIGHT_EXPERIMENT_SCRIPTS.md`. The next
short diagnostic should preserve coordinate-scoped lifecycle evidence through
the pre-first-frame window, especially to resolve `(3,4,3)`, then exercise the
other two callsites only if a compact route naturally reaches them. Do not add
a host-CPU admission threshold.

### M622G: startup trace retention verified

Commit `1a8b899c` adds bounded open-sky-specific lifecycle and demand-owner
rings so startup dependency coordinates survive saturation of the generic
8,192-row ring. The same short, visible route completed 35 periods. Analyzer
acceptance remained false on non-CPU gates; host CPU was 99.8/100/100%
median/p95/max while game CPU was 15.25/31.25/40.63% of machine capacity.
CPU remains contextual only. No operator framebuffer verdict was recorded.

The new trace emitted 131 worker/GPU lifecycle rows for 18 coordinates and
339 demand transitions for 21 coordinates. It preserved frame-zero source and
dependent activity. Source meshes `(3,3,2)` and `(3,3,1)` reached matching
published geometry/light revisions at frames 1 and 4. Dependencies `(3,3,3)`
and `(3,4,3)` were drained as `applied` and had epoch-zero revision bumps, but
no valid build attempt/publication closure was retained. Later zero-identity
`install_published` rows are not valid completion evidence. Both targets were
behind the westbound camera path, so investigate owner persistence and
out-of-focus scheduling before treating this as a rendering defect.

Report: `bin/suite_reports/engine_refactor/m622g_open_sky_lifecycle_retention_20261016.json`.
Raw logs are listed in `FLIGHT_EXPERIMENT_SCRIPTS.md`. For subsequent
diagnostics, preserve the short route and use the pinned trace to determine
whether dirty ownership survives until a dependency enters the visual focus.

### M622K–M622L: dependency reason and safe drain tracing

The M622K attempt has zero perf periods and `run_outcome=harness_fail`; exclude
it from renderer comparisons. The first implementation queried a dependency
map entry with `.at()` after the successful drain branch had erased it. Use the
safe trace in the current source, which emits the retained reason or
`already_drained` without throwing. M622L completed the established compact
35-period M335 route (`process_rc=0`) and confirmed that
`priority_light_dependency_repair` reaches 369 dependent coordinates. The
analyzer still returned `pass=false` on non-CPU checks. Its visual-hole and
visible-black values are mesh/readiness proxies, not framebuffer proof.

For reason-aware dependency admission, classify only
`relight_light_change` and `enter_open_sky_boundary` as light-only. Coalescing
must allow a geometry-capable cause to override a pending light-only cause.
The singular seam/coverage invalidation API defaults to geometry-capable.
When inspecting the next run, compare desired/published geometry revisions by
coordinate and retain the applied-event reason. Host CPU is advisory only and
never blocks a run.

Reports: `bin/suite_reports/engine_refactor/m622k_light_dependency_reason_20261016.json`
and `bin/suite_reports/engine_refactor/m622l_light_dependency_reason_20261016.json`.

### M622M–M622N: trace singleton open-sky invalidations

M622M confirmed that reason-aware batch drains can apply light-only repairs,
but watched dependents still received `priority_dirty_enqueued` at frames
1464, 1646, and 1833. A static callsite audit found that each changed chunk
from `ApplyEnterOpenSkyBoundary` also goes through the singleton invalidation
API, whose default is intentionally geometry-capable for seam/coverage debt.
The open-sky caller must pass `PriorityLightDependencyRepair` explicitly.
M622M did not log singleton enqueue reasons, so it cannot establish that this
path caused every geometry drain.

The correction adds a reason-taking singleton overload while retaining the
geometry default for seam/coverage callers. The two open-sky callsites pass the
light-only reason; coalescing still lets any later geometry-capable cause win.
The trace uses `mesh_dependency event=queued_single` and the drain's existing
`applied` event. In the next compact M335 run, join those rows by coordinate
and frame epoch and check whether singleton light-only queue entries drain as
`priority_light_dependency_repair`. M622N completed this check; its result is
recorded below. Revision movement and publication closure remain necessary
checks; do not treat proxy pass/fail as operator pixel proof. Host CPU is
informational and must not gate the route.

M622N completed the compact route and verified the singleton reason path:
428 watched enqueue rows comprised 131 light-only rows over 80 coordinates
and 297 geometry rows over 244 coordinates. The drain logged both retained
classes. The `(-11,2,2)` retry samples stayed at geometry revisions 2/1, but
this was a fresh world rather than a controlled replay of M622M. The analyzer
remained `pass=false` on non-CPU proxy gates; no operator framebuffer verdict
was captured. Keep the reason-aware route and do not interpret the changed
aggregate proxy score as a visual regression or improvement.

Report: `bin/suite_reports/engine_refactor/m622n_light_source_reason_20261016.json`.

### M622O–M622Q: deferred relight no-op reconciliation

M622O ran the compact M335 route on the persistent `World_164`: 35 periods,
process exit 0, no deferred-far entries, FIFO median 1, and no age-service
events. Mesh-hole proxy was 0.0303, fly visible-black max 4, post-stop
visible-black max 0, and readiness-debt max 36. This is a saved-world reference,
not a controlled comparison with the M622N map.

M622P was not a replay on M622N's map. Its requested world alias resolved to
new runtime `World_183`; INFO recorded `light_flags event=load outcome=missing`.
M622N used runtime `World_182`, also with `outcome=missing`. M622P recorded 31
accepted age-service events, deferred-far peak 38, and FIFO median 15. The aged
requests were placed at priority index 0 or 1, but age ranged up to 14.929 s. For
`(-7,2)`, INFO shows a validated capture of band `0:103` completing after a new
deferred request for `32:47`; that deferred entry survived until a later
age-service admission, whose capture was discarded as
`surface_band_no_relight_needed`. The trace establishes stale queued work, not
a user-visible bad frame.

M622Q added a Release-path check in `AdmitDeferredFarRelightColumns`: before
admission, use the existing no-op policy to discard a deferred record when the
column is already lit-ready, has no pending-light owner, and no drawable dark
face remains in the saved Y band. The Release build and static EXE verification
passed. M622Q used runtime `World_184`, with `light_flags event=load
outcome=missing`, so it was not a replay on M622P's map. The compact M335 run
completed 35 periods with process exit 0 and logged 205
`deferred_far_skip reason=already_satisfied` events. Compared with M622P,
aged-service events fell 31→6 and deferred-far peak 38→17, but these were
separate fresh worlds and this is not a controlled A/B. The mesh-hole
proxy increased 0.0606→0.1212, fly visible-black max changed 15→14, and
post-stop visible-black/stalled max remained 1. Do not claim a visual
improvement; no operator framebuffer verdict was recorded.

The analyzer returned `pass=false` on non-CPU performance/readiness gates. Host
CPU median/p95/max for M622O/P/Q was 93.7/100/100%, 86.2/100/100%, and
90.7/100/100%; app CPU medians were 19.36%, 19.48%, and 21.38% of machine
capacity. All three routes completed. Host CPU is informational, never an
admission gate.

M622R verified the miss-witness unit correction by static call-site/API review:
`FindNearestMissingGreedyMesh` returns chunk coordinates, and
`EnqueueTerrainColumnRelight` takes block-space X/Z; the call now multiplies
both axes by `CHUNK_SIZE`. The no-op deferred clear reports its precise
`already_satisfied` reason. One compact Release M335 route completed 35 periods
with process exit 0 on runtime `World_185`. The requested alias was a new cold
world (`light_flags event=load outcome=missing`). INFO contained 116 no-op skip
events and 116 matching clear events. A `column=(-1,2) band=0:-1` deferred
entry is consistent with preserving negative chunk coordinates after
conversion, though the event has no producer tag and does not isolate this
call. Correlating each skip with the latest deferred-owner event shows that 103
of 116 were most recently deferred for `fifo_admission_denied` with priority
set. Column `(-11,4)` repeatedly cycled through defer and no-op clear during the
last part of the route, with later cycles under 0.5 s apart. Nearby pending
notes name `WorldStreaming::InitChunkScheduler`, `ChunkEmergeCoordinator`, and
`AdmitUnfinishedVisualDemand`, but FIFO-denial records do not contain a source
tag, so the exact producer for each cycle is still unknown.

M622R's analyzer returned `pass=false` on non-CPU gates: mesh-hole proxy 0.2121,
fly visible-black max 18, post-stop readiness debt 34. Post-stop mesh-missing
and no-ticket visible-black maxima were both zero; raw visible-black/stalled
max was 1. No operator framebuffer verdict was captured, so these are not
proof of an on-screen black frame. Deferred-far peak 22, FIFO median 23, FIFO
drop delta 90, and 13 age-service events show ongoing queue pressure; 116
already-satisfied clears also show repeat production of no-op deferred work.
Host CPU median was 92.6%, with application CPU median 19.1% of machine
capacity. Host CPU is context only and must not block a run.

M622S/T completed the producer and slice-settlement trace and tested an early
no-op check at FIFO denial. Keep first-mesh settlement checks, the FIFO cap,
and pin policy intact. The remaining investigation is limited to deferred
no-ops created by separate visible-replacement/admission paths; keep framebuffer
evidence separate from mesh/readiness proxies.

Report: `bin/suite_reports/engine_refactor/m622r_miss_witness_chunk_units_20261016.json`.
Perf: `bin/logs/perf_20261016-180806_35700.jsonl`. INFO:
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-180801.35700`.

### M622S producer and settlement trace

FIFO-denial events now contain `source`, chunk/block key, Y band, FIFO depth,
`pending_column`, `lit_ready`, and counts for loaded/currently-settled/pending
slices. There were 1,077 denials in the 35-period M335 route; all had world
context. Major sources were `DrainIdleFocusPendingLight` (326),
`DrainRelightQueues` and its lambdas (298 combined),
`EnqueueVoidDarkColumnRelightNote` (134), `RecoverUnlitFocusMeshes` (127), and
`AdmitUnfinishedVisualDemand` (98). The 102 already-satisfied deferred clears
were associated with FIFO denial in 94 cases and visible exact replacement in
8 cases.

The 28 `miss_witness` denials had `pending_column=0` in every sample; 27 were
`lit_ready=1`. Thirteen had all loaded slices currently settled (4/4 or 5/5);
the others had at least one resident slice without a current settlement stamp,
but no slice in the pending band. Settlement stamps alone do not decide the
no-op policy: the existing rule requires lit-ready, no pending-light owner,
and no drawable dark face in the requested band.

### M622T early no-op check

At FIFO denial, the enqueue path now applies that same no-op rule to the union
of the current request and any saved deferred band. Audit logs use
`relight_fifo event=noop_skip source=...`; genuine requests continue to emit
`event=deny` and enter the existing deferred path. No queue cap or pin policy
changed.

M622T completed 35 periods with process exit 0, no runner timeout, and cold
runtime `World_187`. It logged 190 early skips, 42 later deferred-admission skips,
and 43 already-satisfied clears. The analyzer remained `pass=false` on non-CPU
gates. Mesh-hole proxy was 0.0909, fly visible-black max 10, stop mesh-missing
max 0, readiness debt max 39, and post-stop visible-black/stalled max 2. No
operator framebuffer verdict was collected. M622S and M622T ran on different
cold worlds under different host load, so cross-run image/performance claims
are not supported. CPU measurements are context only and are never preflight
gates.

M622S report/perf/INFO: `m622s_fifo_producer_slice_settlement_20261016.json`,
`perf_20261016-190703_43384.jsonl`, `...INFO.20261016-190659.43384`.
M622T: `m622t_noop_before_deferred_20261016.json`,
`perf_20261016-191404_43720.jsonl`, `...INFO.20261016-191400.43720`.

### M622U: identify stable published face-debt samples

Static review correlated M622T's remaining 42 deferred-admission no-op skips
with same-coordinate history. Thirty-four were last seen deferred after FIFO
denial and became satisfied before later admission. For eight, the latest
same-coordinate `visible_exact_replacement` event describes a FIFO victim
removed to admit exact visible work. Deferred entries do not retain their
original identity or producer, so these joins are approximate. The observed
no-op cleanup and exact-victim eviction provide no evidence for another
queue-policy change.

M622T also repeatedly sampled a fully solid chunk at `(-8,2,5)` with current
geometry revisions, a resident zero-quad GPU mesh, `face_debt_mask=33`, and
matching overlay mask. It sat at Chebyshev focus-ring distance 3–4 while the
focus advanced. `HasStablePublishedFaceDebt()` explicitly recognizes this
shape; `EnsureVisualRepairDirtyPriority()` leaves it to the face-debt repair
path instead of repeatedly minting FirstMesh work. `MissingMesh` in the
focus-ring census is therefore an unfinished-coverage label, not proof of a
black framebuffer region.

M622U adds `stable_published_face_debt` to opt-in `focus_slice_trace` and ran the
same short visible M335 route on cold runtime `World_188` (`light_flags
event=load outcome=missing`). The run completed 35 periods with process exit 0
and no timeout. Of 1,024 focus-slice rows, 941 were stable published face-debt
across 55 coordinates; all 941 had current published geometry, zero GPU quads,
and an overlay mask covering the outstanding face debt. Those rows were at
focus-ring distance 3 or 4. Analyzer `pass=false` on non-CPU gates: mesh-hole
proxy 0.1515, fly visible-black max 12, post-stop mesh-missing max 0,
readiness debt max 37, and post-stop visible-black/stalled max 1. No operator
framebuffer verdict was collected. This is not evidence that the world looked
wrong; it does show the focus-ring debt proxy is dominated by stable boundary
coverage in this run.

The M622U source change is diagnostic only. CPU usage is recorded as context,
not used as a launch gate. Do not relax face-debt readiness or suppress its
repair based on these samples. The next useful step is to correlate one
camera-visible missing-slice witness with its exact renderer/pixel probe and
repair owner on the established short cold-world route. Do not use a long
operator-monitoring flight for this question.

M622U report/perf/INFO: `m622u_stable_face_debt_trace_20261016.json`,
`perf_20261016-193322_30696.jsonl`, and
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-193318.30696`.

### M622V: frustum, peak lifecycle, and sparse pixel evidence

M622V adds `stable_published_face_debt` to `view_frustum_coverage_trace` and
`camera_band_peak_render_probe`. The marker is a diagnostic classification;
do not equate it with a pixel defect. In the short cold `World_189` M335 run,
74/256 broad-frustum samples had stable face debt. Seventy-two passed drawable,
render-ready, and visible-MDI checks. The two exceptions had a repair ticket,
no ready ref or visible MDI command, and nearest AABB distance about 79 units;
the measured fog end was 32.

There were 151/256 samples without a render-ready ref. Twenty-two were within
32 units of the camera, across seven coordinates. All had no drawable mesh;
18 were mesh-satisfying, and four samples across three coordinates had
`MissingMesh` plus an active FirstMesh ticket with queue age at most seven
frames. Same-incarnation lifecycle rows later ended in successful publication
for those three targets at frames 889–962. These are owned startup samples, not
ownerless queue debt. No selected pixel ray hit these target chunks.

The 19 camera-band render rows represented 16 unique chunks. Eighteen rows
were non-stable exact-frustum candidates; their rectangles are conservative
projected chunk AABBs. Scheduler traces show all 16 unique targets eventually
had an accepted mesh schedule within 27 frames (median 14). At the last sample
of their 64-frame watch, all 16 had drawable meshes; 11 still had priority
remesh entries aged 41–60 frames, and five had no dirty entry. Scheduler
admission and drawable presence establish progress, not that every pixel in a
rectangle was covered.

The pixel probe used `glReadPixels` on 320 pixels in the four peak-triggered
frames. It found zero exact voxel-mesh triangle gaps. Across the 2,960 pixel
samples, 36 provisional opaque-depth mismatches were all farther than the
configured fog end (44.7–56.8 versus 32); all nine unloaded-before-hit rays
were farther still (59.1–63.1). These are outside the measured fog horizon and
do not support an in-fog hole. Sampling is sparse: no sampled ray hit the
camera-band candidate chunk itself, so use a targeted projected-pixel or exact
screen-ray witness before calling the candidate visible or missing.

Analyzer `pass=false` is retained in the report. Relevant values are
`visual_holes_rate=0.0909`, `post_stop_mesh_missing_max=0`,
`post_stop_readiness_debt_max=31`, and
`post_stop_visible_black_stalled_max=1`. The analyzer's readiness and stop
gates describe unfinished work, not a framebuffer verdict. Host CPU remains a
context metric only and must not gate a run.


### M622W/X/Y provenance correction; M622Z verified load

The earlier descriptions of M622W/X/Y as saved-world replays were incorrect.
Their requested alias `World_M622V_FrustumStableFaceDebt_20261016` was missing;
the runtime created `World_190`, `World_191`, and `World_192` respectively.
The INFO logs show `RelightAudit outcome=missing`, `WorldLoadPerf kind=create`,
zero chunk files, and 43,264 generated columns in each run. `resume=true` is
route intent, while `warmup_sec_stamp` labels periods; neither proves a saved
world or warm storage. Their synchronized pixel/ray results still describe the
captured generated-world samples, but cannot be cited as warm replay evidence.
M622V/W/X/Y do not form a controlled cold/warm pair.

M622Z is the first verified saved-world replay. It used existing `World_164`,
`-RequireExistingWorld`, the established short visible no-teleport M335 route,
and requested `(120,56,56)` as the product start. The users file already held
that position and the report predates a runtime expected-versus-actual start
assertion, so its matching focus did not prove the pin was honored. The later
`cd606d3d` runner change checks that explicitly. Its manifest and INFO log agree on
requested/runtime world `World_164` and `WorldLoadPerf kind=load`: 121/121
spatial columns were present, zero generation columns ran, and startup took
5.371 seconds. The loader read zero individual chunk files, so use the recorded
load operation/column totals and do not infer a different storage layout. The
route completed 35 periods, process exit 0, no timeout; focus moved from `(7,3)`
to `(-3,3)`.

The screen-ray capture recorded 300 stream rays, 2,500 pixel probes, and 100
exact coordinate joins. Fourteen synchronized refresh candidates were inside
the pixel-specific fog horizon; all 14 hit the same voxel, with zero voxel or
mesh-ray gaps. Across all joined ray pairs with hits, 52 matched the same block.
This is sampled coordinate agreement, not full-frame coverage. A renderer trace
found 56 low-luminance samples (<32); all had drawable, draw-ready opaque depth
and none had a voxel-mesh ray gap. Sixteen had unsettled or preview light (13
preview). There is no operator framebuffer verdict, so these dark pixels do not
prove an operator-visible defect.

The analyzer returned `pass=false` on non-CPU readiness/convergence gates:
`visual_holes_rate=0.0303`, `fly_visible_black_max=18`,
`post_stop_missing_max=29`, and `post_stop_visual_holes_max=0`. Preserve these as
diagnostic counters. Do not change queue policy from them alone. CPU is
contextual only; there is no host-CPU admission gate and long operator-monitoring
flights are unnecessary.

For future warm evidence, require an existing folder before launch and verify
all of: requested world equals runtime world, startup operation is `load`,
`generation_columns_completed=0`, and the actual product start focus is pinned.
Read `run_manifest.world_startup.cache_mode` for storage/cache classification;
keep `period_warmup_stamp` as a separate label. A controlled cold/warm pair
still requires cold generation followed by replay of that exact runtime world.

M622Z artifacts: report
`bin/suite_reports/engine_refactor/m622z_warm_world164_screenray_probe_20261016.json`,
parity analysis
`bin/suite_reports/engine_refactor/m622z_screen_ray_parity_20261016.json`, pixel
analysis
`bin/suite_reports/engine_refactor/m622z_renderer_pixel_trace_20261016.json`,
perf `bin/logs/perf_20261016-205417_39284.jsonl`, and INFO
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-205413.39284`.

### M622AA–AE: start validation and relight short-path diagnosis

M622AA requested a product start whose focus should be `(7,3)`, but the
`World_192` runtime started at `(-3,1)` and moved to `(-11,1)`. Although the
route record said `product_start_position_pinned=true`, the report had no
expected-versus-actual start fields; this is a failed pin verification even
though process exit was zero. Its filename says `stationary`, but it was a
moving product route. Commit `cd606d3d` adds actual runtime focus verification.

M622AB also used a moving product route on `World_164`. Its manifest verified
requested start `(120,56,56)`, expected/actual focus `(7,3)`, and a successful
user-position-file pin; the route later moved to `(-1,3)`. The pixel trace had
2,740 probes, 74 low-luminance samples, all
attached to drawable opaque depth; there were no missing surface joins or
light-revision mismatches. The paired trace had 400 streaming rays and 100
exact joins, with 53 matching opaque-hit blocks. Since the camera moved, it did
not measure preview settlement at one fixed view. It is not a static lighting
probe or a complete framebuffer verdict.

M622AC was a 35-second fixed-camera `--no-fly` probe on existing `World_164`,
focus `(7,3)`. It produced 1,600 pixel probes; 124 were low-luminance, all
joined drawable opaque surfaces. No mesh-ray gap or light-revision mismatch
was sampled. Chunks `(5,3,5)` and `(5,3,6)` remained preview through the captured
interval, while focus pending-light median was 36 and relight drain was near
zero for 28 seconds. This is the baseline for the short-path change.

M622AD added `cooperative_streaming_tick_blocked` to perf samples. It stayed
zero across 29 samples. In 17/18 periods the async scheduler did not tick after
pre-scheduler work exceeded the 5 ms budget; `focus_missing_mesh=0`, the
underfeet mesh existed, and pending-light was 30–34. Since the normal capture
drain follows that early return, the queue could be stranded without a coop
block. Keep this field when collecting future traces.

Commit `3b6ea7d7` now services at most one relight capture on that early-return
path when near-focus or underfeet light debt exists. M622AE kept focus `(7,3)`
for 35 seconds. Pending-light median fell from 34 in M622AD to 0 (max 5),
post-stop pending median was 0, and final relight apply was recorded in six
frames.
The renderer trace had 1,440 probes and 147 low-luminance samples, all on
drawable opaque surfaces, with no missing surface joins or light revision
mismatch. Four frontier chunks had provisional-light samples. The pixel analyzer
found 125 same-pixel/same-camera preview-to-nonpreview observation intervals,
all five seconds apart; this is a sampling bound, not an exact transition time.
The lifecycle trace recorded 86 `light_calculation_settled` events and joined
settlement to four observed chunks at 0.7–1.7 seconds after first preview. For
`(5,3,5)`, mesh publication and install were recorded at about 1.03 and 1.16
seconds. No same-view pixel sample captured a settled-light/preview intermediate
state. Readiness and mesh debt remained, and no operator verdict was captured,
so this is mechanism evidence, not acceptance.

The M622AC/AD/AE reports use an empty scenario with `--no-fly`; the analyzer's
flight acceptance gates therefore fail by design on movement-related metrics.
Do not run `--scenario product-174657-far` for a short static trace: that named
scenario supplies its long route timing even with `--no-fly`. Keep host CPU as
context only; the M622AE host reached 98.8%, but the run was not gated or
withheld. See the exact short probe command in `FLIGHT_EXPERIMENT_SCRIPTS.md`.

Artifacts:

- M622AA report/perf/INFO: `m622aa_world192_stationary_light_trace_20261016.json`, `perf_20261016-210605_14756.jsonl`, and `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-210601.14756`.
- M622AB report/perf/INFO: `m622ab_world164_stationary_light_trace_20261016.json`, `perf_20261016-211730_48924.jsonl`, and `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-211726.48924`.
- M622AB pixel and parity analyses: `m622ab_renderer_pixel_trace_20261016.json`, `m622ab_screen_ray_parity_20261016.json`.
- M622AC report/perf/INFO/pixel analysis: `m622ac_world164_stationary_light_trace_20261016.json`, `perf_20261016-212350_40976.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-212347.40976`, and `m622ac_renderer_pixel_trace_20261016.json`.
- M622AD report/perf/INFO: `m622ad_world164_coop_tick_light_trace_20261016.json`, `perf_20261016-213741_41508.jsonl`, and `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-213736.41508`.
- M622AE report/perf/INFO/pixel analysis: `m622ae_world164_shortpath_relight_20261016.json`, `perf_20261016-214318_46276.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-214314.46276`, and `m622ae_renderer_pixel_trace_20261016.json`.

### M622AF/AG: cold world and exact replay

M622AF requested a fresh alias and the application created runtime `World_193`.
The report shows `create`, 43,264/43,264 generated columns, 40.563 seconds,
focus `(-3,1)`. M622AG replayed that exact runtime world with
`-RequireExistingWorld`: requested/runtime names match, operation is `load`,
121/121 spatial columns are present, generation is zero, and load took 4.793
seconds. Both manifests have the same seed and metadata hash, so these two runs
form a controlled world-identity cold/replay pair. Individual chunk-file reads
were zero; retain the loader fields and do not infer storage layout.

Both runs used a 25-second visible static no-fly probe. Each completed with
process exit zero, no timeout, 13 periods and nine steady periods. Focus
pending-light median was zero; final relight apply appeared in six cold frames
and three replay frames. The cold pixel trace sampled 960 pixels, 72 below luma
32; all had drawable opaque surfaces, with zero unsettled/preview dark samples,
missing joins, or light-revision mismatches. The replay trace sampled 1,920
pixels, 147 below luma 32; all had drawable opaque surfaces and no missing joins
or revision mismatches. Fifteen replay dark samples were unsettled/preview and
five were preview; 59 fixed-view preview-to-nonpreview observations were
bounded by 0–1 second. The lifecycle trace recorded 154
`light_calculation_settled` events overall, but no same-view pixel sample caught
the intermediate settled-light/preview state. The analyzer returned
`pass=false` on static movement/readiness gates.
No operator framebuffer verdict was recorded, so these results do not clear
unsampled pixels or human-visible defects.

M622AF/AG reports and diagnostics:

- Cold report/perf/INFO: `m622af_cold_world_short_light_probe_20261016.json`, `perf_20261016-214926_10140.jsonl`, and `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-214922.10140`.
- Cold pixel analysis: `m622af_cold_renderer_pixel_trace_20261016.json`.
- Replay report/perf/INFO: `m622ag_world193_warm_light_probe_20261016.json`, `perf_20261016-215134_45964.jsonl`, and `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-215130.45964`.
- Replay pixel analysis: `m622ag_warm_renderer_pixel_trace_20261016.json`.

M622AH attempted that moving route, but `World_193` had no solid ground at the
requested `(120,56,56)` locus. `EnsurePlayerOnGround` fell back to world spawn
`(-44,51.12,20)`, producing actual focus `(-3,1)` instead of expected `(7,3)`;
the start-integrity check correctly rejected the route. Do not use this cold
world for that pinned M335 start unless its route columns have been generated.
M622AI then used the established short M335 route on `World_164`; start focus
matched `(7,3)`, the camera moved west to `(-3,3)`, and the process completed
without timeout. Host CPU was 68.8/86.5/97.1% median/p95/max (app CPU
31.3/39.4/44.9% of machine capacity); these values are context, not gates.

M622AI's aggregate report remained `pass=false` on timing, scheduler, and
stop-line criteria. `visual_holes_rate=0.0606`, `pending_light_focus` median 0,
`post_stop_mesh_missing_max=0`, `post_stop_visible_black_max=2`, and readiness
debt max 31. Pixel analysis found 62 low-luminance probes of 2,880; all 62 joined
a drawable opaque depth surface, 55 had settled light with no preview, and five
were unsettled/preview shader samples. There were no missing surface joins or
light-revision mismatches. Ten chunks had provisional-light samples; moving
camera observations bounded preview-to-nonpreview visibility at 1–16 seconds,
but camera positions differed and exact same-view transitions were not observed.
This confirms that the mesh-coverage and luma counters are not direct hole
proofs; it does not settle the operator-visible transient-light report.

The next bounded investigation is to use the existing pixel/lifecycle trace on
one short repeated route and require same-pixel/same-camera witnesses plus the
light-settlement and mesh-publication event chain for a frontier chunk. Do not
block on host CPU or repeat the invalid `World_193` start. Keep new-world checks
as the separate static cold/replay pair. Long operator-monitoring flights remain
unnecessary.

M622AH artifacts: report `m622ah_world193_m335_moving_short_20261016.json`,
perf `perf_20261016-215408_44256.jsonl`, INFO
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-215404.44256`.
M622AI artifacts: report `m622ai_world164_m335_short_20261016.json`, pixel
analysis `m622ai_renderer_pixel_trace_20261016.json`, perf
`perf_20261016-215807_42564.jsonl`, INFO
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-215803.42564`.

### M622AJ: higher-cadence fixed-view preview handoff

The M622AJ Release build added the opt-in
`CUBA_VISUAL_BLACK_TRACE_PIXEL_PROBE_STRIDE` setting. At stride 15, the static
`World_164` probe recorded 4,080 pixel samples in 25 seconds versus 1,440 in the
35-second M622AE trace. The camera remained at `(120,50.12,56)` and focus
`(7,3)`. Four chunks showed provisional-light pixels. Exact same-pixel/camera
observations found preview-to-nonpreview by 1 second for `(5,3,4)` and `(5,3,5)`
and by 2 seconds for `(5,3,6)`. Since the analyzer's pixel sample clock is
second-granular, these are upper bounds, not exact shader handoff times.

For the same chunks, lifecycle events recorded light settlement at 653, 695, and
1,163 ms after the first preview. Install publication was recorded at 666, 742,
and 1,176 ms; `(5,3,6)` also had mesh publication at 1,033 ms. This narrows the
work path to roughly 0.65–1.18 seconds on this saved-world startup view. The
trace found 248 luma-below-32 probes of 4,080; all joined drawable opaque depth,
five had unsettled/preview state, and there were no missing surface joins or
light-revision mismatches. `visual_holes_rate=0` and focus pending-light median
was 0. The static analyzer returned `pass=false` on movement/readiness gates;
host CPU 36.0/58.7/60.4% median/p95/max is context only. No operator framebuffer
verdict was recorded.

This bounds the visible preview on the tested entry view to at most 1–2 seconds.
It does not cover the cruise-height frontier of a moving M335 route or establish
whether the operator considers that interval acceptable. Next use the same short
known-good M335 route with stride 15, retaining the start-integrity check and
assessing runtime frame cost separately from host CPU. Do not expand the route
or make a visual acceptance claim from the luma threshold.

M622AJ artifacts: report `m622aj_world164_static_pixel_stride15_20261016.json`,
pixel analysis `m622aj_renderer_pixel_trace_20261016.json`, perf
`perf_20261016-221304_10076.jsonl`, INFO
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-221300.10076`.

### M622AK: moving M335 trace at stride 15

The established visible no-teleport `World_164` route completed with a valid
start focus `(7,3)` and reached `(-3,3)`: 35 periods, 18 fly periods, process
exit 0, no timeout. Host CPU median/p95/max was 83.7/99.3/100%; application
share was 29.9/40.1/43.7% of machine capacity. CPU remains context only and did
not gate this diagnostic run.

The aggregate analyzer returned `pass=false` on performance, scheduler, and
readiness criteria. `visual_holes_rate=0.0606`, focus pending-light median 0,
post-stop mesh-missing max 0, post-stop visible-black max 2, and readiness debt
max 33. The targeted route symptom check did not pass because focus-missing
median was 0 and visible-black median 12; this is not a visual acceptance.

Stride 15 collected 13,040 pixel probes. All 256 luma-below-32 samples joined
drawable opaque depth; nine carried unsettled/preview state, six were preview,
and there were no missing surface joins or light-revision mismatches. Ten
frontier chunks had provisional-light samples. Only one same-view
preview-to-nonpreview candidate was joined, with both samples in the same
second-resolution bucket; the moving route therefore did not provide a useful
handoff interval or an operator framebuffer verdict. The dark-pixel threshold
does not distinguish intentionally dark materials from a visible defect.

The combined cumulative `cruise_fifo_dropped_delta=44` cannot be read as 44 lost
light jobs. Pressure trim removes far tickets from the active FIFO while moving
them to deferred-far storage; overflow is a separate path. M622AK's period
samples do not attribute the cumulative delta reliably. The next code step adds
cumulative trim-to-deferred and overflow counters, then repeats this exact short
route. Use the split values to decide whether queue loss needs a behavioral fix.

M622AK artifacts: report `m622ak_world164_m335_stride15_20261016.json`, pixel
analysis `m622ak_renderer_pixel_trace_20261016.json`, perf
`perf_20261016-221611_38804.jsonl`, INFO
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-221607.38804`.

### M622AL/AM: classify FIFO eviction and time the moving preview handoff

M622AL added separate cumulative counters for pressure trim and overflow
eviction. On the established short `World_164` M335 route, the old combined
`cruise_fifo_dropped_delta` was 48, split into zero trim-to-deferred events and
48 overflow evictions. Source review shows both overflow enqueue paths preserve
the victim ticket in deferred storage before removing it from the active FIFO.
The metric therefore describes active-queue churn, not proven loss of relight
work. Deferred-far peak was 3. Do not change FIFO policy from this count alone.

M622AM retained the same short route and stride-15 pixel sampling. The new
absolute steady-clock pixel timestamp (17-digit JSON precision) restored
sub-second lifecycle joins that the old default stream precision had collapsed
into one-second buckets. Nine same-pixel/same-camera preview handoffs were
measured at about 0.66–0.87 seconds. This is a useful moving-route bound, but
there was no operator framebuffer verdict and the run's aggregate analyzer
still failed non-CPU performance/readiness gates. Host CPU remains context only.

Artifacts:

- M622AL report/pixel trace: `m622al_world164_m335_fifo_split_20261016.json`, `m622al_renderer_pixel_trace_20261016.json`.
- M622AL perf/INFO: `perf_20261016-223913_44256.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-223910.44256`.
- M622AM report/pixel trace: `m622am_world164_m335_pixel_clock_20261016.json`, `m622am_renderer_pixel_trace_20261016.json`.
- M622AM perf/INFO: `perf_20261016-224707_44944.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-224703.44944`.

### M622AN/AO: cold-create and exact saved-world replay

M622AN created the fresh alias `World_M622AN_ColdEntry_20261016`, which the
runtime assigned `World_194`. It generated 43,264/43,264 columns in 40.917 s.
M622AO replayed that exact runtime world with `-RequireExistingWorld`: 121/121
spatial columns, zero generation, and 5.417 s load. Both static visible probes
held focus `(1,2)` for 25 seconds, completed with process exit zero and no
timeout, and used the stride-15 pixel trace. Their static analyzer `pass=false`
is expected for movement-oriented gates; CPU did not gate either run.

Cold-create sampled 5,680 pixels: 83 had luma below 32, all joined to drawable
opaque depth and none were unsettled or preview. There were no missing surface
joins or light-revision mismatches. Replay sampled 5,840 pixels and had no
luma-below-32 samples, missing joins, or revision mismatches. Four replay
chunks showed preview pixels; same-view handoff bounds were 0.63–2.17 s. For
three slow examples, light calculation settled 1.34–2.05 s after first preview,
then the pixel became non-preview about 0.20 s later. A fast example settled
and installed its mesh in roughly 0.32 s. These traces point to lighting
settlement, rather than mesh construction time, as the main delay on this
entry view. They do not prove a user-visible defect: the sampled replay had no
dark pixels, and no operator framebuffer verdict was recorded.

Static source audit found a false-clear in the entry worklist. The worklist is
column-wide and monotonic, but its `terminal` predicate accepted any lit
drawable slice before considering `pending-light`. A column could therefore
be marked `Done` while another visible slice still had pending preview light;
the gate would not revisit it. The source now allows a column to become
terminal only after pending light clears. M622AO's gate trace contains only an
aggregate snapshot/debt state, so it cannot establish that a sampled preview's
pending bit was still set at the exact frame when that column became terminal.
Treat this as a source-invariant correction awaiting per-column temporal
instrumentation, not as a runtime-verified cause. It does not wait for
unrelated future work or add a CPU admission gate.

One replay sample at `(3,2,1)` had unsettled pixel demand without a pending-light
owner in the pixel trace, so the narrow correction does not explain every
preview transition. Keep that ownerless case as a separate follow-up; do not
claim it is fixed by the gate predicate. The current evidence still shows no
black/empty geometry or operator framebuffer verdict.

Artifacts:

- M622AN report/pixel trace: `m622an_cold_world_stride15_20261016.json`, `m622an_cold_renderer_pixel_trace_20261016.json`.
- M622AN perf/INFO: `perf_20261016-225152_38176.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-225148.38176`.
- M622AO report/pixel trace: `m622ao_world194_replay_stride15_20261016.json`, `m622ao_replay_renderer_pixel_trace_20261016.json`.
- M622AO perf/INFO: `perf_20261016-225413_43916.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-225409.43916`; entry-gate JSONL `enter_lit_20261016-225417.jsonl` and `enter_lit_20261016-225418.jsonl`.

### M622AP–AU: keep pending light in the visible repair path

These are short 25-second visible, static replays of the same saved `World_194`
at focus `(1,2)`, with stride-15 pixel sampling. Each process exited zero,
loaded 121/121 spatial columns, generated none, and did not time out. The
wrapper's aggregate analyzer returns `pass=false` for these no-fly runs because
movement and readiness gates are outside this route's scope; an empty scenario
also fails manifest acceptance. Use process outcome and pixel joins for this
diagnostic, not that aggregate `pass` field. Host CPU is contextual only.

M622AP retained zero dark pixels in 5,680 samples. Its aggregate entry gate
reported zero snapshot debt, but no per-column timeline connected that state
to the pixel observations. M622AQ routed entry-FOV snapshot work through the
bounded visible-relight admission. It still sampled deferred-far tickets at
some first-preview pixels. These runs show that neither aggregate gate state
nor admission of the entry snapshot proves ownership for every later renderer
preview.

M622AR's 6,720 samples had zero dark pixels and 22 same-view handoffs at
0.57–2.36 s. Pixel `(0,-1)` remained deferred-far until the age-based service
dequeued it about 1.7 s later; the visible-deferred lane had no owners. Source
review found that the renderer's local eight-column cap could route additional
preview slices back through ordinary FIFO admission. M622AS merged near
`PendingLightBeforeMesh` columns into the bounded visual-demand scan. In that
run, `(0,-1)` first appeared with a priority queue ticket and handoff measured
1.13 s, but a later `AdmitFocusVisibleMissing` retry re-deferred the same
column through ordinary admission.

M622AT prevented that ordinary fallback for exact visible first-mesh
candidates. The main preview handoffs were 0.60–1.12 s, but its audit still
showed `(3,1)` going through the focus-pending fallback. Source review found
that this path attempted visible admission only when a separate first-mesh
candidate was present. M622AU routes all pending-light columns in the
LitDrawable focus radius through bounded visible admission. It sampled 5,600
pixels, zero dark
pixels, and 20 same-view handoffs at 0.532–0.559 s. The visible preview columns
were priority-owned at first observation; the audit shows `(0,-1)` promoted
and captured without waiting for deferred-far age service. The world mutates
its light state between replays, so these are sequential observations rather
than a controlled A/B. No operator framebuffer verdict was recorded.

The three source changes address separate points in one path: (1) pending-light
columns omitted from the visual key set, (2) exact visible work blocked by the
renderer-local cap, and (3) focus-pending admission falling back into ordinary
far deferral. The `EnqueueVisibleRelight` FIFO and visible reserve remain
bounded. Continue with the established short moving route after this static
check; retain an operator verdict only for a brief visible run where the user
is present. Do not add a CPU-load admission threshold.

Artifacts:

- M622AP report/pixel trace: `m622ap_world194_pending_gate_stride15_20261016.json`, `m622ap_renderer_pixel_trace_20261016.json`; perf/INFO `perf_20261016-230503_34216.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-230459.34216`.
- M622AQ report/pixel trace: `m622aq_world194_visible_admit_stride15_20261016.json`, `m622aq_renderer_pixel_trace_20261016.json`; perf/INFO `perf_20261016-230937_44504.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-230933.44504`.
- M622AR report/pixel trace: `m622ar_world194_visible_preview_admit_20261016.json`, `m622ar_renderer_pixel_trace_20261016.json`; perf/INFO `perf_20261016-231458_46728.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-231455.46728`.
- M622AS report/pixel trace: `m622as_world194_pending_scan_stride15_20261016.json`, `m622as_renderer_pixel_trace_20261016.json`; perf/INFO `perf_20261016-231912_36416.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-231909.36416`.
- M622AT report/pixel trace: `m622at_world194_visible_requeue_guard_stride15_20261016.json`, `m622at_renderer_pixel_trace_20261016.json`; perf/INFO `perf_20261016-232140_19492.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-232136.19492`.
- M622AU report/pixel trace: `m622au_world194_visible_pending_priority_stride15_20261016.json`, `m622au_renderer_pixel_trace_20261016.json`; perf/INFO `perf_20261016-232414_42856.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-232410.42856`.

### M622AV: moving-frontier check after visible pending-light admission

M622AV used the established M335 route on saved `World_164`: visible GUI,
no teleport, 35 s westbound flight, 20 s stop, and 15 s idle. It loaded the
existing world (121/121 columns, zero generated columns) and traveled ten
focus columns. The process completed with exit code 0 and no timeout. The
aggregate analyzer still returned `pass=false`: its safety line counted two
single-period near-focus mesh-proxy samples, while post-stop demand/readiness
convergence also remained incomplete. Do not interpret that aggregate failure
as proof of a blank framebuffer.

The two flight samples were at frame epochs 901 and 1018. In both, a fresh
near-focus miss probe ran and returned clear, while the policy-facing
`visual_holes` / `focus_missing_mesh` signals remained positive. The associated
focus-miss witness already had a drawable mesh satisfying column readiness.
This follows the `miss_positive_hold` path in `WorldStreaming.cpp`: a previous
positive is retained briefly after a fresh clear to avoid flicker. The
analyzer now labels `visual_holes` as a hysteretic mesh-pressure proxy and
reports `visual_holes_fresh_clear_probe_n` plus its share among positive
samples. Those fields are diagnostic only; preserve existing scheduling and
safety gates until an independent coverage signal justifies changing them.

The independent stride-15 renderer trace collected 14,340 probes. Of 299
samples below luma 32, every one joined a drawable opaque surface; none had a
missing opaque-surface join. Ten dark samples were unsettled or preview-owned,
and four were preview samples. Fifteen same-view preview-to-settled handoffs
measured 677–1,367 ms (median 680 ms). These probes do not prove what the
operator saw, and low-luma opaque geometry is not necessarily defective.
`post_stop_mesh_missing_max` and `post_stop_visual_holes_max` were both zero,
while post-stop unfinished readiness reached 29. This leaves a real
readiness/convergence backlog to classify without conflating it with missing
visible geometry.

The next diagnostic should report, side by side, fresh radius-query state,
held policy state, readiness debt, pixel-to-surface joins, and queue eviction
reasons. Keep the M335 route and short duration. Do not add a host-CPU gate or
change visible-relight admission based only on the two held samples.

Artifacts:

- M622AV report/pixel trace: `m622av_world164_m335_post_visible_priority_20261016.json`, `m622av_renderer_pixel_trace_20261016.json`.
- M622AV re-analysis with explicit hysteresis fields: `m622av_world164_m335_post_visible_priority_semantics_20261016.json` (2 positive proxy samples also had a fresh clear probe; share 1.0).
- M622AV perf/INFO: `perf_20261016-232752_43592.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-232748.43592`.

### M622AW–AY: refresh focus census, first-mesh owners, and preview-light pixels

The M622AV source review found that an explicit unfinished-census invalidation
could be followed by the near-miss reuse branch. M622AW added the invalidation
bit to `force_full_unfinished`. The Release build passed. On M622AW, all nine
observed focus transitions had matching `focus_not_render_ready` and
unfinished-class totals; one sample at the initial focus `(7,3)` still had a
stale scalar/class snapshot mismatch. The route stopped at focus `(-2,3)` and
did not cover the far-route acceptance boundary. The analyzer still failed
the product safety line on one mesh-hole proxy period.

M622AX repeated the same 30 s M335 moving segment with
`CUBATARIUM_RELIGHT_AUDIT=1`. It ended at focus `(-1,3)`, exited successfully
at the process level, and did not time out. Its two positive `visual_holes`
periods split into one fresh miss and one held-positive sample whose fresh
probe had cleared (`visual_holes_fresh_clear_probe_n=1`, share `0.5`). At the
fresh miss, the probe identified resident solid chunk `(1,3,5)` in the camera
vertical band with no drawable mesh, no satisfying mesh, no dirty/build/GPU
owner, no SoftDefer hold, no ColumnFlow ticket, and stage `Absent`. The camera
band census independently showed one missing drawable slice with one pending
work owner and zero unowned slices.

The audit then followed that same column: `OnLitPendingNeeded` inserted its
exact first-mesh relight request at 23:48:44.713; priority capture submitted
the upper first-mesh band at 23:48:44.973; a slice record reported drawable
and satisfying at 23:48:45.034; the lower band was submitted at 23:48:45.926;
and light flags finalized at 23:48:45.960. A halo change later queued stale
light debt at 23:48:47.543 and published it at 23:48:48.055. This is evidence
of a short, owned first-mesh/light transition for this instance, followed by a
neighbour-light repair. It is not an operator-visible frame verdict. The
stride-15 trace sampled 2,960 pixels: all 60 pixels below luma 32 joined
drawable opaque surfaces, with zero missing opaque-surface joins; it did not
join this lifecycle to a same-view pixel handoff.

M622AX recorded 47 FIFO overflow evictions and at most five deferred-far
columns. The audit contained 27 `visible_exact_replacement` deferrals, 35
`fifo_admission_denied` deferrals, and two aged-service admissions; one was
re-admitted after 1.529 s. Source inspection confirms overflow victims are
preserved in the deferred-far map and can be re-admitted; do not count these
evictions as lost work. M622AW recorded 443, so the two sequential runs show
state/workload sensitivity, not a controlled queue benchmark. Keep the queue
bounded and add a per-witness owner/lifecycle join before changing its
capacity or replacement policy.

Both runs ended with zero post-stop mesh-missing and visual-hole proxy counts,
but readiness/convergence remained false (M622AX post-stop readiness debt
peaked at 35). The moving product gate still fails and no operator verdict was
recorded.

M622AY repeated the same Release M335 route with the relight audit and sparse
screen-ray pixel probes. The process succeeded without a timeout and traveled
nine chunks. Moving-period speed was 4.42–5.20 blocks/s; the first period's
734.9 speed sample is a launch discontinuity and is not representative of
cruise. Host CPU median/p95 was 67.2/84.2%; this is workload context only and
was not a pass/fail gate.

The product report still failed: `readiness_debt_rate=1.0` and post-stop
readiness debt peaked at 34, while `visual_holes_rate=0`, post-stop mesh misses
were zero, and post-stop visual-hole proxy was zero. These values are not a
framebuffer verdict. The 1,940 pixel probes contained 35 final pixels below
luma 32; every one joined a drawable opaque depth surface. Nine of those dark
samples had unsettled demand or preview-light state, and four had a preview
shader override. The run recorded 56 preview-light samples across four chunks.

The clearest startup sample was chunk `(7,3,3)`: at frame 1 it was drawable,
its light demand was unsettled, and its relight request sat in the priority
queue at index 10 of 12. The sample used the preview shader. The first later
settled non-preview observation arrived 4.37 s later at a different camera
height; the analyzer found no same-view handoff. Other chunks had first-preview
to first-settled observation intervals of about 4.37 s and 11.31 s, also with
camera movement. This matches a provisional-light startup transition, but does
not establish how long a fixed visible pixel stayed dim or explain a late-route
operator observation. Source inspection confirms the render path intentionally
keeps drawable geometry visible with an ambient fallback while its light
replacement is pending.

The INFO chronology bounds the actual queue service more tightly than the
sparse pixel interval. For `(7,3)`, the pending note was inserted at 02:20:31.668,
its priority position changed from 10/11 to 3/12 at 02:20:32.015, capture
dequeued it at 02:20:32.055, and light flags finalized at 02:20:32.128. For
`(5,4)` through `(5,6)`, admission denials created deferred-far owners;
`visible_relight_admitted` cleared them 39–123 ms later, then exact first-mesh
capture and light finalization followed 0.26–0.45 s after that. This confirms
fast queue recovery in the sampled startup window. It does not timestamp when
the replacement mesh became visible at the same screen pixel.

M622AY recorded 369 active-FIFO overflow evictions and a deferred-far high
water mark of eight. The eviction path preserves victims in the deferred-far
map, so this is queue churn/backlog evidence, not 369 proven lost requests.
The consecutive runs are not a controlled capacity comparison. Keep the FIFO
bounded and do not tune its size from this count alone.

Static review and the M622AY INFO chronology found no queue-starvation defect
in this startup window. `DrainRelightQueues` reorders the priority deque by
current focus distance, and `EnqueueVisibleRelight` promotes exact visible
repairs. The priority position and service timings below show that the target
did not remain stuck behind older work. FIFO denial became deferred work and
was returned to the visible lane; it was not evidence that FIFO work was lost.

Do not change the FIFO capacity, visible lane budget, or preview gate from this
run. The remaining diagnostic gap is after light-field finalization: M622AY's
pixel trace has no exact lifecycle events for the preview chunks, and a sparse
first-settled observation can trail the real replacement by several seconds.
The next static step is to follow one target from final light install through
stale-halo detection, dirty admission, mesh publication, and GPU apply. Use the
established short M335 route only when that target can be joined to a fresh
pixel/lifecycle witness; do not repeat an identical run just to add duration.
Do not add a CPU threshold or a long operator-monitoring flight.

Artifacts:

- M622AW report/pixel trace: `m622aw_world164_m335_focus_invalidation_20261016.json`, `m622aw_renderer_pixel_trace_20261016.json`; perf/INFO `perf_20261016-234304_37980.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-234301.37980`.
- M622AX report/pixel trace: `m622ax_world164_m335_relight_audit_20261016.json`, `m622ax_renderer_pixel_trace_20261016.json`; perf/INFO `perf_20261016-234824_28028.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261016-234820.28028`.
- M622AX analyzer re-run with fresh-probe semantics: `m622ax_world164_m335_relight_audit_semantics_20261016.json` (`1/2` positive proxy samples had a fresh clear probe).
- M622AY report/pixel trace: `m622ay_world164_m335_same_view_audit_20261017.json`, `m622ay_renderer_pixel_trace_20261017.json`; perf/INFO `perf_20261017-022026_5628.jsonl`, `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-022023.5628`.

### M622AZ: capture the first preview chunk's mesh lifecycle

M622AZ repeated the warm `World_164` M335 route (30 s fly, 20 s stop, 8 s
idle, no teleport) with `CUBATARIUM_RELIGHT_AUDIT=1`. The process completed
without timeout and traveled nine chunks, but the aggregate convergence gate
failed. Readiness debt occurred in every analyzed period; visual-hole and
post-stop visual-hole proxies were zero. Host CPU median/p95 was 59.9/82.2%,
for context only.

The early watcher in `UWorld::EnqueueVoidDarkColumnRelightNote` registers the
nearest unsettled slice before relight admission. This captures lifecycle rows
that could finish before the renderer's pixel probe runs. The offline analyzer
now has a narrow attempt-zero fallback: it accepts a unique post-preview job
only when chunk incarnation and sampled source revisions agree.

The 2,080 pixel probes contained 47 samples below luma 32, all joined to
drawable opaque depth surfaces; five had provisional preview light. There were
63 preview samples across seven chunks. All seven now join to a mesh job (six
by attempt/job identity, one via the constrained attempt-zero fallback). CPU
mesh publication followed first preview by 0.269–1.302 s. The later settled
pixel observation was 3.67–12.02 s later with a moved camera; same-view
handoffs remained zero. This bounds CPU job publication only. No matching
`install_published` or GPU queue/kick/ready event was present in the selected
timelines. Each later settled pixel did join a visible MDI pass, and its
published geometry revision was equal to or newer than the causal job's
published revision. The mesh therefore reached a drawable chunk revision by
that later sample, but the camera had moved and the original screen-pixel
handoff remains unmeasured. GPU queue/kick/ready events describe the compute
meshing route; their absence alone is not failure evidence.

The analyzer now joins mesh-job published revisions with the later pixel's
published revision, mesh revision, and MDI visible-pass state. It also includes
`mesh_job_uploaded`, `mesh_job_gpu_queued`, `mesh_job_gpu_kicked`,
`mesh_job_gpu_counters_ready`, and `mesh_job_gpu_ready` when present. Next use a
short fixed-camera capture to measure a same-view handoff. Keep the established
route and avoid a CPU admission threshold or a long operator-monitoring flight.

Artifacts: report/pixel trace
`m622az_world164_m335_early_lifecycle_watch_20261017.json`,
`m622az_renderer_pixel_trace_20261017.json`; perf/INFO
`perf_20261017-023407_46844.jsonl`,
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-023404.46844`.

### M622BA: fixed-view startup handoff calibration

M622BA held the saved `World_164` camera at `(120,50.12,56)` for a 10 s visible
no-fly capture after an 8 s warmup. It exited successfully without timeout.
The aggregate `pass=false` is not a product failure signal here: the empty
scenario provides no moving-route acceptance data, and the run had five total
periods with one steady period. Readiness debt was present while the
visual-hole proxy stayed zero, so use the pixel/lifecycle trace for visual
evidence. Host CPU median/p95 was 72.9/83.7%, retained only as context.

The stride-15 pixel trace recorded 3,040 probes: 220 were below luma 32, all
220 joined drawable opaque surfaces, and 14 had unsettled/preview light state.
There were 128 provisional-light samples in four chunks. The analyzer found
125 same-pixel/same-camera preview-to-nonpreview observations across those
chunks, bounded at 656 ms–1.314 s with a 657 ms median. No sampled pixel caught
the intermediate settled-light/preview state. Since samples were sparse, the
intervals bound the visible handoff rather than give its exact transition
time. This verifies a short warm-start transition at the fixed camera; it does
not explain the late-route dim-chunk report.

The analyzer now keeps per-chunk arrays for both preview-to-nonpreview and
settled-preview-to-nonpreview observations separately. This avoids conflating
the total preview interval with the narrower post-settlement shader handoff.

Artifacts: report/pixel trace `m622ba_world164_static_start_same_view_20261017.json`,
`m622ba_renderer_pixel_trace_20261017.json`; perf/INFO
`perf_20261017-024721_35260.jsonl`,
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-024717.35260`.

#### M622BA follow-up: fog and light are confounded in the pixel transition

A follow-up analysis added the recorded fog state and shader fog-blend estimate
to each same-pixel lifecycle sample. Six of the 125 same-view rows changed luma
by more than 10 (`50.592`–`101.639`); every one also changed fog state. These
RGB/luminance deltas do not isolate provisional-light removal:

- `(5,3,5)`, pixel `(224,495)`: luma changed by `+53.877`; air fog stayed
  enabled with start/end `30.72/64`, while fog color changed from
  `(0.131557,0.247836,0.462329)` to `(0.443451,0.621815,0.891517)`. At
  horizontal distance `48.293`, estimated fog blend was `0.58112` in both
  samples.
- `(5,3,6)`, pixel `(32,495)`: luma changed by `+97.541`; air fog stayed
  enabled, start/end changed from `30.72/64` to `16/43.3912`, and fog color
  changed from `(0.131557,0.247836,0.462329)` to
  `(0.457308,0.638268,0.910215)`. At horizontal distance `63.347`, estimated
  fog blend changed from `0.9833` to `1.0`.

The pixel trace reports stable day factor (`1`), sky scale (`0.972`),
precipitation (`0`), and weather fog multiplier (`1`) across these samples.
`UUnderwaterFogPass` smooths the atmospheric fog color with a per-frame `0.2`
mix. The observed color progression is consistent with that smoothing, though
the exact target color is not currently recorded per sample. The measured RGB
changes therefore do not isolate a lighting change. Other same-view pixels
had zero luma delta despite the fog color change, so a fog-state difference
alone does not prove that fog affected a given pixel.

The five period snapshots show the presented range at `64`, then `32`, then
expanding back to `64` blocks while the camera stayed fixed. `fog_hole_debt`
was set in the two middle snapshots and clear in the first and last snapshots.
At those middle snapshots, `focus_missing_mesh`, `miss_horiz`,
`focus_unfinished_ahead`, `visual_holes`, and fully-dark-stalled count were
zero; `visible_black_focus_n` was six and then two. These are period snapshots,
not per-frame trigger chronology, and the policy does not use the black-focus
census as a latch predicate. The current trace cannot identify which brief
per-frame readiness signal initially set the latch. Treat the range change as
a startup fog-pull-in transition associated with the streaming gate state;
do not infer a black framebuffer or a lighting defect from it.

The analyzer now stores fog enable flags, range, density, blend, color,
weather multiplier, and underwater fog parameters in lifecycle rows. It also
estimates the shader fog blend from the captured opaque surface and camera
position. M622BB below adds the latch inputs and target atmospheric color.

### M622BB: frame-aligned fog latch and fogged-pixel attribution

The Release `Cubatarium` target was rebuilt with sparse transition records for
fog-latch inputs and target-color capture for pixel probes. A 10 s visible
no-fly probe on warm `World_164` used the M622BA fixed camera and trace profile.
It exited with `process_rc=0`, `run_outcome=success`, and no timeout. Its
aggregate `pass=false` is expected for the empty no-fly scenario and is not a
renderer failure verdict. CPU usage is contextual only.

The four frame-aligned fog records explain the static-camera range change:

- Frame 1: baseline range `30.72/64`, no latch.
- Frame 3: `fog_hole_debt` latched with one fully-dark-stalled ticket. The
  target became render distance `3`, margin `44`, start ratio `0.28`; presented
  range moved smoothly to `28.073/61.353`.
- Frame 16: latch remained set with `FocusMissingMesh=1`, `MissHoriz=1`, and
  `visual_holes=1`. Presented range reached `16/32`; target margin increased
  to `52`.
- Frame 246: clear conditions were true and the latch cleared after
  `4012 ms` of quiet. Focus-miss, miss-horizon, unfinished, and fully-dark
  stalled inputs were zero. The census still reported three visible-black
  focus columns, which are intentionally not a clear/latch predicate. The
  presented range had begun its smooth expansion (`16.693/32.693`).

This confirms that the startup fog movement at a fixed camera follows transient
streaming debt and its hold/clear policy. It does not show an arbitrary fog
teleport. Do not tune the fog pull-in from this run; it was covering a real
startup repair interval.

The analyzer recorded 4,000 pixel probes and 277 samples below luma 32. All
277 joined drawable opaque surfaces, and all had estimated air-fog blend zero.
The exact depth-hit source faces split into 251 block `572` samples and 26 block
`573` samples. The active pack definitions identify these as `tree_leaves`
(`leaves_opaque` on every face) and `tree_log` (`tree_side` on the sampled side
face). The source `leaves_opaque.png` has a 16×16 opaque-pixel RGB minimum of
`(22,29,20)`; `tree_side.png` has minimum `(34,23,14)`. Captured examples are
near these low ends, which is consistent with the authored textures. The trace
does not retain the exact atlas texel, so this is not an exact shader-output
reconstruction.

The low-luma summary had 10 preview samples and 13 unsettled-demand samples;
23 samples had one or both conditions. The other samples mostly had sky light
`1`, block light `0`, and settled demand. The dark-pixel count therefore
describes dark leaf/log texels on valid drawable geometry in this view; it is
not evidence of blank chunks or a general light failure. The new analyzer
summary records depth-hit block and face counts for future captures.

For a direct example of the RGB/fog confound, chunk `(5,3,6)`, pixel `(32,495)`
brightened by `97.541` luma between its preview and settled samples. The
estimated fog blend changed from `0.9833` to `1.0` at a horizontal distance of
`63.347` blocks. The sampled surface RGB closely followed the fog color in
both samples. The presented fog color approached an almost unchanged target:
`(0.131557,0.247836,0.462330)` to `(0.457311,0.638226,0.910111)`, while target
color stayed near `(0.4578,0.639,0.911)`. This explains the pixel's brightness
change through startup color smoothing and range contraction; it is not
evidence that the mesh's light changed. Same-view preview-to-nonpreview bounds
were `95.551 ms`–`1.521 s` (median `848.6 ms`) across three chunks.

An early focus-column trace also observed `(6,2,2)` as
`FullyDarkStalledTicket` at frame 2, then `FullyDarkPendingRepair` at frame 3.
Its first matching mesh job (attempt 12) was built at frame 3 and GPU-ready and
published at frame 4; a newer geometry attempt was GPU-ready and published at
frame 10. This is a short startup handoff for that witness, not proof that all
startup dark surfaces are resolved.

The next useful evidence is a same-view M335 pixel whose depth-hit material is
not explained by the recorded block texture, or an exact atlas-texel/light
reconstruction if the same dark leaf/log surface remains visually suspicious.
Use the established short M335 route to gather a fresh moving witness; keep the
dark-pixel threshold separate from actual visual-hole and visible-black
stalled evidence.

Artifacts: report/pixel trace
`m622bb_world164_fog_transition_trace_20261017.json`,
`m622bb_renderer_pixel_trace_20261017.json`; perf/INFO
`perf_20261017-030534_39796.jsonl`,
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-030531.39796`.

### M622BC/BD: targeted repair ownership and promote deadline deferral

M622BC's final visible-black census attributed one fully-dark column to a
`PromoteRelight` Flow ticket. Static inspection showed that this ticket is a
region-wide maintenance pass: it calls `PromoteNearTerrainColumnRelights` for
the focus radius and optionally promotes the nearby pending-light set. Its
`work.column` is only the pass's anchor, not ownership of repair for that
specific dark column. The census had treated any Flow ticket on that anchor as
a per-column repair owner, producing `FullyDarkStalledTicket` without targeted
repair.

The fix in `1be89710` adds `HasTargetedRepairTicket`: only
`RelightThenMesh`, `FirstMesh`, and `RemeshSeam` count as exact-column repair
owners. General `HasRepairTicket` remains unchanged for scheduler clients that
need to know whether any Flow work exists for the column. A Release
`Cubatarium` target was rebuilt before M622BD.

M622BD repeated the visible, no-teleport 30 s M335 route on the pinned warm
`World_164` start, followed by 20 s stop and 8 s idle phases. It exited with
`process_rc=0` and no timeout. The classification change is visible in the
paired result: M622BC ended with `post_stop_visible_black_stalled_max=1`; M622BD
ended at `0`, and the stalled stop gate changed from false to true. M622BD did
have three fully-dark/no-ticket columns, but all three were excluded by the
equal-light-revision legal-dark predicate
(`post_stop_visible_black_no_ticket_max=3`,
`post_stop_visible_black_no_ticket_excluding_legal_dark_max=0`). This validates
the census ownership fix; it does not prove that a user-visible dark chunk was
repaired.

The route-level signals remain unsettled. M622BD reached eight chunks, with
`fly_visible_black_max=18`, a `visual_holes_rate=0.1111` near-focus
missing-mesh pressure proxy, `unfinished_visual_rate=1`,
`post_stop_missing_max=32`, and `post_stop_readiness_debt_max=32`. Its
post-stop near-focus mesh-coverage proxy was zero; neither value is a direct
framebuffer-hole measurement. M622BC had corresponding values 18, 0.0370, 1,
30, and 30. The reports are adjacent runs, not a controlled performance
comparison: M622BC host CPU
median was 75.3%, and M622BD was 77.3% (p95 100%). Host CPU is context only,
never an acceptance gate. The fly-frame median rose from 32.09 ms to 38.62 ms;
do not assign that difference to the classification-only patch.

M622BD's three `visual_holes=1` period samples were at frames 513, 675, and
763. The miss-probe witness remained 63.0–73.0 blocks from focus while the
presented fog horizon was 32 blocks; at frame 513 the current probe was clear
and the short miss hysteresis still held. The screen-ray pixel probes recorded
2,800 samples: 1,421 opaque-cube hits and 1,379 rays with no opaque cube before
the renderer horizon. No sample had state 2 (an unloaded chunk before a hit).
Probe frames adjacent to all three proxy positives likewise had no state-2
sample. This weakens the interpretation of those proxy positives as visible
holes, but the sparse screen-ray grid cannot prove every pixel was covered.

M622BD pixel tracing recorded 2,800 probes and 55 samples below luma 32. All
55 joined drawable opaque depth surfaces and all had estimated fog blend zero.
The depth-hit summary was 46 leaves (block 572), three logs (block 573), and
six samples without a resolved material id. Eight samples had unsettled demand
or preview state, including five with preview active. These are dark textured
surfaces in the sampled pixels, not evidence of empty chunks; exact atlas texels
and an operator framebuffer verdict are unavailable.

The same run exposes a scheduling question worth investigating separately:
the raw perf stream has 29 period snapshots. `PromoteRelight` work was live in
23 snapshots; all 23 were probed and deferred, with zero dispatches. The queue
reached 11 live tickets. The code path marks this work noncritical, so
`UFrameDeadline::ShouldDeferProducer(false)` rejects it after the shared frame
deadline is exhausted. This is not yet evidence of a relight stall: the run also
recorded nine deferred-far age admissions and 43 relight FIFO priority inserts,
with seven deferred-far entries remaining at the end. Static inspection found
an explicit age-service path: a deferred entry becomes eligible after 1.5 s,
at most one candidate is attempted every 250 ms, and this bypass is allowed
while the relight FIFO is below three-quarters of its configured soft cap and
the prior candidate is no longer waiting. The M622BD audit logged nine
accepted age-service admissions at ages 1.513–4.257 s (median 1.541 s). The
priority FIFO peaked at 35 entries and ended at one; deferred-far entries
peaked at nine and ended at seven. This is evidence of service under the
current queue, with a measured worst observed wait of 4.257 s; it is not proof
of a stuck queue or of a user-visible delay. Do not widen the critical reserve
or change the deadline policy from these counters alone. Next correlate a
long-wait entry with its exact light preview/settlement and whether it enters
the fog horizon before changing service policy.

#### M622BD follow-up: age-service versus pixel lifecycle

The saved pixel trace uses the absolute steady-clock field, so its lifecycle
events can be compared with the INFO audit. The only exact XZ overlap between
an age-service candidate and a sampled preview chunk was column `(0,3)` /
slice `(0,3,3)`. Its first sampled preview pixel was bright (`RGB 117,161,227`)
and already reported settled demand with published, meshed, and field light
revision 1. The first later settled nonpreview observation was 8.159 s later,
but at a camera position 34.6 blocks away; this is not an observed same-view
handoff duration. The trace recorded no same-view preview-to-nonpreview
handoff. It did record one zero-ms same-view light-settlement candidate for
`(0,3,3)`, because demand was already settled on the first preview sample;
this is not a measured transition time.

The second preview chunk `(0,3,4)` first appeared at the same camera with
unsettled demand but the same bright RGB. Its mesh job published at about
242 ms; the first settled nonpreview observation was 7.333 s after the first
preview, with the camera 31 blocks away. It is in adjacent XZ column `(0,4)`,
so it does not match the deferred-far service records for `(0,3)`. Sparse
moving-camera samples cannot tell when either chunk actually changed on
screen, and these observations do not establish a visible-light defect.

The `(0,3)` queue audit shows real backpressure and progress: at 03:26:34 it
was deferred with one of four loaded slices settled; it was admitted at
03:26:35.815, then displaced as `visible_exact_replacement` and admitted again
at 03:26:37.759. By the 03:26:36.220 denial record all four slices were settled
and none were pending, though `lit_ready` was still false. At 03:26:42.220 a
`miss_witness` request for the same column was denied while all four slices
were settled, `pending_column=0`, and `lit_ready=1`; it was retained in
`deferred_far` and later admitted. This may be legitimate geometry-miss repair,
but it is a concrete queue-churn path to inspect: verify whether a missing
drawable mesh still needs a full relight after light readiness is true, and
whether the request can be narrowed to the required slice. Do not change the
age-service cadence or deadline reserve until a short route captures a
same-camera pixel transition or proves this relight is redundant.

Artifacts: M622BC report/pixel trace and perf
`m622bc_world164_m335_fog_latch_short_20261017.json`,
`m622bc_renderer_pixel_trace_20261017.json`,
`perf_20261017-031703_36216.jsonl`; M622BD report/pixel trace and perf
`m622bd_world164_m335_targeted_black_gate_20261017.json`,
`m622bd_renderer_pixel_trace_20261017.json`,
`perf_20261017-032610_34308.jsonl`; INFO log
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-032607.34308`.

### M622BE: expose denied-relight slice causes

The M622BD correlation did not justify changing queue policy. Its
`miss_witness` denial had `pending_column=0` and `lit_ready=1`, but the
no-op-enqueue guard only permits that combination when the merged requested
band has no fully-dark greedy face. Because the guard did not skip this request,
the M622BD control flow implies that at least one such face existed (subject to
state changing between the guard and audit log); that run did not record its
slice. The `miss_witness` name alone therefore did not prove redundant
geometry-only relighting.

M622BE adds audit-only `audit_band`, `band_needs`, and `dark_face_cy` fields to
the FIFO-denial log. Release build succeeded with
`tools/build_release.ps1 -Jobs 8`. The established visible M335 route completed
on `World_164` with no timeout and `process_rc=0`. It recorded 73 FIFO denials;
all had `band_needs=1`. Sixty-four listed one or more cached fully-dark mesh
slices (`cy=2`, `cy=3`, or both), while the remaining nine had no such cached
face and each had one pending-light slice. A same-column capture followed 64
of the dark-face denials within five seconds (34 within one second); every
captured band overlapped all listed dark-face slices. Eight of the nine
pending-only denials likewise had a capture within five seconds. Capture
submission is evidence of queue service, not proof that relight, mesh
publication, and GPU visibility completed.

The moving pixel trace sampled 2,040 probes. Its 37 low-luma samples all joined
drawable, draw-ready opaque surfaces outside fog; six had unsettled/preview
state and none had a light-revision mismatch. The 990 ray state-1 hits and 1,050
state-3 horizon misses included no state-2 unloaded-chunk-before-hit sample.
Only nine dark pixels shared an XZ/slice coordinate with a logged denied
dark-face event, and the nearest same-slice denial was at least 16.8 s earlier;
this run does not show a time-aligned dark framebuffer surface caused by FIFO
backpressure. Sparse probes still do not cover every pixel, and no operator
framebuffer verdict was recorded.

M622BE acceptance remained false: 29 periods/27 steady periods, readiness debt
rate 1.0, `visual_holes_rate=0.037`, fly visible-black max 18, post-stop
readiness-debt max 38, and post-stop stalled-black max 0. The nonzero readiness
and near-focus mesh-coverage values remain proxies. Host CPU median/p95 was
92.9/100%, while app CPU was 26.2/34.5% of machine capacity; CPU is context
only. This is not a controlled performance comparison with M622BD.

Next inspect the completion chain for a denied target with a logged dark slice:
capture completion, `MarkRelit`, dirty-mesh admission, mesh publication, and
GPU-ready state. Change queue/deadline policy only if a same-view pixel
transition or a break in that completion chain is demonstrated.

Artifacts: report/pixel trace `m622be_relight_denial_slices_20261017.json`,
`m622be_renderer_pixel_trace_20261017.json`; perf
`perf_20261017-100942_45864.jsonl`; INFO log
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-100937.45864`.

### M622BF/BG: stale-dark witness and no-op admission

M622BF added `stale_dark_face` witnesses to denied-relight audit events using
`ChunkHasStaleDarkFaces`, which compares a baked zero-light face with current
world light. Its Release route completed normally (`process_rc=0`, no timeout)
with 100 FIFO denials and no stale-dark witness. Seventy-three denials still
had dark cached faces; most were unsettled or pending. Four had all four
loaded slices settled and no pending slice, but no stale witness. This run
shows why `lit_ready` alone is not a settlement proof and why any dark face is
too broad for a no-op decision. The original witness probe also ran on
unsettled slices, so a missing witness there is inconclusive; only the four
fully settled rows can be interpreted as an up-to-date-light check.

M622BG narrows only no-op admission decisions: a cached dark face keeps relight
work while its slice is unsettled or when `ChunkHasStaleDarkFaces` proves the
baked face stale. A settled dark face with no stale witness may now be skipped
when the column has no pending-light owner. The conservative surface-band
fallback/closeout checks remain unchanged. Release build succeeded, and the
same 30 s visible no-teleport M335 route completed normally. It logged 166 FIFO
denials, seven immediate no-op skips, and 13 deferred-far skips as already
satisfied. All 165 denials with `band_needs=1` were pending/unsettled or dark
and unsettled; none had a stale witness. No denial had a fully-settled dark
slice with zero pending slices. The one `band_needs=0` denial had
`lit_ready=0` and three loaded-but-unsettled slices, so it correctly remained
eligible for relight.

The M622BG pixel trace sampled 2,760 probes and 72 low-luma pixels. All 72
joined drawable, draw-ready opaque surfaces; five had unsettled/preview state
and none had a light-revision mismatch. Ray states were 1=1,373 opaque hits and
3=1,387 no opaque hit before the renderer horizon; there were no state-2
unloaded-chunk-before-hit samples. The aggregate gate still failed on readiness
and proxy metrics: `visual_holes_rate=0.074`, readiness-debt rate 1.0,
post-stop debt 29, fly visible-black max 18, and post-stop stalled-black max
0. M622BF/BG are adjacent diagnostics, not a controlled performance or visual
comparison; host CPU is context only. These results support the no-op guard's
classification but do not prove an operator-visible improvement.

Follow-up audit instrumentation now emits `unsettled_dark_face_cy` and
`stale_dark_face_checked_cy`; stale-face inspection is skipped until the
slice has current light settlement. The M622BF/BG artifact files predate this
field split, so use a later audit run to obtain separately attributable counts.

Next inspect one unsettled/stale target through capture completion,
`MarkRelit`, dirty-mesh scheduling, mesh publication, and GPU-ready state. Keep
the focus on same-view pixel evidence; do not tune FIFO/deadline policy based
only on denial counts.

Artifacts: M622BF report/perf/INFO
`m622bf_stale_dark_face_witness_20261017.json`,
`perf_20261017-101926_43832.jsonl`,
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-101922.43832`; M622BG
report/pixel trace `m622bg_settled_dark_noop_guard_20261017.json`,
`m622bg_renderer_pixel_trace_20261017.json`; perf
`perf_20261017-102307_36176.jsonl`; INFO log
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-102304.36176`.

### M622BH: exercise settlement-aware audit on the established route

The Release build passed after splitting unsettled dark faces from stale-face
checks. The short visible M335 no-teleport replay on warm `World_164` completed
normally (29 periods, 27 steady, no timeout). Replay integrity confirmed all
121/121 spatial columns were loaded from the saved world with no terrain
generation. The route met input adequacy, but the intended high symptom level
was not reproduced; keep this as diagnostic evidence, not a fix acceptance
run.

The new fields executed, but all ten FIFO-denial rows had
`dark_face_cy=none`, `unsettled_dark_face_cy=none`, and
`stale_dark_face_checked_cy=none`. Thus this flight did not exercise either
branch of the stale-dark classifier. It recorded five immediate no-op skips
and nine deferred entries later cleared as already satisfied. Do not infer
that dark-face repair is correct from this sample.

The analyzer still failed aggregate acceptance: readiness-debt rate 1.0,
near-focus visual-hole proxy 0.111 (threshold 0.10), and fly wall median
39.66 ms (25.22 effective FPS). At stop, the readiness-debt max was 29 and
focus-visual missing-mesh max/end was 26/26, while direct near-focus
`post_stop_mesh_missing_max=0`, `post_stop_visual_holes_max=0`,
black-sticky max=0, and stalled-visible-black max=0. These counters sample
different scopes and do not establish an operator-visible hole; no operator
frame verdict was recorded. Host CPU was 91.5/100% median/p95 and app CPU
26.3/34.9%; retain these as context only, with no host-load admission gate.

Next use a short route only when it reaches a cached fully-dark face. Correlate
the same slice's settlement, stale witness, queue ownership, relight apply,
mesh publication, and GPU readiness; retain same-view pixel evidence. The
current run did not provide a target slice or proof of a rendering defect.

Artifacts: report `m622bh_settled_dark_noop_audit_20261017.json`; perf
`perf_20261017-102903_41892.jsonl`; INFO
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-102859.41892`.

### M622BI: apply no-op classification before FIFO admission

The M622BG trace exposed a path gap: the settled/no-op test in
`EnqueueTerrainColumnRelightImpl` ran only after FIFO admission failed. A
normal-capacity non-visible producer could therefore repeatedly queue work
for a lit column. M622BI moved the same conservative test ahead of FIFO
admission for new non-visible requests; visible-priority requests and already
keyed work keep their existing paths. Release build passed.

The established warm M335 route again completed normally (29 periods, 27
steady, input adequacy passed; no timeout). Audit recorded 1,929
`relight_admission event=noop_skip` decisions, 562 capture submissions, and
104 FIFO denials. Among the denial rows, 82 contained dark cached faces; 80
reported unsettled dark slices, 22 inspected settled slices, and two rows
reported a stale witness for the same settled slice `(3,3,6)` at block
`(62,48,100)`. The unsettled and stale cases therefore remain eligible for
repair; the no-op guard did not classify that stale face as satisfied.

For `(3,3,6)`, `slice job=175` installed changed light at field revision 2 and
left the drawable mesh dirty at meshed/published revision 1. The stale-light
debt was queued; at 10:36:05.720, `stale_light_debt event=published` observed
field, meshed, and published light revision 2 with a matching halo. This
verifies light/mesh publication convergence for that sample. A targeted pass of
the saved pixel trace with `--watch-chunk 3,3,6` found 38 associated probes: 28
whose opaque depth surface resolved to this chunk and 28 whose separate voxel
ray hit it (18 probes joined both). Every depth-surface probe was drawable and
draw-ready, had visible MDI pass flags, settled demand, no preview or pending
stale-light remesh, and matching field/meshed/published light revision `2/2/2`.
This is framebuffer-pixel/GPU-state evidence after the first publication, not
just a lifecycle inference.

The targeted trace also found one repeated same-view pixel group at `(416,450)`
with camera position `(120,70,56)`: two samples 3.629 s apart, RGB
`(117,163,232)` then `(117,162,231)`, both draw-ready and light-revision
matched. The first sample was about 2.875 s after the 10:36:05.720 publication.
No pre-publication sample exists at that same pixel and view, so this does not
show a bad-to-good transition or establish what an operator saw. Nine of the 28
depth-surface probes had desired geometry revision 4 while revision 3 remained
published; they were still drawable, indicating concurrent geometry debt that
the light-revision check alone does not describe.

The chunk later acquired stale-light debt again at 10:36:23.402 and published
revision `2/2/2` at 10:36:23.553. The last depth-surface probe for `(3,3,6)` was
earlier, at about 10:36:22.74. Later probes that still had a voxel-ray hit in
`(3,3,6)` resolved their opaque depth surface to adjacent chunk `(2,3,6)`;
there is no post-second-publication pixel sample of the target surface. Thus the
first repair has direct post-publication pixel evidence, while the repeated
stale-light episode remains without a matching framebuffer sample.

One earlier M622BG run had 1,141 capture submissions to column `(2,7)`;
M622BI had 13 to that column on the same route parameters. This is consistent
with less duplicate relight work, but the runs are not a controlled comparison:
the saved world and host load changed. M622BI still failed aggregate gates
(readiness-debt rate 1.0, visual-hole proxy 0.111, fly wall median 31.06 ms).
At stop, readiness debt was 33 while direct mesh-missing, visual-hole,
black-sticky, and stalled-visible-black maxima were zero. Host CPU 55.1/74.9%
median/p95 and app CPU 19.8/35.0% are context only.

The separate sparse renderer-pixel analyzer processed 3,300 probes. All 102
samples below luma 32 joined a drawable opaque depth surface; none lacked that
join. Eight dark samples had unsettled/preview state, none had a light-revision
mismatch, and the samples were 101 outside fog and one partly fogged. The
trace found zero same-view preview-to-nonpreview handoffs, and its dark-pixel
set did not include `(3,3,6)`. The targeted watch query above recovers bright
observations omitted by the dark-pixel filter. Low-luma drawable pixels alone
do not establish a lighting defect; the post-publication pixel joins do not
prove a pre/post visual transition or an operator-visible defect.

Next capture a repeated stale-light episode with a same-view pixel sample before
and after the second publication, while retaining the target chunk watch and
geometry-revision fields. Continue inspecting why FIFO denials coexist with
many redundant-looking producer requests. Keep aggregate readiness debt
separate from framebuffer defects.

Artifacts: report `m622bi_pre_admission_noop_guard_20261017.json`; perf
`perf_20261017-103559_47804.jsonl`; INFO
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-103555.47804`; pixel
trace `m622bi_renderer_pixel_trace_20261017.json`.

### M622BJ: coalesce retries owned by DeferredFar during FIFO pressure

Static review showed that FIFO denial already retains the exact column and
vertical band in `DeferredFarRelightColumns`, but repeated non-visible
producers could retry the same key while the shared pressure predicate still
blocked it. The new admission path merges an existing deferred band's coverage
and priority, then returns. It does this only when the key has a deferred owner
and the current FIFO rule would still deny admission; visible requests and
requests that become admissible retain their previous behavior. The deferred
service remains responsible for retrying ownership.

The Release target was rebuilt with
`cmake --build bin --config Release --target Cubatarium --parallel 8` and the
short warm M335 no-teleport route completed 29 periods/27 steady with exit 0
and no timeout. The audit logged 46 `deferred_owner_skip` rows (26 from
`DrainIdleFocusPendingLight`); none of that producer's requests became FIFO
denials. There were 38 FIFO denials total, mostly from
`EnqueueVoidDarkColumnRelightNote` (30). Compared with the preceding M622BI
count this is suggestive only: saved-world state and host load were different.
Host CPU was elevated (median/p95 61.2/86.5%) and is context, not an admission
gate. No FIFO overflow drop was recorded.

The M622BJ route did not pass aggregate readiness gates (readiness debt 1.0,
visual-hole proxy 0.074, post-stop readiness debt 34). At stop, direct
mesh-missing, visual-hole, black-sticky, and stalled-visible-black counts were
zero. Its pixel trace contained 2,160 probes and 74 low-luma samples; all
joined drawable opaque depth, seven had unsettled/preview light, and none had a
light-revision mismatch. The dark-pixel set did not include `(3,3,6)`.

The target slice `(3,3,6)` again had a stale-dark witness at block
`(62,48,100)`. Its stale debt was queued at 10:53:12.215, FIFO-denied at
10:53:12.811, and published at 10:53:13.055 with matching light revisions
`2/2/2` and halo. A second stale debt was queued at 10:53:44.831 and published
at 10:53:45.279. Both target invalidations followed a light install in adjacent
source chunk `(4,3,6)`, which queued a `mesh_dependency` for dependent
`(3,3,6)`: job 152 changed the source from published revision 1 to field
revision 2; job 501 later changed it from published revision 2 to field
revision 3. The source chunk's own debt also converged after each install. The
remaining question is why job 501 produced a later settled-light install, not
whether the target was invalidated without a neighbor light change. The watched
pixel trace found nine target associations, eight
opaque depth-surface samples and three separate voxel-ray hits. All eight depth
samples were drawable/draw-ready, matched geometry and light revisions, and
had no preview or stale-remesh pending; none was dark. They all preceded the
second stale episode, and no repeated same-view pixel group was captured. This
is direct post-first-publication evidence, not a pre/post transition or proof
of the second repair on screen.

Job 501 captured the whole `(4,6)` band `0:80` (four full slices,
`neighbor_light=16`). About 34 ms earlier, `OnLitPendingNeeded` had inserted a
pending-light note for that same column's narrower `16:31` band. The existing
`DrainIdleFocusPendingLight` code unions a short pending band with its focus
vertical band before enqueue; with water enabled, the configured route height
can widen that union to `0:80`. This timing and band match that path, but the
accepted-enqueue and capture records do not include producer provenance, so the
join is not proven. Before narrowing that policy, add an audit join or otherwise
confirm which producer submitted job 501.

Artifacts: report `m622bj_deferred_owner_coalesce_20261017.json`; perf
`perf_20261017-105305_30268.jsonl`; INFO
`Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-105301.30268`; pixel
reports `m622bj_renderer_pixel_trace_20261017.json` and
`m622bj_renderer_pixel_trace_watch_3_3_6_20261017.json`.

Next trace the upstream owner/capture request for late source install job 501
on `(4,3,6)`, including whether `DrainIdleFocusPendingLight` expanded the exact
`16:31` pending note into a full `0:80` capture. Determine whether the
revision-3 update is expected streaming work or a repeated calculation before
narrowing the band policy. Arrange the existing short pixel trace to remain on
`(3,3,6)` across that neighbor update. Keep the M335 route and its known
start/speed; do not use aggregate readiness as a substitute for a framebuffer
transition.

### M622BK: use accepted enqueue provenance to follow band widening

Build the Release target, then use the M622BK short M335 command in
`FLIGHT_EXPERIMENT_SCRIPTS.md` with `CUBATARIUM_RELIGHT_AUDIT=1`. The new
`relight_admission event=queue_request` event reports `source`, requested and
queued bands, priority/visibility, FIFO/key ownership and index, distance from
the FIFO focus, and pending/lit-ready state. It is emitted only on actual
insert/coalesce paths, so pair it with existing pending-note, deferred-owner,
capture, snapshot, worker, and apply events for early returns and downstream
completion.

For the M622BK witness, inspect the exact column with:

```powershell
$log = 'bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-110345.41384'
rg 'pending note source=ChunkEmergeCoordinator.OnLitPendingNeeded.*column=\(4,6\)' $log
rg 'relight_admission event=queue_request.*column=\(4,6\).*requested_band=0:80' $log
rg 'capture submit column=\(4,6\)|snapshot job=678|slice job=678 coord=\(4,3,6\)|stale_light_debt event=(queued|published).*coord=\(3,3,6\)' $log
```

The narrow `16:31` note was followed by ten coalesced `0:80` requests from
`DrainIdleFocusPendingLight` and one full-band capture. Job 678 changed source
`(4,3,6)` and queued stale-light debt for dependent `(3,3,6)`, which later
published. This proves the request path, not that the expanded range is
unnecessary. Inspect the water-aware band formula and the solver's vertical
dependencies before changing it. The captured target pixels were all
draw-ready and revision-matched; there was no same-view transition or operator
verdict. See the M622BK audit entry and saved artifacts listed in
`FLIGHT_EXPERIMENT_SCRIPTS.md`.

### M622BL: skip only a queued owner that covers the whole idle-focus request

`DrainIdleFocusPendingLight` queries `GetTerrainColumnRelightQueueInfo` before
re-enqueue. It skips only if all of these hold: the key is present, the key is
in a physical deque, the owner is priority, the Y band is defined, and that
band covers the new candidate's complete range. A far-deferred owner, a
key-without-deque ghost, a non-priority queued request, or a narrower Y band
still reaches the existing enqueue path. Skips do not increment the
`max_columns` drain count. With `CUBATARIUM_RELIGHT_AUDIT=1`, grep
`event=idle_focus_skip_covered` to verify the decision and its queued/requested
bands. M622BL logged 553 such decisions.

This is independent of the breadth of the request. M622BK/M622BL traced a
narrow `16:31` pending-light note to a water-aware `0:80` capture. Preserve that
band while deriving the solver's required input: snapshot capture includes the
requested chunk interval and face shells, and skylight scans up to 15 chunks
above the processed chunk. The historical water-aware range includes sea
level minus four chunks through sea level plus two chunks. A narrow request may
therefore omit occluders or water-adjacent contributors. See M622BL in the
remediation plan for the next static review and the flight script index for
artifacts.

### M622BM: identify a covered DeferredFar idle-drain skip

`idle_focus_skip_covered` now includes `owner=priority_queue` or
`owner=deferred_far`. The latter is eligible only when the entry is unkeyed,
priority, has a defined Y band, and fully covers the idle-focus request.
`AdmitDeferredFarRelightColumns` continues to retry it; this guard only avoids
re-enqueueing the same already-owned work and charging it to `max_columns`.
Keyed-but-not-in-deque ghosts and bands that need widening are not skipped.

For M622BM, the INFO log had 405 covered skips (404 queued, one DeferredFar)
and two remaining `deferred_owner_skip` records from
`DrainIdleFocusPendingLight`. Those records log the merged band only. If their
pre-merge reason matters, add before/after fields to that audit event and
repeat only the established short M335 route. Pixel analysis for the target
`(3,3,6)` is saved in the M622BM watch report; all seven depth-surface joins
were ready and revision-matched. See the M622BM plan entry for remaining band
analysis.

### M622BN: validate partial-band skylight inputs

`PropagateSkylightColumn` examines up to 15 chunk slices above each target
chunk. The requested relight Y band is the write set; its solver input extends
above that band. Since commit `0b3f125b`, snapshots copy non-air geometry from
that upper interval into a read-only map and validate content/incarnation for
both resident and absent context chunks. They still apply results only to
target chunks. With `CUBATARIUM_RELIGHT_AUDIT=1`, `snapshot` and `worker_done`
records include `sky_context` (resident non-air context chunks copied).

The synthetic Release regression is `relight_result_install_test`; its partial
band case checks an opaque roof above the target, then mutates and streams in
upper geometry to verify stale-result invalidation. M622BN's short M335 run
exercised the code but stopped at focus x=-1 and did not cover the distant west
route. Treat it as a smoke run only. The captured `(3,3,6)` pixels were ready
and revision-matched, with no repeated same-view sample; they neither confirm
nor disprove the operator-visible long-flight symptom. Do not extend the route
just to raise aggregate gates. Before changing band widths, measure
`sky_context` copy count/cost for center-only and 3x3 captures and preserve the
read-set invalidation behavior.

### M622BO: distinguish context cost from capture stalls

M622BO's new `sky_context_ms` measures map construction, context coordinate
stamping, and non-air block copies inside `Capture`. On 1,458 context-bearing
captures it was 0.0111 ms median, 0.0152 ms p95, 0.1271 ms max. One complete
`capture_ms` sample was 31.7404 ms, but its context section was 0.0193 ms;
period telemetry showed maxima of 0.0005 ms lock wait and 1.337 ms snapshot
copy. Those measurements do not implicate either context copying or the global
capture mutex, and they do not identify what occupied the rest of that wall
interval. Leave the mutex and representation unchanged unless the same signal
recurs with a joined cause. M622BO reached x=-2, so it did not complete the
west-route acceptance segment.

The 1,805 snapshot jobs in the run all started and completed; at peak, two were
waiting and five snapshots were live, carrying four copied context chunks
(about 32 KiB, context only). This is evidence for the center-column flight
path, not the 3x3 player-edit path. The Release regression now constructs all
nine target columns plus upper context and verifies context chunks are stamped
read-only and never returned for apply. A full 3x3 context can hold 135 chunks
(about 1.06 MiB) per snapshot; this is a latent API bound, not an observed
streaming allocation.

### M622BP: interpret relight queue depth correctly

Static call-site review found no producer for `EnqueuePlayerRelight` in the
tracked source, and no caller for the generic async chunk-relight helper.
Treat the 3x3 capture size as a latent API bound; do not attribute it to the
normal streaming workload. The active terrain-column producer is gated by
`DrainRelightQueues`'s `max_inflight`, which covers queued, running, and
completed-but-unapplied jobs. This application-level gate bounds the unbounded
low-level relight pool queue for that producer. M622BO's short M335 queue
depth is an observation under that route, not a stress result.

Relight still submits on shared work-slot denial, so the counter is not a hard
cross-pool concurrency ceiling. Do not use host CPU percentage as a test
launch gate. Require an attributable contention trace and a lossless retry
path before changing this policy.

The active column solver's vertical geometry read closure is the requested
target band plus up to 15 chunk slices above it for direct skylight occlusion.
Adjacent chunks' boundary-light planes seed propagation into the target.
Liquid attenuation is already part of the solver; the water/sea-level policy
selects the scheduled band and does not imply additional solver input below
the band. Keep current scheduling
bands until a concrete mismatch is measured.

Do not reconnect the dormant async edit API before fixing its frontier
coverage: the snapshot's `HasChunk` sees full captured chunks only, and the
generic capture includes only the initial 3x3 footprint. Its frontier
iterations therefore cannot append further loaded chunks to the result. The
active streaming-column path uses zero frontier iterations. These findings
need no additional flight; continue with short M335 checks for runtime changes
and periodic fresh-world/source-lifecycle checks.

### M622BQ–M622BS — boundary light and snapshot skylight

Run the established short, visible M335 warm replay on `World_164`, with
`CUBATARIUM_RELIGHT_AUDIT=1`, `CUBA_ALLOW_DIRTY_AF=1`, screen-ray pixel probes,
warm-period stamps, and the same camera/route values recorded in
`FLIGHT_EXPERIMENT_SCRIPTS.md`. Keep these flags scoped to the process and
restore their previous values in `finally`.

`light_delta` is emitted only in relight audit mode. It records per-chunk sky
and block voxel changes, the first changed voxel, counts of changed face cells
in order `x-`, `x+`, `y-`, `y+`, `z-`, `z+`, and at most 12 boundary samples
with packed old/new values. Face counts overlap at edges and corners. A
boundary change is evidence of changed solver input/output, not by itself a
visible defect or proof that a specific neighboring voxel caused it.

M622BQ and M622BR repeated blocklight `1->0` / later `0->1` at `(64,54,98)`
in `(4,3,6)`, while adjacent `(3,3,6)` had blocklight changes on its `x+`
face. Their pixel joins did not bracket the transition. M622BS fixed a separate
confirmed snapshot bug: incoming horizontal skylight was queued but not
written into the cleared target; the boundary seed now applies the destination
material's attenuation before it enters the flood. The M622BS short flight and
pixel trace did not directly exercise a matching skylight boundary transition.

After any solver change, first build Release and check the short-route log for
the relevant channel and coordinates. Then inspect
`analyze_renderer_pixel_trace.py` output for same-view framebuffer joins and
light revisions. Readiness counters, dark luma, and pixel probes are supporting
evidence only; do not report an operator-visible regression without a matching
view or operator observation. Do not start long operator-monitoring flights.

### M623A: opt-in performance JSONL

Direct game launches now leave `CUBATARIUM_PERF_LOG` unset, which disables
`FramePerfMonitor` collection, periodic INFO summaries, and its disk session.
The flight harness explicitly sets `CUBATARIUM_PERF_LOG=1` before launching
the app, so analyzed flights still produce `bin/logs/perf_*.jsonl`. Its short
M335 smoke produced 29 intervals, traveled eight chunks, and did not reach the
west checkpoint. The harness returned 2 for existing renderer/stop-line gate
failures; the run only confirms that the harness still opts in and parses the
result. The report and trace are listed in `FLIGHT_EXPERIMENT_SCRIPTS.md`.

Runtime world/streaming telemetry used by scheduler decisions remains
enabled. When the periodic monitor is off, `WorldStreaming` samples Windows
process memory through its fallback no more often than every 30 frames,
matching the monitor's normal sampling cadence instead of querying every
frame. Do not interpret this smoke as product acceptance or as a visual
regression result.

### M622BT: horizontal skylight boundary contract

`RelightResultInstallTest.cpp::TestHorizontalSkylightBoundarySeedsAirAndWater`
blocks direct sky with an opaque target-chunk roof and seeds light only from a
read-only `x-` neighbor face. Expected target values are air=14 and water=13;
the water value specifically verifies the two-level horizontal liquid cost.
The Release test executable passed. This validates the snapshot solver's
boundary-seed contract, not a renderer or visible-flight symptom.

### M622BU: reconstruct relight FIFO wait

Run the offline analyzer on the M622BQ and M622BR application glog INFO files:

```powershell
python tools/analyze_relight_queue_age.py `
  bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-115807.29908 `
  bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-120147.42784 `
  --column 3,6 --column 4,6 --events
```

Observed first-insert-to-dequeue waits were at most 120.713 ms for `(3,6)` and
2,106.248 ms for `(4,6)`, below the 17.35/32.8 second spacing between the
repeated target light changes. One `(4,6)` dequeue preceded its `0->16` target
update by about 53 ms. Queueing contributes to some delay but is not a complete
explanation. These older M622BQ/BR logs predate `job_submit`, so `--jobs` reports
their affected light-delta records as unpaired.

### M622BV: correlate relight submit and apply

Newer `CUBATARIUM_RELIGHT_AUDIT` logs include `job_submit job=...` beside the
existing `apply job=...` and `light_delta job=...` events. Use the same analyzer
with `--jobs`, and select horizontal chunk columns in chunk coordinates:

```powershell
python tools/analyze_relight_queue_age.py `
  bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-123020.45676 `
  --column 3,6 --column 4,6 --jobs --job-events
```

The M622BV short M335 run paired five of six affected jobs. Targeted-column
submit-to-apply maximum was 69.105 ms; queued waits peaked at 661.784 ms in
`(3,6)` and 402.467 ms in `(4,6)`. Across the full run the paired-job median,
p95, and maximum were 35.746, 137.600, and 262.919 ms. These intervals combine
worker execution and completed-result waiting; they do not identify which part
dominates. The run did not reach the west checkpoint and failed existing
renderer/stop-line gates, so it remains diagnostic only.

### M623C: horizontal light-boundary dependency handoff

Static tracing showed that a center-only relight installs only its target
column, even when the computed horizontal face changes. The apply path now
queues a dependency relight for each resident cardinal neighbor whose face
light actually changed. Requests merge by target column and Y band, preserve
the normal bounded FIFO/deferred ownership, bypass the settled/no-op admission
guard, and keep the dependency band out of the surface-band clamp until
capture. No dependency is emitted for unchanged faces.

Build only the Release target, then run one short visible M335 replay with the
usual warm `World_164` settings and both audit opt-ins. Example:

```powershell
$oldAudit = $env:CUBATARIUM_RELIGHT_AUDIT
$oldDirty = $env:CUBA_ALLOW_DIRTY_AF
$env:CUBATARIUM_RELIGHT_AUDIT = '1'
$env:CUBA_ALLOW_DIRTY_AF = '1'
try {
  .\tools\run_m335_water_visibility_ab.ps1 `
    -EndMarginBlocks 0 -EnableFogPullIn -EnableScreenRayPixelProbes `
    -StampWarmPeriods -RequireExistingWorld -World World_164 `
    -ProbeMinCameraX -128 -ProbeMaxCameraX 128 `
    -StartX 120 -StartY 56 -StartZ 56 -CruiseEyeY 70 `
    -Yaw 180 -Pitch -30 -FlyPhaseSeconds 30 -StopPhaseSeconds 20 `
    -StopAfterBlockedSeconds 8 -IdleSeconds 8 `
    -PhaseId M623C `
    -Report bin/suite_reports/engine_refactor/m623c_boundary_dependency_20261017.json
}
finally {
  if ($null -eq $oldAudit) { Remove-Item Env:CUBATARIUM_RELIGHT_AUDIT -ErrorAction SilentlyContinue }
  else { $env:CUBATARIUM_RELIGHT_AUDIT = $oldAudit }
  if ($null -eq $oldDirty) { Remove-Item Env:CUBA_ALLOW_DIRTY_AF -ErrorAction SilentlyContinue }
  else { $env:CUBA_ALLOW_DIRTY_AF = $oldDirty }
}
```

For M623C, the short route completed without timeout and traveled eight chunks
(focus x=7 to x=-1), short of the west checkpoint. The INFO log recorded nine
dependency jobs and eight light deltas. Job 154 changed a blocklight voxel on
chunk `(4,3,6)`'s `x-` face; the implementation queued `(3,6)`, captured it
442 ms later, and its changed `x+` face then queued `(4,6)` and `(3,5)`. This
confirms the handoff and a short return wave. The nine dependency jobs had
31.160 ms median and 74.408 ms p95/max submit-to-apply. The full trace paired
1,484 jobs (25.295 ms median, 77.556 ms p95, 196.062 ms max).

The harness returned 2: readiness debt stayed at 1.0 and the post-stop missing
count reached 34, even though its near-focus visual-hole proxy stayed zero.
Those counters do not prove a framebuffer defect; the run also did not reach
the west checkpoint. Keep it diagnostic only. Before another visible run,
review bounded convergence under recurring boundary changes and add a focused
deterministic regression. Do not increase flight duration. Run the offline
correlation with:

```powershell
python tools/analyze_relight_queue_age.py `
  bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-125615.44512 `
  --column 3,6 --column 4,6 --jobs --job-events
```

### M623D: exercise the extracted boundary helper and deferred ownership

After extracting the face comparison into `ChangedHorizontalLightBoundaryMask`
and adding the two-column fixed-point fixture, build the Release app and
`relight_result_install_test`. Run the test executable, then repeat only the
short M335 route from M623C using `-PhaseId M623D` and report
`bin/suite_reports/engine_refactor/m623d_boundary_dependency_m335_20261017.json`.
The M623D app log is
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-130140.42972`;
the full-run perf JSONL is `bin/logs/perf_20261017-130144_42972.jsonl`.

M623D completed without timeout, covered eight chunks, and did not reach the
west checkpoint. Adequacy and eye-proxy stop-lines passed, and the
near-focus visual-hole proxy stayed zero; post-stop convergence still failed
(readiness debt 1.0, missing mesh/readiness maximum 46). Keep it diagnostic.
The audit recorded 14 boundary jobs and 16 light deltas. The initial streaming
job changed `(4,3,6)` at `(64,54,98)` from blocklight 1 to 0; 0.84 s later a
boundary-dependency result restored 0 to 1 after `(3,3,6)` had updated its
`x+` face. No further light delta was recorded for `(4,3,6)` in this short
trace, consistent with a two-step boundary return wave rather than a
continuing oscillation.

The bounded FIFO did deny a dependency request for `(3,5)` at capacity with
`band_needs=0`. Its dependency owner remained durable and later submitted job
444, with a merged `0:80` Y band. Across the run there were 606 paired submit /
jobs (604 paired, two missing applies), median/p95/max
37.105/127.051/228.939 ms;
the 14 boundary jobs had 44.714 ms median and 82.281 ms p95/max. Host CPU was
80.6% median / 100% p95 and is context only. The Release
`TestHorizontalBoundaryDependencyWaveSettles` fixture passed: a torch lights
the adjacent chunk to level 13 and a source → neighbor → return-check wave
settles in three jobs. It does not model deferred FIFO pressure or a full
world's boundary graph.

An offline lifecycle reconstruction of the full M623D INFO log found 16 raw
boundary-source enqueue/coalesce/denial events and 14 `boundary_exact=1`
captures. All keys had a later capture; the log also contains the ordinary
shutdown/save summary. There is no direct marker-count record yet, so this is
event-based evidence rather than a dedicated queue invariant.

### M623E: invalid cold-runtime route integrity check

The short no-teleport M335 profile was repeated on `World_188` with
`-RequireExistingWorld`, `-FlyPhaseSeconds 30`, `-StopPhaseSeconds 20`,
`-StopAfterBlockedSeconds 8`, and `-IdleSeconds 8`, with relight/perf audit
opt-ins. The loader reported `kind=load`, 121/121 spatial columns, and zero
generation; startup was 3.944 s. The runner pinned the requested
`(120,56,56)`, but the runtime began at focus `(-3,1)` instead of `(7,3)`, so
the manifest integrity gate failed. This world does not provide a valid replay
for that locus; do not compare its route/readiness metrics with M623C/D.

The run completed 29 periods without timeout. Host CPU was 95.8% median / 100%
p95 and app CPU was 21.2% median / 45.1% p95 of machine capacity; treat host
load as context only. The available INFO file ends mid-record before orderly
shutdown. It contains 449 raw boundary-source events, 241 exact dependency
captures, 198 paired boundary applies, and 43 submitted dependency jobs with no
logged apply. Fourteen keys were still outstanding at the last complete
relevant audit line, but the truncated log cannot establish whether they later
drained. These are queue-pressure diagnostics from an invalid start, not proof
of a visual defect or a shutdown leak. The short probe exposed why route
integrity and complete shutdown logs are required for cold-runtime analysis.

Artifacts remain local at `bin/suite_reports/engine_refactor/m623e_boundary_dependency_cold_runtime_m335_20261017.json`,
`bin/logs/perf_20261017-130919_43876.jsonl`, and
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-130914.43876`.
For primary M335 comparisons continue to use `World_164`; only use a new world
after verifying its start locus, without changing the camera or flight timing.

### M623F: direct boundary queue ownership counters

The Release app now appends these fields to audit-only `capture phase=end`
events: `boundary_markers`, `boundary_in_fifo`, `boundary_in_deferred`, and
`boundary_without_owner`. The count scan and log output are guarded by
`CUBATARIUM_RELIGHT_AUDIT=1`; normal direct launches do not pay this cost.

M623F repeated the established short visible M335 route on `World_164` with
the same 30/20/8 timing and start `(120,56,56)`. Start integrity passed, focus
moved `(7,3) -> (-1,3)`, and the process completed 29 periods without timeout.
The audit had 1,799 complete capture-end rows: boundary markers peaked at 3,
`boundary_without_owner` stayed zero, and the last complete row was
`0/0/0/0`. There were 7 boundary jobs and 12 light-delta events. Across 591
jobs, 586 had apply records; paired latency was 33.703 ms median, 125.106 ms
p95, and 197.729 ms max. Host CPU was 51.5% median / 75.0% p95, context only.

The harness still returned `pass=false` on readiness and eye-proxy stop-lines;
readiness debt remained 1.0, visual-hole proxy rate was 0.074, and its
post-stop maximum was zero. No operator framebuffer verdict was recorded.
Treat this as a queue-ownership check, not a product acceptance or visual
defect claim. The available INFO log is truncated before the ordinary shutdown
summary, but its last complete queue sample already had zero boundary markers.

Artifacts remain local at
`bin/suite_reports/engine_refactor/m623f_boundary_marker_counts_world164_m335_20261017.json`,
`bin/logs/perf_20261017-131539_40028.jsonl`, and
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261017-131535.40028`.
For future boundary work reuse the M623C command and parameters, changing only
phase id/report path; preserve the start-integrity check and short duration.
