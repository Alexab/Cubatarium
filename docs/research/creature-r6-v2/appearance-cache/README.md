# R6: appearance, mapping и live resource cache

Актуальная evidence: context_mapping_final_catalog.json / native_mapping_final;
последний раздел описывает принятый bounded mapping subset. Старые разделы
и неуспешные диагностические проходы ниже сохранены как история.

Первый mesh-only subset: Release EXE `a0dd958928551dde21f9724190468aa48d514840a4b2b04f2ed2c94ea1cf6910`,
`context_resource_catalog.json`, `native_resource/resource_cache_probe.json`.
Актуальное продолжение skin ownership — ниже, context_skin_catalog.json.
Независимый `resource_cache_audit.json`: 161/161, актуальные SHA EXE/shaders/
всех записанных ресурсов проверены; diagnostic fixtures проверены отдельно.

## Воспроизведение и исправление

`native_before/resource_cache_probe.json` воспроизводит удержание старых B/G
mesh pointers после сброса кэша. Rigid visual в этом исходном probe проходит.
B/G visuals теперь отслеживают поколение кэша, species, filename, source
folder и geometry ID (B), обновляют реальную модель и сохраняют время клипа.
Ключи используют разрешённый путь. Definition loader сохраняет абсолютный
sourceDirectory; preview/QA/world используют тот же locator. Pack merge
сбрасывает B/G/R кэши перед разбором definitions, а не после загрузки R parts.
Preview fit bounds инвалидируются по revisions definitions и кэшей.

## Проверенные сценарии

- 56 retained/fresh visuals B17/G35/R4 после same-root invalidation:
  положительные draw counts, точные submission hashes и свежие mesh pointers.
- 52 смены filename при прежнем species и B geometry ID.
- 52 смены только sourceDirectory при прежних species/filename.
- Один вызов настоящего ApplyCreaturePackOverlays для rigid_demo_walker:
  новая ширина .703125 вместо .5625 после изменения diagnostic model ×1.25.

B diagnostic geometry отличается от исходной только origin.x первого cube
на +16 raw pixels; G использует другую существующую собственную G-модель.
Fixtures находятся в evidence, production assets не изменены. Fresh reference
при смене G модели получает прежнее clip-local time, включая уже произошедший
wrap; максимальная ошибка pose matrices в текущем прогоне 0 (допуск 5e-6).
Исторический `native_paths` содержит ошибки fixture/oracle (выбор fox B как G
и неверный wall-time вместо local-time), исправленные до текущего прогона.

## Визуальная и структурная сохранность

`body_parity.json`: все 5290/5290 RGBA PNG совпали с equipment-fit/final
native_standard, включая 3660 matrix и 1630 legacy PNG всех61 ресурсов.
Это позволяет перенести прежние sampled art observations; нового полного
art review или приёмки остальных world states не заявляется. Прямо просмотрен
runtime_resource_catalog/frame_002.png: world wolf на берегу, без заметной
подмены модели/текстуры. Кадр не проверяет маршрут, склон или весь цикл шага.

`ledger.json`: exact reviews/source contexts/current inputs/published+bin
payload61/61. 11 выбранных Release C++ tests проходят после пересборки;
логи assetgen_output/creatures/r6_resource_cache_tests{_build,}.log.

## Границы

Первый проход проверял mesh invalidation/locator subset. Продолжение ниже
закрывает bounded ownership/effective selection пяти bundled skins; остальные
effective texture keys, texture root reload, missing-resource recovery, порядок overlays,
полный pack reload всех backend, FP/save, контакты/маршруты/среды/culling,
чистый package и matched performance. Счётчик FPS этих art flights не является
performance acceptance. Полная R6 и общая цель остаются активными.

Воспроизвести независимый аудит:
`python tools/research/audit_creature_resource_cache_probe.py --out docs/research/creature-r6-v2/appearance-cache --label mapping_final_catalog --native-dir native_mapping_final`


## Актуальный проход: skin ownership и effective selection

