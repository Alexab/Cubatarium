# B/R/sprite: отсечение по подготовленной геометрии

Функциональное исправление реализовано и проверено. Модели, кости, текстуры,
анимационные профили и физические коллизии на этом этапе не менялись.
Финальная R6-приёмка, в том числе performance, остаётся открытой.

## Решение

У glTF сохранён предыдущий аналитический envelope всех clips/crossfade.
Для B/R/sprite `CreatureDrawPass` теперь сначала подготавливает текущую позу
и её настоящие draw requests, включая одежду и оба предмета в руках.
`RangeIntersectsClip` проверяет границы каждого request в homogeneous clip
coordinates. Граница static mesh строится из его действительных vertices,
затем преобразуются восемь углов; у billboard используется плоский quad.
Физический collider для frustum больше не подменяет эту геометрию.
Политика ограничения дистанции и fog осталась прежней.

Если одна часть может быть видна, проверка заканчивается и сохраняет весь
actor. Если все requests снаружи, удаляются только requests этого actor;
ранее добавленные существа остаются в очереди. Это также точнее общего AABB:
две разнесённые части за противоположными краями экрана не обязаны сохранять
пустой промежуток между ними. Запрос позы выполняется один раз, повторного
`SubmitCreatureVisual` после успешной проверки нет.

Local mesh bounds кешируются вместе с GPU content key и количеством floats.
Новый ресурс/reload получает новые данные, изменение ключа инвалидирует
границу. Меши без content key пересчитываются консервативно: это защищает
неименованные изменяемые fixtures от stale bounds. В частности, холодный
item mesh с ещё нулевым GPU key может сканироваться до первой отрисовки.
Неизвестный skinned request, нечисловые данные или отсутствие надёжной
геометрии не приводят к ложному rejection. Для skinning нельзя использовать
bind AABB вместо реальной позы; обычный G backend использует свою отдельную
аналитическую границу.

## Последняя проверенная версия

Release EXE:
`26f7f77d36a7ab28661db03ae468c309ae1f08ca9303ad59d4189bf41758b2aa`.
Актуальная матрица — `fast_clip/`; текущие world subsets — там же для `bot`
и в `fast_world/` для `rigid_demo_walker`/`fire_spirit`.
`verified/` и `production/` сохраняют предыдущий промежуточный EXE с общим
накоплением plane maxima. Они не выдаются за кадры последнего бинарника.

| Проверка | Результат | Что она не закрывает |
| --- | --- | --- |
| Поставленные позы B/R/sprite | 26 ресурсов, 44 loadout cases, 1308 GPU PNG | Не art approval всех анимаций |
| Экипировка настоящего synthetic actor | 504 кадра, все шесть armor slots, main/offhand; минимум 12 дополнительных requests | Не все предметы проекта и все UI flows |
| Edge witnesses | Все 41 найденные видимые фрагменты сохраняются; старый collider test отвергал их | Не все camera routes и occlusion |
| Early-exit regression | 1349 normal/edge RGBA exact к промежуточному варианту | Не измерение выигрыша CPU p95 |
| Полная native matrix | Все 5290 body PNG 56 существ +5 скинов RGBA exact к предыдущей версии моделей | Не full-world animation/terrain acceptance |
| FP | 936 RGBA exact, 1872 authored GPU arm draws, шесть retained-cache cases | Framing/attack/walls/presets ещё pending |
| Entity persistence | 128 legacy/v2 cases, 256 actual restores, world actors unchanged | Не повторная whole-world/user/cooperative save проверка |
| Tests/build | 14 targeted Release tests проходят, static Windows EXE verification PASS | Общий CTest не принят |

Native probe создаёт synthetic `UCreature`, экипирует его через обычные
inventory API и вызывает действительные `UpdatePose`/`SubmitDraw`. Подготовленная
очередь передаётся native FBO sink; GUI-only armor overlay не используется.
Проверяются idle/walk/run/crouch/jump/fall/action и соответствующие habitat
fly/hover/glide/swim/tread states, четыре фазы. Это входные состояния теста,
а не утверждение, что у каждого профиля есть отдельная attack-анимация.

Python auditor независимо повторяет old-collider six-plane test по фактическому
VP и сверяет GPU alpha coverage. Новый clip test обязан принять каждый
действительно видимый frame. В последней колонке примеров камера специально
показывает только часть вне collider; обрезание там — устройство fixture.
У sprite отдельный false-cull witness не найден; он проверен положительными
кадрами и unit cases плоского quad, а не объявлен ещё одним исправленным bug.

