# Существа: полный аудит визуала, анимации, ИИ, спавна и движения (2026-10-31)

Ветка: `develop` (HEAD `1f0c71dc1`). Предыдущие документы: `CREATURES_DEEP_AUDIT_2026-10-29_RU.md`
(его пункты P1–P3 все закрыты), `CREATURES_FIXES_PLAN_2026-10-29_RU.md`, `CREATURES_RUNTIME_AUDIT_2026-10-29_RU.md`.
Этот аудит — независимая проверка текущего кода; повторы закрытых пунктов исключены, статус известных
хвостов проверен (§5).

## Как проверялось

- Четыре независимых код-обзора: Visual/Pose, Activity (ИИ), Spawning, Locomotion/Navigation — по всем
  файлам подсистем с file:line.
- Выборочная верификация критичных находок чтением кода (GLES-шейдеры, LoadWear, FindAquaticSpawnFeetY,
  clearOnApply бота, attackCooldown) — все подтверждены.
- `ctest -L creatures -C Release`: **26/26 PASS**.
- Полный `ctest -C Release`: **184/184 PASS** (11 «известных» падений из аудита 28.10 больше не
  воспроизводятся — починены).
- `tools/audit_style.py`: 61 нарушение (было 55 на 27.10) — деградация, см. §6.

## 1. Сводка состояния

С 29.10 влиты (все merge-коммитами): glTF clip-local time + кроссфейд (`ba42af8f4`), отложенное
удаление в обходе (`bee9bce14`), застывшие факты (`687e5ccde`), спавн по взгляду под размер тела
(`205bc2f93`), melee LOS (`1115ad7c1`), перенос всадника через коллизию (`fa6abfa4c`), повёрнутые
коробки (`b4bfeab3b`), кэши моделей предметов по ревизии (`0090a9cd1`), гистерезис Run/Walk
(`4f034247d`), поворот по согласованному движению (`2823125fd`), AI round-robin по существам
(`41b43fc1e`), сбор клеток индекса диапазоном (`ebab159f1`), аллокации отрисовки (`85c538c0a`),
один источник размера коллизии (`63b44e804`), шаг вниз без прыжков (`79f346abf`), имена существ
(`876d78cd1`), свет из клетки тела (`71ff504ec`), амфибии всплывают (`637cc787d`).

Архитектура существ после фаз 0–6 и фиксов 29–31.10 в хорошем состоянии:.ExecuteIntent декомпозирован
(9 стадий), функций >300 строк в подсистеме нет, TODO/FIXME — 0, тесты проходят. Найденное ниже —
точечные баги, функциональные пробелы и остаточный долг, а не системная гниль.

## 2. Новые находки

Приоритеты: **[K]** критично, **[B]** высокий, **[C]** средний, **[N]** низкий.

### 2.1 Визуал и анимация

