# glTF sampling: длинный Orc clip и точность оптимизации

Статус: завершён ограниченный CPU/pose/native regression этап, **R6 не принят**.
Все 56 существ + 5 скинов сохраняют R6 pending. Данные сняты 2026-11-12 на
Ryzen 7 4700U, Windows, MSVC Release. Ресурсы на этом этапе не менялись.

## Изменение runtime

`GltfKeyframeUpperIndex` общий для bone palette и root sampler. До 128 ключей
остаётся прежний линейный scan; длинные каналы используют `lower_bound` от key1.
Это сохраняет первый timestamp **>= time**, включая exact-key предыдущую пару
интерполяции и её округление. `upper_bound` здесь изменил бы результат.
Alpha/SLERP, wrap/clamp, blend, hierarchy/IBM/weights не менялись.
128 — консервативный порог, сохраняющий путь распространённых 65-key каналов;
это не доказательство глобально оптимального порога.

Дифференциальный Release test: 25 017 запросов к старому scan, размеры
0/1/2/3/7/8/9/16/65/127/128/129/257/513/2049, неравномерные ключи,
exact/nextafter, края, ±inf/NaN и детерминированные random probes.
Это проверка интервала; валидные glTF timestamps должны быть упорядочены.

## Парное измерение

[paired_run_0.json](paired_run_0.json), [provenance](paired_context.json).
Старые реальные C++ sampler функции с переименованными символами и новый
sampler связаны в одном Release процессе. Loader общий, два asset retained.
CPU affinity core2; 1024 warm poses, 81 batch, в каждом четыре условия в
детерминированно перемешанном и обратном порядке, 2048 poses/condition.
Walk/Run, без fade и с Idle fade0.5. Одновременно builds/flights не запускались.
До/после — own Orc непосредственно до slow sway и текущий опубликованный Orc.
Не исходный сторонний Orc. Фазы каждого asset нормализованы на его период.

Значения — **p50 batch-average µs/pose**, не p95 отдельного кадра:

| Clip | Fade | Прежний clip / linear | Новый clip / linear | Новый clip / indexed | p50 парного indexed/linear, новый |
|---|---:|---:|---:|---:|---:|
| Walk | нет | 2.864 | 4.040 | 3.007 | 0.745 |
| Walk | 0.5 | 4.196 | 5.555 | 4.514 | 0.813 |
| Run | нет | 2.922 | 3.733 | 3.021 | 0.810 |
| Run | 0.5 | 4.264 | 5.237 | 4.531 | 0.865 |

На новом asset sampling дешевле на 13–26%. Новый asset с оптимизацией всё ещё
дороже прежнего короткого asset: p50 парных отношений +3.4…7.7%, p95 отношений
+5.9…8.6%. Это quantiles **отношений batch averages**, не creature-pass frame
p95 и не применение общего world CPU/GPU ≤10% gate. На старом asset новый
scan тот же, различия времени порядка 1–2% не означают отдельную оптимизацию.

1025 palette phases на каждый clip/fade/asset и 513 root phases на каждый
animation дали одинаковые float-bit signatures old/new. Подписи сравниваются
между алгоритмами для одного asset; разные версии анимации имеют разные позы.

Первый межпроцессный ABBA эксперимент [matched](matched/summary.json) сохранён,
но **не используется для performance verdict**: host CPU доходил до 98%,
batch p95 выброс до219µs. Там применялся промежуточный cutoff8, не финальный128.
`passed` в том report означает execution/signature checks, не perf acceptance.
Промежуточный EXE485445… и его sources зафиксированы в indexed8_context.json.

## Цена данных и загрузки

Orc animation BIN 131400→313200 bytes. Учтённые vector capacities + heap-only
strings 158356→398400 bytes (**+240044**). Это subset resident CPU asset storage:
без map nodes, allocator/control blocks, GPU и полного application RSS.
Сканирование ключей не сокращает память данных.

21 парная warm reload/asset: p50 2.4363→3.8618ms, p95 2.6152→3.9800ms.
Это повторный loader с тёплым файловым cache, не cold startup.
Наблюдавшийся межпроцессный peak working set median 4546560→5873664 bytes —
вспомогательный noisy process показатель, не точная цена asset и не desktop RSS.
Рост памяти/загрузки реален; нулевой regression не заявлен.

## Release и native evidence

Desktop собран требуемой командой `cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8`.
EXE `a8d94126dad8067857a3332b3e929f544b43d380bee48f3ab138620a78a61374`;
verify-windows-exe PASS. [15 targeted tests](release_tests.log) PASS.
Flight через `run_creature_pilot_qa.py` → `flight_sim_run.py`, production56,
matrix/FP/save, actual Orc world; context и команды в `production/`.

- [Body parity](production/body_parity.json): **5290/5290 RGBA exact** против
  previous Release235ce5…, resource/shader/pose metadata exact; разрешена только
  смена EXE. Никаких changed-resource/time-factor исключений.
- [FP previous parity](production/fp_previous_release_parity.json): 936/936 exact.
  Отдельный FP audit против legacy fallback PASS: 1872 authored GPU arm draws,
  6 retained cases; world FP framing/walls/attack/presets этим не приняты.
- Save probe PASS: 128 cases/256 loads. Не полный пользовательский world corpus.
- [Ledger](ledger_after.json): 61 свежий R5, published/staged/bin совпадают.
- [Bounds](bounds_audit.log): **FAIL17**, существующий долг не скрыт правкой
  collider/tolerance. Весь CTest не объявлен зелёным.
- [Native лист](native_lookup_comparison.png): одинаковые кадры до/после поиска,
  несколько G плюс B/R; это regression sheet, не новая художественная приёмка.

Лист просмотрен: у Orc сохранены наклон корпуса и профиль шага; у badger —
низкий вытянутый корпус и лицевая маска; у treeman — тонкие ветвистые конечности;
у puffin — раскрытые крылья; у cow и rigid walker — прежние силуэты и материалы.
Новых отличий от baseline Release не видно, что согласуется с полной RGBA parity.
Тонкие конечности treeman, постановка крыльев puffin и stance Orc остаются
предметом исходной художественной/motion задачи, не принимаются этим листом.

## Следующие обязательные шаги

1. Matched actual-world CPU/GPU creature-pass и individual frame distributions,
   load/total RSS, одинаковые actors/camera/state/seed, noise/control runs.
   Этот sampler report не закрывает общий performance gate.
2. До массового multi-rate bake — family memory/load budgets, контроль числа
   ключей и сравнение bounded bake/сжатия с точностью поз и ground clearance.
   Не уменьшать rig/UV/atlas/анимацию ради произвольного BIN cap.
3. Отдельные badger/treeman multi-rate pilots и puffin Fly posture/folding/cadence,
   ground gait; сохранить articulated limbs и сравнивать equal-elapsed движение.
4. Все прежние R6 world stance/terrain/sliding, culling, user/control/UI-pack,
   FP, совместимость, Android/clean-package gates остаются обязательными.

Воспроизводимый стенд: `tools/research/creature_gltf_sampling_paired_bench.cpp`.
Compile batch + SHA source/objects/assets/exe находятся в paired_context.json;
архив baseline исходников и EXE — в assetgen_output/creatures/gltf_sampling_perf_v1.
Новый запуск пишет свежий OUTPUT.json; argv: `paired_bench.exe OUTPUT.json BEFORE.gltf CURRENT.gltf`.