Release EXE `ea27f4b303b32dc38aa70f40f27f06025f8b2a73cc58e820cd66a3cbd77dbc4d`:
`context_skin_catalog.json`, `native_skin`, `runtime_skin_catalog`.

До исправления B/G брали raw creature.GetSkinId(), обходя ownership check
общего ResolveCreatureAppearance. Также игнорировался результат resolver
для пустого raw ID и выбранного user appearance. До: native_skin_before /
skin_before_summary.json — 560 сценариев, 260 ошибок (255 foreign raw и
5 compatible resolved-only). Это ожидаемо неуспешный flight, не приёмка.

ResolvedCreatureAppearance теперь хранит effectiveSkinId, заданный только
после проверки владельца. B/G читают его, как world/preview уже читали общие
resolved properties. R продолжает использовать прежние resolved parts.
Совместимые и universal definitions сохраняют ID; foreign/missing — пустой.
Существующие mapping/fallback правила и все authored resources не менялись.

Native probe для56 видов ×5 скинов ×2 способа выбора прошёл560/560:
550 foreign cases дают те же texture IDs и draw hashes, что base;10 compatible
cases используют ожидаемый registered skin/diffuse texture. Оба способа
дают одинаковые submissions. Это actual visual draw submissions с GPU IDs,
не чтение GPU pixels и не end-to-end переключение профиля пользователя UI.
Независимый skin_ownership_audit.json проверяет полный набор пар/ownership,
положительные texture IDs, exact sibling hashes и текущие source atlas SHA.

Повторные161 mesh/locator/rigid-pack cases прошли на том же EXE;
На том этапе cache evidence находилась в native_skin; shared audit JSON
сейчас указывает на последний final context, описанный ниже. Native artifacts
и context не смешиваются с историческими неуспешными проходами.
`skin_body_parity.json`:5290/5290 RGBA PNG прежние, matrix61 ресурсов прежняя.
Прежние sampled art observations перенесены по exact proof, а world wolf
frame_002 прямо изучен; visual_review.json хранит SHA/границы наблюдений.
Повторная art-оценка каждого кадра не заявляется.

11 выбранных Release C++ tests пересобраны и проходят, включая новые
compatible/foreign/universal assertions effectiveSkinId. Ledger61/61,
source freshness/runtime mirror актуальны; diff --check проходит.
Логи: assetgen_output/creatures/r6_skin_ownership_{fix_build,after,tests_build,tests,ledger}.log.

Остались обязательными: явный texture_map и unmapped/missing-stem fallback
на actual B/G/R submissions; missing-resource recovery; texture/root/overlay
reload и priority; полный world user appearance flow, FP/save; прочие
contacts/transitions/routes/habitats/culling; clean package и matched perf.
Полная R6 pending, цель активна. Этот subset нельзя расширять на будущие
non-B skins без дополнительного независимого oracle: probe явно отвергает
такое расширение каталога.

Аудит скинов:
`python tools/research/audit_creature_skin_ownership_probe.py --out docs/research/creature-r6-v2/appearance-cache --label mapping_final_catalog --native-dir native_mapping_final`


## Актуальный mapping/fallback subset всех56 видов

Release EXE `4e76f9220ab4fea5cb40e5fd101aff6ac607f3377b990291187487a44e821968`.
Context: context_mapping_final_catalog.json; native: native_mapping_final;
world: runtime_mapping_final_catalog. Скелеты, модели, PNG, clips и metadata
пяти bundled skins не менялись; генерация/публикация новых ресурсов не нужна.
Ledger61/61 остаётся актуальным.

### Поведение

Appearance-resolver после ownership check сохраняет явные texture overrides.
B/G/sprite и FP используют общий texture resolver с этим resolved appearance.
Явный texture_map имеет приоритет над texture key и B diffuse convention;
при отсутствующем mapped файле применяется исходный материал существа.
Не указанные в mapping материалы G/R/sprite остаются базовыми. Для B
сохранён whole-mesh skin/diffuse atlas пяти существующих skins; это отдельная
legacy convention для diffuse stem, а не подмена всех материалов G.

