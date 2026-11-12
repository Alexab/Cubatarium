# Art bible v1: узнаваемый вид, собственное оформление

Обновлено 10.11.2026 после аудита `b35f00a75..d35cc74e1`. Имя файла оставлено
для совместимости ссылок. Стиль «живой атлас» означает узнаваемую фауну с
качеством не ниже лучших исходных свойств. Радикальное отличие не является целью.

Корова остаётся коровой, барсук — барсуком, морской конёк — вертикальным
S-образным пловцом. Их типичные цвета и пропорции сохраняются; рисунок, UV,
детали формы и отделка авторские. Монстры допускают больше свободы, но не
должны становиться одним общим humanoid в разных цветах.

Порядок: силуэт, затем 3–5 крупных зон, затем лицо. Мех и полосы идут вдоль части, не шумом по всему атласу. У вида один главный и два вторичных признака, записанные в `tools/creature_recipes/<id>.yaml`.

Палитры: `tools/creature_palettes.yaml` (`meadow`, `forest`, `river`, `peat`, `mineral`, `traveler`, `chitin`, `flame`).

Пилотные листы трёх концептов (сбоку и в три четверти):

- [cow](research/concepts/cow_concepts.png) — выбран `wedge_muzzle`
- [badger](research/concepts/badger_concepts.png) — выбран `digger`
- [rigid_demo_walker](research/concepts/rigid_demo_walker_concepts.png) — выбран `tuber`

Эти v0-листы — исторические sketch/wiring artifacts, не утверждённые эталоны.
Три масштаба одной массы (`primary`, `tall`, `long`) не заменяют три concept.
Следующие эталоны: cow / badger / rigid_demo_walker, затем остальные пилоты.

Правила v1:

- До текста и микродеталей проверить вид по silhouette, side/front/3/4/top,
  в том числе в общей block-scale рамке с baseline.
- Сохранить главные анатомические признаки: ноги/суставы/усики/клюв/хвост/
  крыло/шею по виду. Не удалять их, чтобы упростить builder.
- Material palette не равна species palette: барсук и panda чёрно-белые,
  fox рыжая, wasp контрастная жёлто-чёрная, с нашими оттенками и рисунком.
- Текстура описывает материал и лицо осмысленными кластерами. Плоская заливка,
  одинаковая жёлтая морда и произвольные пятна не считаются готовым рисунком.
- Сохранять рабочие pivots, anchors, цепи и качество деформации. Для сложного
  вида нужны собственные rig/clip inputs, а не общий X-swing трёх ключей.
- Clip names не являются motion review. Проверять реальные циклы, опоры,
  оси плавания/взмахов и подготовку атаки.
- Иконка показывает модель, не UV-атлас. Каждый skin проверяется надетым.
- Для экипировки разделять bone motion и authored wear/grip frame. Проверять
  actual anatomy и внутреннюю полость, не считать pivot или общий AABB
  готовой посадкой. Hat accessory не заменяет объём head. На узких конечностях
  сокращать частоту рисунка и смотреть боковые/задние виды в движении.
  Inventory и worn glTF в одной папке должны иметь независимые material cache
  keys. Текущий sampled пример и ограничения: research/creature-r6-v2/equipment-fit/README.md.
- Собственные исходники и воспроизводимость обязательны; hand-authored
  модель и доводка в DCC допустимы. Простой параметрический шаблон не обязателен.

