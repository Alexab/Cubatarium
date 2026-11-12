# Аудит CreatureGen: узнаваемость, качество и соответствие плану

Дата: 10.11.2026. Исходная ревизия — `b35f00a75`, проверенный HEAD —
`d35cc74e1`. После исходного плана в ветке `creatures_visual` шесть коммитов.
Изменения этого аудита ещё не закоммичены. Последующая массовая художественная
коррекция существ не выполнялась: ниже отделены найденное, исправления
инструментария и оставшаяся работа.

**Вердикт:** техническая инфраструктура существенно продвинулась, но
художественная цель и условие «не ухудшить» не выполнены. Весь каталог был
выпущен до приёмки пилотов. Оригинальность достигалась слишком сильным
упрощением анатомии, общими палитрами и заменой специализированной анимации.
Нужно восстановить узнаваемость и выразительность исходных видов, сохранив
свои ресурсы, три backend и воспроизводимость.

## 1. Проверенные коммиты

| Коммит | Что сделано | Соответствие исходному плану |
|---|---|---|
| `2c15de321` | Effective texture baseline, fallback, расширение валидаторов | Полезная часть C0: все 17 bone-видов учтены, 3 missing diffuse названы ошибками; не полноценная визуальная приёмка |
| `32c0346e5` | Генератор, рецепты, экспорт всех 56 видов, атласы и skins, публикация | Реальная основа C2; C1/C3/C4 были обойдены перед C5/C6. Слишком мало anatomy/animation специализации; `accepted` выставлялся автоматически |
| `af90f145f` | Четыре настоящих Release PNG галереи | Реальные кадры лучше BMP-маркеров; размер моделей/перекрытия/среды не дают coverage 56 видов, UI и движений |
| `3f9c3f63a` | Offline preview с выборкой текселей | Полезен для рисунка; собственный упрощённый renderer и synthetic walk не эквивалентны runtime |
| `98ec9ccad` | Центровка парных конечностей, удаление ног bee | Исправление симметрии полезно; удаление анатомии пчелы — ухудшение, которое не следует принимать как правило стиля |
| `d35cc74e1` | Более знакомые пропорции bone, human fists/skins, rigid tiles | Верное направление, но точечные исправления не закрыли системные дефекты G, материалов, UV/масштаба и анимаций |

Суммарный diff от плана: 535 файлов, 38 346 добавленных и 116 152 удалённых
строки. Размер diff не является мерой качества, но показывает, что выпуск
не ограничился 10 пилотами. Исходные ID и backend сохранены; это выполнено.

## 2. Доказательства и пределы проверки

- [Все сравнительные листы](research/creature-review-2026-11-10/index.html):
  **8 сводных листов, 56 пар моделей**, по три ракурса baseline/HEAD.
  Масштаб в блоках одинаков внутри пары; разные виды не нормализуются друг к другу.
- [Лист скинов](research/creature-review-2026-11-10/skins.png): все 5, на
  соответствующей геометрии каждой ревизии.
- Шесть motion-листов: badger, hermitcrab, stingray, octopus, wasp,
  water_dragon. Это выборки **реальных glTF-каналов** в шести фазах клипа,
  с `skin.joints`, четырьмя weights, inverse bind, node hierarchy и quaternion
  interpolation. Это не синусоидальная имитация по имени кости.
- [Машинные метрики](research/creature-review-2026-11-10/metrics.json):
  все 56 видов, число joints/triangles, клипы и tracks, extent в блоках,
  текстурные размеры и цвета, parent/scene-root нарушения.
- [Логи проверок](research/creature-review-2026-11-10/checks.json).
- [Свежие игровые кадры и отчёт](research/creature-review-2026-11-10/runtime/run_report.json).

Листы сгенерированы и просмотрены, включая все 8 сводных, 5 skins и 6 motion.
Offline renderer — диагностический: orthographic, без игрового света/fog,
двусторонняя отрисовка; bone показаны в rest, C++ procedural profiles в этих
листах не воспроизводятся. Остались отличия UV corner/mirror от полного
C++ draw-пути. Выводы о форме подтверждаются также геометрией и размерами;
по одному offline-пикселю нельзя объявить runtime-ошибку.

