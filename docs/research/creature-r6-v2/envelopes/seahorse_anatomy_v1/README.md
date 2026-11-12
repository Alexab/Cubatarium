# Seahorse: восстановление передней массы корпуса и проверка свободного плавания

Опубликована R5 anatomy revision, все61 R6 остаются pending. Актуальная
публикация и доказательства находятся в **`release_swim/`**. Файлы в корне
этой папки — первоначальный исследовательский прогон на прежнем Release EXE.
Это решение ассистента по прямому просмотру, не user approval/финальная R6.

## Геометрия и собственный материал

Исторические anatomy17 sheets показывали потерю side area при почти равном
AABB. Сечения экспортированных triangles уточнили причину: собственный trunk
недостаточно выступал вперёд. Шея, crown и задняя граница уже задавали нужную
связность. Увеличивать всё существо или переносить хвост не требовалось.

В собственном рецепте trunk depth5→8 texels, передний нижний край Z−1→−4.
Задний нижний край остаётся Z4. `side_profile` сужается к прежнему верхнему
сечению Z[−.25,3.25], X[−2.1,2.1]; оно совпадает по exported vertices точно.
Переход к шее и заднему гребню сохранён. Это локальная проверка сечений,
не доказательство watertightness или отсутствия всех animated intersections.

Голова, snout, гребень, семь артикуляций хвоста и плавники не изменены.
Все17 суставов, hierarchy/IBM/JOINTS/WEIGHTS и42 animation sampler arrays
точны, включая `die`. Ранее опубликованный65% uncurl сохранён; равенство всей
выразительности оригиналу этим ещё не доказано.240 triangles, atlas64×64,
BIN49316 bytes не выросли. Прежние80 constant-UV triangles однотексельных
поверхностей не выросли; median model texel density9.2376 также прежняя.
UV trunk заново выделены и собственный hippocampal surface material запечён;
палитра/алгоритм текстуры прежние, original pixels/mesh в builder не читаются.

На общей GPU камере Idle: side area к original82,0%→89,7%, oblique79,4%→84,5%,
front90,3%→90,8%. Это projection area, не физический volume и не автоматическая
art acceptance. Прямо просмотрены original/own-before/candidate Idle/Walk/Action,
три полных поворота Idle/Swim.125/Swim.625,20 world frames и отдельные full-size
candidate и production frames. Character остаётся собственным золотистым
кольчатым коньком с более полным передним профилем.

## Почему появился отдельный QA input route

Первый `world_swim/` на старом EXE завершился **failed postcheck**: горизонтальный
`water_pause` не запрашивал подъём. За18,71 sim секунд получены653 Tread/408 Idle
и один начальный, не requested-move Swim. Существо закономерно опустилось на дно.
Не засчитывать этот run как доказательство свободного плавания и не исправлять
ожидание на Tread только ради зелёного результата.

Новый **`water_swim_pause`** в `CreatureQaWorldMotion.h` оставляет обычные паузы
и движение по±X, дополнительно подаёт `CreatureIntent.wantJump`, когда BodyOrigin
ниже первоначальной submerged Y на0.15 блока. Target запоминается один раз.
Позиция, facts, pose, анимационный state, коллизии и possession не форсируются.
Product motor/state/definition не изменялись. Это диагностический запрос input,
не исправление/приёмка автономного AI hover или новой плавучести.

QA-маршрут прошёл на кандидате:1071 samples/18,8398 sim секунд,656 moving Swim,
5,1007 блока реального XZ пути, оба направления,16 swim-up requests, без frozen
или solid overlap. В normal production **без model override**:701 samples/
18,9015 sim секунд,419 moving Swim,4,8364 блока пути, оба направления,16 requests,
без frozen/solid-overlap. `world_motion_audit.json` проверяет derived Swim и
отсутствие onFluidBottom у moving Swim; target и driver также проверены.

В production700 Swim/1 Idle: «пауза» означает отсутствие горизонтального
запроса, а вертикальное движение продолжается. Для Aquatic существ product
`RebuildLocomotionFacts` заполняет поле `horizontal_speed` длиной **3D** delta/dt.
Поэтому Swim во время подъёма/погружения при нулевом XZ input не означает stuck
clip. Этот маршрут не доказывает спокойное зависание/Idle после полной остановки.
Глубинный controller имеет импульсы; его плавность и world AI не приняты.

