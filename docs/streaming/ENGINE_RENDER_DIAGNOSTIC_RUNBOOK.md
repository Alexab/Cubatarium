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

Дальше проверьте эту ветку на cold-but-repeatable коротком baseline и отдельно
измерьте время от settlement до stale-remesh queue/admission/publication. Не
запускайте длинный операторский M335 для подтверждения этого локального
исправления; сначала достаточно target-lane, затем — только при необходимости
ещё одного короткого повтора.