| # | Приоритет | Находка | Где |
|---|---|---|---|
| V1 | **K** | **Скелетная анимация сломана на GLES/Android.** Мировой путь `DrawSkinnedMesh` грузит палитру костей только через UBO-блок `BonePalette` (BindCreatureBonePaletteBlock + UploadCreatureBonePaletteGpu), а GLES-шейдер объявляет `uniform mat4 uBones[48]` — обычный массив, не UBO-блок. `uBones` никто не заполняет (uniform-фолбэк есть только в превью, `src/Gui/Preview/CreaturePreviewRenderer.cpp:89-102`) → нулевые матрицы → 35 из 56 видов схлопнуты/невидимы на Android. Цель CUBATARIUM_GLES реальная (CMakeLists.txt:829, 1845). | `shaders/gles/vshader_creature_skinned.glsl:9`; `src/Render/Engine/CreatureDrawPass.cpp:449-461` |
| V2 | **K** | **GLES-фрагментник игнорирует uTint**: `vec4(tex.rgb * creatureLit(), 1.0)` против desktop `tex.rgb * uTint.rgb, tex.a * uTint.a`. На GLES: хит-флеш не виден у rigid/bone/sprite, tint-биллборды (fire_spirit) без тонировки, alpha всегда 1. При этом skinned-вариант GLES uTint применяет — непаритет даже внутри GLES. | `shaders/gles/fshader_creature.glsl:50` vs `shaders/fshader_creature.glsl:50` |
| V3 | **B** | **wear.json читается с диска и парсится каждый кадр для каждого надетого предмета** (LoadWear: ReadAssetText + json::parse; вызов из SubmitItemOnBones на каждый кадр). Плюс FitGltfToBone каждый кадр обходит все вершины предмета. Комментарий в коде признаёт, что от per-frame JSON уже ушли для мешей — LoadWear остался. | `src/Creatures/Visual/WornEquipmentDrawer.cpp:72-115, 435-459, 475` |
| V4 | **B** | **Скины bone_skeleton мертвы**: резолвер текстур сначала перебирает базовые ключи вида (species/stem/diffuse/default/body), которые почти всегда заняты, и лишь потом `skin/<id>/...` → параметр skinId не действует. glTF-скины не поддержаны вовсе (skinId не передаётся). Для rigid скин работает через appearance. | `src/Creatures/Visual/CreatureTextureResolver.cpp:25-52`; `CreatureVisualBoneSkeleton.cpp:102-103`; `CreatureVisualGltf.cpp:154` |
| V5 | **B** | **Проваленные загрузки моделей не кэшируются негативно**: glTF/bone_skeleton при отсутствии ассета дергают диск и печатают ошибку КАЖДЫЙ кадр на каждое существо (кэш не помнит негативный результат). | `CreatureVisualGltf.cpp:45-55`; `Gltf/CreatureGltfCache.cpp:54-85`; `CreatureVisualBoneSkeleton.cpp:49-65` |
| V6 | **B** | **Рассинхрон лимита костей**: desktop/UBO 96 (hermitcrab 67 суставов), GLES/превью — жёстко 48; jointIndices — uint8_t (стихийный overflow >255 суставов). | `CreatureBonePaletteGpu.h:16`; `CreaturePreviewRenderer.cpp:90-92`; `GltfTypes.h:32` |
| V7 | C | Хрупкий контракт переиспользования палитры: `use_count()==1` корректен только из-за порядка Clear()/reset() в Render — любой новый потребитель очереди молча включит аллокацию каждый кадр. | `CreatureVisualGltf.cpp:119-122`; `CreatureVisualGltf.h:35-37` |
| V8 | C | `CreatureMeshGpuCache` не чистится при смене ресурс-паков и не ограничен; ключ — слабый 31-полиномиальный хэш без проверки коллизий (разные меши могут получить чужой VAO). | `src/ResourcePacks/CreaturePackMerge.cpp:21`; `CreatureMeshGpuHash.h:15-28` |
| V9 | C | Утечка GL-текстур при мерже паков: `IndexPngTextures` перезаписывает `out[key]` без удаления старого id (MergeCreatureRoot/MergeSkinRoot не вызывают DeleteCreatureGlTextures). | `CreatureTextureStorage.cpp:64, 88, 120-175` |
| V10 | C | `BoneSkeletonHierarchy::ComputeBoneMatrix` — O(глубина) рекурсия на каждую кость каждый кадр + строковые find по имени; флаги absolute-pivot/bind-rotation можно предвычислить. Поза — unordered_map<string,·>, пересобираемая каждый кадр (17 видов). | `BoneSkeletonHierarchy.cpp:55-158`; `BoneSkeletonPoseEngine.cpp:360` |
| V11 | C | rigid: копия `pose.Parts` (unordered_map) на существо на кадр. | `CreatureVisualRigid.cpp:186` |
| V12 | C | Кроссфейд клипов действует только на skinned-модели; для нескеинных glTF корневой трансформ переключается без фейда (щелчок). | `CreatureVisualGltf.cpp:113-134` |
| V13 | C | Aerial-профиль в Jump/Fall даёт пустую позу (ни одна кость не задана — bind pose); Swim для наземного профиля гоняет наземную походку под водой. | `BoneSkeletonPoseEngine.cpp:365-392` |
| V14 | C | Полоса здоровья — GL_LINES-каркас вместо заливки (заполнение тоже контур; lineWidth>1 на core-profile не гарантирован); подписи/бары рисуются немедленно в фазе сабмита. | `CreatureDrawPass.cpp:852-871`; `GeometryEngine.cpp:8884-8921` |
| V15 | C | `UCreatureVisualRigid::SubmitDraw` разыменует `ctx.Queue` без проверки (ранний выход проверяет только Settings; остальные бэкенды — через DrawEnabled). | `CreatureVisualRigid.cpp:192-202` |
| V16 | C | `.glb` принимается резолвером, но загрузчик читает файл как JSON → GLB всегда падает в цветной бокс (в models/items 22 .glb). | `WornEquipmentDrawer.cpp:149-150`; `Gltf/CreatureGltfLoader.cpp:404-420` |
| V17 | N | Мёртвый переходный слой: `UEngineCreatureDrawSink` не инстанцируется; 4 форвардера `UGeometryEngine::DrawCreature*` без вызывателей. Долг «WornEquipmentDrawer на ctx.Engine» — жив (`CreatureDrawContext.h:24-27`, двойная immediate-система). | `CreatureEngineDrawSink.h`; `GeometryEngine.cpp:8857-8882`; `WornEquipmentDrawer.cpp:799-809` |
| V18 | N | Мёртвые члены: `EquippedArmorIds` (заполняется, не читается), `BodyYaw` в UCreatureVisualGltf (затеняет базу), `def.visual.boneSkeleton.textureSize` (парсится, не потребляется). | `CreatureVisualBoneSkeleton.h:37`; `CreatureVisualGltf.h:28-29`; `CreatureDefinitionStorage.cpp:266-273` |
| V19 | N | Статические VAO/VBO в функциях WornEquipmentDrawer (единые на процесс, не удаляются, привязаны к контексту); глобальное mutable-состояние `g_ItemDefinitions`/`g_HidePossessedWield` без синхронизации. | `WornEquipmentDrawer.cpp:37-38, 307-313, 506-512` |
| V20 | N | OOB-чтения в glTF-загрузчике на битых файлах (accessor count не проверяется); только float-веса (normalized ubyte WEIGHTS_0 молча нескинит). Пустой identifier в geo → перезагрузка ассета каждый кадр. | `Gltf/CreatureGltfLoader.cpp:81-87, 153-271`; `CreatureBoneSkeletonLoader.cpp:225` |
| V21 | N | Дубли: WalkSwingScale ×4 копии (с разными клампами), immediate-рисование ×2, загрузка куба ×3. | `Terrestrial*PosePresenter.cpp`; `WornEquipmentDrawer.cpp`; `CreatureDrawPass.cpp:43-75` |
| V22 | N | Спрайт-бэкенд без анимации: uAnimFrame/uAnimFrameCount в шейдере есть, сеттеры всегда 0/1 (мёртвый код); fire_spirit — единственный вид-биллборд, статичный. | `fshader_creature.glsl:43-46`; `CreatureDrawPass.cpp:310-311` |
| V23 | N | Слой Pose (5 презентеров + реестр, ~600 строк) обслуживает только 3 rigid-демо-вида; продакшн-контент его не использует, логика пробега дублируется в BoneSkeletonPoseEngine и glTF независимо. | `src/Pose/**` |