Прямо просмотрены `fast_clip/posed_examples.png` (восемь ресурсов, несколько
поз и edge cases) и игровые кадры B с бронёй, R и sprite. Остальные native
изображения проверены численно; все 1308 не объявлены прямо просмотренными.
Эти диагностические листы не заменяют original/own художественные сравнения.

## История первого отказа

Первый запуск в корне этой папки завершился ошибкой runner:
`QA species did not actually spawn: human`. Native captures были сформированы,
но world spawn gate не пройден. `human` имеет роль controlled_default;
`WorldEnvironment::SpawnCreature` запрещает второго такого actor при уже
существующем PlayerCreatureId. Существующий игрок имел src=0, нужного QA src=3
не было. Этот запуск сохранён как неуспешный, его кадры не подставлялись вместо
проверенного world evidence. Повторный запуск использовал NPC `bot` и прошёл.

## Доказательства и ограничения

- `fast_clip/audit.json`: 26 ресурсов и 41 GPU edge witness с SHA изображений.
- `fast_clip/body_parity.json`, `fp_audit.json`, `audit_production_final.json`:
  текущие common-catalog, FP и persistence проверки.
- `regression_fast.json`: независимые RGBA comparisons, текущий EXE и три
  действительных world spawn/process subsets. Дополнительные equipment inputs
  нового probe проверены по SHA; все прежние inputs сохранены.
- `visual_review.json`: конкретные просмотренные изображения и SHA.

Подготовка B/R позы теперь происходит и для существа внутри distance limit,
которое затем отвергается frustum. Это имеет стоимость, особенно с procedural
ground clearance, холодными ресурсами и экипировкой. Ранний выход уменьшает
число corner transforms, но не доказывает соблюдение performance бюджета.
Порог плана <=10% роста p95 creature-pass CPU/GPU пока не принят: нужны
сопоставимые сцены, числа видимых/скрытых/экипированных actor, warm/cold runs
и память. FPS на коротких QA screenshots для этого не используется.

Свежий `creature_bounds_audit_test` по-прежнему **FAIL: 17 glTF mismatches**:
badger, dirt_monster, dungeon_master, golem, land_guard, lobster, oerkki, orc,
owl, penguin, puffin, sand_monster, seahorse, stone_monster, treeman, wasp, whale.
Это отдельная проверка physics/body versus visual extents; исправление frustum
не делает её зелёной. Для каждого вида надо отличить настоящую потерю core
массы/опоры от appendages и неверного collision-envelope критерия. Не запускать
auto `--apply`, не увеличивать tolerance и не сжимать крылья ради теста.
Исторический широкий CTest192:50 passed/141 unbuilt/1 Failed. Теперь
зарегистрировано195 тестов; новая широкая проверка не выполнена.

## Следующие работы

1. Классифицировать 17 body/collision discrepancies и исправлять действительные
   анатомические потери с original/own сравнением, сохраняя physics contracts.
2. Измерить стоимость текущего culling и при необходимости добавить доказанную
   coarse границу перед дорогостоящей pose/gear подготовкой.
3. Frame-synchronous world camera sweeps, stance/stride/contacts/terrain и
   habitats/routes/control. Историческая close-QA near-clipping проблема
   барсука ещё не объявлена исправленной.
4. Остальные R6: pack/UI, FP framing/attack/wall/presets, clean Release package
   без .orig, matched CPU/GPU p95/memory и полные требуемые проверки.

Цель active, полная R6 pending. Этот этап сохраняет полный объём 56+5/три backend.

## Primary sources и инженерное решение

[OpenGL 3.3 Core specification, §2.19](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf)
задаёт clip volume через `-w <= x,y,z <= w`. Именно эти шесть линейных
неравенств проверяются по преобразованным углам.
[Unity 6: visibility troubleshooting](https://docs.unity.com/en-us/engine/6000.0/manual/assets-and-media/asset-types/mesh/troubleshooting-skinned-mesh-renderer-visibility)
объясняет, почему procedural/additive deformation выходит за заранее известные
bounds, и различает расширенные заранее известные bounds и пересчёт offscreen
с соответствующей стоимостью. Конкретная комбинация G analytic envelope и
B/R/sprite posed requests — решение Cubatarium, а не перенос Unity API.
