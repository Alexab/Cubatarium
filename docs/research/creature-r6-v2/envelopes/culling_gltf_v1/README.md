# glTF visual culling envelope: реализация и проверка

Статус: прежние GPU edge cases исправлены для glTF. Это не завершённая
приёмка всех B/R/G, мировых маршрутов или производительности. Цель active.
Основные рецепты/модели/PNG/коллизии этим подэтапом не менялись.

## Изменение

`GltfConservativeRenderRadius.h` вычисляет при загрузке модели conservative
радиус из реальных vertices, weights, inverse bind matrices и иерархии nodes.
Радиус не получен объединением дискретных фаз. Для каждого local TRS берётся
верхняя граница нормы translation и linear transform по всем key values/rest;
она сохраняется при linear translation/scale blend и shortest-arc quaternion
slerp. Иерархия композиционно переносит эти границы к joint, затем к vertex.
Неотрицательные нормированные weights дают выпуклую комбинацию joint poses.
Небольшой запас покрывает float accumulation. Цикл, неверные weights/joints,
нефинитные данные и неподдержанное root-only extrapolation дают infinity —
такая модель не отвергается визуальным frustum test.

Радиус хранится в loaded mesh asset: одна загрузка/вычисление для shared asset,
обычная revision/path/sourceDirectory invalidation создаёт новую границу вместе
с новым ресурсом. `UCreatureVisualGltf::GetConservativeWorldBounds` не меняет
clock/pose; переводит radius в world blocks с model scale/feet offset и запасом
на grounded crossfade lift, включая retained takeoff lift после замены ресурса.
Sphere инвариантна к yaw. `CreatureDrawPass` использует эту визуальную AABB
для шести плоскостей frustum, сохраняя прежнюю политику max-distance/fog.
Расчёт позы/вершин каждого невидимого существа перед отсечением не добавлен.
В пределах distance radius первая встреча с asset теперь может загрузить его
до frustum rejection; стоимость этого изменения должна учитываться отдельно.

Backend B/R/sprite пока сохраняют прежний fallback. Их envelopes и callbacks
не считаются проверенными этой glTF реализацией. Физические collision bounds
не увеличивались и старый collision-to-model audit не был ослаблен.

## Проверенные факты

Текущий Release EXE:
`929205bada51d2c6862ad3989471ccc79a70c91104c099894de497b0279bb5f1`.

| Evidence | Результат | Граница утверждения |
| --- | --- | --- |
| `all35_radius_audit.json` | Все35 own glTF,74186 actual CPU poses внутри finite radius | Все asset clips33 фаз и cold-entry пары16 фаз×11 fade steps; не произвольная world история |
| `original_radius_audit.json` | Все35 original glTF,69379 CPU poses внутри границы | Research baseline, не production dependency |
| Native paired envelope matrix |2472 основных GPU PNG, ни одного alpha clipping по рамке; submitted CPU bounds внутри новой AABB |17 спорных glTF, shared scale/framing |
| Native edge witnesses | Восстановлены все13 original и16 own ложных отсечений | Реальные GPU pixels + новый runtime frustum predicate; не full-world camera route |
| `regression_audit.json` |2472 GPU RGBA exact,10912 retained transition frames/palettes exact,232 transition preview RGBA exact | К историческим evidence до culling fix; geometry/animation/material сохранены в этих матрицах |
| Release tests |13/13 targeted проходят; новый synthetic envelope test4851 mixed poses | Общий CTest не принят |

Native6-plane edge acceptance независимо повторяет Python по actual VP и новой
render AABB. Во всех зарегистрированных видимых edge cases новый predicate
разрешает draw. CPU transition probe запрашивает envelope **до следующего
UpdatePose**, затем сверяет реальные submitted bounds; сохраняются все10912
костных палитр и frame bounds к предыдущему исправлению пола.

У own моделей radius/наибольший измеренный posed radius <=1.55. Загрузка+расчёт
всех35 заняли около229мс в одном исследовательском запуске. Это combined load
и bound timing, **не** дополнительная стоимость bound и **не** matched runtime
CPU/GPU p95/memory acceptance. Высота conservative AABB содержит запас на lift;
увеличение числа admitted draws у края камеры ещё нужно измерить.

## Обычный игровой путь и прямой просмотр

`world/audit.json`:4 последовательных Release flights с обычными bin/models,
actual spawned badger/treeman/lobster/seahorse, берег/water placement,
116 preview PNG и12 world PNG. SHA assets/shaders/EXE и process success
проверены. Performance metrics disabled. Это8-second idle/no-travel subset.
Прямо просмотрен общий world лист и отдельно badger frame001/frame002.
**Badger frame002 непригоден для art QA**: close camera попадает слишком близко
к модели; frame001 показывает модель нормально. Это открытая проблема
QA camera/pose synchronization, её причина ещё не доказана. Не выдавать
process success за четыре принятых world visuals или исправленный camera gate.

Дополнительно прямо просмотрены15 front/side листов, ранее не отмеченных как
изученные: dirt_monster,dungeon_master,golem,land_guard,lobster,oerkki,orc,owl,
penguin,puffin,sand_monster,seahorse,stone_monster,wasp,whale.
Вместе с19 historical sheets, чьи model PNG теперь подтверждены RGBA exact,
прямой просмотр покрывает все34 представления17 видов. SHA и конкретная версия
каждого просмотренного листа записаны в `visual_review.json`.

Выводы: новые wings у owl/penguin/puffin/wasp закономерно выходят за collider,
их нельзя сжимать ради culling. Whale сохраняет длинный объём при более
сформированном силуэте. Own humanoid monsters получили читаемые лицо/материал
и движение относительно часто статичного source; эти свойства сохранить.
Orc по-прежнему теряет сильный body roll исходника. Published lobster не имеет
сильного подъёма передних клешней, поэтому отдельный v4 кандидат остаётся
актуальным. У published seahorse хвост почти неподвижен; v4 candidate ещё не
опубликован. У puffin side fly/glide заметно более крутой наклон корпуса,
чем у source: проверить непрерывный полёт в мире до решения о правке.
Treeman mass и badger secondary roll остаются задачами motion proposals.
Эти наблюдения не являются окончательной художественной приёмкой.

## Следующая работа

1. Отдельные conservative envelopes B/R/sprite и реальные edge cases,
   reload/skin/model substitution bounds; matched culling CPU/GPU/memory cost.
2. Разобрать close-QA camera framing барсука по frame-synchronous camera/body/
   visual данных; world frustum sweeps с actual draw/reject observations.
3. Доработать массу treeman и motion proposals, проверить stance/stride/contacts/
   terrain/water/air/continuous motion. Принятые версии через normal review/publish.
4. Продолжить все оставшиеся R6 save/control/pack/UI/FP/package/performance gates.

Последняя широкая CTest192 проверка50 passed/141 unbuilt/1 bounds Failed
остаётся историческим незелёным результатом. Сейчас зарегистрированы194 tests;
новая широкая проверка не выполнена. Новый13-test subset этого не заменяет.

Техническое основание skinning/TRS:
[Khronos glTF2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
Конкретный conservative bound — собственное решение под фактический runtime
sampler проекта, а не заявленная возможность любого glTF renderer/interpolator.