R resolved parts сохраняют baseTextureAssetKey: отсутствующий mapped PNG
больше не скрывает деталь. UV layout R определяется исходным материалом,
поэтому имя нового PNG не превращает face layout в обычный cube layout.
Preview использует те же fallback и UV правила. Fallback-деталь без parts
теперь также учитывает mapping. FP caller получает world resolved appearance
вместо raw SkinId; visual FP/hands/save gate этим не закрывается.

Для авторов внешних G packs: whole atlas для body следует назначать явно,
например texture_map body → diffuse. Прежняя неявная подмена каждого
немаппированного материала через skin/diffuse устранена. Собственные56+5
сохранили весь прежний sampled visual; совместимость всех внешних packs
не заявляется.

### Native evidence

297/297 сценариев для всех56: mapped56, unmapped56, missing_mapped56,
direct_key56, missing_skin56 и B legacy_atlas17. Диагностические4×4 PNG
собственные, контрастные, находятся только под native_mapping_final /
texture_mapping_fixture; production resources не переписаны. GPU readback891
совпал с исходными RGBA. Для всех297 сохранён полный multiset geometry
submission hashes: MVP, actual UV/indices, skin palette; у R также selector
исходного UV mesh. Это sampled Walk at .1375, не все состояния/маршруты.

Независимый texture_mapping_audit.json проверяет current EXE/shaders/assets,
точный набор сценариев, actual source material stems, число source cubes B,
fixture JSON/file inventory/PNG SHA и palette, expected keys → positive GPU
IDs, actual draw counts/IDs/geometry hashes и readback RGB. Wrapper отвергает
stale/missing/неуспешные mapping и GPU artifacts.

Фактический runtime route этого набора: B17/G35/R3/sprite1. fire_spirit
объявлен rigid_voxels, но factory применяет sprite.billboard до backend;
новый probe выявил и исправил обход appearance в этом sprite пути. В прежних
инвентарях R4 — число declared definitions, не четыре actual rigid visuals.
Три основных backend сохранены; fire_spirit не превращён в объёмный куб.

Повторно на том же EXE: ownership560/560, cache/locator161/161,11 выбранных
пересобранных Release C++ tests. mapping_body_parity.json:5290/5290 RGBA PNG
всех61 ресурсов прежние,3660 matrix +1630 legacy. Прежние sampled art
observations перенесены через exact proof; прямо изучен новый world wolf
frame_002. SHA/границы: visual_review.json. Это не новая автоматическая
художественная приёмка всех состояний; performance flight отключён.

### Исторические неуспешные diagnostics

mapping_before_catalog завершился на недостаточном fixture fire_spirit без
parts (mapping_before_setup_error.log), до полного отчёта. Он не доказывает
runtime regression продукта. Следующий mapping_catalog записал297 cases,
104 false verdicts:102 B oracle cardinality errors (по одному atlas stem
вместо draw на каждый cube; actual IDs и geometry уже правильны) и2 actual
sprite mapping failures. Все891 GPU readbacks тогда уже проходили. Точный
разбор: mapping_intermediate_diagnosis.json. Source cube oracle и sprite
path исправлены до окончательного mapping_final_catalog.

Аудит: python tools/research/audit_creature_texture_mapping_probe.py --out docs/research/creature-r6-v2/appearance-cache
Логи: assetgen_output/creatures/r6_texture_mapping_{oracle_fix_build,final,tests_final_build,tests_final,ledger,diff_check}.log.

Остаются обязательными: missing model/base texture recovery, texture/root/
overlay reload/priority, полный world user selection/FP/save, оставшиеся
contacts/transitions/routes/habitats/culling, clean package и matched perf.
Ни один r6_state не переведён в accepted; полная R6 и цель остаются активными.


Актуальное продолжение loader/root/pack recovery и process restart: [resource-reload](../resource-reload/index.md).
Исторические contexts выше сохранены; текущий EXE и remaining gates — в новом README.
