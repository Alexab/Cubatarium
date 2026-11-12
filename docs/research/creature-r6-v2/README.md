# R6 — текущая проверка полного набора

R5 принятие всех 56 существ и 5 скинов не считается полной финальной приёмкой.
Этот каталог фиксирует проверяемые результаты R6 и незавершённые гейты.

## Инвентаризация до R6

`ledger_before.json`, полученный audit_creature_acceptance_ledger.py:
61/61 staged bundles имеют exact hash review с читаемыми, неизменёнными
comparison/motion/runtime/UI доказательствами. Все 61 payload совпадают
с models и bin/models. Неожиданных species нет. Это проверка целостности,
не новая автоматическая художественная оценка.

Только 2/61 bundles тогда имели текущие source_inputs; после изменений R6
эта цифра требует нового измерения. В manifest нет пяти skins. У 24
contracts оставалось planned. Контракты согласованы с найденными R5 reviews:
все 56+5 accepted на этапе R5, с путями доказательств; r6_state у всех pending.

## Полный glTF motion audit

`motion_before/metrics.json`: 35/35 опубликованных G видов, 257 фаз каждого
исходного clip, весь skinned mesh и отдельные полностью owned effectors.
Число фаз пока диагностическое; финальные ground-гейты требуют межключевых
проверок с большей плотностью. measure_creature_gltf_motion.py теперь
измеряет и clips без foot-contact metadata, явно различая semantics.

Найдены ground-отклонения badger, hedgehog, kitten, panda, penguin, rat,
warthog. У badger Run −0.016909, Punch −0.039122 блока. Подводные хвосты
и крылья в воздушных clips ниже origin не объявляются ошибкой наземной опоры;
они требуют habitat/bounds/route проверки. Исходная hovering посадка mese
также сохраняется, а не принудительно притягивается к полу.

## Badger: исправленная опора

В motion.py добавлен явно включаемый `contact.geometry: skinned_mesh`:
минимум вычисляется по всему мешу, включая вторые joint weights и shape
corners. `samples:257` уплотняет только корректирующий root translation;
другие channels семплируются по своим собственным times, через shortest
SLERP для rotations и linear interpolation для translation/scale.
Исходные ключи суставов не переписываются и не размножаются.

`badger/metrics.json`: 1025 фаз всех четырёх clips. Итоговый минимум
−0.000218, верхняя ошибка опоры +0.000702 блока. До исправления idle/Walk/
Run/Punch проверялись менее полно; положительный R5 art verdict не скрывает
эти найденные позже ошибки.

`badger/payload_change_audit.json`: POSITION/TEXCOORD_0/JOINTS_0/WEIGHTS_0 и
PNG body/icon побайтно совпадают с R5. Изменены только animation channels
и служебная glTF раскладка. model.bin 36,488 → 52,600 bytes; рост 16,112,
а не размножение плотных ключей на всех joints. Это не performance PASS.

Новая regression-проверка независимым exported skinning подтверждает:
все authored joint keys сохранены; q_after−q_before — единый вертикальный
сдвиг, без изменения horizontal motion или deformation. Прямо просмотрены
comparison, полная native sheet, иконка и world actor после physics.
`candidate_badger` — промежуточная версия с уплотнёнными всеми channels;
`final_badger` — принятая оптимизированная версия с плотным только root.

## Оставшиеся обязательные гейты

| Гейт | Сейчас | Необходимое доказательство |
|---|---|---|
| Полный 56+5 ledger | Текущие exact reviews/provenance/payload 61/61, manifest 56+5, bin mirror совпадает | Сохранить актуальность при следующих изменениях; полный R6 verdict ещё pending |
| Текущий генератор | Две независимые output-сборки 61/61 byte-identical на текущем host; Python read audit без reference mesh/PNG | Чистая Release сборка/package остаются отдельным гейтом; см. repro/README.md |
| Ground motion | Семь ранних G исправлены; B clearance устранил проникновение в 87 плотных сценариях 11 видов, включая wolf. Rotations/shape и положительный отрыв сохранены | bone-motion/README.md: stance/stride/terrain и переходы pending; benthic/shore и travelScale остальных backend |
| Полные визуальные ракурсы | 61/61 original/current: 8 yaw × 3 позы + верх/низ, цвет/силуэт; clipping 0. После B clearance 16 листов прямо повторно просмотрены, 45 побайтно прежние | bone-motion/index.md — актуальная matrix; world/state/equipment отдельно |
| Форматы | 35 G: Khronos без issues; независимый активный subset B17/R4/skins5 без ошибок/UV overlap, исправлены 17 legacy headers | formats/README.md; сохранять актуальность. Полная Bedrock-сертификация не заявляется |
| Appearance/cache | Local reload/recovery935×2, GPU RGBA720×2; actual world user selected/legacy fallback проходит в4 процессах | user-world/README.md и resource-reload/README.md: UI/pack switching, другие users/flows, OS cold cache/package/perf pending |
| Equipment/FP/save | Equipment9072, FP936/GPU1872, actual cooperative user/world4 runs/64NPC и20 control roundtrips проходят;4 world idle frames просмотрены | user-world/README.md: framing/attack/walls/presets, UI/control movement и прочие applicable states pending; equipment-fit/fp-save сохраняют границы |
| Collision/contact envelope | Existing glTF bounds audit Failed:17 discrepancies/35; full CTest не принят | save-roundtrip/bounds_diagnostic.json: видовые body/render contracts и native contacts всех backend обязательны |
| Build/package | Обычная Release сборка проходит | Чистая Release сборка и проверенный package без references |
| Performance | Art probes metrics disabled | Сопоставимые Release scene/count, CPU/GPU p95/frame/memory, шум/бюджет |

Ни одна строка pending/частично не означает PASS. Цель остаётся активной.


## Итог первого R6 прохода

Badger и шесть hedgehog/kitten/panda/penguin/rat/warthog повторно приняты
и опубликованы после коррекции опоры. Последние шесть: 1025 фаз,
−0.000047…+0.000831 блока; у warthog Run понадобилось 513 root samples.
Их buffers/textures/deformation/horizontal paths сохранены по независимым
проверкам. 203/203 normal-production native кадров совпали с принятыми
кандидатами (29 badger + 174 остальных); world кадры прямо просмотрены.
Все 35 fidelity tests проходят, включая обновлённое trajectory assertion;
9 quality и 9 выбранных C++ tests проходят. Это не закрывает все движения,
stride calibration, режимы/переходы и другие строки R6.

`ledger_after_ground.json`: 61/61 exact reviews и payload совпадений,
текущие generator source_inputs у 7/61; manifest по-прежнему не содержит
пять skins. Все species contracts accepted на R5, R6 у всех pending.

## Независимый Khronos Validator — первый проход

Использован официальный npm gltf-validator 2.0.0-dev.3.10, pinned lock сохранён
в khronos/tool-package-lock.json; источник API:
https://github.com/KhronosGroup/glTF-Validator/tree/main/node
Скрипт tools/research/validate_creature_gltf.cjs читает реальные опубликованные
35 G моделей, не использует внутренний validate_staging и не скрывает issues.
URI resolver не допускает сетевых URI или выхода из species folder.
Точные hashes считанных model.gltf/model.bin и отчёты сохранены в khronos/.

Результат: 35/35, ноль errors; 6030 warnings
ACCESSOR_JOINTS_USED_ZERO_WEIGHT — не-нулевой индекс joint в слоте с нулевым
весом. Ещё 35 info UNUSED_OBJECT для TEXCOORD_0: exporter задаёт текстуру
через отдельный creature.json/runtime material, а glTF baseColorTexture
не привязан. Поэтому этот проход НЕ валидирует body.png как glTF image:
его внешний URI не объявлен в документе. Итоговый формат-гейт не объявлен
полностью закрытым: предстоят канонизация zero-weight slots и проверка
стандартных material/image bindings с сохранением runtime parity. Артефакты
этого прохода оставить как before, затем повторить на итоговых resources.


## Исправление формата после первого Khronos прохода

Описанные выше 6030 warnings и 35 infos устранены, before-отчёты сохранены.
Все 35 G вновь опубликованы: у zero-weight slots канонический индекс 0;
объявлены стандартные material/image/nearest sampler bindings собственного
PNG. Geometry/UV/weights/active joints/animations/bind данные, texture bytes
и creature.json прежние. Exporter не использует неактивные skin indices
как authoring metadata; seam tests определяют кубы по независимым UV islands.

Khronos candidates и published models: 35/35, ноль errors/warnings/infos,
с настоящей проверкой объявленных PNG. 1015/1015 кадров текущего Release
рендера до/после совпали; ещё 1015/1015 из обычного каталога совпали с принятыми
кандидатами. Это 35 native/UI probes; настоящий world spawn в этих групповых
запусках только badger. Остальные world proofs явно наследуются из прежних
индивидуальных приёмок неизменённых моделей. Подробности и exact review:
format/README.md и format/review.json. Полная R6 matrix не подменяется parity.

35 fidelity, 10 quality и 9 выбранных C++ tests проходят. Ledger after_format:
61 exact reviews/payload matches, 35 current-generator source_inputs, ноль
unexpected species; сводному manifest ещё недостаёт пяти skins. Впереди
финальные provenance/manifests/61-resource rebuild и другие pending строки
таблицы. Production Python requirements теперь зафиксированы отдельно.

## Полный воспроизводимый каталог и provenance

Следующий проход завершён для текущего host: две сборки 56+5 в независимых
output directories полностью совпали, включая manifests. Read audit не
обнаружил reference asset/network входов; live models tree до публикации
остался неизменным. Воспроизведены все принятые runtime bytes, включая
rigid walker: его прежний tail center pivot теперь явно записан в recipe.
Скины получают stems из skin.json; произвольные файлы каталога больше не
влияют на генерацию. Новые manifests связывают toolchain/functional contract,
mesh/clip/file/pixel hashes и авторство собственных outputs с exact review.

