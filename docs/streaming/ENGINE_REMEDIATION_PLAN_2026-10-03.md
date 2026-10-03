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
   конфигурационные hashes, траекторию и метрики.
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
участки исправлены или доказано, что они не мешают загрузке/созданию. До этого
дальние stress-маршруты не запускать.

### G2 — Разделить disk reload и procedural generation

Добавить координатную трассу жизненного цикла колонки:

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
прогоном с framebuffer/ray/lifecycle evidence; после серии исправлений повторить
дальний маршрут и дождаться stop convergence. Пустой/чёрный proxy не считать
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
Включить `/MP` для основного приложения; строить только Release target и
подтвердить compiler options в generated project/verbose build. Не запускать
Debug или тестовые targets в этой работе.

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