Актуальные [аудит](CREATURE_REDESIGN_COMMIT_AUDIT_2026-11-10_RU.md) и
[план §13](CREATURE_ORIGINAL_REDESIGN_RESEARCH_PLAN_2026-11-10_RU.md#13-актуальный-план-после-выпуска-creaturegen-и-замечаний-пользователя).


## Материалы и UV при skin variants (R6 уточнение)

Новая текстура заменяет материал, сохраняя UV layout исходной детали.
Имена PNG не выбирают новую геометрию/UV. Explicit texture_map имеет приоритет;
немаппированные G/R/sprite материалы остаются базовыми, отсутствующий mapped
PNG возвращает исходный материал. B whole-mesh diffuse convention сохранён
для существующих пяти skins; G whole atlas требует явного body→diffuse.
Применимость skin проверяется один раз в appearance-resolver; world, preview
и FP используют этот результат, без отдельного raw SkinId обхода.

Factory route проверяется по фактической отрисовке: fire_spirit использует
sprite.billboard при declared rigid_voxels. Сохранять исходный эфирный вид;
не превращать sprite в объёмную модель ради формального backend count.
Native297/GPURGB891 и exact61-preview5290 подтверждают только описанный
bounded subset; полная FP/save/reload/state/performance приёмка обязательна.


## FP arm source при изменении B recipes

FP должен использовать именованные actual arm geometry/UV, а не предполагать
40/16 и4×12×4 у каждой руки. Uniform presentation scale сохраняет thin skeleton
и пропорции; left source mesh не заменяется отражением right UV. Текущие atlas
layouts/rig/animation channels сохраняются, новые allocations проходят native
FP source-buffer/skin проверки. Проверка Source UV не является полной art
приёмкой FP framing, camera, held animation, use presets и world interaction.
Последняя evidence/границы: research/creature-r6-v2/fp-save/README.md.

## Уточнение проверки анатомической массы — 12 ноября

Не оценивать толщину корпуса по union AABB анимации: у барсука большой размах
возникает из roll. И не считать совпадение AABB сохранением формы: penguin
имел похожие крайние размеры при меньшей боковой полноте. Сравнивать bind
silhouettes front/side/top в общем мировом масштабе, затем выделять torso и
appendages, проверять native ракурсы и движение. Площадь opaque проекции —
диагностика геометрии, не объём тканей и не окончательная оценка texture alpha.

Править отдельные собственные recipe части с сохранением скелета/пивотов и
проверкой весов/animation arrays; пересчитать UV allocation и rebake по новым
texel размерам. Не растягивать старую текстуру и не исправлять collision box
ради art test. Первый penguin belly proposal был отклонён за выступающую
плоскую панель; умеренный v2 принят только после трёхсторонних native листов,
полного Idle/Walk оборота и настоящего водного движения. Сохранять отклонённые
варианты и их причины; зелёный rig/format тест не является art acceptance.

Источник решения: research/creature-r6-v2/envelopes/anatomy_shapes_v1/README.md
и penguin_belly_v2/{README,revision_review,publication_audit}. Все R6 pending.


## Уточнение по lobster v3: перенос bind требует проверки траекторий

Похожий AABB недостаточен: сравнивать распределение массы панциря, длину
клешней и положение коленей/стоп на одинаковом масштабе. Исправлять собственный
рецепт; оригинал использовать только в независимом research comparison.
Новые UV выделять под новые размеры и заново запекать surface material.

При переносе суставов сохранить hierarchy/семантику и пересчитать inverse binds.
Анимируемые translations проверить отдельно: старые абсолютные clip-local значения
могут отменить новый bind, а uniform scale может увеличить foot lift. Проверять
плотно во времени и joints, и root, и full mesh: contact auto-ground способен
скрыть ошибку стоп снижением корпуса. Нужен отрицательный контроль и attachment
проверка. В lobster v3 сохранены90 angular tracks, но18 bind positions и16
translation tracks сознательно изменены; не объявлять весь rig byte-identical.

Приёмка native требует original/own-before/candidate, полного поворота, action
и world route. Для benthic существа фактический Tread/onFluidBottom — отдельная
проверка, его нельзя подменять ожиданием свободного Swim. Сохранение числа
суставов/треугольников и размера atlas не доказывает art/performance acceptance.
Протокол: [lobster v3](research/creature-r6-v2/envelopes/lobster_anatomy_v3/README.md).


## Уточнение по oerkki: локальная масса и честное сравнение UV

Восстанавливать толщину конечностей и корпуса отдельно от ширины постановки:
у oerkki руки/ноги тонки в профиль, но own ноги уже стоят шире original.
Сохранять центры, pivot/skin/animation arrays, если изменение суставов не нужно.
Размеры authored cubes целочисленные в texels; half-texel origins допустимы.
При изменении размеров заново выделять UV и запекать собственный surface atlas.
Не трактовать каждый constant-UV triangle как новый дефект: exporter сжимает
однотексельные полосы к texel centres. Сравнивать с before и размером острова,
проверять реальные pixels; этот частный случай не оправдывает UV collapse
многотексельных поверхностей. Near-black source ограничивает texture judgement,
его silhouettes и sampled poses не доказывают полную motion/lighting fidelity.
[Oerkki R5 protocol](research/creature-r6-v2/envelopes/oerkki_anatomy_v2/README.md).


## Уточнение по seahorse: масса отдельно от articulation и input route

Переднюю массу trunk расширять с контролем верхнего сечения и заднего края,
чтобы сохранить neck/crest attachments; не увеличивать весь AABB. Новые UV
и поверхность запекать из своего рецепта. Exact joints/weights/samplers
подтверждают сохранение собственной артикуляции, но не равную выразительность
оригиналу. Отдельно сравнивать tail/neck range и все состояния.

Free-swim проверять с обычным вертикальным input: горизонтальный-only сценарий
может опустить существо на дно и доказать только Tread. QA driver не должен
назначать facts/state/pose/position. Пауза XZ input не равна полной физической
остановке: aquatic facts учитывают3D travel. Native free-fluid probe без
supported frames не даёт ground-contact verdict. Impulse depth controller
не является приёмкой автономного hover/AI или новой buoyancy.
[Seahorse protocol](research/creature-r6-v2/envelopes/seahorse_anatomy_v1/README.md).


## Уточнение по whale: направление/центр и признаки над новой поверхностью

Перед сравнением native front/side проверить semantic forward и продольный
центр. Actor origin и одинаковая camera не гарантируют одинакового анатомического
ракурса: у whale исходный reference обращён иначе и смещён вдоль корпуса.
Допустимое research выравнивание — отдельная копия с явно записанным постоянным
rigid transform, source SHA и проверкой неизменности mesh/animation/material.
Original snapshot и production recipe от неё не зависят. Не выдавать такой кадр
за raw original при исходных visual settings. Проверять aligned и raw листы.

При наращивании корпуса вокруг прежнего spine согласовывать реальные сечения
head/trunk/rump/tail с одинаковыми weights на стыках во времени. Отдельно
проверять blowhole/fin/crown: более высокая оболочка может спрятать старую деталь.
Положительный cap clearance и embedded base проверять на exported vertices,
с реальным отрицательным prototype control. Совпадение projection area или шести
сечений не заменяет girth/intersections/watertight/art/motion приёмку.
[Whale v2 protocol](research/creature-r6-v2/envelopes/whale_anatomy_v2/README.md).


## Уточнение по motion: семантика, skin contributions и несколько ритмов

Перед изменением движения сопоставлять actual state_map/clip и реальный период
asset duration/runtime speed с travelScale. Walk может быть alias на Fly,
а записанный animation channel не двигает вершины, если weights к нему нулевые.
Проверять skin contributions и активные palettes, а не только имя bone/clip/AABB.
Собственный articulated rig сохранять при переносе характера legacy whole-mesh sway.

Разделять частоту шага/wingbeat и медленную пластику корпуса. Один долгий clip
может содержать несколько выпеченных gait cycles и один body sway, с сохранением
прежних ключей по elapsed time. Простое растяжение всех ключей замедлит конечности;
усиление sway на каждом быстром шаге может создать jitter. Проверять local poses
по одинаковому времени, root clearance, C0 loop seams и world stance/stride.
Normalized macro phase не является тем же stride phase. Локальные каналы могут
сохраниться при изменившихся world-space траекториях — не объявлять их идентичными.

Рост baked animation keys требует matched load/memory/pose costs. В текущем G
sampler есть linear key scan; его стоимость измерить до распространения длинных
loops. Для capture parity изменённый период должен быть явно объявлен по виду/
state/factor, все прочие metadata остаются exact. Плохой фактор обязан отвергаться.
[Диагностика](research/creature-r6-v2/envelopes/motion_semantics_v2/README.md),
[Orc R5 protocol](research/creature-r6-v2/envelopes/orc_sway_v1/README.md).


### Multi-rate bake: измеренная цена и interval semantics (2026-11-12)

Длинные clips требуют budget по keys, retained CPU storage и warm/cold load,
а не только triangles/atlas. На Orc sway учтённый vector/string heap вырос
158356→398400 B, warm loader median2.4363→3.8618ms. Снижение sampler CPU
не устраняет рост памяти. Не урезать articulated rig/UV/анимацию для формального
BIN cap; сравнивать bounded bake/сжатие с точностью поз, clearance и cadence.

Runtime сохраняет прежний scan до128 ключей и использует lower_bound от key1
для длинных каналов. Exact-key bracket и float rounding должны сохраняться.
Sampler timing не заменяет matched actual-world creature-pass CPU/GPU/frame
performance gate. [Измерения и native regression](research/creature-r6-v2/gltf-sampling-perf-v1/README.md).
Все61 R6 pending.


### Actual-world timings не подменяются sampler benchmark (2026-11-12)

Orc world-series подтвердила более дешёвый UpdatePose с новым lookup, но
наблюдаемые median-run p95 полного CPU pass/GPU interval против короткого
pre-sway превышают provisional10% gate (+27.2%/+41.3%). Uneven host load и
до1.976× repeat GPU p95 не позволяют отделить причинный regression от noise.
Gate остаётся открытым; не использовать это как допуск для массового bake.
Фиксированный population QA fixture, GPU command interval, individual window
frame и retained CPU sampler имеют разный scope и должны маркироваться отдельно.
[World evidence и ограничения](research/creature-r6-v2/world-pass-perf-v1/README.md).
Полная body/FP RGBA parity финального Release подтверждена; все61 R6 pending.


### Badger: общий clock и root contact resolution (2026-11-12)

Не выбирать художественный motion pilot только по четырём normalized phases.
Для разных macroloop periods нужны native captures на общих elapsed часах с
явно указанными travelScale, phase sampling error и GIF boundary. Период
конечностей и медленный перенос массы проверяются раздельно.

Badger v1 contact128steps/stride дал gap0.001313block и отклонён. V2 использует
256Walk/224Run, при неизменных limb keys и13-joint rig: gap≤0.000786block на2049
samples/clip. Root bake density выбирается по actual skinned contact error,
а не по удобному общему числу ключей. Не вводить shape/UV/rig упрощения ради BIN.
Стоимость тоже измерена: heap64508→160988B, sampler≈+8…16% к live; pilot не
опубликован и не принят по performance/world stance.
[Листы, GIF, инварианты и ограничения](research/creature-r6-v2/envelopes/badger_sway_v2/README.md).


### Сохранять motion через проверяемую оптимизацию (2026-11-12)

Рост baked motion не компенсировать удалением суставов/текстур/ключей без
художественного основания. Проверяемый hint key interval сократил sampler
цену multi-rate badger при exact brackets,415098 interval probes и142684
реальных palette/173430 root float-bit comparisons. Uniform estimate должен
проверяться фактической парой timestamps, иначе lower_bound; exact-key
предыдущая пара и interpolation arithmetic сохраняются.

Полные5290 body/936 FP PNG и165 pilot PNG/2156 transition data exact на новом
Release. В одном fixed-world badger fixture CPU pass worst observed ratio1.0803
укладывается в provisional10%; GPU/frame controls всё ещё нестабильны и gate
открыт. Это не право автоматически принимать art/stance или все56+5.
[Протокол](research/creature-r6-v2/gltf-hint-perf-v1/README.md).


### Опорная фаза проверяется в движущемся мире (2026-11-12)

Сохранять выразительность/rig и форму фаз, но не фиксировать неверный elapsed
cadence. Preview unit travelScale не заменяет проверку actual motor speed и
реального clip clock. У badger stride0.35s в таком preview оказался1.591s в
мире. Двойная частота дала0.795s, но не устранила footskate; вариант не принят.

Для grounded семейства recipe должен описывать stance/swing, semantic support
anchors и slow-body rhythm независимо. Опорное окно задаётся до замера candidate
world drift. Height/near-zero floor — только геометрическая диагностика, не
доказательство опоры. Наблюдать claw/toe/heel поверхности, включая поднятые в
bind вершины. Проверять joint limits/достижимость/целостность ног и плавный
release/plant; rootY clearance не считать XZ foot locking. Не прививать IK
без family-specific native review и budget. Run требует своего world route.
[Эксперимент и первичные источники](research/creature-r6-v2/badger-stance-v3/README.md).


### Качество rig допускает дополнительную артикуляцию (2026-11-12)

Сохранение скелета означает сохранение/улучшение его возможностей и деформации,
а не запрет на joints, необходимые для опоры. Перед новой анимацией — identity
control с прежними bind shape/UV/PNG и существующими motion, затем ненулевой
skin stress: volume, связи claw/toe/sole, limits, pole и плавный plant/release.
Нулевой контроль не гарантирует хорошую ненулевую деформацию; несколько pixels
разницы нельзя объявлять exact. Любые hierarchy revisions остаются unpublished
до native/world/performance/compatibility review.

Разделять выразительный upper-body rhythm и hips/support: в scratch badger
сохранены20° верхнего корпуса при8° hip target. Это ещё не foot locking.
Flat-paw reach — частный необходимый тест; естественные heel/toe roll и другой
gait остаются вариантами. Не навязывать один gait/угол всем семействам.
[Данные и просмотренные листы](research/creature-r6-v2/badger-hip-control-v1/README.md).


### Опора проверяет геометрию, не только суставы (2026-11-12)

Достижимые joint targets не гарантируют сохранения mesh: первая badger paw
привязка сминала whole foot block при IK и проникала под plane на59mm. Такой
дефект не компенсировать root lift или увеличением floor tolerance. Целую жёсткую
лапу и когти можно привязать к foot joint, сохранив geometry/UV/PNG; плавное
смешивание применять там, где для него подходит топология, с проверкой объёма
и соединений. Rigid paw вариант устранил этот конкретный дефект; ещё не art pass.

Проверять опору по заранее заданным фазам и fixed surface points в реальном
motor route. Height-only candidates и неподвижные точки в воздухе не являются
footplant. Actual motion clock нужен наряду с геометрией: baked nominal шаг при
минимальном travelScale может скользить на малой скорости. Параметры mode,
joint limits, gait/heel-toe и budgets проверяются по семействам; нельзя переносить
один badger fixture на все56+5/три backend.
[Данные, native/world визуализации и ограничения](research/creature-r6-v2/badger-stance-ik-v2/README.md).


### Ограниченный выпуск по указанию пользователя —12ноября2026

Текущий каталог56+5 выпущен как `published_bounded`, с явным backlog и без
заявления полной R6. Строгие критерии этой art bible сохраняются для последующей
приёмки; они не расширяют завершённую90-minute цель обратно до бесконечной
доводки. [Выпуск и листы](research/creature-timebox-2026-11-12/README.md),
[последующая работа](CREATURE_RELEASE_BACKLOG_2026-11-12_RU.md).
