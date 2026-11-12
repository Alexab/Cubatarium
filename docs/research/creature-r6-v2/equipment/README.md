Актуальное продолжение: [анатомическая посадка и material isolation](../equipment-fit/README.md).
Ниже сохранён исторический первый root/anchor audit до исправления посадки.

# R6 equipment: общий root исправлен, посадка ещё не принята

При проверке appearance выявлен недочёт предшествующей clearance правки:
world equipment уже следовала общему body root, но preview повторно
вычислял BoneSkeletonPoseEngine/Hierarchy и строил root с Y=0. В фазах с
поправкой высоты 0.01…0.036 блока overlays оставались ниже тела.

Preview теперь получает готовые world bone transforms от настоящего B
visual через `GetWorldBoneTransform`. Те же cached matrices и тот же
BodyOrigin/Yaw/ground lift используются для body draw и equipment anchor.
Повторная реализация pose/root в preview удалена. Физика, кости, pivots,
анимации, PNG и модели существ этим изменением не переписаны.

## Проверенная матрица

Новый opt-in `CUBA_FLIGHT_QA_EQUIPMENT_MATRIX` вызывает настоящий preview
renderer и worn/wielded drawers. Шесть ресурсов: human/bot/zombie/skeleton
и два human skins. На ресурс: Walk/Run × speed ratios .22/1/1.5 × 16 фаз
плюс найденная native-расчётом худшая clearance-фаза × четыре loadouts
(armor, main, off, all). Всего **2448** PNG и реальных observations тех
anchor matrices, которые переданы drawers.

Expected transform независимо вычислен по actual loaded geometry,
Hierarchy/PoseEngine и минимуму полных transformed vertices. Во всех 2448
случаях ошибка observed anchor matrix — 0. Коррекция высоты действительно
ненулевая в части кадров, поэтому прежняя ошибка Y=0 была бы обнаружена.
Проверены нужные bones всех шести armor slots и обеих рук. Speed ratio
теперь входит и в preview bounds cache key; старый fit другой скорости
не маскирует расширенный размах конечностей.

`audit.json` проверяет 408 уникальных кадров на ресурс, нужные anchors,
фазы, finite matrices, source/EXE/shader hashes, свежесть/читаемость PNG,
активные gear model/buffer/texture/wear/content inputs. Это **численная
проверка и целостность**, не художественное принятие.

Первый `anchors_catalog` отрендерил полный preview-набор, но harness
правильно завершился ошибкой мирового spawn: выбранный human был только
controlled player (src=0), не QA actor. Этот запуск сохранён как частичное
preview-only evidence, мировой успех ему не приписывается. После завершения
процесса второй `equipment_catalog` с zombie прошёл actual src=3 gate.
Два разных процесса дали побайтно одинаковые 2448 PNG и anchor manifests.
Это чистый повторный процесс для шести previews, не полный pack/cache audit.

`runtime_equipment_catalog/frame_002.png` прямо просмотрен: реальный zombie
после physics виден целиком, камера не внутри terrain. Он без gear; этот
кадр не выдаётся за world equipment-проверку всех humanoids.

Новый обычный production capture всех 61 ресурсов (`standard_catalog`,
`native_standard`) повторил **5290/5290** прежних PNG точно, включая
стандартные human equipment кадры, где lift=0. `standard_parity.json`
явно фиксирует разные EXE до/после, одинаковые shaders и runtime files.
Этот parity не доказывает новые стрессовые экипированные позы сам по себе.
Повторная требуемая Release сборка и 10 выбранных C++ tests проходят.
Performance art-проб отключён, FP/save/route PASS не заявляется.

## Визуальная проверка: needs_anatomical_equipment_fit

Прямо просмотрены шесть detail sheets худших clearance-фаз со всеми
скоростями/обоими состояниями/четырьмя loadouts. `visual_review.json`:
**needs_further_work**, а не accepted. Полные 36 loop sheets также созданы,
но их художественное принятие не заявляется. Ссылки: `index.md`.

Подтверждены существующие семантические ошибки посадки:

- Шлем оказался у шеи; броня разных частей собирается вокруг суставов.
  `FitGltfToBone` центрирует весь предмет на pivot. Pivot головы расположен
  у её основания (1.5), а центр skull — 1.75; body/limb pivots также не
  являются центрами поверхностей этих частей.
- Copper arms/legs используют mesh с теми же bounds, что chest; одинаковое
  uniform 0.7 fit не превращает его в подходящую защиту руки/ноги.
- Main/off items в просмотренных видах частично скрыты внутри/за руками.
  Рецентрирование AABB оружия не сохраняет authored grip origin. Нужны
  дополнительные ракурсы и настройка grip frame, не объявление «предмет
  виден» только по факту вызова rightItem/leftItem.

Эти ошибки не исправляются сменой костей существ и не могут быть приняты
по нулевой matrix error. Контракт R6 оборудования остаётся **pending**.

## Обязательное продолжение

[Epic Skeletal Mesh Sockets](https://dev.epicgames.com/documentation/unreal-engine/skeletal-mesh-sockets-in-unreal-engine)
описывает attachment sockets с относительными location/rotation/scale к
parent bone и возможность mesh-specific sockets при общем skeleton.
Это подтверждает необходимость отдельного attachment frame; сам bone
pivot не задаёт посадку произвольного предмета. Применение к Cubatarium —
следующее инженерное решение, не готовая интеграция Unreal API:

1. Получать локальную анатомическую область actual B bone mesh, включая
   fallback пустых hat/item anchors к геометрии родительских head/arm.
   Для bounds использовать inverse(restAnchor) × restMesh; кости не менять.
2. Разделить движение anchor и статическую посадку wear model. Явный режим
   fit в wear metadata должен помещать шлем на верх головы, chest на torso,
   sleeves/greaves/gloves/shoes — в соответствующие области. Сохранить
   поддержку прежних явно настроенных pack offsets; sole не опускать ниже
   body feet plane. Старый feet offset нельзя слепо перенести в новый fit.
3. Для held items сохранять declared/authored grip origin вместо AABB-center,
   задать frame относительно palm, проверить обе руки с нескольких сторон.
   Не менять FP transform через общую wield metadata без отдельной проверки.
4. Повторить плотную native matrix, world gear и FP, затем save/reload и
   pack/cache scenarios. Принять посадку только после прямого просмотра;
   body/skeleton/texture/animation invariants и бюджет времени обязательны.

Appearance/cache audit также ещё требует actual resolved keys/hashes,
совместимости skin ownership и overlays. По чтению кода надо отдельно
проверить удерживаемые visual-объектами mesh pointers после cache/root
reload: очистка weak cache сама по себе не доказывает обновление visuals.
Все остальные строки родительского R6 README остаются обязательными.
