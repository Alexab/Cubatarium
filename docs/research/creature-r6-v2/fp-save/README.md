# R6: FP arm source и подготовка save-проверок

Текущий Release EXE `6a1c454f5d4d3f0a22d34d75c4d1ec8eb8aa98d922df9150580d8dce8a6811cf`.
Context: context_fp_final.json; captures native_final/fp и body matrix native_final.
Baseline: context_fp_before.json / native_before/fp. Intermediate authored run:
context_fp_authored.json / native_authored/fp, только диагностика первого исправления.

## Проблема и изменение

FP использовал один фиксированный right-arm atlas4×12×4 для обеих рук и
две разные коробки sleeve/hand, повторяя один и тот же arm UV. В текущих
собственных моделях leftArm имеет отдельные UV; у skeleton рука2×12×2 с
другим origin. Старый механизм не соответствовал опубликованной anatomy/UV.

UFpViewmodelRenderer теперь получает текущий CreatureDefinition и resolved
appearance из world caller. Для usable B rightArm/leftArm он берёт настоящие
cube meshes из CreatureBoneSkeletonCache и общий GPU mesh cache. Position/UV/
indices и cube rest transforms source сохраняются, FP fit равномерный по
геометрическому shoulder-to-palm extent0.63. Левый authored mesh не получает
дополнительную generic right-arm reflection; item root сохраняется. Lookup
кэшируется по generation/species/file/geometry/source directory. Legacy boxes
остаются fallback для definitions без подходящих arm meshes; FP других rigs
этим не объявляется принятым. Ни опубликованные skeletons, PNG, clips, ни
inventory models, wield/grip files или presets не менялись.

Поддерживается optional offscreen target для того же DrawWorldOverlay:
production по умолчанию framebuffer0, те же FOV72/depth/presets. QA не рисует
заменитель renderer. Audit pointer отсутствует в обычном runtime, GPU readback
выполняется только explicit native capture. Items трансформируются прежним кодом.

## Evidence и независимый аудит

936 actual FP PNG768×432:6 ресурсов human/bot/zombie/skeleton и2 human skins,
плюс3 варианта raw-empty user/legacy selection. Для каждого4 loadouts
(bare/main sword/off torch/dual) ×26 samples (idle1/walk8/attack17).
World.GetResolvedAppearanceShared вызывается на synthetic player-marked
creature, с настоящим current user; selected fields временно меняются и
восстанавливаются, controlled inventory остаётся прежним. Это тест fallback
функции, не переключение реального controlled character через UI.

fp_audit.json независимо проверяет:
- current EXE, recorded model/skin/item/preset и FP shader SHA;
- exact936 frame matrix и fresh PNG/alpha coverage;
- source bone/cube UV по legacy geometry и Float32 VBO layout:1872 фактических
  GPU vertex-buffer reads совпали с source-derived UV/positions;
- равномерную source model transform, max error1.25e-7;
- sampled FP roots до/после, извлечённые из прежних sleeve matrices:
  max error9.28e-8; Root/swing/bob curve не менялась;
- все visible bare-hand RGBA принадлежат ожидаемому skin atlas;
- raw skin, user SelectedSkinId и legacy SelectedAppearanceType дают точные
  одинаковые пиксели для одного outfit; inventory и selected fields сохранены.

Один настоящий renderer дополнительно проходит6 retained-cache cases:
fixture, same-path UV-file edit без invalidation (ожидаемо cached), explicit
Clear (новый right-arm UV), restored fixture, switch human→skeleton и combined
species/source-directory return human. CPU/GPU hashes совпали, left source
не меняется при right-only edit. Directory-only и все возможные reload paths
здесь отдельно не изолированы. Diagnostic JSON правит только rightArm UV.u+1,
потом точный оригинал возвращается; production geometry не переписана.

Независимый аудит: tools/research/audit_creature_fp_capture.py. Он сохраняет
SHA всех936 baseline и936 current PNG. Actual own atlas/data, а не модель,
синтезированная для теста. 11 выбранных Release C++ tests проходят; ledger61/61
актуален, body_parity.json подтверждает5290/5290 прежних RGBA всех61 ресурсов.
Прежние body art observations наследуются по exact proof. Прямо изучен также
world wolf frame_002, сохранённый через4.18s после FP manifest/cache diagnostics;
штатный body material и mesh остались видимыми. Это third-person QA frame,
а не проверка настоящей first-person world camera.

## Прямой визуальный просмотр