Исходная ревизия сама имеет дефекты: отсутствующие diffuse у трёх B-видов,
неудачные skin-atlases, static octopus/butterfly, manatee с разрывом частей.
Это не образцы, которые требуется буквально восстановить. Эталон — лучшие
анатомические и визуальные свойства исходного вида, с исправлением старых
ошибок и собственной графикой.

### Release-прогон

Собрано строго:

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
```

Запущено через `tools/flight_sim_run.py`, Release `bin/Cubatarium.exe`,
`CUBA_FLIGHT_QA_GRID=1`, `CUBA_FLIGHT_QA_ALL=1`, yaw sweep, capture interval 5 s,
18 s в игре. Получены 4 настоящих PNG, exit 0, 295 ingame frames.
Вместо несуществующего `CreatureReview_20261110` приложение создало **World_253**;
это явно записано как `runtime_world_matches_requested=false`, а не скрыто
под именем запрошенного мира. Старые пользовательские миры не редактировались.

Кадры показывают текущие формы и цвета; часть моделей перекрыта деревьями,
сетка не покрывает все habitats. На просмотренном кадре счётчик показывает
35 видимых из 36 существ. Это **частичная runtime-проверка**, не 56/56 PASS.
Performance gates не запускались (`pass:null`, metrics disabled), поэтому
FPS кадра не объявляется регрессионным бенчмарком. Приложение работало около
68 s, включая создание мира, без Debug.

## 3. Находки по приоритету

### P1. Специализированная анатомия заменена общими коробками

`anatomy.py::BUILDERS` имеет 13 шаблонов на 56 видов, но проблема не в числе
шаблонов: в них отсутствуют необходимые сочленения/формы и используются
одинаковые решения головы, лап, хвоста и материала для разных животных.
`_crab` выдаёт короткие вертикальные опоры и бруски клешней; `_ray` — широкую
плиту с маленькими wing-кубами; `_serpentine` — горизонтальную цепочку.
Последняя используется там, где исходный water_dragon имеет длинную
вертикальную шею, а aquatic-шаблон не способен передать S-образного seahorse.

Визуально badger/panda/kitten утратили видовые маски и характер тела; crab,
hermitcrab, lobster — суставные боковые ноги/клешни; wasp — талию, ноги,
направление тела; water_dragon — шею; seahorse — вертикальность.

**Действие:** species-specific anatomy contracts, авторская модель вокруг
проверенного рига, контроль proportions/силуэта до текстур. Сохранить
характерные признаки вида и рабочие pivots. Не «приближать» одной общей
подгонкой трёх размеров bounding box.

### P1. Анимация и rig capability упрощены, несмотря на сохранённые имена клипов

В `export_gltf.py` каждый куб имеет один жёсткий вес 1.0, остальные нули.
Такое допустимо для жёстких деталей; применение ко всем 35 G-видам теряет
возможности плавной деформации тела, крыльев и щупалец. Число костей
уменьшилось у **20** видов. Само уменьшение не доказывает ухудшение, но в
данном случае оно сопровождается видимой утратой артикуляции.

| Вид | Joints до → после | Triangles до → после | Наблюдаемое последствие |
|---|---:|---:|---|
| crab | 43 → 10 | 276 → 120 | Пропала цепочка ноги/клешни |
| hermitcrab | 67 → 11 | 444 → 132 | Суставные ноги и домик заменены простыми массами |
| lobster | 55 → 10 | 324 → 120 | Утрачены усы, длинное сегментированное тело, работа клешней |
| seahorse | 22 → 6 | 156 → 72 | Нет изогнутой оси и соответствующей пластики |
| stingray | 24 → 5 | 168 → 72 | Малые боковые детали не заменяют волну по большому крылу |
| wasp | 15 → 6 | 292 → 72 | Силуэт и полёт стали общим aerial-блоком |

Exporter выбирает bones по `leg*`, arms/legs/wing/tail, ограничивает
`targets[:8]` и всем не-idle клипам выдаёт три X-rotation ключа 0 → угол → 0.
Это не отдельные walk/run/swim/fly/attack. Соседние конечности могут
различаться знаком, но нет stance/contact, устойчивой фазы и видовой оси.
У `tentacle0…7` octopus нет собственных animation targets.

У **12** видов исчез `die`: crab, hermitcrab, lobster, manatee, mese_monster,
puffin, seahorse, seal, shark, stingray, warthog, wasp. Не все эти клипы
обязательно сейчас вызываются игровым автоматом; потеря asset capability
подтверждена, runtime-доступность смерти требует отдельной проверки.

**Действие:** явно авторские клипы и bone-role/axis/phase mapping, сохранение
рабочих цепей и deformation capabilities. Для сложного G запретить generic
name-based motion как production-вариант. Количество костей — диагностический
сигнал; разрешать уменьшение только после проверки эквивалентной работы.

### P1. Автоматическое accepted и пропуск пилотной приёмки

`pipeline.py::write_asset_manifest` делал `state="accepted"` только по наличию
staging manifest. На проверенном HEAD все **56** имели accepted, несмотря на
очевидные дефекты. Catalog одновременно помечает виды как authored. Проверка
PNG-signature не доказывает, что модель хорошо выглядит или видна в кадре.

`publish` также автоматически пересобирал рецепты и публиковал все skins
даже при запросе одного вида. Это ломает независимую художественную доводку
и делает неконтролируемые изменения частью обычной публикации.

**Исправлено сейчас:** снят необоснованный accepted у 56 записей; введён
review-gate по точному payload hash с именем reviewer, критериями anatomy,
texture, rig/motion, world scale, runtime, UI/skins, authorship и изображениями
comparison/motion/runtime/UI с проверяемыми SHA. Невалидные BMP-маркеры,
пустая/неполная/устаревшая evidence блокируют publish.
`build/publish` больше не переэмитят рецепты; overwrite — отдельный явный
`emit --overwrite-recipes`. Skins публикуются только с `--skins` и проходят
собственную staging-проверку. Batch preflight выполняется до первой записи.

Гейт проверяет целостность evidence, **не умеет автоматически оценивать
художественный вкус или правдивость подписи reviewer**. В тестах используются
синтетические картинки только для проверки границы, не как приёмка моделей.
Полная транзакционность на случай IO-сбоя остаётся задачей следующего этапа.

### P1. Масштаб и пропорции потеряны при нормализации

`build_model` игнорирует `target_px` и `bounds`. G затем получает единый scale
из максимальной стороны collision bounds и максимального span модели.
Так можно вписать длинное горизонтальное животное в численный envelope,
но невозможно восстановить утраченную вертикальную форму.

| Вид | Extent в блоках до (X,Y,Z) | После | Вывод |
|---|---|---|---|
| seahorse | 0.185, 0.478, 0.310 | 0.105, 0.168, 0.504 | Стал низким горизонтальным пловцом |
| water_dragon | 1.092, 5.700, 2.883 | 0.586, 0.938, 5.157 | Утрачена высокая шея; длина не заменяет высоту |
| hermitcrab | 0.475, 0.308, 0.958 | 0.864, 0.480, 0.576 | Сменились отношения ширины/высоты/длины |

**Действие:** утверждённые model-space landmarks, per-axis proportions,
контактная плоскость и visual envelopes отдельно от collision bounds.
Листы делать и с одинаковой шкалой в блоках, и в крупном индивидуальном виде.
Уменьшение плохо выглядящей модели не считать восстановлением качества.

### P2. Палитра и рисунок обезличили виды

Технический atlas есть, но `meadow/forest/river` используются как готовые
цветовые решения всего вида. Поэтому panda/badger теряют чёрно-белую маску,
fox — характерные рыжие/светлые массы, wasp — различимую предупредительную
окраску. Многим видам выдаётся одинаковая жёлтая квадратная морда.

В `_paint_box` наличие любого `marks` отключает общую pattern-ветку.
Поддерживаемые `_paint_zone` покрывают не все детали; итог — большие плоские
зоны, случайные локальные пятна и несогласованные границы частей. Уменьшение
числа цветов само по себе не ошибка: у badger 1206 → 32 opaque цвета,
но существеннее потеря читаемой маски и распределения меха. Больше шума
не исправит этот дефект.

`bake_icon` вырезает верхний квадрат UV-атласа и уменьшает его до 32×32.
Это изображение развёртки, а не существа. Большая atlas-резолюция не означает
больше полезной детализации; human вырос с 64×32 до 128×128, но лицо/одежда
стали беднее. Все пять skins также требуют просмотра в надетом состоянии.

**Действие:** material library + species palette overrides, обязательные
face/chest/back/belly/mark masks, осмысленные pixel clusters и направленные
детали. Иконки получать из реальной модели. Уникальны формы пятен и детали,
но базовая биологическая окраска и читаемость могут оставаться близкими исходнику.

### P2. Экспорт нарушает glTF, старые тесты этого не ловят

На HEAD у всех 35 G `scenes[].nodes` включает joints, уже являющиеся children.
Спецификация допускает там только корневые узлы.
[glTF 2.0, Scenes](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#scenes-overview).

**Исправлено в exporter:** scene теперь перечисляет roots + mesh.
Добавлен regression test. Опубликованные 35 файлов не переписывались:
их нужно пересобрать вместе с художественно исправленными кандидатами.
Штатный validator и bind-pose tests проходят даже старые неверные scenes;
значит, зелёный результат этих checks уже известным образом неполон.

В `CreatureBoneSkeletonLoaderTest.cpp` конкретные UV-проверки bee/bunny
заменены проверкой попадания UV в 0…1. Отвязка от меняющегося ассета полезна,
но она потеряла регрессионное покрытие правильной ориентации/mirror/face.
Нужен независимый fixture UV checker, не возврат жёсткой привязки к старой арте.

### P2. Offline synthetic walk выдан за движение

Старый preview вручную вращает кости по имени, не читает реальные glTF clips,
игнорирует weight blending, joint-node mapping и многие bind transforms.
Bone skin preview имеет ещё отдельную упрощённую логику. Поэтому колонка
«шаг» могла выглядеть движущейся при неподходящем реальном клипе.
`_sheet` также нормализует каждую позу/ракурс, скрывая величину и дрейф.

**Исправлено сейчас:** колонка названа «синт. поза», HTML явно объясняет
ограничение; отдельный сравнительный renderer использует реальные G-каналы
и один масштаб пары. Также исправлена depth-запись до alpha-discard: прозрачный
тексель больше не закрывает лежащую за ним поверхность в textured preview.
Runtime parity и C++ bone motion capture остаются необходимыми.

## 4. Результат просмотра всего каталога

Сводная рабочая классификация, а не финальная приёмка. Во всех строках
нужно своё оформление при сохранении лучших качеств исходного вида.

| Группа/виды | Что восстановить прежде всего |
|---|---|
| cow, pig, sheep | Отношение головы/тела/ног, видовые уши/рыло/морда, читаемые пятна/шерсть; не скрывать тело овцы монолитным кубом |
| wolf, fox, bunny | Профиль морды, низкий/пушистый хвост, характер лап/ушей и прыжка; устранить общее «копытное» тело |
| human, bot, zombie, skeleton | Лицо и руки, одежда vs кожа/кость, запястья и anchors; skeleton не одетый humanoid с вертикальными полосами |
| chicken, bee, spider | Птичьи лапы/хвост/гребень; у bee крылья/усики/ноги, у spider боковые сегментные ноги; не удалять анатомию ради упрощения |
| dolphin, squid, tortoise, trout | Ось тела, плавники/панцирь/щупальца, соответствующие положения и материал; хвост и усы не заменять универсальной мордой |
| badger, kitten, panda, rat, warthog, hedgehog | Видовую маску, силуэт спины/хвоста, лапы/бивни/иглы; отдельные пропорции вместо quadruped с разным scale |
| owl, puffin, penguin | Вертикальную посадку, собственный клюв и лицевой рисунок, грудь, крыло в покое и полёте/плавании |
| butterfly, wasp | Разделение тела и крыльев, собственный контур крыла, талия/ноги/усики и правильные оси взмахов |
| crab, hermitcrab, lobster | Состав ног, клешней, раковины/хвоста/усов; боковые движения, многозвенные цепи |
| manatee, seal, shark, whale | Цельность тела и видовые плавники/голова/хвост; не возвращать старые зазоры manatee, не заменять новым блоком |
| seahorse, octopus, stingray, water_dragon | S-ось, восемь управляемых рук, деформируемое крыло, высокая шея соответственно; это разные rigs и гейты |
| dirt_monster, stone_monster, sand_monster, tree_monster | Грунт/камень/песок/дерево читаются материалом и массой; не одинаковая одежда humanoid |
| dungeon_master, land_guard, golem, mese_monster | Разные массы груди/плеч/рук и читаемые материал/атака; не один серый персонаж |
| oerkki, ogre, orc, treeman | Длины сегментов, силуэт челюсти/живота/конечностей/кроны; treeman снова должен читаться деревом |
| rigid_demo_walker, rigid_demo_flyer, rigid_demo_swimmer | Сохранить простоту backend, но разнести контур/рисунок/назначение, убрать лицо/крупное пятно на каждой грани |
| fire_spirit | Сохранить полезную новую alpha-маску, сделать живой читаемый огонь вместо полосатого значка; отдельный billboard review |

## 5. Best practices и как они применяются

1. **Узнаваемость важнее количества отличий.** Простая модель должна
   сохранять характер формы; текстура несёт мелкие детали, но не заменяет
   пропавшую анатомию. В art bible запрещено упрощение до неузнаваемости.
   Источник: [Blockbench Style Guide, Modeling](https://blockbench.net/wiki/guides/minecraft-style-guide/).
2. **Согласованная texel density и намеренные кластеры.** Сначала цветовые
   массы, затем тени/свет и материал; избегать неинформативного шума. Переносим
   принципы, не обязательство копировать Minecraft-оформление. В рецептах
   нужны per-species palettes и surface-space masks, в QA — UV checker и
   просмотр в игровом размере. Тот же [первичный guide](https://blockbench.net/wiki/guides/minecraft-style-guide/).
3. **Силуэт и совместимость скелета проверяют вместе.** Valve допускает
   изменение облика при сохранении идентифицируемости и работы исходного
   skeleton/animations. Для Cubatarium это означает сохранить rig contract
   и доказать качество движения до замены, а не сохранять только clip names.
   [Valve Character Art Guide](https://help.steampowered.com/en/faqs/view/0688-7692-4D5A-1935),
   [Valve Model Requirements](https://help.steampowered.com/en/faqs/view/5FB8-4078-8B2A-C52B).
4. **Формат glTF — строгий контракт.** Nodes, skins, weights, inverse bind,
   TRS и channels имеют разные роли. Введён root-node regression; следующий
   этап — pinned Khronos Validator и проверки реальных samples, не только
   существования массива. [glTF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html),
   [Khronos Validator](https://github.com/KhronosGroup/glTF-Validator).
5. **Хранить editable authoring source, запекать лишь runtime-выход.**
   Blockbench glTF export сохраняет hierarchy/animations, но не заменяет
   удобный исходник для правок. Сложный вид может быть ручным эталоном,
   генератор должен собрать его без потери рига. Параметрический template
   не является обязательным для каждого вида.
   [Blockbench Export](https://blockbench.net/wiki/guides/export-formats/).

Эти практики не дают автоматической численной оценки «красоты». В генератор
включена граница публикации по evidence и сохранность рецептов; полноценные
новые anatomy/texture/motion recipes — следующие этапы исправленного плана.

## 6. Что изменено этим аудитом и что осталось

Изменено: инструменты сравнения/метрик, 8 regression tests, quality gate,
безопаснее build/emit/publish, явная публикация skins, состояние manifest,
species `palette_overrides` с валидацией RGB, scene roots exporter,
alpha-depth и честная подпись preview, обновлённые цель,
art bible и план. Сырые reference assets остаются только в игнорируемом
`assetgen_output/.../baseline`, не включены в production build.

Не выполнено: исправление анатомии/атласов/риг-клипов всех 56, полная
runtime-галерея в чистой среде, экипировка и все GUI-проверки, независимый
Khronos validation прогон и performance-сравнение. Их нельзя засчитывать
по этому аудиту. Массовая перегенерация runtime-ассетов намеренно не проводилась;
в `models/creatures` изменён только статусный manifest, геометрия/PNG/клипы
этим аудитом не перезаписаны.

Проверки сейчас: 8 новых tests PASS; существующие G validate 35/35 PASS,
B validate 17/17 PASS, bind pose 35/35 PASS. Они описывают техническую
валидность в пределах своих проверок, не снимают находки этого отчёта.

## 7. Использование исправленного генератора

Обычный `build --species cow` собирает candidate в staging, не меняет
recipe и не публикует модель. Для цвета конкретного вида теперь можно задать
в его авторском YAML (пример параметров, не новый готовый дизайн):

```yaml
palette_overrides:
  body: [210, 205, 190]
  mark: [38, 35, 32]