## Новый Release и подтверждение доставки

Desktop собран требуемой командой Release:

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
```

Новый EXE SHA256:
`235ce58dd65546f38164020295ff403bf48c2816b14bb16cd96228faee030483`.
Новые native original/own-before/candidate сняты заново через `flight_sim_run.py`
на одном EXE и profile. Старые reference captures не переиспользовались.
Все89 candidate PNG и все6 comparison/full-turn sheet файла byte/RGBA exact
к прямо рассмотренному pre-build материалу. Отдельно непосредственно просмотрены
текущие `release_swim/threeway_idle.png`, world contact sheet, full-size candidate
и production `frame_008.png`. Доказательства в `sheet_carry_forward_audit.json`,
`release_render_parity.json`, `visual_review.json`.

Native138 основных original/current PNG согласованы с offline skinning,
max bounds error2,2812e−6 блока. По одному collision-based false cull у original
и own восстановлено visual envelope.4 free-fluid transition cases/44 CPU frames
покрыты envelope. **Supported frames0, minY verdict отсутствует**: native probe
явно исключает supported route для free-swimming seahorse. Research verifier
разрешает именно этот документированный fixture, не отменяет grounded gates.
Khronos35 QA glTF:0 errors/warnings/infos.

`release_swim/publication_audit.json` PASS:

- Normal build/validate/review/publish; exact recipe/staging/models/bin/review.
- Полная матрица5290 PNG:5207 exact,83 изменённых только seahorse.
  Сравнение с предыдущей oerkki production явно допускает смену EXE; shaders
  прежние. Все89 candidate→production exact, включая6 неизменённых ракурсов.
- 936 FP PNG exact к предыдущей production,1872 GPU arm draws/6 cache cases;
  независимый authored FP audit с историческим legacy root reference PASS.
- 128 entity cases/256 actual restores PASS; это не вся user/world compatibility.
- Все61 R5 ledger fresh. Own16-file pre-publish snapshot сохранён; regeneration
  glTF semantic и BIN/PNG/recipe byte exact в `seahorse_anatomy_v1_repro`.

15 targeted Release tests PASS после build и после публикации. Fresh bounds
audit **FAIL17**,196 CTest зарегистрировано, весь CTest не объявлен зелёным.
Collider/physics/state map/playback speeds не менялись. Размер ресурсов и
world FPS overlays не являются matched CPU/GPU p95/performance proof.

## Воспроизведение и следующие задачи

```powershell
python tools/research/build_creature_seahorse_anatomy_pilot.py --qa-name seahorse_FRESH --source-root assetgen_output/creatures/seahorse_anatomy_pre_publish
```

Native output/label должны быть свежими. Прежние bin model hashes контекстов
own-before после публикации проверять по pre-publish snapshot; не выдавать новый
bin за старые входы. Historical anatomy17 metrics сохранены: own13 live и4 own
snapshots (penguin/lobster/oerkki/seahorse) и original17 заново сверены SHA в
`../anatomy_shapes_v1/source_freshness_after_seahorse.json`.

Далее whale profile; затем motion orc/puffin/badger/treeman и оставшиеся R6 gates.
Для seahorse ещё открыты полная исходная tail/neck expressiveness, autonomous
habitat/depth/Idle behavior, continuous GPU fades, terrain/contacts/camera/light,
control/UI-pack. Общие compatibility/performance/FP framing/walls/presets/clean
package и все56+5/три backend обязательны. Все61 R6 pending, цель active.

## Анатомические ориентиры

[Australian Museum, White’s seahorse](https://australian.museum/learn/animals/fishes/whites-seahorse-hippocampus-whitei-bleeker-1855/)
описывает хватательный хвост и движение спинным/грудными плавниками. Эти функции
обосновывают сохранение артикуляций и отдельную проверку движения в воде;
из источника не заимствованы mesh/texture. [Monterey Bay Aquarium](https://www.montereybayaquarium.org/animals-the-ocean/animals-a-to-z/big-belly-seahorse)
показывает, что выраженная передняя масса и длинный удерживающий хвост совместимы
с узнаваемой анатомией. Это ориентиры для художественного решения, не заявление
о моделировании точного биологического вида.