Все 61 metadata bundles вновь опубликованы с сохранением R5 решений на
неизменённый runtime payload. Ledger after_repro: 61 exact reviews,
61 current source contexts/file records, 61 published/bin payload matches,
56 species + 5 skins, идентичный global manifest в bin, unexpected species 0.
Прежние notices архивированы; own generated files используют существующую
MIT-лицензию проекта. У billboard fire_spirit удалены два неиспользуемых
старых glTF файла. Подробности: repro/README.md, summary.json,
audit_candidate.json, audit_published.json и review.json.

Candidate renderer parity — 1630 кадров всех 56 + 87 трёх дополнительных
скинов; две human skins входят в первый набор. Обычный production каталог
повторил все 1717 кадров точно. Повторный Khronos для
published G: 35/35 без errors/warnings/infos. 36 fidelity, 12 quality и 9
выбранных Release C++ tests проходят. Полная visual matrix, маршруты,
appearance/cache/save-load/package/performance не закрыты этими результатами.

## Полная native pose matrix и исправление framing

Теперь сняты и прямо просмотрены все 61 парный лист original/current:
8 yaw × 3 позы + точные верх/низ, цвет и shader-coverage силуэт. Для каждой
версии это 1830 real renders / 3660 PNG. Свежесть, source/EXE/shader hashes,
полнота и pixel coverage проверены; visual_review.json содержит отдельные
наблюдения reviewer и SHA листов. Это просмотр данной sampled pose matrix,
не всех мировых состояний/маршрутов и не всей R6.

Матрица обнаружила обрезание B/R previews: bbox collider/rest не охватывал
движение/диагональ. Camera fit теперь использует настоящие SubmitDraw
requests и 65 procedural фаз, охватывающую сферу и cache/invalidation.
На итоговой matrix обрезанных изображений 0; G previews до изменения sand
palette — 1015/1015 прежних pixels. Плоский fire_spirit закономерно имеет
пустые top/bottom и одинаковые статические фазы, как исходный sprite;
исключение явно записано, не скрыто агрегированным PASS.

Также исправлен бирюзовый crown accent sand_monster: только 30 atlas pixels
верхней грани и 2 icon pixels стали тёплыми минеральными. Geometry/buffers/
UV/weights/animations/alpha/creature.json неизменны. Новая exact review,
world/motion/UI и matrix подтверждены; исправление опубликовано. У других
60 ресурсов все 3600 matrix PNG остались прежними; их прямо просмотренные
листы сохранились побайтно. Итоговые files: matrix/README.md, index.md,
visual_review.json, audit_accepted_catalog.json, production_parity.json.

После sand-коррекции повторены обе полные 61-resource сборки: byte-identical,
no unexpected Python build reads, source context и reviewed models/bin
подтверждены. Новые summary/ledger находятся в matrix/. Старый repro/summary
исторический до sand crown. Khronos published вновь 35/35 без issues;
targeted elemental fidelity, 12 quality и 9 Release C++ tests проходят.
Весь список оставшихся R6 гейтов в таблице выше сохраняется.

## Root-only reference fit и независимый B/R format audit

Оставшиеся 19 обрезанных original octopus projections устранены cycle-fit
реальной root-only geometry/root animation. Обе версии всех 61 пересняты:
clipping 0 у original и current. Все 5290 current PNG прежние; 59 парных
листов прежние, reference framing butterfly/octopus повторно просмотрено.
Актуальная художественная matrix: matrix/index.md и visual_review.json.

Независимый аудит B/R выявил 17 ошибочных объявлений format_version 1.12.0
при legacy keyed box-UV layout. Exporter теперь объявляет соответствующую
официальной схеме 1.8.0. Полное parsed содержимое geometry кроме header
идентично; остальные runtime файлы всех 61 совпали побайтно. Две новые
полные сборки 61/61 совпали, все 61 опубликованы с exact metadata reviews;
5290/5290 native PNG candidate и ещё 5290/5290 production PNG точны.
Два published roots B17/R4/skins5 без ошибок/UV overlap, G35 Khronos без
issues. Это структурный subset и sampled renderer parity, не весь R6.
Текущие evidence, ledger и ограничения: formats/README.md и ledger.json.
36 fidelity, 12 quality и 9 выбранных Release C++ tests проходят.

## B clearance и явные профили генератора

Плотный native B audit выявил небольшую новую регрессию wolf и остаточное
проваливание других B. Общий root теперь только поднимается над feet plane
на недостающую высоту; костные матрицы/вращения, pivots, bind, физический
collider и все authored runtime files прежние. Политика не опускает
положительный отрыв и исключает rabbit/air/water/serpentine; при takeoff
поправка плавно уходит к нулю. 17 × 66 × 1025 native поз до/после подтверждают
исправление 87 ground сценариев 11 видов без изменения XZ envelopes;
это ещё не stance/stride/terrain/transition приёмка.

Все 61 original/current пересняты новым Release EXE: clipping0, 16
изменившихся парных листов прямо просмотрены, 45 парных листов побайтно
прежние. Прямо просмотрены ещё 11 полных native motion sheets и два human
equipment кадра; реальный world actor этого прохода только wolf. Текущие
art observations и их границы: bone-motion/index.md, visual_review.json,
motion_review.json. Performance этих запусков по-прежнему отключён.

Все 17 B recipes теперь явно назначают прежний animation_profile;
генератор отвергает неявный fallback. Новые две полные 61-resource сборки
совпали, весь source runtime payload побайтно прежний. Все 61 metadata
bundles опубликованы с profiles_review.json. Последний ledger:
bone-motion/ledger.json — exact reviews/source contexts/file records/
published+bin matches61/61, manifest56+5, runtime global mirror точен.
36 fidelity, 13 quality и 10 выбранных Release C++ tests проходят.
Две неактивные bin-only owl .orig копии явно записаны отдельно; полный
bin inventory не объявлен чистым package. Остальные pending гейты выше
остаются обязательными; цель не завершена.


## Anatomic equipment fit: текущий sampled результат

См. equipment-fit/README.md и index.md: 9072 кадров трёх комплектов для шести
humanoid ресурсов, восемь yaw и обе руки; detail/yaw sheets изучены, sampled
посадка принята. Новый полный capture61 сохранил5288/5290 PNG (все3660 body
matrix прежние); два намеренных изменения — human equipment. Actual world
gear этого прохода — zombie для трёх материалов. Copper restart3024 точен.

Добавлен и исправлен реальный GPU material cache regression: полный model
path разделяет inventory/worn glTF в одной папке, 9 RGBA/ID probes проходят.
Все девять полых моделей/атласов воспроизводимы; G35 и worn9 Khronos0issues,
11 Release C++ tests проходят. Текущий ledger: equipment-fit/ledger.json.
Границы sampled art, source invariants и remaining gates указаны в новом
README. FP/save/live creature cache/полные state-route/performance/package
приёмки остаются обязательными; полная R6 и цель не завершены.


## R6: обновление удерживаемых моделей и locator ресурсов

Подтверждена и исправлена stale B/G mesh regression: live visuals отслеживают
поколение кэша и идентичность запроса; resolved path включает sourceDirectory
definition. Pack merge очищает B/G/R до Load, preview bounds инвалидируются.
Actual native probe и независимый SHA/fixture audit:161/161 (same-root56,
filename52, directory52, один real rigid pack reload). Clock сохраняется;
текущая pose matrix error0. Все5290 PNG61 ресурсов точно прежние относительно
equipment-fit/final; creature/item meshes, PNG и authored clips не менялись.
11 пересобранных Release C++ tests проходят, ledger61/61 актуален.
Evidence и ограничения: appearance-cache/README.md.
Это subset live mesh/locator, не весь appearance/pack reload. Следующие
обязательные проверки: skin ownership/effective texture keys, texture reload,
FP/save, motion/route/habitat/culling, clean package и matched performance.
Полная R6 pending, цель активна.


## R6: проверенный effective skin для B/G

Native regression до исправления:260/560 ошибок — foreign skin255 и
resolved-only compatible selection5. B/G теперь получают effectiveSkinId
общего appearance-resolver после ownership check; raw ID больше не обходит
валидацию и не отменяет выбранный appearance. R parts и authored resources
не изменены. Повторный native audit56×5×2:560/560, foreign submissions550
точны к base, compatible10 используют ожидаемый GPU texture ID; оба способа
выбора эквивалентны. Это submission subset, не UI/FP/save или GPU pixel audit.

На текущем Release EXE ea27f4b303b32dc38aa70f40f27f06025f8b2a73cc58e820cd66a3cbd77dbc4d
также прошли161 cache/locator cases,5290/5290 прежних RGBA PNG,11 выбранных
пересобранных C++ tests и ledger61/61. Последние evidence:
appearance-cache/context_skin_catalog.json,
skin_ownership_audit.json, resource_cache_audit.json, skin_body_parity.json,
visual_review.json и README.md. World frame этого прохода — только wolf;
полные sampled art observations наследуются через exact RGBA proof.

Следующий шаг R6: explicit texture_map/unmapped/missing-stem для B/G/R,
missing-resource recovery, texture/root/overlay reload; затем полный world
user selection/FP/save, motion/route/habitat/culling, clean package/performance.
Ownership subset пяти bundled skins закрыт; полная appearance compatibility,
вся R6 и общая цель остаются pending/active.


## R6: единый texture mapping/fallback, включая фактический sprite route

