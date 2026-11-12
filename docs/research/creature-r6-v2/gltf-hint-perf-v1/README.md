# Проверяемая оценка glTF key interval: та же поза, меньшая цена multi-rate

2026-11-12. Runtime оптимизация завершена в рабочем Release. **Не R6 acceptance**:
пилот badger v2 остаётся неопубликованным, GPU/full-frame/stance и остальные
gates сохраняются. Цель всех56+5/трёх backend не сокращена.

## Решение

`GltfKeyframeUpperIndex` раньше сканировал каналы до128 keys и использовал
lower_bound для длинных. У badger57/73-key repeated limb tracks оставались
линейными. Теперь до16 keys сохранён прежний scan; для остальных вычисляется
оценка индекса по endpoints/count, проверяется соседняя пара и только после
точной проверки bracket принимается результат. При неудаче — lower_bound.

Для ordered timestamps условие принятия `times[u-1] < t <= times[u]`
(с особым первым интервалом от key1) доказывает тот же индекс, что старый scan.
Это наш вывод из монотонности, не предположение о равномерности каждой glTF.
У сильно неравномерных/повторяющихся меток оценка может ошибаться; fallback
сохраняет семантику и logarithmic worst-case lookup. Возврат первого not-less
и требования partitioned range описаны в
[C++ working draft](https://eel.is/c%2B%2Bdraft/algorithms#lower.bound).
Exact timestamp по-прежнему использует предыдущую/текущую пару, не следующую.

Alpha, lerp/slerp, wrap/clamp, hierarchy/IBM/weights, fade и все asset bytes
не менялись. Оценка служит только поиску индекса, её double арифметика не
попадает в interpolation factor. Длительности/число ключей/скелеты/PNG не урезаны.
Новых per-asset/per-actor heap fields нет. Это не отменяет рост памяти самих
длинных clips: у badger proposal accounted heap+96480B остаётся.
Порог16 — выбранный вариант, не доказанный глобальный optimum для всех платформ.

## Проверки точности

- [415098 Release interval checks](interval_checks.log):23 размера0…4097,
  9 распределений — обычное неравномерное, uniform, два skewed clusters,
  repeated blocks, flat, subnormal, huge finite и ordered infinite endpoints.
  Все exact keys/nextafter, края, ±inf/NaN query time,1000 LCG probes на случай.
  Последние случаи шире валидного glTF; unordered/NaN key arrays не заявлены.
- [Полная asset equivalence](equivalence.json):35 packaged G + badger v2,
  **142684 palettes и173430 roots float-bit exact** против первоначального
  linear C++ sampler. Все реальные clips, объединение exact/adjacent keys,
  257 равномерных phases, обе root loop modes, clip pairs/17 phases/6 fade
  weights включая порог0.001, null/rest cases. Это не только Walk/Run или хеш PNG.
- [15 targeted Release tests](release_tests.log) PASS. Весь CTest не объявлен
  зелёным; прежний bounds FAIL17 не исправлен этой оптимизацией.

Сначала shadow helper был скомпилирован в отдельный Release bench. После
положительного ABBA результата этот файл перенесён byte exact в runtime,
расширены tests и выполнена обычная desktop Release сборка.
[Trial provenance](trial_provenance.json), [implementation hashes](implementation_context.json).
Frozen baseline ABI/clock headers совпадают с текущими; старые object files
с переименованными функциями имеют тот же layout и compiler flags.

## CPU sampler: парное измерение, не world gate

[Raw trial](trial/context.json), файлы0…5 рядом. Core2, serial, один процесс
с двумя алгоритмами,81 randomized/mirrored batches/condition; исходный linear
sampler выступает общим control. Badger hint/indexed128/indexed128/hint.
Остальные два runs — Orc и treeman. Во время timing не запускались flights/builds.

Здесь **p50 парного candidate/linear-before отношения batch averages**, по2 runs:

| Badger | Предыдущий indexed128 | Новый hint |
|---|---:|---:|
| Walk/no fade |+12.5…12.8%|+3.2%|
| Walk/fade0.5 |+7.4%|−1.8…−1.6%|
| Run/no fade |+15.5…15.7%|+3.2…3.4%|
| Run/fade0.5 |+9.4…9.5%|−1.9…−1.2%|

Важно: linear-before — live короткий badger до proposal. Если **обе** версии
с новым hint, candidate p50 sampler дороже live примерно2.3…6.4%, не ноль.
Нельзя смешивать эти два baseline. Orc current multi-rate с hint в отдельном
run дешевле прежнего короткого linear на9–11%; treeman same-asset примерно27–28%.
Это три measured G cases, не доказательство ускорения каждого из35 видов.
Во всех timing cases palette/root float signatures exact.

## Actual-world badger: CPU часть выдержана, GPU/frame не приняты

[World fixture](world_fixture.json): полная disposable копия World_254,
полдень0.25 frozen, clear weather/current target, auto weather disabled.
Оригинальный world не редактировался. В двух QA model copies одинаково отключён
natural spawning; только выбранный badger before/current различается.
Обычные motor/collision/state, intent land_walk_pause, без присваивания clips/
facts/позиции. Это fixed-population fixture, не AI/natural spawning acceptance.

8 runs по40sec, первые20sec прогрев, измеряются20…39sec; обратный порядок
во втором повторе. **baseline** — прежний linear Release5dd79a…;
в JSON ключ **indexed** означает новый hint Release1507b3…, не старый indexed128.
После серии bin восстановлен и проверен SHA.
[Context](world_badger/context.json), [summary](world_badger/summary.json),
[independent audit](world_badger/independent_audit.json).

На каждом steady frame2 considered/1 culled/1 drawn/1 draw call/1 palette
upload/1 pose; grounded Walk и Idle подтверждены facts. Requested/runtime world
совпадают, обычный saved terrain load,121 spatial columns. GPU query skips0,
frame drops0, ≥99% GPU coverage. Окружение/камера/расстояние меняются только
через тот же ordinary QA маршрут; это не pixel-identical physics replay.

Walk-only: median двух **run individual-frame p95**, не pooled p95:

| Asset / lookup | CPU pose ms | CPU pass ms | GPU draw interval µs | Window wall ms |
|---|---:|---:|---:|---:|
| Live / linear |0.02560|0.12645|15.800|20.265|
| Live / hint |0.02530|0.13340|28.180|26.976|
| Proposal / linear |0.02760|0.12865|13.860|20.042|
| Proposal / hint |0.02740|0.13125|12.620|21.089|

[CPU bound observation](world_badger/cpu_bound_observation.json): max observed
candidate run p95 / min observed live-linear run p95 =1.0803 Walk,
1.0589 all-steady. **Предварительный CPU pass ≤10% выполнен в этих повторах
одного fixture.** Это не confidence interval и не гарантия всех будущих запусков.

GPU/full frame остаются **unproven**, даже при благоприятных median:
один неизменный live/hint control дал GPU p95 12.16→44.20µs (3.635×),
window15.06→38.90ms (2.584×). Whole-process host CPU median по runs32.1…96.6%,
включает startup/warmup и не является точным steady-frame CPU correlation.
Прогрев/фиксированный свет уменьшили часть разброса, но не устранили его.
GPU величина — Queue.Flush command interval, не shader-only/полный GPU frame.
Без noise-consistent controls весь provisional CPU/GPU/frame gate не закрыт.
Cold load, full RSS/GPU memory/family budgets и Android здесь не приняты.

## Native regression окончательного Release

Desktop собран требуемой командой
`cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8`.
verify-windows-exe PASS. Финальный bin SHA
**1507b3e843d54a29e7e8150d04da3d353f870828362ed4371461c87b6a574755**.

- [5290/5290 body PNG RGBA exact](production/body_parity.json) против81283a…,
  shader/resource/pose/state metadata exact, разрешена только смена EXE;
  ни changed-resource, ни period-factor исключений.
- [936/936 FP RGBA exact](production/fp_previous_release_parity.json) против
  предыдущего authored Release. Отдельный legacy/native FP audit PASS:
  1872 authored arm draws/6 retained cases. FP framing/walls/presets не приняты.
- Save128 cases/256 loads PASS; не полный пользовательский world corpus.
- [Badger proposal GPU parity](badger_candidate/sampling_parity.json):165/165
  PNG exact (ordinary/matrix/envelope/edge) и весь retained transition JSON
  exact к81283a…, включая2156 frames. Поэтому старые artwork comparisons не
  переписывались под новый EXE; свежая проверка явно связывает два runtime.
- [61 live R5 ledger](ledger_after.json) fresh; кандидат не опубликован.

## Следующие обязательные шаги

1. GPU/frame controls при сопоставимой host/GPU нагрузке, family memory/load
   budget и world stance/stride/sliding/turn/terrain/AI/action badger.
2. Затем normal regeneration/review/publication/full production regression
   badger revision; сейчас canonical badger остаётся прежним.
3. Отдельные treeman multi-rate и puffin Fly/ground posture/cadence pilots;
   world/control/UI-pack/compatibility/FP/Android/clean-package и все56+5/три backend.

Все61 R6 pending, цель active. Новый helper ускоряет сохранение более полного
motion, но не является художественной или полной runtime приёмкой.

Воспроизведение world серии: `run_creature_world_pass_bench.py` с явными
`--baseline-exe`, `--candidate-exe`, `--species badger`,
`--candidate-species-dir assetgen_output/creatures/badger_sway_v2/models/creatures/badger`,
`--world CreatureQaPerf_HintV1 --fixed-time 0.25 --seconds 40 --warmup 20 --out FRESH_PATH`.
Затем `audit_creature_world_pass_bench.py --out FRESH_PATH`. Все binaries и world
fixture должны соответствовать сохранённым hashes; новые captures — в fresh path.
