# Реальный world pass: Orc Walk/pause и границы performance evidence

2026-11-12. **Performance gate не пройден; R6 не принят.** Сделан следующий
этап после [retained CPU sampling](../gltf-sampling-perf-v1/README.md): измерены
отдельные кадры обычного мира, включая CPU поз, CPU прохода существ и GPU
интервал отправленных команд. Это один Orc/G, не все56+5/все backend.

## Диагностика и сборки

`CUBA_CREATURE_PASS_TIMING_PATH` включает `CreaturePassTimingProbe`:

- CPU `UpdatePose` и CPU creature pass, индивидуальный window wall/swap;
- два `GL_TIMESTAMP` вокруг `Queue.Flush`, ring16 без ожидания результата;
  чтение только после `GL_QUERY_RESULT_AVAILABLE`;
- фактические considered/culled/drawn/draw calls/palette uploads и pose states;
- одинаковый резерв под observer frames2359296 bytes, bounded16384 frames;
- никакой записи timing файла внутри кадров: JSONL сохраняется после оконного
  цикла с ещё действующим GL context. Неполный файл/потери кадров отвергаются.

GPU величина — elapsed command interval, не чистое время shader ALU, не весь
world GPU frame и не сумма, которую можно прибавлять к CPU wall.
Timestamp API и асинхронное чтение сверены с первичными источниками:
[Khronos Query Object](https://wikis.khronos.org/opengl/Query_Object),
[glQueryCounter](https://wikis.khronos.org/opengl/GlQueryCounter).
Без env flag нет query/observer allocation/file writes; normal rendering
семантика проверена полным native regression. GLES ветка не вызывает desktop
timer API; Android runtime этим не проверен.

Обе сборки MSVC Release, требуемой desktop командой. Одинаковый diagnostic код,
разница только в helper: сохранённый исходный scan для всех каналов или текущий
scan≤128/lower_bound для длинных tracks. Временная baseline source замена
восстановлена, затем окончательно пересобран индексированный Release.
[Build provenance](build_context.json).

Baseline EXE `5dd79a6997274d4d4b8a9dc9c95359d2a19eb80cc3d61952b10dbcf114d4fed6`.
Окончательный bin EXE
`81283a1c5b148e659eaa66e9f4be9e14cbf2730ad70b3e50a1f6a59535076018`.
GL3.3 Core23.19.14.07.240801, AMD Radeon(TM) Graphics; CPU Ryzen7 4700U/8 logical.

## Сценарий и проверки равенства нагрузки

[Context](matched_fixed/context.json), [raw summary](matched_fixed/summary.json),
[independent audit](matched_fixed/independent_audit.json).
20sec World_254, обычный Orc motor/collision/state derivation. QA driver
`land_walk_pause` использует прежний горизонтальный cycle: .75…3sec движение,
4sec cycle с разворотом ±X, requested speed0.5. Позиции, facts, clips не назначаются.
Это фиксированный QA маршрут, не свободная AI/terrain/action приёмка.

Два полных изолированных каталога с одинаковыми ресурсами кроме pre-sway/current
Orc. **В обеих копиях** catalog.natural_spawnable=false для фиксированного
population. bin/models и staging не редактировались. Пользовательские сохранения
вручную не менялись; их эквивалентность этим экспериментом не проверялась.
Это не тест естественного spawning. SHA всех файлов и variant differences
записаны. Сначала четыре условия linear-before/indexed-before/indexed-current/
linear-current, затем обратный порядок: всего8 запусков, без параллельных flights
или builds. Во время каждого flight внешний psutil сохранял actual app PID,
working set и host CPU. По завершении bin восстановлен в indexed Release.

Первые5sec и последняя1sec исключены заранее. Каждый принятый steady frame:
considered2 (user+Orc), culled1, drawn1, draw call1, palette upload1, одна pose.
Walk/Idle подтверждены обычными facts, frozen=false. GPU coverage≥99%,
skipped queries0, dropped frames0. Y/Z камеры точны между запусками; разброс
X extrema менее0.02block из-за различного physics timestep. Не pixel-identical
world routes. Per-run camera ranges/GL log witnesses сохранены в audit.
Свет/погода не получили отдельного нормализованного fixture в этой серии.

## Измерения и отрицательный результат gate

Таблица для фактических Walk кадров. Каждая ячейка — **median двух run p95**
индивидуальных кадров, не pooled p95 и не p95 interval means:

| Asset / lookup | CPU pose ms | CPU pass ms | GPU draw interval µs | Window wall ms |
|---|---:|---:|---:|---:|
| pre-sway / linear | 0.02895 | 0.12800 | 22.100 | 27.301 |
| pre-sway / indexed | 0.02860 | 0.15580 | 34.340 | 42.988 |
| current / linear | 0.04095 | 0.15720 | 31.645 | 33.531 |
| current / indexed | 0.03585 | 0.16285 | 31.235 | 33.869 |

На current asset CPU pose p50 linear0.01970→indexed0.015625ms (−20.7%),
median run p95 −12.5%. Это согласуется с изолированным sampler улучшением.
Но полное время прохода не обязано следовать sampler: его p95 здесь выросло
0.15720→0.16285ms при смене lookup. Нельзя выбирать только удачный показатель.

Относительно pre-sway/linear наблюдаемые current/indexed CPU pass p95 **+27.2%**,
GPU interval **+41.3%**, window wall **+24.1%**. Они превышают provisional10%
gate в этой серии. Причина не отделена от noise: host CPU median по запускам
69.9…96.2%; у неизменного indexed/pre-sway повторный GPU p95 расходится
в1.976 раза, CPU pass p95 в1.434 раза, window p95 в1.765 раза. Самый тяжёлый
host load пришёлся именно на indexed/pre-sway. Это не доказательство такого же
causal regression в asset, но **и не основание считать gate пройденным**.

Whole-process RSS median разных запусков442.8…458.5MiB. Это process-level
наблюдение с observer/cache/terrain, не выделенная стоимость клипа; оно не
отменяет измеренные +240044B CPU vector/string storage и warm load2.4363→3.8618ms
из предыдущего эксперимента. Cold startup/total GPU memory не приняты.

## Неуспешные пробы сохранены

- `smoke/`: начальный observer не записал файл из-за flight exit до renderer
  destructor. Исправлена явная запись при завершении оконного цикла.
- `smoke_session/`: исправленный диагностический run,557 frames, ordinary
  Walk328/Idle199 +30 до появления QA actor,556 GPU results, skipped0/dropped0.
- `matched/`: первая полная scene серия остановлена на первом run: natural
  population добавил другие виды, draw workload1→3. Не принята. После этого
  fixed-population fixture введён в обе копии одинаково; original assets не менялись.

Эти отказы не были превращены в successful runs выбором удобных кадров.

## Регрессии финального Release

После серии timing flag выключен, обычные production56+5 ресурсы:

- [5290/5290 RGBA exact](production/body_parity.json) против предыдущего
  Releasea8d941…, shader/resource/pose/state metadata exact, разрешена только
  смена EXE, без resource/time-factor исключений.
- FP legacy/native audit PASS:936 frames,1872 GPU authored arm draws/6 retained
  cases. Дополнительно936 FP RGBA exact против предыдущего authored Release.
- Save128 cases/256 loads PASS; это isolated entity scope, не full user-world corpus.
- [15 targeted Release tests](release_tests.log) PASS; verify-windows-exe PASS.
- [61 R5 ledger](ledger_after.json) fresh. [Bounds audit](bounds_audit.log)
  по-прежнему **FAIL17**; весь CTest не объявлен зелёным.

[World лист](world_orc_comparison.png) дополнительно снят и просмотрен отдельно
от timing серии: видны обычные шаги, пауза и изменившийся slow sway на реальной
береговой поверхности. Верхний ряд — own pre-sway, нижний — own current,
не сторонний original reference. Сравнение независимых frames002/003/004,
фазы не выравнивались. Текстуры/снаряжение и mesh прежние; разные наклон и
состояние ожидаемы. Лист не доказывает отсутствие sliding, полную постановку
стоп или качество всех переходов. [SHA и crop recipe](world_orc_comparison.json).

## Решение и следующий шаг

Не распространять длинные multi-rate clips массово на основании этой серии.
Нужны noise controls/сопоставимая host нагрузка и нормализованный свет/погода,
затем повтор CPU pass/GPU/frame gate. Инструмент для такого повтора уже работает.
Перед дальнейшей runtime оптимизацией профилировать весь `UpdatePose`/fade
clearance и cache/appearance costs: 3µs palette sampler — только часть world pose.
Фиксировать общий family memory/load budget до массового bake.

Можно независимо готовить изолированные badger/treeman multi-rate proposals и
puffin Fly/ground posture/cadence review, но не выдавать их за опубликованную
performance/художественную приёмку. Все остальные R6 stance/terrain/sliding,
AI/control/UI-pack/compatibility/FP/Android/package gates сохраняются.
**Все61 R6 pending, цель active.**

Воспроизведение: `python tools/research/run_creature_world_pass_bench.py --out FRESH_RESEARCH_PATH --seconds 20`,
затем `python tools/research/audit_creature_world_pass_bench.py --out FRESH_RESEARCH_PATH`. Нужны сохранённые
два Release EXE из build_context и immutable pre-sway snapshot. Запуски только
через `run_creature_pilot_qa.py` → `flight_sim_run.py` на bin/Cubatarium.exe.