Явные validated appearance overrides теперь используются B/G/sprite и FP;
R и preview сохраняют исходный материал для missing mapped PNG и UV layout.
297 native scenarios всех56 +891 диагностических GPU RGBA readbacks прошли,
geometry/UV/pose submissions сохранены. Actual routes — B17/G35/R3/sprite1:
fire_spirit declared R, но factory выбирает billboard; этот обход также
исправлен. R4 в исторических инвентарях означает declared definitions.
Собственные56+5 assets не менялись; внешним G skins нужен явный body→diffuse
mapping, если требуется whole atlas вместо базового материала.

Текущий Release EXE 4e76f9220ab4fea5cb40e5fd101aff6ac607f3377b990291187487a44e821968:
ownership560, cache161,11 C++ tests проходят;5290/5290 PNG61 ресурсов прежние,
ledger61/61 актуален. Evidence: appearance-cache /
context_mapping_final_catalog.json, texture_mapping_audit.json,
mapping_body_parity.json, visual_review.json и README.md. Исторические
ошибки diagnostic fixture/oracle явно отделены от product defects.
Дальше: missing resource recovery, texture/root/overlay reload/priority,
полный world user selection/FP/save, motion/routes/habitats/culling,
clean package/performance. Полная R6 pending, цель active.


## R6: texture/root/pack reload и missing-resource recovery

Исправлен definition epoch при пустом Load: Release assertion воспроизводит
старую ошибку и проходит после bump при clear. Два отдельных Release процесса
на EXE c0853349cc1f0f6633ff67c2e8297ad06f0cd687ea9cc40a32b7deb81613b09b проходят935
новых scenarios каждый:671 retained61 texture/root/pack,260 negative B/G
recovery,3 rigid same-path recovery/edit/restore,1 empty catalog. На каждом
720 полноразмерных GPU RGBA readbacks точны. Настоящие resolver/merge проверяют
selection/tier/priority и source directories; fixture copies395 точны к source.

Первый5290 PNG61 ресурсов точен к предыдущему EXE; повторный5290 точен между
процессами. Два world wolf кадра после диагностики прямо просмотрены. 11 Release
C++ tests проходят, ledger61/61 актуален. Последние evidence и ограничения:
resource-reload/README.md, index.md, resource_reload_audit.json,
resource_reload_restart_audit.json, body_parity.json, restart_body_parity.json,
visual_review.json. Assets/animations/save formats не менялись.

Закрыт bounded local loader/resolver/merge/resource recovery subset. Это не
world bootstrap/UI switching, не OS cold cache, не FP/save, не все routes/states,
не clean package/performance. Следующий шаг — фактический world/user/FP/hands/
equipment/save flow, затем остальные motion/route/habitat/culling, clean package
и matched performance. Полная R6 pending; цель active.


## R6: FP использует authored arm meshes/UV

Исправлен hardcoded right-arm4×12×4 для обеих рук. FP B arms теперь берутся
из актуальной rightArm/leftArm geometry с uniform fit; skeleton остаётся2×12×2,
left UV собственный. World rig, published PNG/clips, item models/presets прежние.
Release EXE6a1c454f5d4d3f0a22d34d75c4d1ec8eb8aa98d922df9150580d8dce8a6811cf:
936 actual offscreen FP PNG,1872 GPU buffers совпали с source UV,6 retained
renderer cases; raw/user/legacy selected pixels эквивалентны. Sampled root
error<1e-7; body5290/5290 всех61 точны,11 C++ tests/ledger61 проходят.

Прямо просмотрены18 paired idle/walk/attack sheets шести B ресурсов. Принят
только source shape/UV subset. Старое низкое idle framing и уход правой руки
за экран на части attack cycle остаются видимыми; full FP presentation и
world/user/terrain/preset/inertia/save flow не объявлены принятыми. Evidence:
fp-save/README.md, index.md, fp_audit.json,
body_parity.json, visual_review.json. На момент FP capture entity save roundtrip ещё не запускался; следующий выполненный этап описан ниже.

Следующий этап: isolated LoadCreatures→Save v2→Load→Save для56+5/legacy aliases
с inventory/stats/footprint/material comparison, затем disposable cooperative
world/user save и настоящая FP camera. Подробный маршрут и первичные Epic/
Godot ссылки добавлены в новый README. Остальные routes/habitats/culling,
clean package/performance остаются обязательными. Цель active, R6 pending.


## R6: завершён isolated entity save/load subset; bounds/contact gate pending

Release EXE4f9aea971602ec7c02d7142c2cf32828f37caf259d48dccc24502eeeb79c309d
проверен в двух независимых процессах. На каждом128 scenarios:56 видов+5
скинов+3 legacy aliases в pre-versioning и v2 fixtures;256 actual entity
restores/run через LoadCreatures→Save v2→Load→Save. Вместе256 scenarios и512
restores. Все9 native JSON файлов двух процессов побайтно идентичны.

Сохранены type/skin/source/body_origin/yaw/pitch/mode/footprint, stats,
storage/2hotbars/active slot/item wear; у4 humanoids+2human skins6copper armor
slots и torch offhand. Actual factory visuals до/после имеют точные sampled
MVP/geometry/UV/glTF bone palettes/material submissions, collision/eye dimensions
и effectiveSkin. Независимый auditor сверяет input aliases/defaults и все
saved/observed states, definitions и SHA ресурсов. World actors неизменны.
Это supported synthetic entity schemas, не все historical client saves,
не actual whole-world cooperative save, LoadUsers или saved-actor GPU raster.

Все5290 body RGBA61 ресурсов точны к предыдущему FP EXE; ledger61/61 актуален.
Два world wolf кадра после диагностик прямо изучены.11 targeted Release tests
проходят. Общий CTest **не принят**:50 passed,141 Not Run/unbuilt executables,
1 actual Failed creature_bounds_audit_test (192 registered). Existing audit
35glTF даёт17 mismatches: badger, dirt_monster, dungeon_master, golem,
land_guard, lobster, oerkki, orc, owl, penguin, puffin, sand_monster, seahorse,
stone_monster, treeman, wasp, whale. Не скрывать failure за targeted11.

У audit общий animated extent включает appendages/wings; автоматически
--apply не запускать. Нужно классифицировать body collision против visual/
culling envelope по каждому виду, дать доказанные видовые contracts и native
contacts/passages/turn/wall/terrain/water tests для всехB/R/G/sprite. Реальные
body/clearance defects исправить, затем повторить art/save проверки; не
увеличивать tolerance или коллизию для формально зелёного теста.

Evidence: research/creature-r6-v2/save-roundtrip/README.md и index.md,
audit_save_first.json, audit_save_restart.json, restart_parity.json,
body_parity.json, ledger.json, bounds_diagnostic.json, full_ctest.log,
targeted_release_tests.log, visual_review.json. Runtime assets/rigs/clips и
save format этим этапом не менялись; добавлены native QA observer и auditor.

Следующий обязательный этап: disposable cooperative world/user Save→restart→
LoadUsers, selected/legacy appearance, player/controlled/possessed actor,
user inventory и настоящая FP camera/depth/walls; pack/UI switching. Затем
видовые bounds/contact/stance/stride/transitions/routes/habitats/culling,
FP framing/attack clipping, clean Release package и matched CPU/GPU p95/memory.
Полная R6 pending; цель active. Entity save subset не закрывает весь save gate.


## R6: выполнен настоящий cooperative world/user save→restart→load subset

Current Release EXE8543a363e0a48f06b4abfeb1cac41e793f1d529db092782b03a8bbf952ddf8c4
проверен четырьмя отдельными user/world процессами. Legacy appearance
human_guard мигрирует в selected_skin_id и сохраняется при restart; современный
human_adventurer тоже. В каждом normal LoadUsers/LoadCreatures восстанавливают
одного actual UPlayer и64 NPC; normal cooperative Save проходит init/scan/chunks/
save_metadata/done. Все user inventory/2hotbars/6armor/wear/offhand/stats и
полное persisted64 NPC состояние точны до/после и disk→следующий process load.
20 normal control roundtrips охватывают B/G/R/sprite и возвращают игрока;
это state/appearance subset, не маршруты possession и не UI switching.

Созданы marked disposable worlds, original source world SHA до/после каждого
user-run неизменны. Штатный Application FP pass с actual loaded player inventory
разрешён под explicit QA flag в bench. Четыре idle world frames после Save
прямо просмотрены и сведены в world_fp_contact_sheet.png: authored hands,
wood_sword/wood_torch видны ночью и днём. Low idle framing/attack clipping остаются
pending. Отдельный combined current-EXE flight подтверждает5290 body RGBA и936
FP RGBA exact к предыдущим версиям,1872 GPU arm buffers,6 retained renderer
cases и ещё128 entity scenarios/256 restores.11 targeted Release tests и
ledger61/61 проходят. Никакие geometry/PNG/rig/clips/save formats не менялись.

Evidence: research/creature-r6-v2/user-world/README.md, index.md,4 audit_*.json,
restart_parity.json, visual_review.json, integrated/{body_parity,fp_audit,
fp_parity,audit_integrated_final}.json. Full CTest остаётся незелёным:50 passed,
141 unbuilt/Not Run,1 bounds audit Failed;17 mismatches никуда не скрыты.

Следующий приоритет —17 видовых collision/body/render envelope discrepancies
и actual native contacts всехB/R/G/sprite с original/current comparisons.
Затем stance/stride/transitions/terrain/routes/water/air/lava/culling,
world/UI appearance/pack switching и control movement, FP framing/attack/
wall-depth/presets, clean Release package и matched CPU/GPU p95/memory.
Синтетический single-user save subset не объявлен всеми историческими
пользователями/всеми world flows. Полная R6 pending, цель active.

## Envelope/motion audit и crossfade correction

