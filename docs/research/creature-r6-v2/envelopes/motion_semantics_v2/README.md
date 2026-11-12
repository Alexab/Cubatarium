# Motion triage: одинаковое имя состояния не гарантирует одинаковое действие

`diagnosis.json` получен read-only скриптом `audit_creature_motion_semantics.py`.
Все BIN reference byte exact к git `b35f00a75`, glTF/definition отличаются только
CRLF/LF. Первоначальный strict byte check JSON закономерно отказал; исправленная
проверка разрешает только newline differences, не semantic изменения. Это не
художественная приёмка. Own данные здесь **до публикации Orc sway**: прежний orc
подтверждён immutable own snapshot, остальные3 own ещё live;
`source_freshness_after_orc.json`. Метрики не перезаписаны.

## Что меняет следующую работу

1. У reference puffin Walk/Run/Fly все используют `fly`. У own Walk/Run —
   отдельный `walk`, Fly — `fly`. Прежнее сравнение Walk width0.225 против0.504
   сопоставляло наземный шаг и полёт. Оно не доказывает потерю wing motion.
   При **Fly↔Fly** sampled union width own1.013 против reference0.504 блока,
   то есть own больше. Сначала сравнить flying posture, wing folding и cadence;
   автоматически увеличивать крылья либо заменять наземный шаг полётом нельзя.
2. У reference orc/badger/treeman100% веса каждой вершины принадлежит одному
   skin joint: соответственно340/644/842 вершины с duplicates. Min/max этого
   влияния1, прочие weights0. Большие motion unions отражают rigid whole-mesh
   движение этого узла, а не суставную пластику остальных записанных channels.
   Это подтверждено реальными git bytes исходных ресурсов, не предположение
   по именам костей. Передавать характер sway на собственный артикулированный
   rig; не копировать эту схему привязки ради совпадения AABB.
3. Сравнивать **период движения**, а не только angular range. Наивное увеличение
   body roll на каждом быстром own stride даст резкое качание вместо медленной
   исходной пластики. Замедление всего clip, наоборот, изменит движение ног.

Периоды ниже — exported asset duration/runtime speed **при unit travelScale**.
Реальный renderer дополнительно масштабирует playback по фактической скорости.

| Существо | Reference Walk | Own Walk | Reference Run | Own Run |
| --- | ---: | ---: | ---: | ---: |
| Orc |2.400s|0.6355s|2.400s|0.8737s|
| Badger |2.500s|0.3500s|2.500s|0.2750s|
| Treeman |3.000s|1.0784s|3.000s|1.6947s|
| Puffin |0.08876s (`fly`)|0.2400s (`walk`)|0.08876s (`fly`)|0.2400s (`walk`)|

Puffin Fly own0.2400s против reference0.08876s. Это ещё одно основание проверять
flight cadence отдельно от размаха и наземного шага. Само различие не является
приказом копировать все особенности исходного clip.

## Реализованная следующая стадия

[Orc sway v1](../orc_sway_v1/README.md) опубликован как локальная R5 revision:
4 шага/один slow sway в Walk,3 в Run; прежняя локальная кинематика и частота
ног сохранены при одинаковом elapsed time. Остальные3 вида **не изменены** этим
triage. Следующий gate — matched load/memory/pose/culling performance на Orc,
поскольку длинный baked clip увеличил BIN. Затем отдельные badger/treeman
multi-rate proposals и корректный puffin Fly/ground comparison.

Ключи уже выпечены в canonical own recipe и нормальный CreatureGen их
воспроизводит. Metadata `gait_cycles_per_loop` описывает baked loops; runtime
не делает скрытого procedural retargeting. В Cubatarium один ClipClock на clip,
поэтому прежние limb keys повторяются по времени длинного clip, а не просто
растягиваются. [Khronos animation tutorial](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_007_Animations.html)
описывает связь sampler timestamps с node TRS channels; решение о нескольких
stride cycles на один sway — наш authoring design, а не требование glTF.

Все56+5/три backend остаются в цели. Все61 R6 pending; triage и sampled unions
не заменяют terrain/stride/sliding/world/GPU fades/control/UI-pack/performance/
compatibility/FP/package приёмку.


## Следующий изолированный пилот — badger

[Badger sway v2](../badger_sway_v2/README.md):7/9 strides на slow body sway,
unit-travel period2.45/2.475s, локальная частота лап0.35/0.275s сохранена.
Собраны native shared-clock GIF и триптихи original/own/proposal. V1 отклонён
по root floor gap; V2 уточняет только root contact, сохраняя13 joints/geometry/
UV/PNG. Цена памяти и sampler выросла, поэтому live badger не заменён и R5/R6
publication не засчитана. All61 R6 pending; цель полных56+5/трёх backend прежняя.
