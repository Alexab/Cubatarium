# R6: collision, visual envelopes и переходы glTF

Актуализация: glTF culling исправлен и повторно проверен;
[новая реализация и evidence](culling_gltf_v1/README.md). Ниже сохранён ход
предыдущего исследования; его «неисправленный culling» относится к тому EXE.
Envelopes B/R/sprite и world/performance gates ещё pending. Полная R6 pending.
Коллизии, основные рецепты и опубликованные модели этим этапом не изменялись.

## Что измерено

Исходники из research baseline используются только инструментами исследования.
Генератор предложений читает собственные рецепты, не исходные модели.
`legacy_audit_baseline_comparison.json` повторяет старый bounds audit с тем же
MIN_BOX clamp: все 17 оригиналов проходят, все 17 текущих моделей не проходят.
Физические определения у них сохранены. Это не разрешение автоматически
переписать коллизии через `--apply`: крылья, клешни и движение корпуса требуют
отдельной оценки физического тела и визуального объёма.

`offline_metrics.json` измеряет каждый asset clip в 129 фазах с фактическими
joint weights, inverse bind matrices, local TRS/SLERP, scale и feet offset.
`native_audit.json` проверяет **2472 основных native GPU PNG** исходников и
собственных моделей при одинаковой камере/масштабе, без обрезания alpha по рамке.
Расхождение независимых и native CPU bounds не выше 0.000572561 блока.
Сгенерированы 34 листа: 17 motion и 17 front/side. Прямо просмотрены все 17
motion и front/side badger, treeman — 19 листов. Остальные 15 front/side пока
не объявлены просмотренными. Перечень с SHA — `visual_review.json`.

Дополнительные GPU кадры у границы frustum и независимая проверка шести
плоскостей подтверждают ложное отсечение видимой геометрии у **13 оригиналов
и 16 собственных моделей**. `CreatureDrawPass.cpp` использует collision
maxSizeBlocks для frustum culling. Исправление ещё требуется; увеличивать
физическое тело или сжимать крылья ради этой проверки нельзя.

Основная матрица закреплена за историческим EXE
`cd844f497998112174958507d029328a015122e5fa39fad4b3c4c31bac361db0`.
После исправления переходов текущий EXE другой; это историческое evidence,
не повторная проверка старых кадров новым бинарником.
`ordinary_preview_parity.json` подтверждает 493 неизменённых обычных PNG
на историческом этапе. `dark_source_light_backing.png` прямо просмотрен:
тёмный original oerkki/whale нельзя оценивать как отсутствие текстуры по
одному тёмному фону. Перекомпоновка фона не меняет производственные PNG.

## Четыре собственных motion proposals

`motion_pilots/` — исторический v3. Все четыре motion листа просмотрены;
seahorse v3 отклонён: умножение tail X на четыре усиливает узел вместо
распрямления. Эти файлы нельзя считать принятым вариантом.

`motion_v4/` — актуальное изолированное предложение, **не опубликовано**.
Рецепты и модели находятся в `assetgen_output/creatures/envelope_motion_v4/`.
Риги/nodes/skins, определения/коллизии и PNG сохранены; изменены собственные
motion tracks, у treeman также planform толщины конечностей без смены UV.
Сначала builder доказывает точное совпадение экспорта неизменённого рецепта
с текущим опубликованным JSON+BIN. Все восемь v4 листов прямо просмотрены.

| Вид | Результат прямого просмотра v4 | Решение |
| --- | --- | --- |
| badger | Body roll 6° walk / 9° run возвращает вторичное движение; исходный сильный roll не копируется без проверки опоры и скольжения | Кандидат; world/stride и художественная приёмка pending |
| treeman | Roll 22°/26° читается; утолщение limbs помогает, но древень всё ещё выглядит слишком скелетным относительно массы исходного ствола | Доработать силуэт; не публиковать как завершённое восстановление |
| lobster | Клешни теперь поднимаются, добавлен наклон передней части корпуса 15°; вместо опускания negative-X удар стал читаемым | Кандидат; benthic world/contact приёмка pending |
| seahorse | Распрямление собственной tail chain по bind centerline возвращает движение хвоста без v3 узла; own окраска/голова сохранены | Кандидат; water/world и непрерывная анимация pending |

`motion_v4/native_audit.json`: 576 основных paired PNG, восемь листов с SHA,
одинаковый world framing, ошибка bounds не выше 0.000140740 блока.
Current EXE: `ef4925fcfe9b860d1625c04509be8f7b5963432953969a2452929ef1fbd4c7f5`.
В этой матрице ложный culling воспроизводится у всех четырёх исходников
и предложений. Изображения сами по себе этот bug не исправляют.

## Исправлено: проваливание при glTF crossfade