Продолжение: [envelopes/README.md](envelopes/README.md).17 спорных glTF,
2472 native PNG,34 листа; подтверждён неисправленный collision-based culling
defect13 original/16 own. Изолированные4 motion proposals v4 не опубликованы,
все8 v4 листов просмотрены; treeman требует более массивного силуэта.
Grounded glTF crossfade floor defect исправлен:10912 actual CPU frame pairs,
palette bit exact/XZ exact,232 ordinary GPU PNG RGBA exact,12 targeted Release
tests проходят. GPU fade/terrain/all-histories/performance gates остаются.
Общий CTest не принят,основные рецепты/модели этим подэтапом не заменены.

Дополнение envelope v4: выполнены4 отдельных8-second Release world flights
через flight_sim_run.py,badger/treeman берег,lobster/seahorse water placement.
Все4 последних world кадра и общий лист прямо просмотрены;116 preview PNG/
12 world PNG,actual species spawn/model/shader/EXE hashes проверены. Это idle/
no-travel subset,не stride/contact/performance acceptance. В world treeman
также слишком тонкий;положение геометрии против debug collision box требует
отдельного world-pose сопоставления. Dense pure clips1025/sample minima
наземных/донного активных clips >=−0.000218 блока;free swimming к полу не
прижимать. Evidence: envelopes/motion_v4/{pure_contact_1025.json,world/audit.json,
world/world_contact_sheet.png};канонические предложения не опубликованы.

## R6: glTF visual culling реализован и нативно проверен

В CreatureDrawPass физическая коллизия отделена от glTF frustum envelope.
Радиус вычисляется при загрузке из vertices/weights/IBM/node hierarchy и
диапазонов всех animations; не sampled AABB. World envelope учитывает scale,
feet offset и crossfade/takeoff lift,не двигая clocks перед culling.
B/R/sprite пока сохраняют прежний fallback;их gate ещё открыт.

Все35 own glTF/74186 CPU poses и35 original/69379 покрыты finite envelopes.
Новая17-species native matrix2472 GPU PNG восстанавливает13 original/16 own
edge cases;independent6-plane test подтверждает.2472 RGBA exact к historical,
10912 retained transition frames/palettes exact и внутри pre-update envelope,
232 transition previews RGBA exact.13 targeted Release tests проходят.
EXE929205bada51d2c6862ad3989471ccc79a70c91104c099894de497b0279bb5f1.

Дополнительно прямо просмотрены15 ранее непросмотренных front/side sheets;
вместе с19 historical закрыт прямой просмотр34 distinct видов/ракурсов17 видов,
не финальная art acceptance.4 обычных production world flights успешны по
process/spawn/assets,но последний badger frame002 отвергнут для art review
из-за слишком близкой close-QA camera;frame001 читается. Камера/pose framing
и world-frustum sweeps ещё требуют проверки. Масса treeman,roll orc,клешни
lobster,хвост seahorse остаются;проверить puffin continuous flight pitch.

Evidence: research/creature-r6-v2/envelopes/culling_gltf_v1/README.md,
native_audit.json,regression_audit.json,all35_radius_audit.json,
original_radius_audit.json,visual_review.json,world/audit.json. Culling стоимость
ещё не принята:combined35 load+bound229ms не matched runtime overhead.
Последняя широкаяCTest192:50passed/141unbuilt/1Failed bounds;теперь194 tests
registered,новая широкая проверка не выполнена. Цель active,полная R6 pending.

Далее: B/R/sprite envelopes и reload/skin/actual-world frustum regression,
matched CPU/GPU p95/memory;исправить close-QA camera диагностику;доработать
арт/motion proposals через normal review/publish;оставшиеся stance/stride/
contacts/habitats/control/pack/UI/FP/clean-package gates.

## R6: четыре улучшения опубликованы в основных ресурсах

Через штатные build/validate/review/publish обновлены badger, treeman, lobster
и seahorse. Барсуку возвращено вторичное раскачивание корпуса, древню — масса
ствола и ветвей, лобстеру — подъём клешней, морскому коньку — распрямление
хвоста. У древня заново выделены UV-размеры и запечена собственная кора;
кости, inverse bind matrices и физические определения сохранены.

После прямого просмотра восьми сравнительных листов, шестнадцати UI-ракурсов
и четырёх игровых кадров ассистент явно принял R5 revision по точным staging
bundles. Это не подтверждение пользователя и не финальная R6-приёмка.
Предыдущее собственное состояние четырёх ресурсов сохранено в
`assetgen_output/creatures/motion_v5_pre_publish/` с SHA всех 55 файлов.

После публикации 116 native UI PNG точно совпали с рассмотренными кандидатами.
Общая матрица 56 существ и 5 скинов содержит 5290 кадров: 5081 RGBA exact,
209 ожидаемых изменений только у четырёх принятых видов. Все кадры остальных
57 ресурсов совпадают. Проверены 936 неизменённых FP-кадров, 1872 GPU-буфера
рук, шесть retained-cache cases и 256 actual loads в 128 legacy/v2 entity
сценариях. Это не повторная проверка whole-world/user save. Ledger подтверждает
актуальность всех 61 ресурсов; все их r6_state остаются pending. Проходят
13 targeted Release tests и static Windows EXE verification.

Текущий EXE: `929205bada51d2c6862ad3989471ccc79a70c91104c099894de497b0279bb5f1`.
Доказательства и границы проверки:
[Motion v5 README](envelopes/motion_v5/README.md),
`publication_audit.json`, `invariant_audit.json`, `ledger_after_publish.json`
и `production/{body_parity,fp_audit,audit_production_final}.json` в той же папке.
Новая геометрия также проходит Khronos validation и dense contact/transition
проверки; эти результаты не подменяют прямое художественное решение.

Следующие обязательные работы: B/R/sprite envelopes с процедурными смещениями
и экипировкой; world frustum sweeps и стоимость отсечения; close-QA camera/pose
synchronization; stance/stride/terrain/contacts/habitats/control/pack/UI;
движение orc и полёт puffin; FP framing/attack/walls/presets; чистый Release
package без .orig и matched CPU/GPU p95/memory. Последний широкий CTest
остаётся незелёным: 50 passed, 141 unbuilt, один bounds audit Failed из 192.
Сейчас зарегистрировано 194 теста; новая широкая проверка ещё не выполнена.
Полная R6 pending, цель active; исходный объём 56+5 и три backend сохранён.

## R6: отсечение B/R/sprite по текущей геометрии — 12 ноября

Закрыта функциональная часть B/R/sprite frustum fix: проверяются реальные
requests текущей позы, включая одежду и предметы в обеих руках. Для glTF
сохранён аналитический envelope. Позу подготавливаем один раз; диапазон
невидимого существа удаляется отдельно. Проверка завершается при первом
потенциально видимом request. Физические габариты и ресурсы не менялись.
Стоимость подготовки offscreen поз и экипировки ещё требует измерения.

Текущий Release EXE: `26f7f77d36a7ab28661db03ae468c309ae1f08ca9303ad59d4189bf41758b2aa`.
Native probe: 26 ресурсов, 44 варианта экипировки, 1308 основных кадров
(504 с экипировкой), 41 подтверждённый GPU случай восстановленной видимости.
После оптимизации 1349 probe PNG совпали RGBA exact. Полная sampled preview
матрица 5290 кадров и 936 FP кадров также неизменна; проверены 1872 GPU draws
рук и 128 entity cases / 256 actual restores. Проходят 14 targeted Release
тестов. Три коротких world/process/spawn проверки bot, rigid walker и sprite
успешны; их конечные кадры и диагностический лист просмотрены непосредственно.
Первый запуск с human не прошёл world spawn и сохранён как неуспешный.

Доказательства, точные SHA и ограничения: [posed culling](envelopes/posed_culling_v1/README.md),
`regression_fast.json`, `fast_clip/audit.json`, `visual_review.json`.
Короткие stationary world subsets не закрывают camera sweeps, terrain или
performance. Проверка исходных 17 bounds discrepancies повторена и остаётся
**Failed**. Зарегистрировано 195 CTest; новая полная проверка не выполнена.
Исторический результат 192 тестов: 50 passed, 141 unbuilt, 1 Failed.

Далее: классифицировать 17 расхождений (анатомическая масса / appendages /
физический proxy), исправлять доказанные потери анатомии через собственные
recipes и UV rebake; измерить matched CPU/GPU p95 и память отсечения.
Затем world camera sweeps, stance/stride/contacts/terrain/habitats/control,
pack/UI/reload, FP framing/attack/walls/presets и чистый Release package.
Не менять collider и tolerance ради зелёного теста. Все 61 r6_state pending;
цель active, полный объём 56 существ + 5 скинов / три backend сохранён.

## R6: форма и движение разделены; penguin proposal — 12 ноября

Сгенерированы и непосредственно просмотрены три листа front/side/top для всех
17 bounds discrepancies. Новый read-only анализатор сохраняет bind XYZ,
площади opaque triangle проекций, 65 фаз основных состояний и skin influences
вершин каждого экстремума. [Разбор 17 видов](envelopes/anatomy_shapes_v1/README.md)
задаёт конкретные art/motion действия, не снимая ни один Failed gate.

Уточнено прежнее предположение о барсуке: bind width own 0,516 против original
0,532; 0,824 относится к original Walk union, а не толщине неподвижного корпуса.
Расширять тело по этой цифре нельзя. Сопоставимые AABB, в свою очередь, могут
скрывать различия формы: у penguin, lobster, oerkki и seahorse требуется
локализация недостатка объёма по native сравнению.
Коллизии и tolerance не менялись; 17 Failed остаются открытыми.

Изолированные собственные penguin proposals проходят отдельный native review.
V1 отклонён: выступающее плоское брюшко. V2 добавляет умеренную полноту при
неизменной задней грани trunk; UV refit/rebake, nodes/skin/IBM/weights/17 animation
sampler arrays и physical definition сохранены. 35 QA glTF проходят Khronos
validation без ошибок/предупреждений; original/candidate native comparison
содержит138 main PNG, max bounds error <0,000007839 блока. Прямо просмотрены
три original / own-before / v2 листа и два world stills. Подробнее:
[penguin v2](envelopes/penguin_belly_v2/README.md), visual_review и rig_uv_native_audit.
Production recipe/models/bin не изменены: v2 promising_unapproved.

