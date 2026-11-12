# Разбор 17 расхождений: форма отдельно от диапазона движения

12 ноября 2026. Все три `bind_shapes_*.png` непосредственно просмотрены.
`metrics.json` получен из текущих собственных моделей и исходного research
snapshot. Production-генератор исходные ресурсы не читает. Файлы моделей,
скрипт и листы идентифицированы SHA. Это диагностика, все R6 pending.

Это снимок **до публикации penguin v2**. После неё own-строка penguin сохраняет
прежнюю форму для сравнения; её SHA подтверждён собственным pre-publish snapshot.
После публикации lobster v3 также исторической стала own-строка lobster.
После Orc sway актуальны11 own;6 прежних own (penguin/lobster/oerkki/
seahorse/whale/orc) сверены со своими pre-publish snapshots и все17 original
сверены в `source_freshness_after_orc.json`. Прежние freshness reports исторические.
Метрики не перезаписаны.

Прежний bounds audit сопоставляет физический rest box с объединением bind и
нескольких фаз idle/walk/fly. Его 17 ошибок нельзя автоматически трактовать
как потерю массы корпуса. В то же время близкие размеры AABB не доказывают
сохранения формы внутри него. Нужны оба сравнения.

Новый read-only инструмент:

```powershell
python tools/research/analyze_creature_anatomy_shapes.py --out docs/research/creature-r6-v2/envelopes/anatomy_shapes_NEW
```

Он сохраняет bind XYZ, opaque triangle silhouettes front/side/top в общем
мировом масштабе пары, 65 фаз Idle/Walk/Run/Fly/Swim/Action и вершины,
образующие каждый экстремум, с ненулевыми skin influences. Интерполяция —
та же offline-реализация, которая ранее сверена с native bounds. Текущая
проверка сама по себе offline; procedural ground lift и world blends сюда
не включены. Raster area зависит от разрешения; это площадь проекции всей
геометрии, а не объём тканей. Прозрачность текстур не учитывается.

## Результаты и необходимые действия

