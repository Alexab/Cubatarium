# Badger sway v2 — исходный медленный характер, собственные суставные шаги

2026-11-12. **Изолированный неопубликованный proposal**, не R5 revision и не R6
acceptance. Live badger recipe/models, staging, bin assets и runtime C++ на
этом этапе не менялись. Desktop flights выполнены только через
`run_creature_pilot_qa.py` → `flight_sim_run.py`, Release EXE
81283a1c5b148e659eaa66e9f4be9e14cbf2730ad70b3e50a1f6a59535076018.

## Что восстановлено

[Диагностика источника](../motion_semantics_v2/README.md): весь reference mesh
привязан к одному joint; период Walk/Run2.5s при unit travelScale. Большая дуга
dominant joint61.45° не означает такую же суставную пластику ног или чистый
Z-roll диапазон. BIN reference ранее проверен byte exact к b35f00a75, JSON
newline-only; текущие SHA трёх файлов вновь сверены с diagnosis.
Эта исходная схема skinning в генератор не переносится.

В собственной live модели articulated13-joint rig, walk stride0.35s и run0.275s.
Быстрый body roll±6/9° на каждом шаге не воспроизводит медленное source sway.
Proposal повторяет прежние limb/head/tail/spine segments7 раз Walk и9 Run и
вводит один body roll±20/24° на macroloop. Получено2.45/2.475s без замедления лап.
World renderer дополнительно применяет actual travelScale: preview periods
не являются фиксированным реальным периодом при любой скорости актёра.

Собственное направление остаётся прежним: светлая лицевая маска, тёмная морда/
лапы, серый низкий корпус, короткий хвост и отдельные когти. Reference textures,
mesh, keys и rig генератор не читает. Меняется собственный keyed motion recipe;
geometry/UV/skin/PNG вообще не переписываются в кандидате.

## Просмотренные листы и анимация

- [Три версии Walk](threeway_walk.png), [Run](threeway_run.png),
  [Idle](threeway_idle.png), [Action](threeway_action.png): original / own before /
  proposal, одна камера и масштаб, green gameplay collider. Это normalized
  macro phases; они не объявлены одинаковым elapsed time.
- [Walk на общих часах](temporal/temporal_walk.gif),
  [Run на общих часах](temporal/temporal_run.gif).
  По129 native phase samples/state/version, общее elapsed время,25fps/5s excerpt.
  На каждое время выбирается ближайший реальный native frame, не morph картинки.
  Ошибка фазового времени менее0.010s; GIF palette quantization и разрыв конца
  excerpt не являются ошибкой atlas или asset loop seam.
- [Walk strip](temporal/temporal_walk_strip.png),
  [Run strip](temporal/temporal_run_strip.png) просмотрены на одних elapsed часах.
  Медленный перенос массы стал ближе к reference; быстрые артикулированные
  шаги собственной версии сохранены. Крайний наклон поднимает противоположную
  сторону корпуса; эти позы требуют world stance/sliding оценки.
- Просмотрен фактический world `runtime_envelope_current/frame_002.png`:
  badger виден на береговой поверхности. Одна flat-ground route не доказывает
  terrain/slopes/ступени, стабильную постановку каждой лапы или AI behaviour.

Original/own-before captures повторно использованы только после проверки
одинаковых EXE, shaders, profile/framing и SHA payload. Контексты/timestamps
сохранены, не подделаны: [reuse proof](reference_reuse.json).
Profile v1 заморожен для точного сравнения; отдельно сохранён автоматически
построенный `generated_profile_v2.json`. Это не обещание покрытия каждого
аналитического экстремума. [Native audit](native_audit.json):150 original/current
images, bounds error<0.001block, geometry covered; [temporal audit](temporal/native_audit.json):
516 images, max bounds error0.0000362block. Own-before кадры проверены composition
context/инвариантами, но не входят в эти два счётчика paired original/current.

## Сохранность и контакт

[Motion invariant audit](motion_invariant_audit.json):

- 13 joints,360 triangles; positions/normals/indices/UV/IBM/weights, node pivots/
  hierarchy, anatomy и creature definition точны. Все PNG побайтно прежние.
- Idle/Action6 samplers и33 native envelope PNG exact.
- Equal-elapsed non-root rotation error Walk0.0000614°, Run0.0001041°,
  остальные translations/root XZ точны. Изменены body roll и root Y;
  world-space траектории лап из-за parent roll/lift не идентичны прежним.
- C0 seams проверены;2156 retained transition frames,2112 supported,
  minY−1.12e−8block, submitted geometry внутри visual envelope.
- 2049 full-mesh offline samples/clip: floor ranges ниже. Это sampled bound,
  не analytic гарантия всего непрерывного времени.

