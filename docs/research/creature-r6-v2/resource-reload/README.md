# R6: восстановление ресурсов, root и resource pack reload

Текущий Release EXE `c0853349cc1f0f6633ff67c2e8297ad06f0cd687ea9cc40a32b7deb81613b09b`.
Контексты: context_reload_catalog.json и context_reload_restart_catalog.json;
нативные evidence: native_catalog и native_restart. Оба desktop процесса
завершились успешно. Только Release, через tools/flight_sim_run.py.

## Найденная и исправленная ошибка

UCreatureDefinitionStorage::Load очищал definitions без изменения Revision,
если новый каталог пуст. Это нарушало заявленный epoch contract для appearance
и preview caches. Новый Release assertion воспроизвёл ошибку до исправления
(empty_catalog_before.log), после единственного изменения — bump при clear —
проходит (empty_catalog_after.log). Весь выбранный набор11 Release C++ tests
пересобран и проходит (release_tests.log). Это не доказательство полного UI flow.

## Native matrix61

935/935 scenarios на каждом из двух процессов:

| Проверка | Число | Доказательство |
|---|---:|---|
| Texture/root/pack stages61 ресурсов | 671 | 11 stages ×61 actual retained visuals |
| Negative mesh cache/recovery B17+G35 | 260 | 5 stages ×52 retained/fresh pairs |
| Все реальные R model files | 3 | Missing → recovered → edited same path → exact original restored |
| Пустой каталог/revision | 1 | Count0, revision строго +1 |

Texture stages: baseline, missing_textures, merge_recovery, texture_root_swap,
texture_root_return, pack_selection_ab, pack_selection_ba, pack_tier,
pack_removed, pack_repeat, pack_final_base. После удаления textures все61
ожидаемо не рисуют textured geometry и старые GL names удалены; после recovery
возвращаются исходные материалы. При root swap и overlays используются новые
контрастные4×4 диагностические PNG. Все positive texture stages сохраняют
полный multiset geometry submissions: actual MVP/vertices/UV/indices/palette,
а у R — selector UV mesh. Исходные skins owners и material keys сохраняются.

Каждый positive stage даёт72 used logical texture keys, всего720 полноразмерных
GPU RGBA readbacks. FNV64 всех RGBA совпал с CPU PNG decode и независимым PIL
расчётом. Это чтение текстур из FBO, не GPU draw всех моделей во всех состояниях.
Иконки/неиспользуемые в этих submissions PNG не объявляются GPU-проверенными.

Два diagnostic packs имеют точные копии текущих own production JSON,
моделей и buffers всех56 и skin.json всех5; только PNG заменены own palette.
Настоящий UResourcePackResolver выбирает порядок по selection/tier; настоящий
ApplyCreaturePackOverlays применяет definitions/skins/textures. Проверены A→B,
B→A, primary/secondary, удаление и повторное применение. У всех61 проверен
фактический definition sourceDirectory победившего pack и смена revision.
Это локальные storages: мировой pack selection пользователя не менялся.

## Missing meshes и rigid recovery

B/G cache roots намеренно указывают на пустой fixture, definition sourceDirectory
пуст, поэтому проверяется реальный root lookup. После появления geometry/model
и buffer файлов retained и fresh visuals остаются negative-cached без invalidation:
это ожидаемое отсутствие per-frame filesystem polling. Явный Clear восстанавливает
все52; смена root на другой пустой и возврат в production снова корректны. Positive
retained/fresh hashes, draw counts и mesh pointers совпали. После missing clock
ещё не шёл: reference dt0; сохранность существующей фазы при positive reload
отдельно проверяют texture pack stages и прежний live-cache161 probe.

У R отсутствующий model даёт штатную одну fallback деталь, а после появления
файла все3 восстанавливают исходную geometry. Same-path изменение первого size.x
×1.125 проходит через реальный pack entry, совпадает с fresh visual и меняет
geometry; затем точный original payload возвращается. Changed JSON сохранены
только в fixture/rigid_changes. Это diagnostic variants, не новая art-публикация.

После probe global mesh cache roots восстановлены, локальные GL textures удалены.
Production meshes, PNG, authored animations и save formats не изменялись.
Runtime routes: B17/G35/R3/sprite fire_spirit1; исторический R4 — declared backend.

## Независимый аудит и визуальная сохранность

resource_reload_audit.json и resource_reload_restart_audit.json проверяют:
current EXE/shaders/all recorded assets; точный набор61/52/3 и935 cases;72
logical keys; expected source files и GPU RGBA; pack order/priority/locators;
negative-cache semantics, fresh pointers; rigid JSON edit только одной величины;
395 exact original source copies; полный fixture inventory и SHA. Оба проходят.

body_parity.json:5290/5290 RGBA PNG61 ресурсов совпали с прежним mapping-final
EXE. restart_body_parity.json: ещё5290/5290 точны между отдельными процессами
того же EXE. Это1630 legacy +3660 matrix PNG за проход. Прежние sampled art
observations наследуются через exact proof. Прямо просмотрены два world wolf
frame_002, их mtime позже соответствующего probe report; обычные gray texture
и silhouette восстановлены, диагностическая palette не просочилась. SHA и
границы этих наблюдений — visual_review.json. Новая полная art приёмка не заявляется.

Прежние297 mapping +891 GPU readbacks, ownership560 и live-cache161 также
повторно проходят на первом текущем EXE/context и строго проверяются wrapper
в обоих процессах. Новые независимые reports лежат рядом. Ledger61/61 актуален:
exact reviews/source inputs/published+bin payload/runtime manifest. R6 states pending.

## Границы и следующий шаг

Закрыт bounded loader/resolver/merge и retained resource recovery subset.
Процессный restart не является чистым OS/disk cache, clean build или package.
World bootstrap OnCreatureCatalogChanged/UI pack switching, полный world user
appearance flow, FP/hands/equipment/save→reload, все contacts/transitions/routes/
habitats/culling, clean Release package и matched CPU/GPU p95/memory остаются
обязательными. Art flights metrics disabled, FPS HUD не является performance verdict.
Цель активна, полная R6 pending. Следующий фокус — фактический world/FP/save flow.

Аудит:
python tools/research/audit_creature_resource_reload_probe.py --out docs/research/creature-r6-v2/resource-reload
python tools/research/audit_creature_resource_reload_probe.py --out docs/research/creature-r6-v2/resource-reload --label reload_restart_catalog --native-dir native_restart --report resource_reload_restart_audit.json

Полный запуск: run_creature_pilot_qa.py --production --species wolf --preview-species
<sorted56> --resource-cache-probe --resource-reload-probe --matrix --close
--out docs/research/creature-r6-v2/resource-reload --label reload_catalog --native-dir native_catalog.
Логи: assetgen_output/creatures/r6_resource_reload_{capture_build,flight,restart_flight,tests_build,tests,ledger,diff_check}.log.

Повторный запуск требует новых --label и --native-dir. Wrapper отвергает
существующие context/native directory до записи context и старта процесса:
это сохраняет принятые SHA evidence и обеспечивает действительно пустой
missing-resource fixture, без очистки чужих или прежних review artifacts.


Продолжение FP authored-arm/source subset: [fp-save](../fp-save/index.md).
Save/world-camera приёмки остаются pending; старые reload contexts выше сохранены.