| Виды | Что показывает сравнение | Действие |
| --- | --- | --- |
| badger | Bind XYZ own/original 0,969/0,945/0,931; side area 0,985, top 0,812. Walk union ширина 0,542 против 0,824 | Не расширять корпус по motion union. Отдельно проверить длину/сужение и выразительность roll в native; опубликованные 6/9° ещё не доказывают равную амплитуду исходнику |
| dirt_monster, stone_monster | Bind 0,812/1,750/0,438 против 0,805/1,724/0,419; walk depth 0,943 против 0,419, экстремумы foot_left | Корпус не сжимать. Проверять шаг, столкновения стоп с terrain и gameplay proxy |
| sand_monster | Bind ratios 1,000/1,005/0,950; walk depth 0,985 против 0,395, экстремумы стоп | Аналогично: оценить stride/terrain, а не увеличивать collider по размаху шага |
| dungeon_master, land_guard | Bind extents в пределах примерно 7%; walk depth 1,200/1,204 против 0,951; extrema стопа/голова | Сохранить torso, проверить шаг и attack contacts |
| golem | Bind ratios 1,093/0,997/0,953; depth walk 1,405 против 1,082, стопа/голова | Проверить тяжесть шага и torso twist; объём не править по одному тесту |
| orc | Bind extents почти равны; исходный Walk union width 3,489 против own 1,891 | [Orc slow sway](../orc_sway_v1/README.md) опубликован R5 с сохранением local limb cadence,18 joints и textures; строка чисел историческая. Полная stance/stride/perf/R6 pending; исходный whole-mesh skin не копировался |
| owl | Bind XYZ близки, front area 1,314, side 0,826; walk width задают wing_lower | Проверить полноту профиля тела и крылья во всех habitat states, не считать весь недостаток side area потерей torso |
| penguin | Bind XYZ близки, но side area 0,713. В средней высоте тела исходная глубина около 0,272, own около 0,22; центр брюшка смещён назад | `penguin_belly_v1` отклонён; умеренный [v2](../penguin_belly_v2/README.md) после full-turn/fluid/world review опубликован как R5 revision. Wet/dry contact/terrain и полная R6 pending |
| puffin | Bind XYZ около 1,02, side area 1,231; own Walk width 0,225 против 0,504 | Скорректировано: reference Walk=fly, own Walk=walk. Это разные действия. Fly↔Fly own width1.013 против ref0.504; проверить flying posture/folding/cadence, не увеличивать крылья по исторической Walk строке |
| lobster | Bind XYZ 0,966/0,951/0,995 при area front/side/top 0,654/0,721/0,732 | Реальная форма тоньше несмотря на похожий AABB. [Lobster v3](../lobster_anatomy_v3/README.md) опубликован после native/rig/world Tread review; расширены ноги, удлинены клешни, восстановлен carapace. Строка чисел историческая; Walk/terrain и полная R6 pending |
| oerkki | Bind XYZ близки, front/side area 0,853/0,765; исходный материал почти чёрный | [Oerkki v2](../oerkki_anatomy_v2/README.md) опубликован R5: глубже torso/limbs, прежние pivots/weights и62 sampler arrays exact. Строка чисел историческая. Terrain/stride/attack и полная R6 pending |
| seahorse | Bind XYZ практически равны, side area 0,805. Walk union height 0,598 против 0,731 | [Seahorse anatomy v1](../seahorse_anatomy_v1/README.md) опубликован R5: более полный передний trunk, прежние upper section/crest и42 sampler arrays exact. Normal production free Swim проверен. Исторические числа не описывают новую форму; полный tail/neck range и R6 pending |
| treeman | После v5 bind XYZ около 1,00/1,01/0,99, side area 0,959. Walk union width 3,621 против 4,804; край образует crown/head | Масса trunk уже ближе. Проверить пластичность кроны и ветвей, не масштабировать всё дерево по динамическому AABB |
| wasp | Bind width 1,131, front area 2,218; walk width 1,048 против 0,526 задают forewing tips | Разделить сложенные/раскрытые крылья, abdomen и stance. Silhouette difference не означает просто увеличенный корпус |
| whale | Bind width 1,151, height 0,905, side area 0,856; Walk width 7,314 против 4,996 задают flipper tips | [Whale v2](../whale_anatomy_v2/README.md) опубликован как локальная R5 height/profile revision;36 samplers/72 arrays exact. Aligned reference выявил противоположное направление/смещение raw source. Girth/fluke/flipper и полная R6 pending; строка чисел историческая, physics не расширялась |

Числа из bind и union не взаимозаменяемы. В частности, прежнее предположение,
что разница ширины барсука сама доказывает потерю объёма trunk, отвергнуто.
Остаётся меньшая top-проекция и разница движения — это отдельные признаки.

Дополнительно просмотрены native `motion_v5/sheets/badger_views.png`,
`sheets/{whale,stone_monster,penguin,oerkki}_views.png` и
`motion_v5/sheets/seahorse_views.png` в родительской папке envelopes.
Они исторические: для неизменённых моделей полезны как art reference, но
не заменяют текущий executable/native прогон кандидата. Общая R6 не принята.

## Порядок исправлений

1. Penguin v2 прошёл native original / own-before / candidate, rig/UV/transition
   invariants, full-turn и real-world Swim; R5 опубликован normal pipeline.
   Остались wet/dry bank routes, actual Tread, GPU fades/stride и полная R6.
2. Lobster v3 опубликован R5; нормальный production Tread подтверждён.
   Oerkki v2, seahorse anatomy v1 и локальный whale v2 height/profile опубликованы R5;
   girth/fluke/flipper и полная motion fidelity whale остаются открытыми;
   для lobster остаются Walk/terrain/stride и полная R6.
3. Orc slow sway опубликован R5. Сначала matched performance из-за роста
   animation keys/linear lookup; затем badger/treeman cadence proposals. Puffin
   сравнивать Fly↔Fly и отдельно ground gait: Walk alias был различным.
4. Независимо выполнить matched culling performance и world camera/routes.

17 Failed в старом тесте сохраняются как открытый результат. Ни один вид
этим документом не исключён из gate; collider/tolerance не изменены.
