# Native B motion diagnostics: original/current

`creature_bone_motion_audit` — отдельная opt-in Release utility, не тест,
который может присвоить художественное принятие. Использует настоящие
`UCreatureDefinitionStorage`, geometry loader, cube mesh builder,
`BoneSkeletonHierarchy`, `BoneSkeletonPoseEngine`. Оценивает все mesh
vertices через те же bone × restLocal matrices. Origin стоит на Y=0,
entity convention отражает Z и не меняет измеряемую высоту. Это CPU pose
evaluation, **не** world physics/GL render/terrain raycast.

Сборка:

```powershell
cmake --build build/desktop-msvc --config Release --target creature_bone_motion_audit --parallel 8
python tools/research/measure_creature_bone_motion.py --models models --out docs/research/creature-r6-v2/bone-motion/current.json
python tools/research/measure_creature_bone_motion.py --models assetgen_output/creatures/review_2026_11_10/baseline/models --out docs/research/creature-r6-v2/bone-motion/baseline.json
python tools/research/summarize_creature_bone_motion.py
```

Один native EXE измерил обе версии всех 17 B: по 66 сценариев и 1025 фаз
0…4π, итого 1 150 050 pose evaluations на версию. Idle — speed ratio 0;
Walk/Run/Crouch/Jump/Fall/Swim/Tread/Fly/Hover/Glide/Slither/Coil/Action —
ratio 0/.22/.5/1/1.5. Ratio означает horizontalSpeed/walkSpeed, **не**
glTF travelScale. Все состояния диагностируются явно, даже если конкретный
actor их не выбирает; это не утверждение об их runtime достижимости.
0…4π не гарантирует полного периода всех head subharmonics.

`*_context.json` связывают executable, source hashes при запуске и полный
model-file inventory; проверено отсутствие их изменения за измерение.
Это не OS-level запрет native чтения других файлов и не clean-OS build.
`current.json`/`baseline.json`: общий envelope и per-bone floor/envelope,
индексы фаз экстремумов. `comparison.json`: сопоставление ground-candidate
Idle/Walk/Run/Crouch у 13 terrestrial B; air/water не притягиваются к полу.

![Walk mesh-floor ranges](walk_floor_ranges.png)

## Найдено до коррекции

- У wolf 8 сценариев Walk/Run дают отрицательную высоту, хуже original
  более чем на .005 блока. Минимум current −.019512, original минимум
  −.003891; при Walk ratio1 разница −.015623. Это обнаруженная после R5
  регрессия; предотвращение проникновения исправлено ниже. Sole/stance и
  world stride остаются отдельными обязательными проверками.
- У ряда остальных ground B остались отрицательные части цикла или
  положительный mesh-floor, хотя у многих результат лучше исходного.
  Human/bot/skeleton/zombie повторяют исходную кинематику. Например zombie
  Run ratio1.5 даёт floor до +.601578; это ещё диагностическая высокая
  амплитуда, не world-observed ошибка и не художественное принятие.
- Spider Walk ratio1: исходник +.2556…+.3375, current 0…0. Fox и tortoise
  больше не имеют прежнего глубокого проваливания. Это частные floor
  результаты, не полный stride/transition PASS.
- Rabbit current Walk ratio1 имеет намеренную положительную фазу hop;
  такую фазу нельзя автоматически устранить общим root clamp к полу.

Начальные raw-диагностики и график выше — **не** contact/stride приёмка. Mesh-floor не показывает, какая
стопа должна быть в stance, не измеряет world-space скольжение, переходы
и работу на разных поверхностях. Utility не исправляет runtime/geometry.

## Практики и следующий обязательный шаг