```

Override применяется поверх material-family palette, не меняет её для других
видов и проверяет RGB 0…255. Глобальные изменения палитры не должны менять
идентичность всего семейства. Skins имеют свои цветовые планы и отдельную QA.

Пример структуры evidence JSON для одного вида:

```json
{
  "schema": "creature-quality-review.v1",
  "species": {
    "cow": {
      "decision": "needs_art_revision",
      "reviewed_by": "",
      "baseline_revision": "b35f00a75",
      "bundle_sha256": "<hash actual staged payload>",
      "criteria": {
        "recognizable_anatomy": false,
        "texture_quality": false,
        "rig_and_motion": false,
        "world_scale": false,
        "runtime": false,
        "ui_and_skins": false,
        "original_authorship": false
      },
      "artifacts": {
        "comparison": {"path": "<comparison.png>", "sha256": "<hash>"},
        "motion": {"path": "<motion.png>", "sha256": "<hash>"},
        "runtime": {"path": "<runtime.png>", "sha256": "<hash>"},
        "ui": {"path": "<ui.png>", "sha256": "<hash>"}
      }
    }
  }
}
```

Этот пример **блокирует** публикацию. Reviewer заполняет решение и критерии
только после просмотра, пути относительны файлу review. Дайджест вычисляется
`creaturegen.quality.bundle_hash(staging_dir)`. Перед publish сборка повторяется:
изменившийся payload делает review устаревшим. Изменённый screenshot тоже
не пройдёт проверку hash. Не подставлять синтетические тестовые изображения
как реальные доказательства качества.

Команды будущей публикации прошедших проверку кандидатов:

```powershell
python tools/creaturegen.py publish --species cow --review docs/research/cow-review.json
python tools/creaturegen.py publish --skins --review docs/research/skin-review.json
```

Указанные review-файлы ещё не созданы и кандидаты не приняты. Без evidence
publish сейчас возвращает nonzero до изменения опубликованных ресурсов.


## Продолжение рабочего дерева: R6 equipment fit

HEAD остаётся d35cc74e1; следующий результат относится к рабочему дереву,
а не к новым коммитам ветки. Сохраняется исходный объём56+5 и три backend.

Anatomical wear/grip fit и девять собственных hollow wear meshes исправляют
выявленные ошибки посадки без замены skeleton, creature PNG или animations.
9072 sampled preview PNG, eight-yaw/detail sheets и world zombie3 проверены;
числовой anchor audit дополнен прямым художественным просмотром. Intermediate
component/projection варианты не приняты; источник актуальных наблюдений —
research/creature-r6-v2/equipment-fit/visual_review.json.

Выявлена и воспроизведена на GPU новая совместимость: folder-key texture
cache смешивал inventory/worn модели в одной папке. Full model-path key
исправляет подмену, исходный material RGBA проверяется для9 предметов.
Это не основание закрыть cold-cache/root reload, FP или save gates.

Повторный full61 capture:5288/5290 PNG точны, включая всю body matrix;
два намеренных изменения относятся только к human equipment. Ledger61/61
актуален. R6 equipment sampled subset продвинут, полная R6 и цель pending;
полный remaining checklist сохраняется в research/creature-r6-v2/README.md.


## R6: обновление удерживаемых моделей и locator ресурсов

Подтверждена и исправлена stale B/G mesh regression: live visuals отслеживают
поколение кэша и идентичность запроса; resolved path включает sourceDirectory
definition. Pack merge очищает B/G/R до Load, preview bounds инвалидируются.
Actual native probe и независимый SHA/fixture audit:161/161 (same-root56,
filename52, directory52, один real rigid pack reload). Clock сохраняется;
текущая pose matrix error0. Все5290 PNG61 ресурсов точно прежние относительно
equipment-fit/final; creature/item meshes, PNG и authored clips не менялись.
11 пересобранных Release C++ tests проходят, ledger61/61 актуален.
Evidence и ограничения: research/creature-r6-v2/appearance-cache/README.md.
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
research/creature-r6-v2/appearance-cache/context_skin_catalog.json,
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
ledger61/61 актуален. Evidence: research/creature-r6-v2/appearance-cache /
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
research/creature-r6-v2/resource-reload/README.md, index.md, resource_reload_audit.json,
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
research/creature-r6-v2/fp-save/README.md, index.md, fp_audit.json,
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


## Обновление: lobster v3 опубликован, R6 остаётся открытой

Подробный протокол и листы: [lobster v3](research/creature-r6-v2/envelopes/lobster_anatomy_v3/README.md).
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

[Протокол, листы и ограничения](research/creature-r6-v2/envelopes/oerkki_anatomy_v2/README.md). В собственном рецепте восстановлена
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

[Актуальный протокол](research/creature-r6-v2/envelopes/seahorse_anatomy_v1/README.md); текущие доказательства в его `release_swim/`.
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

[Протокол, aligned comparison и ограничения](research/creature-r6-v2/envelopes/whale_anatomy_v2/README.md). В собственном рецепте
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

[Диагностика четырёх видов](research/creature-r6-v2/envelopes/motion_semantics_v2/README.md) изменила план.
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

[Orc sway v1](research/creature-r6-v2/envelopes/orc_sway_v1/README.md) опубликован R5:4 strides/slow sway Walk,
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

[Измерения, provenance и ограничения](research/creature-r6-v2/gltf-sampling-perf-v1/README.md).
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

[Полный протокол, raw frames и визуальный лист](research/creature-r6-v2/world-pass-perf-v1/README.md).
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

[Протокол и просмотренные листы/GIF](research/creature-r6-v2/envelopes/badger_sway_v2/README.md).
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

[Полный протокол и ограничения](research/creature-r6-v2/gltf-hint-perf-v1/README.md).
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

[Данные, native лист, источники и следующий этап](research/creature-r6-v2/badger-stance-v3/README.md).


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

[Reach, controls, native листы и следующий код](research/creature-r6-v2/badger-hip-control-v1/README.md).


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

[Solver, отвергнутый вариант, native/world листы и регрессия](research/creature-r6-v2/badger-stance-ik-v2/README.md).


## Текущая ограниченная цель завершена —12ноября2026

По новому указанию пользователя выполнен90-minute выпуск для всех56существ+
5скинов. Все61 опубликованы в models/bin, имеют свежую provenance и точно
воспроизводятся генератором; опубликованные5290native PNG совпали с проверенным
кандидатом.35glTF без format errors/warnings,5targeted C++ checks PASS, FP/save
smoke PASS, cow B/badger G/rigid walker R прошли ordinary world Walk/pause.
Python46/49 PASS;3 старых findings повторены на backup recipe и вынесены в
TEST-01. Полная R6 и прочие остатки больше не являются частью этой текущей цели.

[Итог и61-resource ledger](research/creature-timebox-2026-11-12/README.md). [Исключённый backlog](CREATURE_RELEASE_BACKLOG_2026-11-12_RU.md).