Проверка выявила конкретный QA-пробел: primary-state Swim управляет только
обычными preview frames4..11; реальный водный кадр показывает вертикальную
позу. Transition probe для amphibious penguin проверил только сухие idle/walk
условия:36 cases/396 кадров,352 supported,minY=0,envelope covered. Повтор того
же probe в water flight не является новым fluid coverage. Help runner уточнён.

Следующее действие: amphibious fluid-transition/native-world Swim/Tread и
full-turn/contact проверка v2; затем normal review/publish при положительном
решении. Продолжить lobster/oerkki/seahorse/whale shape и orc/puffin/badger/
treeman motion по triage. Matched CPU/GPU p95/memory, camera sweeps, terrain,
pack/UI/FP/package и все остальные §13 gates остаются обязательны. Цель active,
все61 R6 pending; текущий production EXE по-прежнему26f7f77d…58b2aa.

## R6: penguin v2 опубликован; исправлена остановка над водой — 12 ноября

Умеренный v2 прошёл прямой R5 revision review и normal build/validate/publish.
Trunk depth5→6 и origin Z−1→−2 возвращают полноту брюшка, оставляя заднюю
грань и крепления крыльев. Собственная texture rebaked с новым UV allocation.
Nodes/skin/IBM/weights/physical definition и17 animation arrays точны к own
состоянию до правки. V1 с плоской выступающей панелью остаётся отклонённым.
Own snapshot16 файлов сохранён; повторное построение v2 из него даёт точные
BIN/PNG и семантически точный glTF. Это решение ассистента, не user approval.

[Доказательства публикации](envelopes/penguin_belly_v2/README.md): revision_review,
publication_audit, reproduction_audit и ledger_after_publish. 61/61 ledger fresh.
Полная production матрица5290:5201 RGBA exact к26f7;89 ожидаемых изменений
только penguin. Все89 рассмотренных candidate PNG достигли production точно.
936 FP PNG неизменны,1872 authored GPU arm draws/6 retained cases проверены.
128 entity cases/256 actual restores проходят; это не повтор всего user save.

Новый explicit amphibious probe:10 направленных land/free-fluid/bottom
переходов×16 outgoing phases×21 шага=3360 CPU кадров,1680 supported,
minY≈−5,59e−8; envelope покрывает все. Старый dry-only probe не переписан.
World QA input driver задаёт обычные паузы и движение по±X; факты/позу/позицию
не форсирует. В production без QA override:438 записей/17,325 sim секунд,
1,859 блока настоящего XZ travel в обе стороны, реальные Swim/Idle/Jump/Fall.
Полный Swim turntable, actual world Tread и wet/dry bank route ещё pending.

Обнаружен и исправлен product bug: у поверхности NPC замораживался на
отсутствующем sky slice4 после всего2,219 sim секунд при20 wall секундах.
Новый guard использует общий с opaque ray tracing known-air contract:
completed column, storage identity, no pending save, выше resident/disk data.
Неизвестные holes/saved towers/emerging columns не объявлены воздухом; NPC
ниже MinHeight не получили нового разрешения. В production345 шага,
которые отвергал старый guard, выполнены; frozen/solid-overlap записей нет.
Частое Jump/Fall у поверхности и цена metadata queries ещё требуют R6 review.

Текущий Release EXE `a0872e87a1551dbe7ac0cff1346b58176743d18eba13817cb7779473415df68f`.
15 targeted Release tests PASS после публикации. Зарегистрировано196 CTest;
свежий bounds audit остаётся Failed17. Широкий CTest не объявлен зелёным.
Четыре текущих production world subsets B/R/G/sprite проверены и просмотрены:
bot,rigid_demo_walker,penguin,fire_spirit; world_backend_audit.json.
Они не заменяют полные маршруты/control/performance.

Следующая очередь: lobster claws/legs и oerkki torso/limbs, seahorse body/crest,
whale profile; затем orc/puffin/badger/treeman motion по17-species triage.
Matched culling/readiness CPU/GPU p95/memory, world camera sweeps, stance/
stride/terrain/contacts/habitats/control/UI-pack, FP framing/attack/walls/presets
и clean Release package остаются обязательными. Все61 R6 pending, цель active;
полный объём56+5/три backend сохранён. Старые fluid_v1/v2 failed и fluid_v3
недостаточный route сохранены и явно описаны; они не засчитаны как приёмка.


## Обновление: lobster v3 опубликован, R6 остаётся открытой

Подробный протокол и листы: [lobster v3](envelopes/lobster_anatomy_v3/README.md).
После прямого просмотра original / own-before / candidate восстановлены объём
панциря, размах ног и длина клешней с собственными геометрией и surface texture.
V1/v2 отклонены; их материалы и failed verdicts сохранены.

Сохранены51 сустав, иерархия,90 угловых каналов и времена ключей. Изменены18
bind-позиций, inverse binds пересчитаны;16 translation tracks стоп перенастроены.
На257 фазах расхождение высоты стоп с прежней собственной траекторией не более
0,000021336 блока, root Y idle/walk/punch сохранён. Отрицательный контроль
обнаруживает ошибку0,075 блока. Это проверенный локальный refit, не универсальная
гарантия retargeting. Рецепт не читает original; повторная генерация из собственного
pre-publish snapshot дала тот же payload. Definition/physics/state map неизменны.

Normal build/validate/review/publish завершён. Полная матрица5290 PNG:
5201 точное совпадение с предыдущей penguin production,89 ожидаемых изменений
только lobster; все89 совпали с рассмотренным кандидатом.936 FP PNG неизменны,
1872 GPU arm draws/6 retained cases и128 entity cases/256 restores проходят.
Все61 записи R5 ledger свежие. Khronos:35 QA glTF без errors/warnings/infos.

Отдельный20-секундный production world run без QA model override проверил
реальный bottom Tread:834 samples/18,311 sim секунд,4,775 блока XZ пути,
движение в обе стороны и паузы; frozen/solid-overlap отсутствуют. Первоначальный
run с ожиданием Swim завершился failed postcheck и не засчитан: benthic модель
выбирает Tread по фактам. Новый runner явно задаёт ожидаемое Swim либо Tread,
а world audit проверяет фактические state/fluid/bottom/speed/positions.
Текстура128×128, BIN134600 bytes,672 triangles не выросли; performance этим
не принят. Release EXE остаётся a0872e87a1551dbe7ac0cff1346b58176743d18eba13817cb7779473415df68f.
После публикации15 targeted Release tests PASS;196 зарегистрировано, полный
CTest не зелёный: свежий bounds audit всё ещё FAIL17. Collider/tolerance не менялись.

Дальнейший порядок: oerkki torso/limbs, seahorse body/crest, whale profile;
затем orc/puffin/badger/treeman motion. Для lobster остаются выразительность
Walk, terrain/stride/contacts и остальные R6 gates. Для всех56+5 обязательны
world/camera/habitat/control/UI-pack, matched CPU/GPU p95 и memory, FP framing/
attack/walls/presets, clean Release package. Все61 R6 pending, цель active.
Исторические anatomy17 metrics сохранены: own15 live и2 own snapshots
(penguin/lobster) заново сверены SHA в source_freshness_after_lobster.json.


## Обновление после lobster: oerkki anatomy v2 опубликован R5

[Протокол, листы и ограничения](envelopes/oerkki_anatomy_v2/README.md). В собственном рецепте восстановлена
толщина core/chest, рук/ног и форма головы. Ноги не расширены: собственная
постановка уже шире исходной. На общей native камере Idle area к original:
front81,4%→92,6%, side77,5%→87,4%, oblique84,9%→92,2%. Это projection area,
не volume/art gate. Прямо просмотрены три comparison sheets, три full-turn
и candidate/production world stills. Original near-black texture ограничивает
сравнение поверхности и не доказывает matching original motion.

Суставы/иерархия/IBM/JOINTS/WEIGHTS/definition и62 sampler arrays exact.
300 triangles, atlas128×128, BIN149712 bytes как прежде. Собственная texture
перезапечена под новые UV. Прежние48 constant-UV triangles не выросли;
однотексельные поверхности не объявлены новой регрессией.2156 CPU transition
frames/2112 supported, minY0, envelope covered. Khronos35 zero errors/warnings/infos.

Normal build/validate/review/publish и независимый publication audit PASS.
5290 PNG:5201 exact к предыдущей lobster production,89 изменённых только
 oerkki; все89 совпали с рассмотренным кандидатом.936 FP exact,1872 GPU arm
 draws/6 cache cases,128 entity cases/256 restores PASS. Все61 R5 ledger fresh.
Own16-file pre-publish snapshot сохранён, regeneration semantic/byte exact.
Release EXE a0872e87a1551dbe7ac0cff1346b58176743d18eba13817cb7779473415df68f
не менялся.15 targeted Release tests PASS; fresh bounds audit FAIL17,
полный196 CTest не заявлен зелёным, collider/tolerance неизменны.

Обобщены anatomy invariant/full-turn/publication research tools. FP verifier
сохраняет обязательный historical legacy renderer reference; exact comparison
с предыдущим authored renderer выполняется отдельно. Промежуточные ошибки
fractional texels/неверный legacy-reference/missing save report описаны в протоколе
и не выданы за successful acceptance.

Далее seahorse body/crest и whale profile, затем motion orc/puffin/badger/treeman.
Oerkki R6 terrain/stride/AI/attack/lighting/GPU fades ещё pending. Общие world/
camera/control/UI-pack/user-save/performance/FP framing/walls/presets/package
и все56+5/три backend остаются в объёме. Все61 R6 pending, цель active.
Anatomy17 metrics исторические: live own14 +3 own snapshots (penguin/lobster/
oerkki) сверены SHA в source_freshness_after_oerkki.json; metrics не переписаны.