Независимый local-TRS blend и actual retained native visual обнаружили
промежуточные позы ниже пола, хотя чистые клипы уже имели ground correction.
В опубликованных моделях минимум достигал −0.030965 блока у badger и
−0.085256 у treeman. Исправление `GltfTransitionGroundClearance.h` измеряет
реальные skinned vertices только во время grounded crossfade и добавляет
необходимое положительное Y-смещение модели, сохраняя кости и физическое тело.
Free swimming/air не прижимаются к земле; fluid bottom проверяется отдельно.
Для выхода с опоры переиспользован существующий clearance decay state.

`motion_v4/transition_clearance_audit.json` подтверждает:

- 10912 actual native CPU frames before/after для опубликованного и proposal
  каталогов, badger/treeman/lobster/seahorse, холодный вход в пары clips;
- 10912 bone palettes bit exact, X/Z bounds exact, изменения только общим
  положительным Y; свободные air/water fixtures exact;
- grounded minimum после исправления в допуске 0.001 блока;
- 232 обычных preview GPU PNG RGBA exact на четырёх видах в двух каталогах;
- 12 targeted Release C++ tests проходят, включая новый weighted-skin/root/
  scale/habitat predicate тест.

Это CPU submissions фактического runtime, **не GPU crossfade raster**, не
проверка всех историй переходов, маршрутов/terrain/stride и не performance gate.
Pure-clip contact1025 вынесен отдельно в `motion_v4/pure_contact_1025.json`;
плавание и death clips нельзя оценивать как foot planting на плоском полу.

Для v4 проверены 1025 фаз каждого чистого клипа. Минимумы наземных/донных
активных клипов: badger −0.000218, treeman −0.000029, lobster −0.000028 блока.
У seahorse fly минимум −0.089709 относительно локального origin — это
свободное плавание, а не доказательство проникновения в пол. Совпадение
шага с перемещением и физическая опора этими величинами не доказываются.

## Дополнительные species-specific world flights v4

Четыре последовательных Release процесса через `flight_sim_run.py`, по8 секунд
idle/no-travel: badger/treeman на берегу, lobster/seahorse с water placement.
`motion_v4/world/audit.json` сверяет actual EXE, model/shader SHA, загрузку мира,
actual spawned species src=3,116 preview PNG и12 world PNG. Process success
не подменяет performance: metrics disabled, verdict not_evaluated.
Последний world кадр каждого вида и `world_contact_sheet.png` прямо просмотрены.
World вид treeman подтверждает необходимость более массивного ствола/конечностей.
Это не запуск attack route, не непрерывный motion playback и не contact/stride
приёмка. Положение видимой геометрии относительно debug collision box требует
отдельного world-pose сопоставления; не объявлено исправленным по скриншотам.

[Лист четырёх world кадров](motion_v4/world/world_contact_sheet.png),
[аудит ресурсов и запусков](motion_v4/world/audit.json).

## Следующие обязательные шаги

1. Отдельный conservative render envelope для culling с доказанным охватом
   анимаций/crossfade, resource reload/skins и измеренной стоимостью; повторить
   реальные edge witnesses. Не подменять этим collision contracts B/R/G.
2. Доработать массу treeman, проверить кандидатов в настоящих наземных/донных/
   водных сценах и с непрерывной анимацией, шагом/скоростью, опорой/стенами.
3. Принятые варианты проводить через обычный build/review/publish, затем
   обновить provenance, ledger, полную native61 матрицу и save/UI regression.
4. Закрыть оставшиеся R6: все habitats/routes/control, pack/UI, FP framing/
   attack/wall/presets, clean Release package и matched CPU/GPU p95/memory.

Общий CTest не принят: последняя широкая проверка 192 тестов имела 50 passed,
141 Not Run/unbuilt и один bounds audit Failed. Сейчас зарегистрирован ещё
один тест (193), новая полная широкая проверка не выполнена. Targeted12 не
подменяют этот статус. Inactive `.orig` backups в bin остаются packaging gate.

## Основания инженерных решений

[Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
задаёт local node transforms, joint/IBM skinning и анимационные channels.
Поэтому проверяются действительные joint palettes и skinned vertices, а не
только bind AABB. [Godot: Collision shapes 3D](https://docs.godotengine.org/en/stable/tutorials/physics/collision_shapes_3d.html)
описывает компромиссы collision shapes и преимущества простых форм для
динамических объектов. Разделение collision и visual culling envelope —
решение этого проекта по измеренному дефекту, а не цитата требования glTF.

## Актуализация:4 motion/mass revisions опубликованы

Прежние v4/v5 proposals проходили отдельныйreview.Четыре улучшения теперь в
основныхресурсах через normalpipeline;[motion_v5/README.md](motion_v5/README.md).
R5revision принята,R6pending;старыеunapproved статусы выше исторические.