Созданы и прямо изучены все18 sheets: для каждого из6 ресурсов idle4 paired
loadouts, dual walk8 и dual attack17 phases. Сравнения сохраняют одинаковые
crop/scale внутри pair; native PNG не перерисованы. Список/SHA:
sheets/manifest.json и visual_review.json. Не заявляется отдельный просмотр
каждого из936 PNG или полная FP art приёмка.

Sampled authored arm shape/UV subset принят: skeleton тонкий, left UV свой,
материалы и cuff colors source-correct, новых snaps на просмотренных cycles
не видно. Унаследованное низкое idle framing и уход правой руки/части blade
за кадр во время атаки видны в обеих версиях. Общая presentation/framing,
world first-person camera, terrain/wall interaction, остальные use presets,
inertia/transitions и полная applicable-species матрица ещё требуют проверки.
Это ограничение не скрывается за зелёными structural tests.

## Best practices и применение в проекте

[Официальный Epic First Person Rendering](https://dev.epicgames.com/documentation/en-us/unreal-engine/first-person-rendering)
описывает отдельные first-person FOV/scale и depth range для view geometry.
Существующий отдельный FP presentation pass Cubatarium сохранён. Переиспользовать
собственную arm geometry/UV вместо canonical UV догадки — решение проекта;
оно не требует менять world skeleton/animations.
[Godot Viewports](https://docs.godotengine.org/en/latest/tutorials/rendering/viewports.html)
описывает rendering в отдельный render target; здесь offscreen QA использует
тот же FP renderer и читаемый GPU output, а не отдельную копию mesh renderer.

Для generator recipes: не полагаться в FP на фиксированный40/16 или generic
4×12×4. Явные сохраняемые rightArm/leftArm geometry/UV являются источником
presentation. Текущие atlas layouts оставлены для совместимости; новая UV
allocation обязана проходить native source/skin/FP проверки. Отдельная FP
траектория/presentation не переписывает мирные bind poses/animation channels.

## Save flow: entity subset выполнен следующим этапом

Entity subset пунктов1–3 ниже теперь проверен: [save-roundtrip/README.md](../save-roundtrip/README.md). Два Release процесса проходят128 cases/256 restores каждый; native JSON точны между процессами. Пункт4 world/user/cooperative flow остаётся pending.

Исследованы реальные UWorldEnvironment::LoadCreatures/SaveCreatures,
UCreatureFactory::Build, inventory serialization и SaveUsers/LoadUsers.
Подготовленный маршрут проверки:
1. Local UWorldEnvironment с owner world только для запросов, отдельными
   actors и файлами под новым QA directory. Не менять список существ мира
   или inventory игрока. Fixtures всех56+5 и legacy scout_golden/drifter_ice/
   brute_rust aliases; позиции в unloaded области исключают terrain relocation.
2. Проверить поддерживаемую pre-versioning schema по source, затем legacy
   fixture → настоящий LoadCreatures → SaveCreatures v2 → новый environment
   LoadCreatures → второй Save. Сравнить canonical entities по ID, type/skin,
   bounds/footprint/mode/orientation, stats и storage/hotbar/wear/offhand.
   Отдельно отметить ожидаемые alias/default/mode normalizations.
3. Перед/после сравнить actual visual backend, sampled matrices/UV/material
   keys; для humanoid gear использовать нормальные inventory APIs. Не выдавать
   synthetic legacy fixture за проверку всех исторических версий/world recovery.
4. Отдельный disposable world/user fixture для настоящего cooperative save,
   пользовательских selected/legacy appearances, possession и первого лица.
   Текущий offscreen matrix не проверяет эти операции.

Полная R6 и цель остаются активными. Кроме world/FP/save обязательны прочие
contacts/transitions/routes/habitats/culling, clean Release package и matched
CPU/GPU p95/memory. Performance gate art flights отключён, HUD FPS не приёмка.

Команды:
python tools/research/audit_creature_fp_capture.py --out docs/research/creature-r6-v2/fp-save
python tools/research/compose_creature_fp_comparison.py --out docs/research/creature-r6-v2/fp-save

Flight: run_creature_pilot_qa.py --production --species wolf --preview-species
<sorted56> --fp-matrix --matrix --close --out docs/research/creature-r6-v2/fp-save
--label fp_final --native-dir native_final. Для нового запуска нужны fresh
label/native-dir, существующая evidence не перезаписывается.
Логи: assetgen_output/creatures/r6_fp_{final_build,final_flight,tests_build,tests,audit,ledger,diff_check}.log.


Следующий выполненный этап: [настоящий cooperative world/user save→restart](../user-world/README.md). Четыре процесса проверяют single-user selected/legacy skins,64NPC и20 control roundtrips; actual idle world FP просмотрен. UI/pack/control movement, full FP presentation и остальные R6 gates остаются pending.