[Khronos foot placement tutorial](https://github.com/KhronosGroup/Vulkan-Tutorial/blob/main/en/Advanced_glTF/Procedural_Animation_IK/04_foot_placement.adoc)
рассматривает terrain targets, их сглаживание, pelvis adjustment и решение
цепей; plant weight отключает коррекцию в swing и сохраняет базовую
анимацию. Для наших одночастных B ног полный humanoid IK нельзя объявить
доступным автоматически: цепи и anchors надо оценить по каждому профилю.

[Epic pose/stride warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/pose-warping-in-unreal-engine)
связывает stride с фактической скоростью, задаёт foot/pelvis definitions,
clamps/interpolation и ограничения сохранения FK формы. Применение к
Cubatarium — наша инженерная интерпретация, не готовая совместимость API.

Продолжение: выделить soles и stance/swing/hop по действующим профилям;
проверить speed/phase coupling и world foot drift после предотвращения
проникновения wolf и обычных non-hopping ground циклов ниже.
Сравнить точные before/after motion sheets и реальные actors после physics,
проверить Idle↔Walk↔Run/Jump/Fall/Crouch, equipment attachments, culling и
стоимость коррекции. Rabbit, flight, swim и hover требуют своих правил.
Остальные R6 гейты из родительского README остаются обязательными.

## Исправленное предотвращение проникновения B mesh в feet plane

В настоящий B visual добавлен `BoneSkeletonGroundClearance.h`. После
вычисления обычных костных матриц он читает Y реальных submitted vertices,
включая rotated/thin-axis cubes. Общий root поднимается только на
`max(0, -meshMinY)`. Положительный отрыв остаётся прежним: он может быть
частью походки. Ни одна кость/её rotation/pivot/bind/длина не переписывается;
XZ и физический collider прежние. Equipment получает тот же общий root.

Политика действует при реальном onGround в Idle/Walk/Run/Crouch у наземных
непрыжковых профилей. Rabbit, air/water/serpentine профили исключены.
Jump/Fall/Fly/Glide/Hover/Swim/Tread не прижимаются к полу; на отрыве малая
поправка высоты экспоненциально уходит к нулю вместо мгновенного скачка.
Это **clearance**, не foot locking/IK и не полная компенсация скольжения.

`current_raw_control.json` и `current_clearance.json` одним новым native
Release EXE повторяют 17 × 66 × 1025 поз. Raw control точно воспроизводит
прошлую кинематику. `clearance_audit.json` проверяет: отрицательная высота
устранена в 87 ground сценариях 11 B видов, XZ envelopes каждого bone
прежние, положительный floor сохранён, все exempt сценарии совпали точно.
Это не выдаёт floor за stance: остальные ground/route гейты pending.

Новым desktop Release EXE заново сняты original/current **всех 61**:
`native_baseline`, `native_current`, `context_*_catalog.json`. Fresh source,
EXE/shader bindings и shader-coverage silhouettes проверены; clipping 0
у обеих версий. Шесть пустых top/bottom плоского fire_spirit прежние.
`render_change_audit.json`: runtime payload и shaders прежние, все 5290
current PNG проверены; 45 ресурсов целиком совпали побайтно, изменения
ограничены 11 ground B и пятью их skins. 45 парных листов также совпали
побайтно с предыдущей прямо просмотренной matrix; остальные 16 листов
прямо просмотрены повторно. `visual_review.json`, `index.md` — точные SHA
и индивидуальные заметки, не полный R6 verdict.

Дополнительно прямо просмотрены 11 полных native motion sheets (rest4,
primary8, Run8 при walk_speed, Action8): `motion_current`, `motion_review.json`.
Суставы/крепления/текстуры целы. Эти листы не изображают Run ratio1.5.
Action здесь — принудительный QA probe; B profile может сохранять прежнее
idle/default поведение. `DeriveTerrestrial` сейчас возвращает наземные
Idle/Walk/Run/Crouch, а Action hint отдельно не возвращает; наличие QA
Action кадров не доказывает runtime attack capability.

Свежий world actor — только wolf, `runtime_current_catalog/frame_002.png`
прямо просмотрен. Его настоящий путь/скольжение/переходы этим кадром не
проверены. Также просмотрены `human_31.png`/`human_32.png`: оба собственных
скина с copper armor, main sword и offhand torch в одной фазе; это частичная
equipment-проверка, не полный обеих рук/FP/save/load гейт.

Требуемая Release сборка Cubatarium прошла, статический EXE — 6 system DLL.
10 выбранных Release C++ tests, включая новый clearance/политику/плавный
takeoff unit test, проходят. Performance art-запусков отключён, PASS не заявлен.

## Явные B-профили в генераторе

Все 17 собственных B recipes теперь явно содержат `animation_profile`:
13 привязок добавлены, четыре уже были. Значения взяты из действующих
принятых runtime definitions без изменения походки (`recipe_profiles.json`).
`runtime_definition` запрещает B recipe без явного профиля; новая проверка
ловит возврат к неявному archetype fallback. Всего проходят 13 quality и
36 fidelity tests. Это перенос выбора профиля в authoring source, не
автоматическая новая анимация по имени вида.

`profiles_repro_summary.json`: две полные независимые output-сборки 61/61
byte-identical, неожиданных Python build reads нет. `profiles_payload_audit.json`
доказывает точное совпадение inventories и bytes всего source runtime
payload, включая creature.json: mesh/PNG/UV/rig/animations прежние. Поэтому
новая source-context metadata review не требует новой художественной
переделки или повторения тех же native изображений; их active input hashes
точно соответствуют результату генератора. `profiles_review.json` связывает
новые bundle hashes с прежними R5 решениями и свежими видимыми evidence.

При сопоставлении bin найдены две **неактивные bin-only** owl копии:
`model.gltf.orig`/`model.bin.orig`. Они отсутствуют в source catalog и в
candidate; actual owl definition/URI ссылаются на model.gltf/model.bin,
а не на backups. Исключение явно записано с SHA в payload audit; совпадение
полного bin inventory с кандидатом не заявляется. Clean-package gate ещё
должен исключить эти файлы вместе с reference/QA ресурсами.

Публикация source-context metadata всех 56+5 завершена. `ledger.json`:
exact R5 reviews/current source contexts/file records/published+bin matches
61/61, manifest56+5, runtime global mirror идентичен. Это точное совпадение
проверяемого payload, не объявление отсутствия двух дополнительных backups.
