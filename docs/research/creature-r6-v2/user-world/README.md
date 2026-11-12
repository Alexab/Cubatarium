# R6 — настоящее world/user save→restart→load

Проверены четыре отдельных процесса Release EXE
`8543a363e0a48f06b4abfeb1cac41e793f1d529db092782b03a8bbf952ddf8c4`:
legacy_first→legacy_restart и selected_first→selected_restart.
В каждом обычный cooperative load загружает одного синтетического пользователя
и64 NPC всех56 видов/5 скинов/3 aliases; выполняется настоящий cooperative save.
Полная R6 остаётся pending, цель active.

## Что проверено

`CreatureQaWorldUserProbe.h` вызывается на frame30 flight после штатной загрузки.
Нужны явный CUBA_FLIGHT_QA_WORLD_USER, имя/папка CreatureQaUser_ и валидный
marker. Только после этого разрешены control/save действия. Созданы отдельные
CreatureQaUser_Legacy_v1 и CreatureQaUser_Selected_v1: terrain/metadata клонированы
из World_254, users.json полностью синтетический Username, creatures.json
из предыдущей проверенной64-entity fixture. Оригинальный мир не является
target Save; полные SHA inventories source world до/после каждого процесса точны.

Legacy user содержит selected_appearance_type=human_guard. Штатный LoadUsers
восстанавливает raw/effective skin и оба выбранных поля; штатный SaveUsers
мигрирует в selected_skin_id. После нового процесса legacy appearance уже
пустой, selected_skin_id и вид остаются human_guard. Современный
selected_skin_id=human_adventurer сохраняется в обоих процессах.

User→player_creature_id1→actual UPlayer→controlled actor совпадают. Player
не дублируется в creatures.json. Сохраняются storage,2 hotbars, primary и
active item, active bar/slot, sword wear,6copper armor slots с разным wear,
torch offhand, armor groups, vitals и attributes. Независимый auditor сравнивает
input→actual loaded state→saved JSON→input следующего процесса, не только
native passed boolean. Для всех64 NPC type/skin/source/origin/yaw/pitch/mode/
footprint/inventory/stats точны к input и saved output.

В каждом процессе normal SetControlledCreature передаёт possession cow(B),
badger(G), rigid_demo_walker(R), fire_spirit(sprite), skeleton(B) и возвращает
игроку. Проверяются actual pointers/IDs, possessed flags, unchanged actor
state и effective skin; чужой user skin не попадает на этих NPC. Итого20
control roundtrips. Это синхронный state/appearance transfer: не locomotion,
camera following в течение маршрута или сохранение possession между сессиями.

BeginCooperativeSave/TickCooperativeSave проходят реальные init→scan→chunks→
save_metadata→done,9 ticks; save observations≈3.39–5.26s. Эти числа диагностические,
не performance acceptance. Непосредственно до/после Save entity/appearance/
camera snapshots одинаковы; нужные файлы свежие, операция завершена. Четыре
независимых audit_*.json проходят, restart_parity.json связывает actual disk
outputs с input/outputs следующего процесса.

## Настоящий первый вид

Bench обычно скрывает FP. Для explicit world-user QA разрешён **штатный**
Application FP pass, который читает current controlled creature, его actual
inventory и resolved appearance. Candidate-only skin/gear substitute не
используется, QA model root отсутствует. Обычные приложения без QA флага
сохраняют прежнюю политику overlay. Camera реально FirstPerson/Perspective,
ShowFpWield=true. На прямо просмотренных четырёх frames после cooperative Save
видны собственные руки, actual wood_sword и wood_torch; legacy ночью,
selected при frozen daylight. Общий лист world_fp_contact_sheet.png тоже
прямо изучен. SHA и положительные интервалы после save manifest записаны
в visual_review.json. Это sampled idle world FP subset, не полная FP art приёмка.

## Проверка текущего EXE после добавления world observer

Отдельный combined flight в integrated/ на том же EXE:

- body_parity.json:5290/5290 RGBA всех61 ресурсов точны к save-roundtrip/native_first.
- fp_audit.json:936 PNG,1872 actual GPU source arm buffers,6 retained renderer
  scenarios, raw/user/legacy skin pixel equivalence, root error<1e-7.
- fp_parity.json: все936 RGBA точны к fp-save/native_final; сохраняются
  прежние18 directly reviewed sheets/ограничения, новых art approvals нет.
- audit_integrated_final.json: ещё128 entity scenarios/256 restores после
  body/FP/cache диагностики. Native geometry/UV/palette/material submissions точны.
- ledger.json:61/61 актуальны; targeted_release_tests.log:11/11 проходят.
- Прямо просмотрен world wolf спустя3.65s после combined save diagnostics:
  собственная серая модель цела. Это continuation observation.

Published/bin geometry/PNG/rigs/animation clips и save format не менялись.
Новые runtime изменения — guarded observer и возможность обычного world FP
под QA flag в bench. Model/skin/item/shader/exe/input SHA закреплены contexts.

## Открытые гейты и следующий порядок

Общий CTest по-прежнему **не принят**: save-roundtrip/full_ctest.log содержит
50 passed,141 Not Run/unbuilt executables и1 actual Failed bounds audit.
17 glTF extent/collision mismatches перечислены в
[bounds_diagnostic.json](../save-roundtrip/bounds_diagnostic.json).
Ни профильные11, ни новые save доказательства не отменяют этот failure.

1. Классифицировать эти17 по actual runtime body/support и conservative
   render envelope, сопоставить с original/current anatomical sheets.
   Проверить всеB/R/G/sprite, не раздувать collider по wing/tail extent и
   не уменьшать хорошую анатомию ради общего AABB. Доказанные species contracts
   и исправления реальных body/clearance defects должны пройти art/save регрессию.
2. Stance/stride travelScale .22..1.5, transitions, реальные маршруты/склоны/
   стены/проходы/shore/benthic/crab sideways, water/air/lava и culling.
3. World/UI selection и pack switching с resource caches; дополнительные
   player/users, другие applicable skins/equipment states, control locomotion.
4. FP idle framing/right-arm attack clipping, other presets/inertia и
   wall/depth/terrain presentation. Наличие рук в четырёх idle frames этого
   не закрывает; старые clipping наблюдения остаются действительны.
5. Clean Release build/package без QA/reference/.orig/disposable worlds и
   matched CPU/GPU p95/memory. Ночной/дневной HUD FPS здесь не budget gate.

## Повторение

`run_creature_user_world_qa.py --out docs/research/creature-r6-v2/user-world
--label <fresh> --world CreatureQaUser_<fresh> --mode legacy|selected` создаёт
новый disposable мир; --restart использует только marked fixture того же mode.
Targets не удаляются/не перезаписываются при создании; labels immutable.
Все приложения запускаются через tools/flight_sim_run.py на bin/Cubatarium.exe.

`audit_creature_user_world_qa.py --out docs/research/creature-r6-v2/user-world
--label legacy_first` независимо проверяет evidence/current inputs.
Для прочих labels: legacy_restart, selected_first, selected_restart.
Combined native capture context_integrated_final и stock baseline context
побайтно скопированы; baseline PNG читаются по ../.. /fp-save/native_before,
не перегенерированы. Логи assetgen_output/creatures/r6_user_world_*.
