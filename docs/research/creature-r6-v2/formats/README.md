# Независимая проверка форматов B/R и исправление заголовка B

Первый read-only проход `audit_creature_bone_rigid_formats.py` нашёл ровно
17 ошибок: все B geometry объявляли `format_version: 1.12.0`, сохраняя
legacy keyed `geometry.<id>`, `texturewidth`/`textureheight`, box-UV и
`bind_pose_rotation`. По официальной схеме это структура 1.8.0:
[schema geometry 1.8.0](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/schemasreference/schemas/minecraftschema_geometry_1.8.0?view=minecraft-bedrock-stable),
[visual reference 1.8.0](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/visualreference/geometry.v1.8.0?view=minecraft-bedrock-stable).
Современная [структура 1.12.0](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/visualreference/geometry.v1.12.0?view=minecraft-bedrock-stable)
использует `minecraft:geometry[]` и `description`; простая смена заголовка
на 1.12 не переводит legacy payload в новую схему.

В `export_bone.py` исправлено только объявление версии на 1.8.0. Это не
изменение анатомии и не миграция скелета. `payload_change.json` независимо
сравнивает полные inventories 56+5 с архивом до исправления: только 17
geometry headers отличаются; после исключения `format_version` parsed JSON
полностью идентичны. Все остальные runtime bytes, включая PNG, skin.json,
creature.json, R parts, G meshes/buffers/animation, совпали точно.

Проверка B/R читает реальные активные render paths без импорта генератора:
путь и geometry ID, уникальные bone IDs, parents/cycles, конечные pivots и
rotations, размеры и атласы, UV bounds/overlap, R parts/pivots/stems,
billboard и совместимость пяти скинов с base atlas. Это независимая проверка
используемого проектом подмножества, **не полная Bedrock-сертификация** и
не художественная приёмка. `br_before.json`: 26 resources, 17 ошибок только
версии; `br_candidate.json`: 26 resources, ошибок 0, UV overlap 0.

`repro_summary.json`: две независимые output-сборки всех 61 ресурсов
совпали побайтно, включая manifests; неожиданных Python build reads нет.
Это один host/toolchain, не чистые разные ОС. Header-only изменения явно
перечислены у 17 ресурсов; их geometry JSON bytes **не** объявляются прежними.
У всех обновлена привязка provenance к генератору, у пяти скинов — SHA
базовой geometry. Полные метаданные/точные R5 reviews поэтому обновляются
для всех 61; функциональные параметры, решения R5 и состояние R6 не меняются.

`candidate_parity.json`: одним Release EXE с одинаковыми shaders получены
ровно те же RGBA у всех 1630 legacy и 3660 matrix PNG — **5290/5290**.
Matrix включает 56 species и 5 skins, 10 ракурсов и 3 позы, alpha coverage.
Реальный world actor этого запуска — только cow. Его свежий world кадр и
полный текущий лист прямо просмотрены: прежние силуэт, пятна, рога, морда
и две фазы ног сохранены. Прежние world proofs остальных видов наследуются
по неизменённым runtime данным, не выдаются за повторный world-тест всех.

`review.json` явно сохраняет предыдущие R5 решения после проверки каждого
файла и pixel parity, обновляет bundle hashes и UI evidence. Прямой reviewer
Codex, не подпись пользователя. Полная R6 остаётся pending: все состояния,
contact/stride/transitions/travelScale, habitat routes, resolved appearance,
equipment/FP/save/cache, чистый build/package и performance отдельно.

Публикация завершена для 56+5. `br_published.json` и `br_runtime.json`
повторяют независимый аудит на `models` и обычном `bin/models`: 26/26,
ошибок 0. `ledger.json`: exact reviews/current source contexts/file records/
published+bin matches 61/61, manifest 56+5, runtime mirror идентичен.
`production_parity.json`: после публикации обычный каталог без QA override
повторил все 5290/5290 PNG кандидата точно. `khronos_published/summary.json`:
35 G, errors/warnings/infos 0, реальные PNG проверены.

36 Python fidelity, 12 quality и 9 выбранных Release C++ tests проходят.
Предыдущие matrix/repro ledgers сохраняют исторические hashes до изменения
exporter header; текущий полный source-context ledger находится здесь.

Этот этап теперь исторический до явного назначения B profiles. После
следующего изменения generator source текущий полный ledger/review и
runtime EXE находятся в `../bone-motion/`; runtime payload форматов при
этом побайтно прежний. См. `../bone-motion/README.md` и `ledger.json` там.