### 2.2 ИИ

| # | Приоритет | Находка | Где |
|---|---|---|---|
| I1 | **B** | **Бот движется импульсами и замерзает на hitch-кадрах**: единственный агент с `clearOnApply = true` — интент стирается после первого мирового кадра (при 20 Гц ИИ и 60 fps бот движется ~1 кадр из 3), при заморозке мозгов стоит (wander-мобы идут по сохранённому интенту). | `src/Activity/Agents/BotPlayerActivityAgent.cpp:119`; `WorldViewBinding.cpp:878-881` |
| I2 | **B** | У бота нет sticky target — флаппинг цели между равноудалёнными врагами (для mob-vs-mob sticky закрыт аудитом M3, для бота — нет). | `BotPlayerActivityAgent.cpp:129-145` |
| I3 | **B** | **`attackCooldown` из каталога мёртв**: FSM ставит Melee-интент безусловно, actionTimer перезаряжается «впустую»; реальный темп — константа 0.5 с голой руки, ни один creature.json не задаёт override. Данные 1.0–2.0 с в 56 файлах не действуют. | `SimpleFsmBrain.cpp:340-343, 465-468`; `InfluenceCapability.h:36` |
| I4 | C | `TickMeleeAtControlled` не задаёт body_width в NavigationQuery (дефолт 0.6) — широкие преследователи (ogre 2.58) получают A*-пути в некомфортные коридоры. Правило скопировано в 3 местах с рассинхроном. | `SimpleFsmBrain.cpp:491-494` vs `:229`, `:354` |
| I5 | C | Flee реагирует только на управляемое существо; на мобов-атакеров нет ответа, `NotifyDamaged` используется только полоской HP — ИИ об уроне не узнаёт вовсе. | `SimpleFsmBrain.cpp:181-190`; `Creature.h:139` |
| I6 | C | Aerial-погоня без feelers/separation/stuck-recovery (наземная ветка их имеет) — оса/сова у кроны без механизма отхода. | `SimpleFsmBrain.cpp:475-490` |
| I7 | C | Дубль hostility-логики: `IsHostileTarget` в боте почти построчно повторяет `CreatureHostility` — фракционные правила надо править в двух местах. | `BotPlayerActivityAgent.cpp:33-70`; `CreatureHostility.cpp:32-75` |
| I8 | C | Aerial horizontalSpeed считается по 3D → вертикальное дрожание считается движением, DeriveAerial не проверяет XZ — вероятный механизм «осы стоит в Fly». Плюс после 12 неудачных проб направления aerial-зонд возвращает случайное как «успех». | `CreatureLocomotionFacts.cpp:53-55`; `LocomotionStateDerive.cpp:134-142`; `CreatureActivitySteering.cpp:179-180` |
| I9 | C | Wander не различает среду для амфибий/наземных: вода и берег равнозначны → пингвины заходят в воду свободно, выход только через bank-jump. | `CreatureActivitySteering.cpp:126-193` |
| I10 | N | Мёртвое: `EnsureCreature` (без вызовов), `path_finished` (не читается), `lastSeenPos` пишется в 3 местах и не читается (бот переиспользует под направление блуждания — семантический misuse), недостижимые `return true` в InfluenceResolver. | `WanderActivityAgent.cpp:87-94`; `InfluenceResolver.cpp:163-165, 206-208` |
| I11 | N | Дубли/магия: BotMoveSpeed == ResolveIntentMoveSpeed; конструкция NavigationQuery ×5; |dy|≤1.15; search_distance=32 ×5; веса сепарации; followStart = max(3.5, aggro·0.35); `behavior.moveSpeed` латентно перекрыт `locomotion.walkSpeed` (у всех равны). | `SimpleFsmBrain.cpp`; `BotPlayerActivityAgent.cpp` |
| I12 | N | Глобальный кэш снапшотов поведения (файловая статика, 1024 записи, полный clear) живёт дольше мира. | `WorldCreatureActivitySink.cpp:29-30` |

