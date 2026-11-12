# R6 — фактический entity save/load roundtrip

Завершён ограниченный этап совместимости существ: два отдельных процесса
Release на EXE `4f9aea971602ec7c02d7142c2cf32828f37caf259d48dccc24502eeeb79c309d`.
В каждом128 scenarios /256 фактических LoadCreatures entity restores.
Это56 базовых видов,5 собственных скинов и3 legacy aliases, отдельно для
поддерживаемой pre-versioning schema и текущей format_version2.
Полная R6 и цель остаются pending/active.

## Метод

Native `CreatureQaSaveProbe.h`, включаемый только явным env QA флагом,
создаёт два отдельных UWorldEnvironment. Они используют загруженные catalogs
и owner world только для штатных collision queries; не тикают AI и не
подменяют список существ мира. Fixtures/files находятся внутри этого evidence
directory. Рабочие world/user save файлы не передаются этим SaveCreatures.
Контрольные сравнения состояния всех текущих world actors до/после точны.

Для каждой schema выполняются реальные операции:
input→LoadCreatures→SaveCreatures(v2)→LoadCreatures в новый environment→Save.
Последний Save повторяется в существующий путь, проверяя replacement path.
Все64 actors остаются в каждой стадии. Позиции далеко в unloaded области
сохранены точно; проверка намеренно не включает terrain relocation/recovery.

Сверяются id/type/skin/source, body_origin, yaw/pitch, movement_mode,
footprint_swapped, фактические collision dimensions и eye offset, vitals и
attributes. Inventory содержит storage,2 hotbars, active bar/slot, sword wear.
Для human/bot/zombie/skeleton и двух human skins используются нормальные
inventory EquipArmor/EquipOffhand:6 copper slots с разным wear + torch offhand.
Другие виды сохраняют обычные storage/hotbar. Проверены item entries;
полная migration matrix всех inventory kinds/invalid/broken states сюда не входит.

Реальные UCreatureFactory visuals каждого seed/loaded/reloaded actor получают
одинаковый sampled Walk facts/phase/dt. Queue sink сверяет полный поток
MVP/geometry/UV/indices, glTF bone palettes, actual texture IDs и draw count.
Resolved appearance и коллизия совпадают. Это CPU submissions реального
renderer, а не GPU raster capture загруженных actors; новых art approvals нет.

Legacy schema здесь означает реально поддерживаемый JSON без format_version,
с body_origin и текущими inventory/stats полями. Это синтетические fixtures,
не коллекция всех исторических файлов клиента. Реальные remaps:
scout/scout_golden→sheep/sheep_wool_golden,
drifter/drifter_ice→wolf/wolf_snow,
brute/brute_rust→sand_monster/empty; отсутствующий spawn_source→legacy_save.
Те же aliases поддерживаются загрузчиком внутри v2. Mode fixtures уже
нормализованы Factory по текущему game mode; это не отдельная policy матрица.

## Результаты и воспроизводимость

- `context_save_first.json`, `native_first/save/`:128 cases,256 restores.
- `context_save_restart.json`, `native_restart/save/`: ещё128/256.
- `audit_save_first.json`, `audit_save_restart.json`: независимая проверка
  input aliases/defaults, сохранённых JSON, dimensions по definition, gear,
  vitals, submissions, freshness и current executable/assets/shaders SHA.
- `restart_parity.json`: все9 native save JSON файлов двух процессов
  побайтно идентичны, включая manifest с observations; временных .tmp нет.
- `body_parity.json`:5290/5290 RGBA всех61 ресурсов точны к предыдущему
  `fp-save/native_final` EXE; matrix metadata/poses/owner/backend прежние.
- `ledger.json`:61/61 reviewed source/staging/published/runtime assets актуальны.
- `visual_review.json`: прямо просмотрены world wolf frame_002 обоих запусков
  после завершения диагностики. Волк цел, gray coat/анатомия ожидаемые.
  Это continuation observation, не saved-actor render/full habitat acceptance.

Native source geometry/PNG/rig/animation assets и save format не менялись.
Новый код — изолированный QA observer/fixture и CLI orchestration/auditor.

## Release checks и обнаруженный незакрытый gate

`targeted_release_tests.log`:11/11 профильных C++ tests проходят.
Общий `full_ctest.log` **не зелёный**:192 зарегистрированы,50 passed,
141 Not Run (Release executables не собраны),1 actual Failed:
`creature_bounds_audit_test`. Не утверждается, что полный CTest пройден.

`bounds_diagnostic.json` независимо повторяет существующий audit35 glTF
видов.17 превышают текущий tolerance25%: badger, dirt_monster, dungeon_master,
golem, land_guard, lobster, oerkki, orc, owl, penguin, puffin, sand_monster,
seahorse, stone_monster, treeman, wasp, whale. Скрипт берёт общий bind/idle/
walk/fly extent, включая декоративные части/крылья; не все runtime states
или backends. Часть output отличается сильно (например treeman width≈.39×,
owl width≈3.31× collision), это требует видовой проверки, не замалчивания.

Автоматический `--apply` не запускался. Подгонять collider/eye под общий
размах изменит навигацию/сохранённые тела и противоречит сохранению gameplay
contract. До R6 нужно сопоставить body-only collision, conservative render/
culling envelope и sampled contacts всех B/R/G/sprite, затем проверить проходы,
повороты/стены/склоны/водные края. Обоснованные appendage differences должны
быть явными видовыми contracts с доказательствами, а не отключением audit
или произвольным увеличением tolerance. Реальные body/clearance проблемы
исправлять в generator/runtime с повторной art и save проверкой.

## Оставшийся план

1. Disposable cooperative world/user save→restart→LoadUsers: selected/legacy
   appearance, player_creature_id, controlled/possessed actor, user inventory.
   Actual first-person camera/world wall/depth states и pack/UI switching.
2. Видовой bounds/contact gate выше; stance/stride travelScale .22..1.5,
   transitions, terrain, shore/benthic, crab sideways, вода/воздух/лава/culling.
3. FP presentation: старое низкое idle framing и right-arm/blade clipping в
   attack сохраняются; их отдельная коррекция не должна ломать world rig/UV.
4. Clean Release package без QA/reference/.orig и matched CPU/GPU p95/memory.
   HUD FPS и эти flights с disabled metrics не заменяют performance acceptance.

Повторение (только fresh labels/native dirs):
`python tools/research/run_creature_pilot_qa.py --production --species wolf
--preview-species <sorted56> --save-probe --matrix --close --out
docs/research/creature-r6-v2/save-roundtrip --label <fresh> --native-dir <fresh>`.
Обёртка вызывает tools/flight_sim_run.py на bin/Cubatarium.exe.
Auditor: `python tools/research/audit_creature_save_probe.py --out
docs/research/creature-r6-v2/save-roundtrip --label save_first --native-dir native_first`.
Первый build log r6_save_build.log; актуальный r6_save_final_build.log.
Прочие логи `assetgen_output/creatures/r6_save_*`.


Следующий выполненный этап: [настоящий cooperative world/user save→restart](../user-world/README.md). Четыре процесса проверяют single-user selected/legacy skins,64NPC и20 control roundtrips; actual idle world FP просмотрен. UI/pack/control movement, full FP presentation и остальные R6 gates остаются pending.