## Обновление после oerkki: seahorse R5 и отдельный free-swim route

[Актуальный протокол](envelopes/seahorse_anatomy_v1/README.md); текущие доказательства в его `release_swim/`.
В собственном trunk depth5→8, передний нижний край Z−1→−4. Прежние верхнее
сечение шеи и задний край сохранены по exported vertices. Голова/гребень/плавники/
хвост неизменны:17 joints, hierarchy/IBM/weights и42 sampler arrays exact,
240 triangles, atlas64×64, BIN49316 bytes как прежде. UV refit и собственный
surface rebake; прежние80 constant-UV triangles не выросли. Side GPU area
к original82,0%→89,7%, oblique79,4%→84,5%; это projection, не volume/art gate.
Прямой просмотр comparative/full-turn/world подтверждён SHA и точными кадрами.

Горизонтальный-only water_pause закономерно привёл к bottom Tread; failed
postcheck сохранён и не засчитан как free Swim. Новый QA water_swim_pause
подаёт обычный wantJump ниже начальной submerged Y−0.15, не назначая position,
facts, state или pose. Product motor/state/definition не менялись. Candidate
1071samples/18,840sim seconds/5,101XZ и production без override701samples/
18,901sim seconds/4,836XZ подтверждают реальный moving Swim в обе стороны,
без frozen/solid-overlap. Это input route, не автономный hover/AI gate.
В паузах вертикальное движение продолжается; aquatic speed в product facts
измеряется3D delta, поэтому Swim при нулевом XZ request допустим.
Native CPU transition probe для seahorse имеет4 free-fluid cases/44 frames,
supported0, minY verdict отсутствует; floor acceptance этим не заявлена.

После изменения QA input driver выполнена требуемая Release сборка. Текущий EXE:
235ce58dd65546f38164020295ff403bf48c2816b14bb16cd96228faee030483.
Original/own-before/candidate native сняты заново на одном новом EXE/profile;
89 candidate PNG и6 листов совпали с pre-build материалом. Normal publish и
независимый publication audit PASS. Полная5290 матрица:5207 exact к предыдущей
 oerkki production,83 изменения только seahorse; смена EXE явно разрешена в
сравнении. Все89 candidate→production exact;936 FP exact,1872 GPU arm draws/
6 cache cases и128 entity cases/256 restores PASS. Все61 R5 ledger fresh.
Khronos35 QA glTF без errors/warnings/infos. Own16-file snapshot и regeneration
exact сохранены.15 targeted Release tests PASS после build и publication;
fresh bounds audit FAIL17.196 CTest зарегистрировано, весь CTest не зелёный.

Далее whale profile, затем motion orc/puffin/badger/treeman. Для seahorse ещё
открыты полный исходный tail/neck range и autonomous habitat/depth/Idle behavior.
Обязательны R6 world/terrain/stride/GPU fade/camera/light/control/UI-pack,
user/world compatibility, matched CPU/GPU p95/memory, FP framing/attack/walls/
presets и clean package для всего56+5/трёх backend. Полная visual matrix на новом
EXE не превращает прежние world subsets B/R/G/sprite в новые route proofs.
Все61 R6 pending, цель active. Anatomy17 metrics исторические: live own13 и
4 own snapshots заново сверены в source_freshness_after_seahorse.json.


## Обновление после seahorse: whale v2, локальный hull/head profile R5

[Протокол, aligned comparison и ограничения](envelopes/whale_anatomy_v2/README.md). В собственном рецепте
восстановлен вертикальный профиль: trunk11→13, head10→12, rostrum6→7 texels,
при прежних spine/pivots и согласованных сечениях. Bind height3.0→3.375 блока
(reference3.3137), physics неизменна. V1 частично прятал blowhole и отклонён;
v2 поднимает cap0.5 texel, сохраняет exposed cap и embedded base. Независимый
negative control на v1 отказал.14 joints/hierarchy/IBM/weights и36 samplers
(72 input/output arrays) точны. Шесть стыков проверены bind+129фаз/clip, все3
clips, max section error8.882e−16; это не вся watertight/intersection proof.
Own UV refit и surface rebake;192 triangles, atlas128×128, BIN41168 bytes прежние.

Выявлена несовместимость raw comparison: reference направлен противоположно
own и longitudinal centreZ1.38979. Дополнительная research-only копия rigid
aligned (yaw180/rootZ−1.38979), без изменений source original/BIN/PNG/IBM/animations,
не используется generator/publish. Aligned original/own-before/candidate сняты
заново и прямо просмотрены вместе с raw sheets/full-turn/world. Aligned Idle
area к reference front91.7→103.2%, side88.0→99.3%, oblique96.3→103.4%; projection
area не закрывает width/mass/rig artistic gate. Width/flipper/fluke и полная
исходная expressiveness остаются открытыми до R6.

Normal build/validate/review/publish и publication audit PASS. Полная5290 GPU
матрица:5201 exact к предыдущей seahorse production,89 изменений только whale,
все89 candidate→production exact.936 FP exact,1872 authored GPU arm draws/
6 cache cases;128 entity cases/256 restores PASS. Все61 R5 ledger fresh.
Khronos35 zero errors/warnings/infos. Собственный16-file pre-publish snapshot
и semantic/byte exact regeneration сохранены. Normal production actual Swim
без model override:758samples/19.331sim seconds/4.965XZ, движение в обе стороны,
без frozen/solid-overlap. Это ordinary input route, не autonomous hover/AI/perf.
Native100 transition cases/1100CPU frames/1056supported, minY≈2.15e−6,
envelope covered. Все11 рассмотренных v2 visual artifacts имеют SHA.

EXE остаётся235ce58dd65546f38164020295ff403bf48c2816b14bb16cd96228faee030483;
C++/CMake не менялись, data-only build не требовал desktop rebuild.15 targeted
Release tests PASS после публикации, fresh bounds audit FAIL17.196 CTest
зарегистрировано, весь CTest не зелёный; collider/tolerance не менялись.

Следующая очередь: motion orc/puffin/badger/treeman; whale girth/fin profile
и полная исходная motion fidelity также остаются обязательными. Затем/параллельно
по независимым gates: matched CPU/GPU p95/memory, world/terrain/contacts/camera/
light/habitat/GPU fades/control/UI-pack, user/world compatibility, FP framing/
attack/walls/presets, clean package. Все56+5/три backend в объёме, все61 R6 pending,
цель active. Anatomy17 metrics исторические:12 own live+5 own snapshots иoriginal17
сверены в source_freshness_after_whale.json. Legacy field
animation_sampler_arrays_exact считает samplers; обе accessor arrays у каждого
проверяются, а численное число arrays вдвое больше.


## Обновление: motion semantics и Orc slow sway R5

[Диагностика четырёх видов](envelopes/motion_semantics_v2/README.md) изменила план.
Reference puffin Walk/Run используют fly, own — отдельный walk. Исторический
Walk width0.225 против0.504 сравнивал разные действия. Fly↔Fly own width1.013
против reference0.504; blind wing enlargement отменяется. Требуются flight
posture/folding/cadence и отдельный ground gait. Reference Fly period0.08876s,
own0.240s при unit travel: это отдельная temporal задача, не повод масштабировать mesh.

У reference Orc/badger/treeman100% weights всех340/644/842 vertices принадлежит
одному joint, остальные0. BIN byte exact к b35f00a75, JSON только CRLF/LF.
Большие unions отражают whole-mesh sway. Сохранять собственные articulated rigs;
проверять amplitude вместе с cadence. Reference walk periods2.4/2.5/3.0s,
прежние own0.6355/0.3500/1.0784s: усиление roll на каждом шаге дало бы jitter.

[Orc sway v1](envelopes/orc_sway_v1/README.md) опубликован R5:4 strides/slow sway Walk,
3 Run, body roll20/24°. Новые sway periods2.5421/2.6212s при прежней локальной
частоте/кинематике конечностей.18 joints, geometry/UV/indices/IBM/weights/PNG/
definition точны;22 Idle/Action samplers и30 native envelope PNG exact.
Equal-elapsed local rotation max error0.00007156°, root XZ точны, root Y заново
выпечен full skinned mesh, C0 seams проверены. World-space траектории меняются
из-за parent sway/lift; foot sliding/stance/terrain не приняты.

2156 native transition frames/2112supported minY0, envelope covered. Native168
original/current PNG согласованы с offline; Khronos35 zero errors/warnings/infos.
Normal pipeline и publication audit PASS:5290 PNG,5234 exact,56 изменений только
Orc; все89 candidate→production exact.936 FP exact,1872 GPU arm draws/6 cache
cases,128 entity cases/256 restores PASS. Все61 R5 ledger fresh. В metadata
разрешён только явно заданный Orc Walk4× time_seconds/primary_period_seconds
для20 matrix frames, остальные pose/state/owner/backend/view metadata exact;
неверный2× factor отвергнут. Failed B-only world gear fixture для Orc/G не принят.
Own16-file snapshot и regeneration semantic/byte exact сохранены.

360 triangles/atlas128×128 прежние, но animation BIN вырос131400→313200 bytes.
Следующий обязательный шаг **matched performance/memory/load/pose** до дальнейшего
масштабирования multi-rate подхода. В SampleNodeTrs и SampleGltfRootTransform
есть линейный key lookup; измерить before/after на Release, затем проверить
оптимизацию с сохранением результатов поз/пикселей. Далее отдельные badger/
treeman multi-rate proposals и puffin Fly/ground review. Size/FPS overlays
не являются perf verdict. Остальные56+5/три backend и все прежние R6 gates
(world/control/UI-pack/compatibility/FP/package) в полном объёме.