### 2.3 Спавн и зоны

| # | Приоритет | Находка | где |
|---|---|---|---|
| S1 | **K** | **Спавн полностью абиомный.** Ни одного упоминания biome/climate в конвейере; eligibility = naturalSpawnable + вес + hostile×pressure, размещение = habitat. Пингвины/тюлени спавнятся в пустыне, sand_monster — в снегу. Полей биомов в каталоге нет. | `UCreatureSpawnPlanner.cpp:14-40`; `CreatureCatalogTypes.h:21-29` |
| S2 | **K** | **Hostile спавнятся без ограничения света/времени суток** — зомби/скелеты днём вплотную к кольцу 22. API времени/света существует (DayNightFactor; свет клетки — только в рендере), геймплей не читает. | `src/Game/ModePolicy.h:65-71`; `WorldEnvironment.cpp` (нет обращений) |
| S3 | **B** | **Деспавна нет вообще** (только смерть). Далёкие существа не выгружаются: тикают, пишутся в сейв, навсегда занимают naturalWorld-лимит 64 → подпитка у игрока останавливается. Заморозка при выгрузке чанка не удаляет. | `CreaturePopulationRules.h`; `WorldEnvironment.cpp:606` |
| S4 | **B** | **Мир при создании не заселяется**: WorldGen существ не размещает («no creatures» в WorldGenContext.h), никакого initial-populate нет; свежий мир наполняется ≤1 группой / 6 с. | `WorldGenContext.h:18`; `WorldEnvironment.cpp:1444, 1490` |
| S5 | **B** | **spawn_weight вырожден: у всех 51 natural-видов вес 1.0** — взвешенный выбор фактически равномерный. | все `bin/models/creatures/*/creature.json` |
| S6 | C | **FindAquaticSpawnFeetY: мёртвый clamp** — `clamp(minFeet, minFeet, max(minFeet, maxFeet))` всегда возвращает дно; вычисление maxFeets мертво. Все водные спавнятся у дна. | `CreatureEnvironment.cpp:229-232` |
| S7 | C | Глубина воды для гигантов: порог depth ≥ bodyHeight·0.35 (магическое 0.35) — киту достаточно ~1.2 блока воды при длине 11; проверка habitat спавна — «хотя бы одна мокрая ячейка». Гиганты застревают в мелкой воде. | `CreatureEnvironment.cpp:225`; `:49` |
| S8 | C | `/spawn` использует дефолт aheadBlocks=3 без учёта размера тела (спавн по взгляду учитывает) — кит «Spawn failed». | `WorldCommands.cpp:699`; `WorldEnvironment.h:216` |
| S9 | C | Тройная повторная валидация + O(eligible × кандидаты) проб на тик (для whale ~12 тыс. GetBlock на пробу) + полный пересчёт CountNaturalPopulation на каждого члена группы. | `WorldEnvironment.cpp:1497-1531, 1545-1549, 526-567` |
| S10 | C | Спавн-решения на протухших данных: HabitatAllowsAtForSpawn (Aerial) и депенетрация идут через TTL-мемо 0.2 с. | `CreatureEnvironment.cpp:716-719, 942` |
| S11 | C | Летуны спавнятся без проверки доступа к небу (критерий — локальный inOpenAir) — пчёлы в пещерах, хотя подсказка UI обещает «нужно открытое небо». | `CreatureEnvironment.cpp:299-301, 752` |
| S12 | N | Спавн в прямой видимости игрока не исключён; beforeSpawn-счётчики неточны; глобальный немьютексный gProbeMemo; 255 как высота мира; естественный спавн работает и в Creative. | `UCreatureSpawnPlanner.cpp:47-56`; `CreatureEnvironment.cpp:278-281, 153` |