| Версия | Walk minY / max gap | Run minY / max gap |
|---|---|---|
| Live own | −0.0000150 /0.0003850 | −0.0000288 /0.0007939 |
| V1 | −0.0000605 /0.0008434 | −0.0001113 /0.0013134 |
| V2 | −0.0000151 /0.0004525 | −0.0000357 /0.0007854 |

V1 rejected за Run gap>0.001; [реальный negative control](contact_negative_control.json).
V2 contact steps/stride256 Walk,224 Run:1793/2017 root translation samples.
Плотность менялась только у root correction, limb segments не resample.
Все V2 minY/gap в пределах±0.001; не заявляется pointwise совпадение с live.
Ныряние reference mesh примерно до−0.248block не воспроизводится.

Фактический world log:509 records,299 grounded Walk и210 Idle/pause, обе стороны
движения, frozen0/body blocked0. Y63.5 постоянен, X−14…−12.931. Драйвер задаёт
только intent; обычный motor/state derivation остаётся активным. Это отдельно
от preview/transition evidence и не world stride acceptance.

## Измеренная стоимость: gate остаётся открытым

BIN53920→135136 bytes. Учтённый vector/string heap64508→160988 bytes
(+96480); без map nodes/allocator/control blocks/GPU/total RSS.
Warm loader p50 1.5404→2.2631ms. [Memory subset](memory_subset.json),
[paired CPU](paired_cpu.json), [hash/command context](bench_context.json).
Измерение после остановки всех desktop flights, core2, тот же парный Release
стенд, что [для Orc](../../gltf-sampling-perf-v1/README.md). 81 mirrored/randomized
batches/condition; palette/root float signatures exact между lookup алгоритмами.

Ниже p50 **batch-average µs/pose**, не world frame p95:

| Clip/fade | Live linear | Candidate linear | Candidate indexed |
|---|---:|---:|---:|
| Walk/no fade |1.919|2.469|2.157|
| Walk/fade0.5 |2.569|3.098|2.765|
| Run/no fade |1.920|2.576|2.222|
| Run/fade0.5 |2.574|3.215|2.823|

Новый lookup снижает цену кандидата, но против live остаётся примерно8–16%
CPU sampler прироста. **Это не успешный performance gate.** World CPU/GPU,
memory/load family budgets и noise controls ещё нужны; предыдущий Orc world
gate также не закрыт. Улучшенный внешний ритм не оправдывает автоматическую
публикацию/массовый multi-rate bake.

Khronos validator:35 assets в QA-копии,0 errors/warnings/infos, включая badger v2.
Это формат, не художественная приёмка. [Live ledger](ledger_live_unchanged.json)
проверяет, что все61 действующих R5 остаются свежими; кандидат не подменяет
ни один из них. Существующий bounds FAIL17 и все R6 gates не отменены.

## Следующая работа

1. Локализовать sampler/UpdatePose/fade/cache цену и уменьшить lookup/storage
   overhead, сохранив exact-key интерполяцию и native позы; family memory/load budget.
2. Actual-world CPU/GPU/frame gate с controls, одинаковым светом/погодой; затем
   stance/stride/sliding/turn/terrain/AI/action, все переходы и gameplay contact.
3. После этих gates — normal pipeline regeneration, provenance/review/публикация
   и полный production regression; сейчас это только ready-to-review proposal.
4. Отдельно treeman multi-rate и puffin Fly posture/folding/cadence/ground gait;
   все остальные56+5/три backend, FP/control/UI-pack/compatibility/Android/package.

Сборка scratch: `python tools/research/build_creature_badger_sway_pilot.py --qa-name FRESH_NAME`.
Audit: `python tools/research/audit_creature_badger_sway.py --qa-name FRESH_NAME --out RESEARCH_OUTPUT`
после native captures с приведёнными labels/profile. Текущий builder создаёт V2;
V1 source отдельно сохранён. **Все61 R6 pending, цель active.**


## Последующая runtime оптимизация (2026-11-12)

Данные paired CPU в таблице выше относятся к прежнему helper indexed128.
[Новый проверяемый hint](../../gltf-hint-perf-v1/README.md) сохраняет позы:
35 packaged+этот pilot float-bit exact,165 его PNG и2156 transitions exact к81283a….
Новый Release1507b3…; старые threeway/GIF оставлены с исходными контекстами,
не переписаны задним числом. Свежий parity report явно связывает runtime.

Residual sampler cost против original linear/live около+3% без fade/≈−2% с fade;
обе версии с hint: proposal+2.3…6.4%. Fixed noon/clear weather world8×40s с20s
warmup показал CPU pass p95 ratio1.0803 в худшем observed повторе и1.0589 для
all-steady: CPU часть provisional≤10% выполнена в одном QA fixture.
GPU/frame из-за control noise всё ещё не приняты, memory+96480B не исчезла.
Пилот остаётся неопубликованным; world stance/sliding/terrain/AI и общий R6 открыты.
