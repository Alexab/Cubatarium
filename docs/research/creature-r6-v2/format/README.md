# R6: переносимый glTF без замечаний валидатора

Изменение формата всех 35 G ресурсов сохраняет принятый дизайн и animation
capabilities. Полная цель R6 пока не достигнута.

## Что исправлено

Khronos Validator находил 6030 ACCESSOR_JOINTS_USED_ZERO_WEIGHT: в неактивном
слоте оставался не-нулевой индекс. Exporter теперь ставит индекс 0 ровно
при нулевом весе, без изменения weights и активных joint indices.
Не перепаковывает/не переставляет влияния и не меняет позы.

Добавлены стандартные images/textures/samplers/baseColorTexture. URI —
textures/body.png, тот же собственный atlas, который берёт runtime.
Nearest sampling и clamp-to-edge сохраняют pixel-art характер;
alphaMode MASK с cutoff 0.05 соответствует существующему shader discard.
Metallic=0/Roughness=1 даёт обычный albedo материал в стандартных viewers.
Имена материалов сохранены: C++ loader по-прежнему разрешает тот же stem.

Исходная принадлежность вершины кубу в seam-tests теперь определяется по
непересекающимся UV островам. Нулевой skin slot не должен служить скрытым
хранилищем authoring metadata. Закрытие реальных skinned стыков продолжает
проверяться без ослабления численного порога.

## Независимая проверка

Официальный gltf-validator 2.0.0-dev.3.10:
https://github.com/KhronosGroup/glTF-Validator/tree/main/node
Pinned npm lock находится в ../khronos/tool-package-lock.json; команда
установки приведена в tools/research/validate_creature_gltf.cjs.
Результаты кандидатов: ../khronos_format/summary.json и пофайловые reports.
35/35: ноль errors, ноль warnings, ноль infos. Body PNG теперь действительно
считаны и проверены как glTF images, hashes включены в reports summary.
Никакие issue codes не отключались; severity overrides не использовались.

payload_audit.json независимо подтверждает:
- geometry/UV/weights/triangle/animation/bind accessors неизменны;
- активные joint indices прежние, изменены только 6030 неактивных slots;
- runtime creature.json и PNG bytes прежние;
- metadata изменено только для стандартных material/image/sampler bindings.
Архив до изменения: assetgen_output/creatures/pre_r6_format/models.

## Настоящий renderer parity

Текущий Release EXE, тот же shaders set и World_254, два последовательных
запуска через tools/flight_sim_run.py. native_reference — обычный bin/models
до изменения; native_candidate — изолированный r6_format_v2 после него.
Все 1015/1015 PNG пиксельно совпали: четыре rest стороны, восемь фаз
locomotion/Run/Action и настоящая 64px иконка каждого из 35 видов.
Свежесть каждого файла и exact capture payload hashes проверены.
Результат: native_parity.json, context_reference_all_gltf.json,
context_candidate_all_gltf.json. Обзор native_overview.png прямо просмотрен.
Для каждого вида также сохранены <id>.png и native_approved_<id>.png.

Новый --preview-species явно отделяет UI/renderer probes от world spawn.
В этих двух запусках настоящий world actor — только badger; его кадры
просмотрены. Это НЕ заявление о повторном world-spawn всех 35: их world
artifacts в review.json явно наследуются из индивидуальных принятых R5/R6
проверок. Меши, poses, texture bytes, runtime definition и renderer вывод
доказанно те же. Review — Codex, не подпись пользователя.

35 fidelity и 10 quality tests прошли. Desktop сборка только Release
с обязательной командой AGENTS.md, static DLL verification прошла.
Pinned production Python dependencies записаны в tools/requirements-creaturegen.txt.
Performance gates art probes выключены. Полные 61-resource manifests/provenance,
чистая воспроизводимая сборка, appearance/cache/equipment/FP/save-load,
маршруты/переходы/travelScale, package и CPU/GPU/memory performance остаются R6.


## Публикация и обычный каталог

Все 35 ресурсов опубликованы через exact bundle review gate. Независимый
валидатор повторён уже на models/creatures: 35/35, ноль errors/warnings/infos;
результаты в ../khronos_published_format/. Обычный bin/models без QA override
повторно отрендерил все 35 видов: 1015/1015 кадров совпали с кандидатами.
production_parity.json фиксирует равенство и payload checks обоих каталогов.
Фактический world actor этой проверки по-прежнему только badger, его
production world кадр прямо просмотрен. 9 выбранных Release C++ tests прошли.

ledger_after_format.json в родительском каталоге подтверждает 61/61 exact
review/published/bin payload; текущие source_inputs теперь у 35/61.
Пять skins ещё отсутствуют в сводном manifest. Полная цель не закрыта.