### 2.4 Движение и ограничения

| # | Приоритет | Находка | Где |
|---|---|---|---|
| M1 | **B** | **A\* игнорирует ширину тела при расширении соседей**: IsTerrestrialStandNode(to, body_height) молча берёт дефолт 0.6, хотя старт/цель проверяются с реальной шириной → широкие мобы получают маршруты в непроходимые проёмы, stuck-циклы (компенсируется стиринг-фолбэком). | `src/Navigation/WorldNavigationQueries.cpp:56, :20` |
| M2 | C | Direct-shortcuts захардкожен размер 0.6×body_height×0.6 — срезание пути узкой коробкой независимо от query.body_width. | `CreatureActivityNavigation.cpp:50` |
| M3 | C | body_width берётся только по X (boundsSize.x), а коллизия реально переворачивается X/Z-свапом → для длинных наземных A* недооценивает габарит половину времени. | `SimpleFsmBrain.cpp:229, 354`; `BotPlayerActivityAgent.cpp:171, 207` |
| M4 | C | Водные с can_fly=true (whale) фабрика ставит в Flying: нет ни гравитации, ни флюид-физики — кит из сейва в толще воды висит, над сушей летит. | `UCreatureFactory.cpp:61-67`; `Creature.cpp:270-273`; `whale/creature.json` (can_fly=true) |
| M5 | C | Приземление аэриалов только по блокам и по центру стопы: оса на другом существе/листве/над щелью никогда не переходит в Walking; в покое у аэриалов нет гравитации/дрейфа — зависание навсегда. | `CreatureLocomotionController.cpp:628-644, 137-141` |
| M6 | N | SetMode(Flying) каждый кадр для аэриалов (сброс OnGround/StanceBlend); окно step-up 0.45–1.05 шире бюджета гейта (jump_height); GetStepUpLanding дублирует поиск и игнорирует существ. | `Creature.cpp:370-374`; `WorldCollision.cpp:352, 1030-1048` |
| M7 | N | Дубли правил: флюид-торможение ×8/×6; эпсилон 0.05 ×2; сэмплинг земли ×2; классификатор флюида ×2; CanCreatureStep ≈ CanStepTerrestrial; гравитация 20 ×2; ширина 0.6 ×3; зонд wander maxClimbDrop 1.25 vs гейт jump_height. | `CreatureMotor.cpp:55`; `Creature.cpp:30-54`; `CreatureTraverseQueries.cpp:30-43, 137` |
| M8 | N | FootprintSwapped не персистится — после загрузки коробка переориентируется за кадр. Копия контроллера локомоции (~120 байт) на существо на кадр. | `CreaturePersistenceJson.h`; `Creature.cpp:307` |

## 3. Инвентарь behaviours (чего в ИИ нет)

Есть: wander (35 видов), flee (1 — овца, только от управляемого), melee_attack (18, на игрока;
mob-vs-mob фолбэк — только когда управляемого нет вообще), bot_player (1). A* — только Chase/Flee/бот;
wander — локальный стиринг.