Release EXE235ce58dd65546f38164020295ff403bf48c2816b14bb16cd96228faee030483
прежний, C++/CMake не менялись.15 targeted Release tests PASS, fresh bounds audit
FAIL17,196 CTest зарегистрировано; весь CTest не зелёный. Все61 R6 pending,
цель active. Anatomy17 metrics исторические: own11 live+6 snapshots сверены SHA;
motion diagnosis4 использует3 live own и immutable pre-sway Orc snapshot.


### 2026-11-12 — CPU key sampling и точный native regression

[Измерения, provenance и ограничения](gltf-sampling-perf-v1/README.md).
Это следующий выполненный этап после Orc sway; прежний EXE235ce5… выше относится
к истории публикации ресурсов. Новый Release EXE
a8d94126dad8067857a3332b3e929f544b43d380bee48f3ab138620a78a61374.

В обоих G samplers общий key interval lookup: прежний scan до128 ключей,
lower_bound для длинных tracks, exact-key предыдущая пара сохранена.
25 017 дифференциальных interval checks, bit-signatures реальных Orc palettes/
root transforms exact. На pinned paired CPU стенде новый Orc clip с новым
lookup дешевле прежнего lookup на13–26%; относительно короткого pre-sway clip
остаётся +3.4…7.7% p50 парных batch ratios. Это CPU sampler subset,
**не world frame/creature-pass CPU/GPU performance acceptance**. Первый noisy
межпроцессный ABBA эксперимент сохранён и не принят как perf verdict.

Цена animation buffer не исчезла: BIN131400→313200 B, учтённые CPU vectors/
heap strings158356→398400 B (+240044 B), warm loader p50 2.4363→3.8618ms.
Total desktop RSS, cold load и GPU в этот scope не входят.
Не переносить длинные multi-rate clips массово до family memory/load budgets.

Desktop rebuilt Release требуемой командой;15 targeted tests PASS.
Новый native production56+5:5290/5290 body RGBA exact против предыдущего
Release,936/936 FP exact; shader/resource/pose metadata unchanged.
Отдельный FP legacy/native audit PASS (1872 GPU draws/6 retained cases),
save128 cases/256 loads PASS.61 R5 ledger fresh. Native лист до/после
просмотрен; силуэты/текстуры в выбранных B/R/G видах одинаковы.
Fresh bounds audit **FAIL17**; весь CTest не объявлен зелёным.

Далее: matched actual-world CPU/GPU/frame timings, memory/load и noise controls;
затем отдельные badger/treeman multi-rate proposals и puffin Fly/ground review.
Orc stance/stride/sliding/terrain, все world/control/UI-pack/compatibility/FP/
Android/clean-package gates обязательны. Все61 R6 pending, цель active.


### 2026-11-12 — фактический world pass, performance gate открыт

[Полный протокол, raw frames и визуальный лист](world-pass-perf-v1/README.md).
Добавлена opt-in QA телеметрия CPU UpdatePose/pass, каждого window frame и
неблокирующих GPU timestamps вокруг Queue.Flush. При выключенном flag нет
query/allocation/file writes. Desktop Release81283a1c5b148e659eaa66e9f4be9e14cbf2730ad70b3e50a1f6a59535076018;
временная linear baseline source заменялась только на время сборки и восстановлена.

8 actual-world runs World_254: own pre-sway/current Orc × linear/indexed, два
обратных порядка. QA input driver land_walk_pause использует обычный motor,
не присваивает facts/позиции/clips. В двух изолированных копиях каталога одинаково
выключен natural spawning. Steady5…19sec:2 considered/1 culled/1 drawn,
1 draw call/1 palette upload/1 pose на каждом кадре, Walk/Idle без freeze.
Неуспешные initial missing-file и population-drift пробы сохранены и не приняты.

Current/indexed world pose p50 0.015625ms против current/linear0.01970ms:
ускорение pose подтверждается по направлению. Но против короткого pre-sway/linear
наблюдаемые median-run p95 CPU pass0.12800→0.16285ms (+27.2%), GPU interval
22.100→31.235µs (+41.3%) превышают provisional10% gate. Host CPU median69.9…96.2%,
повтор неизменного условия расходится до1.976× по GPU p95. Причина не отделена
от шума/clock effects; это **не успешная performance приёмка**. Whole-process
RSS442.8…458.5MiB не выделяет цену клипа и не отменяет прежние +240044B storage.

После серии обычный production Release с timing flag выключенным:
5290/5290 body PNG exact и936/936 FP exact против предыдущего a8d941…;
FP1872 GPU draws/6 retained cases, save128/256 PASS.15 targeted Release tests
PASS,61 R5 ledger fresh, fresh bounds audit FAIL17. World before/current лист
просмотрен отдельно; это own до/после slow sway, фазы не выровнены, contact/
sliding и исходная художественная приёмка этим не доказаны.

Следующие действия: отделить whole UpdatePose/fade/cache costs от sampler,
сформировать family memory/load budgets, повторить world CPU/GPU/frame gate
с noise controls и нормализованным светом/погодой. Массовый multi-rate bake
не разрешён этим report. Изолированные badger/treeman proposals и puffin
Fly/ground review можно готовить независимо; world/stance/AI/control/UI-pack/
compatibility/FP/Android/package gates сохраняются. Все61 R6 pending, цель active.


### 2026-11-12 — изолированный badger multi-rate pilot v2

[Протокол и просмотренные листы/GIF](envelopes/badger_sway_v2/README.md).
Это неопубликованный own proposal: canonical badger recipe/model/PNG, staging,
bin и C++ не менялись, Release81283a… прежний. Исходные ресурсы не входят в генерацию.

Slow body roll±20° Walk/±24° Run,7/9 прежних stride cycles на один sway.
Unit-travel sway2.45/2.475s ближе к reference2.5s; собственные strides0.35/0.275s
сохранены.13 joints/360tri, hierarchy/pivots/IBM/weights/geometry/UV/PNG/
definition точны; Idle/Action6 samplers и33 native PNG exact. Non-root rotation
equal-elapsed error≤0.000105°, root XZ/прочие translations exact, C0 seams PASS.
Новая parent rotation/rootY намеренно меняют world-space траектории, не объявлены
побайтно одинаковыми с live.

Первый v1 отклонён по real sampled Run floor gap0.001313>0.001block.
V2 увеличивает только root contact bake до1793/2017 samples; dense2049 phases/
clip дают gap≤0.000786/minY≥−0.000036block. Negative control отвергает v1.
Native2156 transition frames/2112supported minY≈0, envelope covered.
Khronos QA35 zero errors/warnings/infos; это не художественная приёмка.

Новые shared-clock Walk/Run GIF:по129 native phases/state/version, nearest phase
time error<0.010s,25fps5s excerpt. Изучены static same-elapsed strips и threeway
front/side/oblique; own mask/low body сохранены, slow mass sway ближе source.
GIF boundary не является asset seam; preview unit travel не world velocity.
Reference/own-before reuse только с identical EXE/shader/profile/payload SHA.

Ordinary motor world flat route:509 records,299 grounded Walk/210 Idle,
обе стороны, frozen0/blocked0; stance/sliding/terrain/AI не приняты.
Цена пилота: BIN53920→135136B, accounted vector/string heap64508→160988B,
warm loader p50 1.5404→2.2631ms. Paired final128 sampler vs old linear сохраняет
float matrix signatures, но candidate CPU p50 против live всё ещё≈+8…16%;
это не world performance pass. Публикация/массовый bake пока не выполнены.

Далее: сократить lookup/UpdatePose/storage overhead с exact pose parity,
family memory/load budget и controlled world CPU/GPU gate, затем stance/
terrain/turn/AI/action, normal regeneration/review/publication/full regression.
Treeman и puffin остаются отдельными художественными пилотами. Все56+5/три
backend, world/control/UI-pack/compatibility/FP/Android/package в прежнем scope.
61 live R5 fresh; bounds FAIL17 не отменён, все61 R6 pending, цель active.


### 2026-11-12 — точный hint lookup и CPU gate badger в одном world fixture

[Полный протокол и ограничения](gltf-hint-perf-v1/README.md).
Runtime G helper теперь оставляет scan≤16 keys, для остальных проверяет
оценённый bracket и при неудаче делает lower_bound. Равномерность timestamps
не предполагается без проверки; alpha/SLERP, clock, rig/UV/PNG/clip data прежние.
415098 Release interval checks (9 распределений/23 размера) PASS;35 packaged G+
badger v2:142684 palette и173430 root float-bit comparisons exact, все clips/
keys/adjacent times/crossfades.15 targeted Release tests PASS.

Новый Release1507b3e843d54a29e7e8150d04da3d353f870828362ed4371461c87b6a574755.
Full5290 body и936 FP PNG exact против81283a…, metadata/resources/shaders exact,
только EXE change разрешён. Save128/256 PASS, FP1872 draws/6 retained cases.
Сам badger v2:165 PNG и весь2156-frame transition JSON exact между runtime.

Badger paired CPU: после hint residual proposal/old-linear p50 ratio около+3%
без fade и≈−2% с fade вместо прежних+8…16%. Обе модели с hint: proposal всё ещё
дороже live примерно2.3…6.4%. Данные памяти+96480B остаются; не объявлять их
исчезнувшими от ускорения поиска.

Повторена actual-world серия8×40sec: disposable clone, fixed noon/clear weather,
auto weather off, warmup20sec, измерение20…39sec, обычный motor Walk/pause,
одинаковый population2 considered/1 culled/1 drawn/1 upload/1 draw call.
Walk CPU pass median-run p95 live-linear0.12645→proposal-hint0.13125ms (+3.8%);
max candidate/min reference run p951.0803, all-steady1.0589. Предварительная
CPU pass часть≤10% выполнена **в этих повторах этого badger fixture**.
Не confidence bound всех будущих запусков и не all-species/R6 performance pass.

