# Penguin: восстановление полноты корпуса, кандидат v2

Умеренный v2 опубликован через normal build/validate/review/publish после
дополнительного прямого R5 review. Все61 R5 bundles актуальны; все R6 pending.
Цель 56+5 / три backend сохранена. Разделы ниже сохраняют также исходную
диагностику кандидата до fluid проверки.

## Текущее опубликованное состояние

`revision_review.json` —явное решение ассистента по точному staging bundle,
не подтверждение пользователя и не финальная R6. `publication_audit.json`:
5290 production кадров,5201 RGBA exact к предыдущим данным,89 ожидаемых
изменений только penguin; все89 candidate→production PNG exact. 936 FP PNG
неизменны,1872 GPU arm draws и6 retained cases проверены. Entity probe:
128 cases/256 actual restores. Ledger61 свежий. Текущий EXE:
`a0872e87a1551dbe7ac0cff1346b58176743d18eba13817cb7779473415df68f`.

Новая [fluid/world проверка](fluid_v4/README.md) закрыла dry-only пробел и нашла
реальную остановку NPC в отсутствующем, но известном sky slice над водой.
Исправление использует общий консервативный raycast known-air contract.
15 targeted Release tests PASS;196 CTest зарегистрированы. Свежий bounds audit
по-прежнему Failed17; общий CTest, perf и R6 не приняты.

Собственное состояние до правки сохранено в
`assetgen_output/creatures/penguin_belly_pre_publish/`:16 файлов с SHA.
`reproduction_audit.json` подтверждает точное повторение v2 из этого snapshot.
Для воспроизведения builder после публикации нужен
`--source-root assetgen_output/creatures/penguin_belly_pre_publish` и свежий qa-name.

Диагностика всех 17 расхождений находится в
[anatomy_shapes_v1](../anatomy_shapes_v1/README.md). У пингвина близкий общий
AABB скрывал меньшую боковую площадь и недостаток глубины корпуса.

## Изменение собственного рецепта

Изменяется только trunk: origin Z −1 → −2, depth 5 → 6 texels. Задняя грань
остаётся на Z=4; кости/пивоты и соединение крыльев сохраняются. UV allocator
получает новые размеры, собственная penguin texture запекается заново.
Исходные модели исследуются отдельно и не входят в generator inputs.

Первый вариант `../penguin_belly_v1/` переносил origin Z в −4 и был отклонён
после native review: чрезмерный вынос плоского брюшка. Его зелёные format/rig
проверки не стали основанием для публикации. История и SHA сохранены.

Вариант v2 непосредственно просмотрен в `threeway_{idle,walk,fly}.png`:
original / own before / candidate, front / side / oblique, общая камера.
Полнота улучшена умеренно; плоская панель v1 устранена. После последующего
full-turn и fluid/world review принят R5 revision; R6 остаётся открытой.

## Доказательства

- `rig_uv_native_audit.json`: nodes/skins/inverse bind matrices/definition,
  JOINTS/WEIGHTS и все 17 animation sampler arrays точны к текущему own.
  132 треугольника, ненулевые площади, finite/normalized weights и UV в atlas.
  76 constant-UV detail triangles существовали и до правки; их наличие не
  выдаётся за измерение texel density. Прочие текстурные свойства требуют review.
- `khronos/summary.json`: 35 glTF в QA каталоге, 0 errors / warnings / infos.
- `native_audit.json`: 138 main PNG original/candidate, независимые offline/native
  bounds с максимальной ошибкой 0,000007839 блока. Все найденные edge cases
  сохраняются новым runtime envelope. Это sampled preview, не мир/terrain.
- `native_current/transition_probe.json`: 36 synthetic idle/walk cold-entry
  cases / 396 кадров. 352 supported frames, minY=0, envelope покрывает все.
  Проверка не включает amphibious fluid transitions и не доказывает GPU fade.
- `runtime_envelope_current/frame_002.png`: непосредственно просмотрен
  текущий кандидат в реальном мире на суше. `water/` — отдельный успешный
  process/spawn subset с 29 UI PNG и тремя world PNG; конечный кадр просмотрен.
  Он показывает **вертикальную позу**, а не подтверждённое плавание.
- `visual_review.json`: историческое pre-fluid решение `promising_unapproved`;
  оно сохранено. Текущее принятое R5 решение — `revision_review.json`.

Важное ограничение: `--primary-state Swim` меняет обычные preview frames4..11,
а не locomotion настоящего world actor. Help runner уточнён. И переходный probe
выбирает idle/walk/run/action и inFluid по strictly Aquatic habitat: для
amphibious penguin его water-запуск повторил те же сухие synthetic условия.
Поэтому два файла по 396 кадров не считаются 792 различными fluid cases.
Это конкретная незакрытая проверка, а не доказанная ошибка creature runtime.

Original и own-before native evidence переиспользованы из v1 после проверки
**одинаковых EXE, profile, всех model/texture inputs и shaders**. Их старые
context timestamps сохранены, новая съёмка не приписана. Происхождение —
`reference_evidence_reuse.json`. Candidate v2 снят отдельно текущим Release EXE
`26f7f77d36a7ab28661db03ae468c309ae1f08ca9303ad59d4189bf41758b2aa`.

## Воспроизведение

```powershell
python tools/research/build_creature_anatomy_pilots.py --qa-name penguin_belly_FRESH --variant v2
python tools/research/analyze_creature_envelopes.py --species penguin --samples 129 --current-models assetgen_output/creatures/penguin_belly_FRESH/models/creatures --out docs/research/creature-r6-v2/envelopes/penguin_belly_FRESH
```

Затем обычный `run_creature_pilot_qa.py` с `--qa-name`, species penguin,
`--envelope-profile`, `--envelope-variant current`, label envelope_current,
native-dir native_current и `--transition-probe`. Original/before — отдельные
прогоны либо явное verified reuse через `audit_creature_anatomy_pilot.py`.
Свежие директории обязательны. Desktop запускается через flight_sim_run.py,
Release executable в bin; performance gates этого прогона disabled.

## Выполненные и оставшиеся шаги

1. Выполнены explicit amphibious land/free-fluid/bottom retained transitions,
   world Swim и input pause/move в обе стороны. Реальный world Tread и wet/dry
   bank routes/контакты ещё не проверены.
2. Просмотрены Idle/Walk full-turn/top/bottom и три original/own/candidate
   горизонтальных ракурса. Полный Swim turntable/GPU crossfade и stride pending.
3. Выполнены own snapshot, exact normal staging review/publish и полная61
   sampled regression matrix, FP/entity audits и ledger.
4. Остальные виды triage, поверхность воды/Jump/Fall, performance, camera sweeps,
   terrain/control/UI-pack/FP-framing/package и прочие §13 gates обязательны.