Нет (в скобках — данные каталога для этого): реакции на урон/панику (—); LOS при выборе цели (—);
стайности (есть `spawn_group_min/max`, ИИ не использует); дома/территории/леаша (нет полей);
сна/питания/размножения (виталы тикают только у player-like); дневной/ночной активности (полей нет,
спавн тоже не читает); ranged-ИИ (TryRangedStrike — алиас melee).

## 4. Мёртвые/вырожденные данные каталога

- `bounds.source`, `bounds._derived` — парсится только rest/max/min, source/_derived не читаются.
- `spawn_weight` — читается, но все 1.0 (S5).
- `behavior.attackCooldown` — мёртв (I3).
- `behavior.moveSpeed` — латентно перекрыт walkSpeed (равны у всех).
- `visual.boneSkeleton.textureSize` — не потребляется (V18).
- Полей биомов/климата/света/времени активности не существует — S1/S2 требуют их добавить.

## 5. Статус известных хвостов (из аудита 29.10)

| Хвост | Статус |
|---|---|
| Оса периодически стоит в Fly | Открыт; механизм теперь понятен — I8 + M5 (3D-скорость, Hover-порог, посадка по центру) |
| Амфибии остаются в воде | Открыт (смягчён bank-jump) — I9: wander не предпочитает сушу |
| Mob-vs-mob только без управляемого существа | Открыт — в Creative с игроком враждебные просто бродят |
| ИИ не учитывает LOS при выборе цели | Открыт — моб у стены в Attack, удары отменяются |
| Широкие тела не пересекают канавы глубже шага | Сознательный трейд-офф (обрывы важнее) |
| Whale/shark/manatee повёрнутая коробка в игре | Не покрыто; тестов на свап футпринта нет (M8) |
| WornEquipmentDrawer на ctx.Engine | Жив (V17) |

## 6. Деградация проекта вне существ (для контекста)

- `tools/audit_style.py`: **61 нарушение** (было 55). Новые >300-строчные: `WorldOperationRunner::Tick`
  331, `HandlePrepareViewLoad` 335; выросли сверх бейзлайна: **RunFlightSim 1410 → 1987** (AppRunner.cpp:781),
  `LoadConfig` 427 → 441, `SettingsScreen::Build` 334 → 339.
- `CMakeLists.txt` — 3134 строки (монолит по-прежнему); ctest 184 теста.
- `stall_log.txt`: ResolveTextureAtlas 56–84 мс на кадр.
- `git status` — 409 незакоммиченных записей (в основном bin/, артефакты QA).

## 7. План рефакторинга

По [[staged-branch-merge-workflow]]: этап = ветка от develop, merge --no-ff. Порядок — по критичности
и зависимостям. Каждый пункт: минимальный диф + тест + проверка в Release через `tools/flight_sim_run.py`.

### Статус выполнения (обновлено 31.10, вечером)

**Этап 1 — критичные баги — СДЕЛАН** (4 ветки, ctest 186/186):
- GLES UBO-палитра + лимит 96 + uTint-паритет + тест `creature_shader_parity_test`
  (ветка `fix/creatures-gles-rendering`, merge `e37a32edc`). On-device проверка Android не выполнялась
  (сборки GLES под рукой нет) — изменение зеркалит desktop-шейдер, тест держит паритет.
- Кэш wear.json + fit-матрицы по (item, slot) с инвалидацией по ContentRevision
  (`fix/creatures-worn-wear-cache`, merge `75a6b901a`).
- Бот clearOnApply=false + sticky target + гейт атак по attackCooldown во всех 3 путях
  (`fix/creatures-ai-critical`, merge `2a9489392`). Проверено в игре: бот преследует sand_monster
  ~8 блоков с устойчивой скоростью 2.7, удары по таймеру, после убийства возвращается к игроку.
- Водный спавн в толще (ComputeAquaticSpawnFeetY, тест `creature_aquatic_spawn_test`)
  (`fix/creatures-aquatic-spawn-column`).

**Этап 2 — зоны спавна — СДЕЛАН** (ветка `feature/creature-spawn-zones`):
- Каталог: `catalog.spawn_biomes` (список id биомов реестра WorldGen; пусто = любой),
  `catalog.active_time` (day/night/any; hostile-дефолт — ночь в коде, подземелье обходит окно),
  веса редкости 1.5/1.2/1.0/0.7/0.4 (инструмент `tools/creature_spawn_zones.py`, данные — первый
  проход, тюнить свободно). Тест `creature_spawn_zones_test` (правила + валидность данных каталога).
