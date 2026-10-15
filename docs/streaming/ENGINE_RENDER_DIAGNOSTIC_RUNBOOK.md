# Диагностика стриминга и видимости мира

Обновлено: 2026-10-15. Этот runbook фиксирует повторяемые сценарии и порядок
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
от длинного прогрева/выгрузки или target-run дал противоречивые данные. После
исправления, перед завершением этапа рефакторинга достаточно одного полного
Release прогона для acceptance; повторять его после каждой локальной правки не
нужно.

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

## Пример: pooled-buffer регрессия M589 → M590

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
включая 468 water samples. M589 и M590 вместе дают прямую проверку причины и
исправления на одном участке мира.

Из оставшихся 1,703 совпадений без RGB change 1,681 были дальше fog end;
1,452 точно совпали с fog RGB, 1,660 отличались не более чем на один 8-bit
канал. Поэтому такие далёкие точки надо отделять от ошибок pool/rendering.
M590 при этом не прошёл общий streaming stop-gate (readiness debt); короткий
target-lane не является завершением проверки дальнего стриминга.

Исторические команды и более ранние численные результаты M587–M590 дополнены
в [`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md)
и [`FLIGHT_EXPERIMENT_SCRIPTS.md`](FLIGHT_EXPERIMENT_SCRIPTS.md). Оставляйте
длинный M335 для тех случаев, когда короткая повторяемая выборка не отвечает
на диагностический вопрос, и для одного итогового post-fix acceptance run.