GPU/full-frame остаются unproven: один неизменный live/hint control расходится
по GPU p95 в3.635× и window p95 в2.584×. Whole-process host CPU median32.1…96.6%
включает warmup/startup, не точный steady-frame correlation. Не засчитывать
благоприятный median GPU как пройденный gate и не менять допуск ради прохождения.

Badger v2 остаётся scratch/unpublished;61 live R5 fresh, старый bounds FAIL17
не отменён. Далее GPU/frame noise controls, family memory/load budgets,
world stance/sliding/terrain/turn/AI/action и normal review/publication/regression.
Treeman/puffin и остальные56+5/три backend, control/UI-pack/compatibility/FP/
Android/package сохраняются. Все61 R6 pending, цель active.


### Реальная опора badger: cadence и stance (2026-11-12)

Цель active, все61 R6 pending. Выполнен native world foot-surface probe на
Release7e7f668f…; он opt-in, по умолчанию выключен, не меняет pose/motor.
Первый bottom-only detector пропускал raised claw tips; его записи оставлены
как неудачный observer. V2 наблюдает все фиксированные lower-foot surface
points и проверяет их минимум полным обходом. Actor feet Y — reference plane,
не terrain query; height candidates не равны утверждённым footplant labels.

Обнаружено расхождение unit-travel и обычного motor: у live badger при actual
speed≈0.475 и clip rate≈0.176 реальный stride1.591s вместо preview0.35s.
Ограничение «сохранить прежний elapsed cadence» уточнено: сохранять качество,
rig/phase shape и узнаваемость, но калибровать stride/cadence по actual travel.
Scratch Walk-only stride v3:14 вместо7 шагов на прежний slow-body macroloop,
реальный stride0.795s. Rig/geometry/weights/UV/PNG exact к sway v2;
Run/Idle/Action/head/tail/spine прежние. RootY adaptive1362keys,8193 exported
Walk samples floor0.0000075…0.0004585block, acceptance±0.001 прежний;
Khronos35assets0errors/warnings. Generic generator пока не изменён.

Скольжение не исправлено: foot/body XZ displacement median ratio при height
0.01/0.03/0.05: live1.2266/1.0963/0.9273, v3 1.2099/0.6829/0.8016.
Вывод зависит от состава height candidates; correlated point episodes не
считать независимыми footfalls. Negative drift control реагирует, но это
не footplant acceptance. Лист реальных кадров просмотрен: сильный перекат
корпуса и подъем отдельных лап требуют дальнейшей правки. v2/v3 unpublished.

Следующий порядок: независимо заданные stance/swing curves и semantic anchors;
согласование шага с motor на нескольких скоростях; scratch stance-aware solve
с joint limits/reachability и плавной сменой опоры без разрыва сегментов;
проверка world-space stationarity в заранее заданных опорных окнах; затем
Run, turns/stop/fades/action, terrain slopes/steps и native visual review.
Только после удачного пилота перенос в CreatureGen, family budgets/provenance,
review/publication/regression. RootY clearance не заменяет XZ foot lock.

На default-off Release5290body+936authoredFP PNG exact к1507b3…;
FP1872draws/6cases и save128/256 PASS,15targeted Release tests PASS,
ledger61/61fresh R5. Это не полный R6; bounds FAIL17 остаётся открытым.
Сохраняются GPU/frame noise controls, memory/load, treeman/puffin и остальные
56+5/B/R/G, control/UI-pack/FP/compatibility/Android/package.

[Данные, native лист, источники и следующий этап](badger-stance-v3/README.md).


### Артикуляция опоры без потери upper-body motion (2026-11-12)

Выполнена reach-проверка исходного badger rig: для заранее заданной диагональной
50%-duty горизонтальной опоры нужна разная rootY у двух лап (spread до0.11829
block). Это ограничение выбранного flat-paw contract, не запрет на toe/heel roll
или другой gait. Нельзя компенсировать его растяжением костей.

Подготовлены unpublished rig controls:13→17 joints с дополнительной артикуляцией
стоп, затем17→19 с отдельными fore/rear hip controls. Нулевой17-joint контроль:
bind+257phases/clip vertex error≤2.22e−16; native86/89PNG exact, в3остальных5pixels
изменены — exact RGBA gate НЕ пройден и причина до конца не доказана. Просмотрен
отдельный native stress X±30/Z±20 стоп; skin volume/claw attachments ещё не приняты.

Для19-joint варианта upper-body20° сохранён, hip target8°; все старые rotations,
mesh/UV/weights/PNG exact к17-контролю, все old bind world transforms exact.
На1025фазах верхние world rotations exact, hip target error≤0.000596°;
8193 exported Walk floors0.0000075…0.0004349block (прежний±0.001). Native21
non-Walk PNG exact; Walk лист просмотрен, новые углы ног видны. Khronos35/0/0.
Структура/визуальные controls не означают, что опора решена: IK/footlock ещё нет.
Memory/load/performance19j не приняты. Production resources не заменены.

Принцип плана уточнён: сохранять качество скелета, а не запрещать необходимую
артикуляцию; новые joints/hierarchy допускаются только через bind/mesh/UV/motion
controls, ненулевые deformation tests и native/world review. Следующий этап —
constrained upper/lower/foot solve в заданных опорных окнах, root reach intersection,
limits/pole/swing/release и skin attachment, затем реальные speed/terrain/turn/Run
проверки. Сохранить выразительность upper body независимо от движения таза.

Observer v2 rigid13 не подходит для17/19j: wrapper теперь проверяет rig и отклоняет
такой foot probe до запуска. Расширить наблюдатель на subtree/blended foot до любых
articulated-foot stance выводов. Цель active, все61 R6 pending, весь прежний объём
56+5/B/R/G и остальные R6 gates сохранён.

[Reach, controls, native листы и следующий код](badger-hip-control-v1/README.md).


### Опорный решатель badger: первый world результат (2026-11-12)

Выполнен scratch19-joint constrained Walk bake: заданные stance/swing anchors,
неизменные длины upper/lower, preferred pole и root reach interval; slow upper
body20°/hip8° и source other clips сохранены. Стандартная длина шага выведена
из definition speed/scale, не из подгонки к foot drift. Это ещё не runtime IK.

Первая blended paw привязка дала mesh penetration≈0.059block и отклонена;
допуск±0.001 не менялся. V2 держит целую named paw и когти rigid на foot joint:
geometry/UV/PNG и hierarchy прежние19j, weights вне лап exact.2049exported
poses:0upper/paw OBB разъединений, длины exact, support XZ error≤0.000404block;
8193floor samples≤0.000751block с sub-mm measured lift. Joint foot до102.84°,
knee swing128.51° — limits/силуэт/heel-toe/four-beat art ещё не приняты. BIN
373432B против150616B hip control: memory/load/performance gate открыт.

Обновлён QA observer v3: lower-foot subtree, skin bindings, coincident points
разных weights сохранены. Независимо сверены реальные submitted trajectories
на20s ordinary-motor flat-world flights live13/hip19/IK19. Заранее заданные inner
stance windows дают IK median foot/body displacement ratio0.01931 и drift
0.00272block при actual0.475block/s; stance bottom points0.000298…0.000752
над actor-feet plane. Hip без IK ratio1.1964. Live ratio0.07875 здесь сопровождается
median height0.0591block: нельзя выдавать невысокий drift воздушных точек за
хорошую опору. Height-only candidates отдельно тоже улучшились. Negative
control +2×actor drift:0.01931→1.98069. Это один fixture/CPU submitted skin,
не GPU readback, terrain query, все скорости или art/R6 acceptance.

Native sheets просмотрены,21non-Walk PNG exact к hip source;29ordinary prototype
PNG exact между runtime. Release30714e… default-off production5290body/936FP
RGBA exact, FP1872draws/6cases, save128/256 PASS;61R5 fresh. Неверный первый
catalog run исключён и сохранён; wrapper теперь отклоняет неизвестные IDs
до flight. Production модели не заменены, generic generator не изменён.

Дальше: opt-in distance-synchronized support gait clock, несколько скоростей/
паузы/retained fades, turns/terrain/slopes/steps/Run/action, original/current
shared-scale art review и costs. При rate0.176 nominal gait=0.484world/s,
actor0.475; остаток≈1.9% согласуется с existing travelScale floor0.22 (после
0.04deadzone). Не переносить floor бездумно на новый support gait и не менять
legacy playback всем существам сразу. После пилота — shared family support
contract/solver в CreatureGen, source/reviews и cow B/badger G/rigid walker R.
Остальные56+5/B/R/G, bounds17, FP/UI-pack/control/compatibility/Android/package
сохранены. Цель active, все61R6 pending; вариант не опубликован.

[Solver, отвергнутый вариант, native/world листы и регрессия](badger-stance-ik-v2/README.md).


## Текущая ограниченная цель завершена —12ноября2026

По новому указанию пользователя выполнен90-minute выпуск для всех56существ+
5скинов. Все61 опубликованы в models/bin, имеют свежую provenance и точно
воспроизводятся генератором; опубликованные5290native PNG совпали с проверенным
кандидатом.35glTF без format errors/warnings,5targeted C++ checks PASS, FP/save
smoke PASS, cow B/badger G/rigid walker R прошли ordinary world Walk/pause.
Python46/49 PASS;3 старых findings повторены на backup recipe и вынесены в
TEST-01. Полная R6 и прочие остатки больше не являются частью этой текущей цели.

[Итог и61-resource ledger](../creature-timebox-2026-11-12/README.md). [Исключённый backlog](../../CREATURE_RELEASE_BACKLOG_2026-11-12_RU.md).