- Фильтры в `TryNaturalPopulationSpawn`: биом кандидата (DominantBiomeIdAt поверх кэша
  SampleBiomeEnvironmentAt), окно времени (ночь/день + подземелье через FindHighestSolidY −2),
  аэриалам требуется открытое небо (кандидат ≥ поверхности).
- Деспавн natural-существ дальше 96 блоков от игрока (ручные/командные/QA/legacy — навсегда;
  QA-ручка CUBA_QA_DESPAWN_RADIUS).
- Заселение нового мира: burst 8 групп с интервалом 1.5 с (пока не загружен creatures.json).
- Мелочи: глубина для гигантов 0.35→0.75 высоты тела; `/spawn` размерозависимый SpawnAheadDistance.
- Диагностика: `CUBA_QA_SPAWNLOG=<файл>` — тик популяции + кандидаты (биом/поверхность/viable).
- Проверено в игре (QA_BEH, день): hostile не спавнятся днём, виды соответствуют биомам
  (tortoise/fox на beach/plains), burst 0→15 существ за ~20 с; деспавн при радиусе 30 держит
  популяцию 0–2 против монотонного роста при 96.
- НЕ сделано из плана этапа 2: однократная валидация S9 (тройная проверка осталась), исключение
  спавна в прямой видимости (S12) — мелкие, оставить на следующий проход.

Этапы 3–6 (ИИ, визуал, движение, проектный долг) — не начаты.

### Этап 1 — критичные баги (fix/creatures-critical-…; 1–2 ветки)

1. **GLES скелетная анимация** (V1): UBO-блок `BonePalette` в gles-шейдере (GLES 3.0 поддерживает
   UBO) + выровнять лимит 96 (V6); альтернатива — uniform-загрузка в мировом пути. Проверка: сборка
   CUBATARIUM_GLES, скрин существ в Android-прогоне.
2. **GLES uTint** (V2): применить uTint как в desktop во всех gles-вариантах fshader_creature*.
3. **LoadWear кэш** (V3): кэш wear.json по itemId (инвалидация по ContentRevision предметов, паттерн
   SyncItemModelCache уже есть), FitGltfToBone — кэшировать по (item, bone) в бокс.
4. **FindAquaticSpawnFeetY** (S6): вернуть осмысленный clamp (спавн в толще, не только дно).
5. **Бот clearOnApply** (I1) + sticky target (I2).
6. **attackCooldown** (I3): гейтить Melee-интент таймером (и снять мёртвые данные или прокинуть
   bare_hand interval в каталог).

### Этап 2 — зоны спавна (feature/creature-spawn-zones; главный функциональный пробел)

1. **Каталог**: поля `biomes` (список тегов биомов или климатические диапазоны temperature/moisture),
   `spawn_light` (min/max sky-light), `active_time` (day/night/any). Парсинг + тесты каталога.
2. **Планировщик**: фильтр по биому в BuildEligibleOptions — через `UWorld::SampleBiomeEnvironmentAt`
   (уже возвращает BiomeWeights/temperature/moisture; используется погодой). UWorldEnvironment имеет
   доступ через Owner. Данные: пустынные виды — desert/savanna; пингвины/тюлени — tundra/snowy/океан;
   hostiles — по свету.
3. **Свет/время** (S2): hostile natural-спавн только при небе < порога или ночью (DayNightFactor);
   геймплейный доступ к sky-light клетки (перенести сэмпл из рендера в World API).
4. **Деспавн** (S3): natural-существа дальше X (например, 96) от всех игроков удаляются (кроме
   именованных/верховых/заспавненных вручную — нужен spawn_source, уже есть CreatureSpawnSource.h);
   сейв-фильтр по дистанции опционально. Лимит naturalWorld считать по «активной зоне».
5. **Веса и группы** (S5): настроить spawn_weight по редкости; группу и биом связать (рыбы — стая
   в океане, пингвины — колония на берегу).
6. **Мелочи**: глубина для гигантов (S7), небо для летунов (S11), `/spawn` под размер (S8),
   однократная валидация (S9).
7. **Initial populate** (S4, опционально): при создании мира — расселение по биомам (переиспользуя
   планировщик), чтобы свежий мир не был пустым 6+ минут.

Проверка: QA-сцены по биомам (полёт над пустыней/тундрой/океаном), лог спавнов по видам×биомам,
тесты planner-фильтра.

### Этап 3 — ИИ (refactor/creatures-ai-…)

1. **LOS в ИИ**: при входе в Attack — HasMeleeLineOfSight (уже в CreatureReach.h); нет LOS → Chase к
   lastSeenPos; цель потеряна → поиск вокруг lastSeenPos (поле уже есть, оживить I10).
2. **Mob-vs-mob в Creative** (паритет): включать TickMeleeFallback, когда управляемое существо есть,
   но не таргетимо (Creative) — сейчас ветка недостижима.
3. **Реакция на урон** (I5): NotifyDamaged → flee для мирных (источник — любой), aggro для hostile.
4. **Aerial chase** (I6): feelers + separation из наземной ветки.
5. **Wander-среда** (I9): амфибиям предпочтение суши (штраф на направления в воду), наземным —
   избегание воды (зонд уже может это проверить).
6. **body_width консолидация** (I4, M2, M3): один источник max(0.6, max(bounds.x, bounds.z) с учётом
   свапа); пробросить в NavigationQuery везде, включая direct-shortcuts.
7. Чистка: дубли hostility (I7), BotMoveSpeed, NavigationQuery ×5 → фабрика, мёртвый код (I10).

Тесты: SimpleFsmBrain (переходы/sticky/LOS) — сейчас тестов нет вовсе; CreatureHostility; поведение
амфибий у берега (доля времени на суше за 60 с).

### Этап 4 — визуал/анимация (refactor/creatures-visual-2)

1. **Негативное кэширование** провалов загрузки + лог once (V5).
2. **Скины**: приоритет skin-текстуры над базовой в резолвере (V4); glTF-скины — смена текстуры
   модели (план в CREATURE_MIGRATION_GLTF).
3. **WornEquipmentDrawer**: убрать ctx.Engine (долг V17), кэши (V3 — этап 1), статическое
   GL-состояние в объекты (V19).
4. **Кроссфейд нескеинных** (V12); **Aerial Jump/Fall поза + Swim наземных** (V13).
5. **Полоса здоровья** заливкой (кврд-меш) + пакетная отрисовка подписей (V14).
6. **Кости**: предвычисление флагов иерархии +_pose-буферы без map (V10); rigid без копии Parts (V11).
7. **Кэши/GL**: MeshGpuCache инвалидация по ревизии паков + проверка коллизий хэша (V8), утечка
   текстур (V9), .glb (V16), OOB glTF (V20).
8. Чистка: мёртвый слой UEngineCreatureDrawSink/DrawCreature* (V17), EquippedArmorIds/BodyYaw/
   textureSize (V18), WalkSwingScale → одна функция (V21), спрайт-анимация — либо кадры, либо убрать
   мёртвые uniform'ы (V22). Решение по слою Pose (V23): 3 демо-вида — оставить или мигрировать.

### Этап 5 — движение (refactor/creatures-motion-2)

1. **A\***: ширина в расширении соседей (M1) + консолидация body_width (этап 3.6).
2. **Аэриалы**: гравитация/дрейф в покое и посадка по объёму стопы (M5) + XZ-гард Fly/Hover (I8) —
   закрывает «осу стоит в Fly».
3. **Водные can_fly** (M4): фабрика ставит Swimming для aquatic независимо от can_fly; данные кита
   поправить.
4. **FootprintSwapped** в сейв (M8) + тест свапа для водных длинных (незакрытая проверка аудита 29.10).
5. Согласование констант: step-up окно vs гейт; флюид-коэффициенты; один источник 0.6/0.05/1.25 (M7).
6. Тесты: UpdateFootprintOrientation, CanStepTerrestrial с шириной, амфибийный выход на берег.

### Этап 6 — проектный долг (вне фокуса, по плану cubatarium-audit-2026-10)

RunFlightSim 1987 строк (+577 за 4 дня) — вернуть под бейзлайн, новые >300-функции разбить,
ResolveTextureAtlas-стойлы в stall_log. CMake-монолит и repo bloat — по прежнему плану (фазы 0–2).

## 8. Порядок и обоснование

1 → 2 → 3 → 4/5 (параллельно) → 6. Этап 1 — дешёвый и закрывает пользовательски видимые баги
(Android-рендер, темп атак, спавн водных). Этап 2 — самый большой функциональный пробел и прямо по
фокусу «зоны спавна»; его данные (биомы/свет) потом переиспользует ИИ (этап 3: активность по времени
суток). Этапы 4/5 независимы друг от друга. Этап 6 — фон.
