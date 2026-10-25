# Аудит и план глубокого рефакторинга подсистемы существ

- Дата: 2026-10-24
- База аудита: `develop` / `c2a3a6d123457778f5b54820034ce38e36bc25fc`
- Рабочие ветки: `refactor/3..9-creatures-*` (по ветке на фазу, merge `--no-ff` в `develop`)
- Область: визуал существ, спавн/популяция/persistence, логика движения и ИИ, взаимодействия (мир / друг с другом / игрок)
- Объём согласован: поведение сохраняется; новые фичи (размножение, приручение, LOD, освещение) — в бэклог (§7). Первая предметная область после общефиксов — визуал.

## 1. Текущее устройство

Определения: 57 видов в `models/creatures/<вид>/creature.json` (1 игрок, 1 бот, 3 rigid-демо, 52 естественно-спавнящихся: 38 пассивных группами 2–4, 14 hostile группами 1–2). Визуальные бэкенды: 36 `gltf_skeleton`, 17 `bone_skeleton`, 3 `rigid_voxels`, 1 `sprite`. Скины: 5 в `models/skins/`.

| Слой | Где | Комментарий |
|---|---|---|
| Определения | `src/Creatures/Definition/` | `CreatureDefinitionStorage::LoadFile` — 392 строки в одной функции (`CreatureDefinitionStorage.cpp:105-496`) |
| Сущность | `src/Creatures/Core/` | `UCreature` агрегирует локомоцию/инвентарь/виталы/броню/статусы/визуал; `ExecuteIntent` — 242 строки (`Creature.cpp:263`) |
| Спавн | `src/World/Environment/WorldEnvironment.cpp` (1 433 строки) | Реальный движок спавна живёт здесь, НЕ в `src/Creatures/Spawning/` (там только header-only политика: `CreaturePopulationRules.h`, `CreatureSpawnSelection.h`, `CreatureLoadRecovery.h`) |
| Среда/хабитат | `src/Creatures/Environment/` | Пробы флюидов/грунта строковым поиском по имени блока |
| Локомоция | `src/Creatures/Locomotion/` | Контроллер написан под камеру игрока, переиспользуется для NPC; игрок-локомоция фактически живёт в `Render/Camera/Camera.cpp` |
| Навигация | `src/Navigation/` | A* по stand-нодам, глобальный бюджет раскрытий на тик |
| ИИ | `src/Activity/` | 4 агента (wander/flee/melee_attack/bot_player), единственный «мозг» — `USimpleFsmBrain` |
| Бой/влияние | `src/Creatures/Combat/`, `src/Creatures/Influence/` | Melee/ranged hitscan, статусы, dig-сессии игрока |
| Визуал | `src/Creatures/Visual/` | 4 бэкенда + кэши + очередь; интеграция — `src/Render/Engine/CreatureDrawPass.cpp` и God-объект `UGeometryEngine` (9 158 строк) |
| Тик | `src/World/Core/WorldViewBinding.cpp` `RunLegacyPhysicsFrame` (373 строки, с `:422`) | Один поток; гейты: activity-интервал, stress round-robin, LOD пути >48 блоков |

Ключевые цепочки:

```
Спавн:   RunLegacyPhysicsFrame:490 → TickNaturalPopulation (6 с) → SpawnCreature → ModePolicy →
         AdjustSpawnBodyOrigin/Depenetrate → HabitatAllowsAtForSpawn → UCreature + CreateCreatureVisual
ИИ:      TickActivity:1122 → ActivityDirector.TickAgents → агент → sink.SetIntent →
         → (следующий физический кадр) UCreature::ExecuteIntent → motor → habitat-гейт → facts
Визуал:  CreatureDrawPass::Render (CreatureDrawPass.cpp:452) → ForEachCreature → cull →
         SetAppearance (перерезолв каждый кадр) → UpdatePose → SubmitDraw → Queue.Flush → GeometryEngine
Игрок:   Camera::DoMovement (свой локомошн-контроллер) → синк в существо (WorldViewBinding.cpp:549-603);
         влияние: PlayerInteractionRouter → InfluenceResolver/Apply (WorldViewBinding.cpp:687-737)
```

## 2. Находки

Приоритеты: **P0** = дефект корректности (исправлять всегда), **P1** = архитектура/производительность, влияющие на план, **P2** = гигиена.

### 2.1. Визуал

| # | Пр | Находка |
|---|---|---|
| V1 | P0 | Рассинхрон источников yaw-offset: Rigid читает `creature.GetModelYawOffsetDeg()` (`CreatureVisualRigid.cpp:180`), Gltf/BoneSkeleton — `animDef.visual.*` (`CreatureVisualGltf.cpp:36`, `CreatureVisualBoneSkeleton.cpp:37`) — бэкенды расходятся для одного определения |
| V2 | P0 | `SampleGltfRootTransform` сэмплирует root-translation, но в матрицу не включает (`CreatureGltfAnimPlayer.cpp:52-75`) — анимация корня теряется |
| V3 | P0 | Skinned-шейдер без `uTint` (`fshader_creature_skinned.glsl`, gles-зеркало) — hit-flash невидим на glTF-существах |
| V4 | P0 | `CreatureMeshGpuCache` хэширует каждый float меша на каждом вызове, каждый кадр (`CreatureMeshGpuCache.cpp:21-97`); GL-генерация вне лока (double-create race) |
| V5 | P0 | Утечки: `CreatureTextureStorage.cpp:74` — `clear()` без `glDeleteTextures`; статик-кэши `WornEquipmentDrawer` (`:351-379`, `:661-670`) не чистятся; `CreatureRigidModelLoader.cpp:37` — static-кэш без мьютекса |
| V6 | P1 | `GetResolvedAppearance` пересчитывается на существо каждый кадр (`CreatureDrawPass.cpp:544`) — строковые lookup'ы + копия вектора |
| V7 | P1 | `BoneMatrices` — `std::vector` по значению на каждый примитив (`CreatureDrawRequest.h:30`); UBO-палитра 4 КБ грузится на каждый prim (`CreatureBonePaletteGpu.cpp:69`) вместо раза в кадр на существо |
| V8 | P1 | Сортировка очереди по числовому GL-идентификатору текстуры (`CreatureDrawQueue.cpp:18-27`) — почти бесполезный сигнал для батчинга; настоящего батчинга/инстансинга нет |
| V9 | P1 | Sprite рисуется мимо очереди и лезет в `engine.GetWorld()->GetCurrentUserCamera()` (`CreatureVisualSprite.cpp:62-81`) — Render←Visual←World back-edge |
| V10 | P1 | Дубли между бэкендами: fallback-цепочка текстурного ключа (3 копии: Gltf `:38-47`, BoneSkeleton `:40-53`, Sprite `:30-36`), логирование, guard-ы настроек |
| V11 | P1 | `Gui/Preview/CreaturePreviewRenderer.cpp:400-742` — 342-строчная пере-реализация всех бэкендов для превью со своими VAO-кэшами |
| V12 | P1 | Кэши несогласованы: `CreatureBoneSkeletonCache::SetCreaturesRoot` не чистит записи (`CreatureBoneSkeletonCache.cpp:16-20`), `CreatureGltfCache` чистит; оба не выкидывают мёртвые weak_ptr-ключи |
| V13 | P1 | `IUCreatureVisual::SubmitDraw(UGeometryEngine&, …)` заставляет бэкенды знать God-объект на 9k строк |
| V14 | P2 | Мёртвое: `CreatureGltfAsset.h` (0 пользователей), пара-алиас `DrawGltfMesh` (`CreatureDrawPass.cpp:401` + `GeometryEngine.cpp:8873`), `PushOrDraw` (`WornEquipmentDrawer.cpp:458-482`), `CreatureRigSpec::partIds`, `clips.start/end` парсятся, но не используются, конфиг `fire_spirit` (gltf-бэкенд перекрыт billboard-приоритетом фабрики) |
| V15 | P2 | `ReadVec3/ReadVec4` продублированы в 6 файлах; magic numbers: speed-gate `0.04/0.22/1.5` (`CreatureVisualGltf.cpp:96-106`), `kCreatureBonePaletteMaxBones=64` обязан совпадать с шейдером вручную |
| V16 | P1 | Тесты не покрывают: бэкенды визуала, `ResolveCreatureAppearance`, фабрику/fallback, очередь, `WornEquipmentDrawer`, `CreatureTextureResolver` |

### 2.2. Спавн, популяция, persistence

| # | Пр | Находка |
|---|---|---|
| S1 | P0 | `SaveCreatures` (`WorldEnvironment.cpp:1071-1105`): запись без tmp+rename (краш в середине = битый сейв), без проверки ошибок потока; `format_version` пишется, но при загрузке не проверяется |
| S2 | P0 | `LoadCreatures:961` разыменовывает `CreatureDefinitions->Get(type)` без null-check хранилища (в отличие от `SpawnCreature:326`) — краш при загрузке сущностей до подключения определений; `<filesystem>` не включён явно |
| S3 | P0 | Конструкция существа продублирована и разошлась: `SpawnCreature:432-503` vs `LoadCreatures:973-1008` (load-путь не применяет flight-клэмп выживания и др.) |
| S4 | P1 | Подсчёт населения — полный `ForEachCreature`-скан в 2 местах (`WorldEnvironment.cpp:367-384`, `:1233-1250`); лимит 128 захардкожен 3× (`:392`, `:772`, `:809`); AABB-инсет 0.05 — 5 копий |
| S5 | P1 | `TickNaturalPopulation` — 200 строк всего в одной функции (`WorldEnvironment.cpp:1209-1408`); `SpawnCreature` — 185 |
| S6 | P1 | Нет biome/light/time-правил (зелёное поле — бэклог); нет деспавна по дистанции; `PickCreatureByView` линейный вместо индекса (`:834-871`) |
| S7 | P1 | Полная пересборка пространственного индекса каждый activity-тик (`:1126`) + ещё раз в `ValidateLoadedCreaturePositions:1206`, при том что индекс читается пер-кадровой депенетрацией — между тиками он протухает |
| S8 | P1 | Дорогое восстановление позиций: `ValidateLoadedCreaturePositions` (1 с) → `FindSafeCreatureOrigin` до ~448 кандидатов × `ScanFluidColumn` (скан y=0..255 на колонку, `CreatureEnvironment.cpp:126`) — до ~20k чтений блоков на поиск флюида |
| S9 | P1 | Идентификация флюидов строковым `find("water"/"lava")` в самом горячем зонде (`CreatureEnvironment.cpp:47-69`) |
| S10 | P2 | Мёртвое: `CreatureSpawnSource::Persistence` никогда не создаётся; слой `src/Creatures/Spawning/` — header-only политика, реальный код в World; `WorldCreatureFacade` — 3 слоя делегации на один вызов; снап-ветки `SnapSpawnProbeToHabitat` — 3 копипасты флюид-поиска (`CreatureEnvironment.cpp:288-423`) |
| S11 | P1 | Тесты не покрывают: `TickNaturalPopulation`, `SpawnCreature`, `LoadCreatures/SaveCreatures` round-trip и legacy-миграции, валидацию позиций, сам индекс |

### 2.3. Движение, навигация, ИИ

| # | Пр | Находка |
|---|---|---|
| M1 | P0 | **Асимметрия пространственного индекса**: `InsertEntryIntoCells` кладёт запись только в ячейку origin (`CreatureSpatialIndex.cpp:77-80`), `RemoveEntryFromCells` удаляет по всем ячейкам AABB (`:51-75`) — `AnyCreatureVolumeOverlaps` (`:221`) может пропустить пересечение, если origin-ячейка цели не попала в набор ячеек запроса → взаимопроникновение мобов |
| M2 | P0 | `ProbeEnvironmentAt` (O(r³)-скан объёма со строковыми сравнениями) выполняется на каждое существо **каждый физический кадр** из `CreatureVitalsSystem::Tick` (`CreatureVitalsSystem.cpp:74`), плюс в `ExecuteIntent` (`Creature.cpp:336`) и `RebuildLocomotionFacts` |
| M3 | P0 | FSM melee-фолбэк без фракций: без контролируемого игрока melee-мобы атакуют ближайшее существо любого вида, включая пассивных и друг друга (`SimpleFsmBrain.cpp:227-307`); цель перевыбирается каждый тик без гистерезиса — флаппинг |
| M4 | P1 | Два источника правды локомоции игрока: камера симулирует тело (`Camera.cpp:1033`), потом синхронизирует в существо (`WorldViewBinding.cpp:549-603`) |
| M5 | P1 | Гиганты: `SimpleFsmBrain::Tick` — 331 строка (Flee и Melee в одной функции), `ExecuteIntent` — 242, `WanderActivityAgent::Tick` — 228, `UpdateLocomotion` — 153, `RunLegacyPhysicsFrame` — 373 |
| M6 | P1 | `std::rand()` для всего стиринга (`CreatureActivitySteering.cpp:120-148`) — не сеется по миру; снапшоты определений — строковый lookup на существо на тик (`WorldCreatureActivitySink.cpp:14-59`); множитель aggro `0.5+perception/20` применяется молча при каждом запросе |
| M7 | P1 | Тестовый харнесс в прод-коде: множитель скорости `FlightSimMoveSpeedScale()` прямо в локомоции (`CreatureLocomotionController.cpp:69` — Creatures включает App/Platform) |
| M8 | P1 | Ширина тела 0.6 захардкожена в навигации (`WorldNavigationQueries.cpp:33`) и расходится с per-species bounds; `kGravity=-20` дублирован |
| M9 | P2 | Псевдослучайность: износ брони `tick % size` (`InfluenceApplier.cpp:248`), «luck shrug» `floor(dmg*100)%100` (`CreatureVitalsSystem.cpp:156`) |
| M10 | P2 | Мёртвое: `alertLevel` (0 ссылок), записи в deprecated `attackTargetId` (`WorldViewBinding.cpp:713,722`), `TryRangedStrike` — вербатим-копия melee (`CreatureCombat.cpp:25`), `CreaturesInRadius` используется только тестовыми моками |
| M11 | P1 | Тесты не покрывают: контроллер локомоции, `ExecuteIntent`, агенты, FSM, директор, `SteerCreatureAlongPath`, виталы, статусы |

### 2.4. Взаимодействия

| # | Пр | Находка |
|---|---|---|
| I1 | P1 | `InfluenceResolver::Resolve` — 355 строк на все каналы (Use/Dig/Melee/Ranged) в одной функции (`InfluenceResolver.cpp:39`); `InfluenceApplier::Apply` — 341 (`InfluenceApplier.cpp:54`) |
| I2 | P1 | Ranged = hitscan-копия melee (снарядов нет); radius/aura-канал реализован в резолвере, но никем не используется |
| I3 | P2 | Слой `src/Creatures/Roles/` — 3 маркерных хендлера без поведения + фабрика = over-abstraction |
| I4 | P2 | `UCreature::GetArmorGroups()` — const-геттер с mutable-мутацией и агрегацией (`Creature.h:81-103`) |
| I5 | — | Реализовано только: melee (игрок↔моб, моб↔моб), разделение/депенетрация, урон/броня/щит/статусы, владение/контроль. Размножение/стаи/приручение/езда/лут — отсутствуют (бэклог, см. §7) |

### 2.5. Сквозные

- **Двунаправленная связка Creatures⇄World**: `UWorld` реализует `IUWorldPerception` (`World.h:215`); каждая подсистема зовёт ~15 методов UWorld (коллизии, грунт, чанки, камера, режим) — юнит-тестирование без полного `UWorld` невозможно; `UWorld` держит 291 строку форвардинг-шим (`WorldCreatures.cpp`).
- Игрок-локомоция в Render-слое; визуал знает GeometryEngine и World.

## 3. Целевая архитектура

1. **`src/Creatures/` — дом для всей логики существ**: спавнер (`UCreatureSpawner`), фабрика (`CreatureFactory`), зонды среды за интерфейсом `ICreatureWorldQueries` (реализация — поверх World). World — оркестратор, новые API-шимы в UWorld замораживаются.
2. **Визуал отвязан от рендер-год-объекта**: `SubmitDraw(CreatureDrawContext)` — очередь + текстуры + настройки + камера; превью переиспользует те же бэкенды через headless-контекст.
3. **Один источник правды локомоции**: контроллер у сущности, камера — ввод+вид (поэтапно, за флагом).
4. **Кэши с валидацией**: хэш меша считается при загрузке; appearance — по инвалидации; зонды среды — TTL-кэш на существо; индекс — инкрементальный.
5. **Тестируемость**: среда/спавнер/мозг тестируются на fake-запросах; характеризационные тесты до каждого refactor-шага.

## 4. План фаз

Каждая фаза: ветка `refactor/N-...` от `develop`, сборка Release, полный ctest, headless-прогон AppRunner, `flight_sim_run.py`, merge `--no-ff`.

### Фаза 0 — `refactor/3-creatures-audit`: этот документ + характеризационные тесты
Два минимальных behavior-preserving шва для тестируемости: решение о бэкенде вынесено из фабрики в `CreatureVisualBackendSelect.{h,cpp}`, порядок очереди — в `SortCreatureDrawRequests` (`CreatureDrawQueue.h`). Новые тесты: `creature_spatial_index_test` (вкл. сценарий пропуска пересечения M1 — ожидание переворачивается в фазе 1), `creature_visual_backend_select_test` (fallback-цепочка, legacy-алиасы, billboard-приоритет), `creature_appearance_test` (части/скины/wireframe-фолбэк), `creature_draw_queue_test` (стабильный порядок kind→texture). Round-trip JSON-хелперов spawn_source/body_origin уже покрыт `mode_policy_test`; полный save/load round-trip и `ExecuteIntent` — в фазах 3/4, где появится тестируемость (фабрика/разбор стадий).

### Фаза 1 — `refactor/4-creatures-bugfixes`: исправление P0-дефектов (единственные намеренные изменения поведения)
M1 (симметричный индекс + регресс-тест), M3 (предикат враждебности по образцу `BotPlayerActivityAgent::IsHostileTarget` + интервал переоценки цели), V2 (root-translation), V3 (uTint в skinned, десктоп+gles), S1 (атомарная запись + ошибки потока + проверка format_version), S2 (null-check, `<filesystem>`), M9 (нормальный RNG), V5 (glDeleteTextures, мьютекс rigid-кэша, синхронизация лог-сета), S4-дубли (константа 128, хелпер инсета), V14/S10/M10 (удаление мёртвого кода), честный alias `TryRangedStrike`, дедуп `ComputeSpawnBodyOriginAhead` (`WorldEnvironment.cpp:639` vs `WorldCommands.cpp:700`).

### Фаза 2 — `refactor/5-creatures-visual`: визуал/рендер-конвейер (первая предметная область)

> **Статус (2026-10-25, merge `65b072e8e`)**: выполнено 2.1–2.4, 2.6, 2.7 (V1, V4, V6–V8-частично, V9, V12, V13; `CreatureDrawContext`, `UCreatureVisualBase`, shared-палитра, appearance-кэш, разбор Render 168→80 строк, ParseVisualSpec 392→196, ReadVec3/4 в `Core/Json/JsonVec.h`). Осталось: 2.5 (превью на `IUCreatureVisual`, 342-строчный дубль в `CreaturePreviewRenderer`), V8-батчинг (numeric-ключ сортировки стал незначим после shared-палитры), перевод WornEquipmentDrawer с переходного `ctx.Engine` на очередь. Сортировка очереди теперь по числовому GL-id + новый kind Billboard.
2.1 Базовый класс бэкендов: appearance, yaw+offset (устранить рассинхрон V1), fallback-цепочка текстур, логирование, guard-ы (V10).
2.2 Кэши: V12 (выровнять инвалидацию), выкидывать мёртвые ключи, V4-лок вокруг GL-генерации.
2.3 Производительность кадра: V4 (хэш при загрузке), V7 (разделяемые кости/палитра раз в кадр на существо), V6 (appearance-кэш), V8 (numeric-ключ сортировки), V9 (sprite через очередь, камера в контексте).
2.4 V13: `SubmitDraw(CreatureDrawContext)`.
2.5 V11: превью на `IUCreatureVisual`.
2.6 Парсер определений по доменам (V15, дедуп ReadVec3/ReadVec4).
2.7 Разбор `CreatureDrawPass::Render` (cull/appearance/pose/submit, health-bar отдельно).
2.8 Тесты: V16.

### Фаза 3 — `refactor/6-creatures-spawn`: спавн и популяция

> **Статус (2026-10-25, фаза закрыта: `3ca597f29` ч.1 + `36f922064` ч.2).** Ч.2: `CreatureSpawnPlanner` — чистая планировочная половина спавнера (пригодность/давление/кольцо/взвешенный выбор/группа/джиттер, инжектированный RNG), `TickNaturalPopulation` разбит на гейты + `TryNaturalPopulationSpawn`, `HasCreatureTag` общий, тест `creature_spawn_planner_test`. Порядок расхода RNG сохранён — сид-детерминизм не изменился. `ICreatureWorldQueries` перенесён в фазу 4 (интерфейс зондов строится там под TTL-кэш — делать дважды бессмысленно). Прежние решения ч.1: S8-окно отклонено (полные границы колонки нужны глубине спавна), PickCreatureByView линейный при капе 128.: 3.2 `CreatureFactory` готова (спавн/загрузка/QA/игрок — единая сборка; flight-клэмп теперь применяется и при загрузке); дедуп подсчёта населения в `CountNaturalPopulation` (S4); RNG-ресид при `ClearCreatures`; мемоизация классификации флюидов `ClassifyFluidBlock` (S9). Осталось: вынос `UCreatureSpawner` из WorldEnvironment, `ICreatureWorldQueries`, спавнер-тесты; S8-окно для `ScanFluidColumn` отклонено — полные границы колонки нужны проверкам глубины спавна (вернуться через кэш колонок в фазе 4); PickCreatureByView через индекс отложен (при капе 128 линейный пик дёшев).
3.1 `UCreatureSpawner` + счётчики населения на add/remove (S4, S5); `CreaturePopulationRules` → .cpp.
3.2 `CreatureFactory` для спавна/загрузки/QA/игрока (S3).
3.3 Persistence: миграции за `format_version`, RNG-ресид при `ClearCreatures`.
3.4 `ICreatureWorldQueries` для тестируемости среды; флюид-флаги вместо строкового поиска (S9).
3.5 `ScanFluidColumn` — окно вокруг Y + бюджет колец (S8); `PickCreatureByView` через индекс.
3.6 Тесты (S11).

### Фаза 4 — `refactor/7-creatures-motion`: движение и ИИ
4.1 TTL-кэш зондов среды на существо с инвалидацией при изменении блоков (M2 — крупнейший выигрыш).
4.2 Разбор `ExecuteIntent`/`UpdateLocomotion`/`SnapSpawnProbeToHabitat` на стадии.
4.3 Инкрементальный пространственный индекс вместо полной пересборки (S7).
4.4 Локомоция игрока: контроллер в существо, камера — ввод+вид, убрать синк-блок (M4). Поэтапно, за флагом, проверять ощущения `flight_sim_run.py`.
4.5 ИИ: разбор FSM-тика на обработчики режимов; кэш снапшотов; aggro-множитель в определение; сеянный RNG; убрать FlightSim-множитель из локомоции (M5–M7).
4.6 Навигация: ширина из bounds; пул A*-аллокаций (опционально).
4.7 Тесты (M11).

### Фаза 5 — `refactor/8-creatures-combat`: взаимодействия
5.1 По-канальный разбор Resolver/Applier (I1).
5.2 Распустить слой Roles (I3).
5.3 Убрать `WorldCreatureFacade`; заморозить UWorld-шимы; const-корректность брони (I4); radius/aura — тест или удаление.
5.4 Тесты по каналам + матрица фракций.

### Фаза 6 — `refactor/9-creatures-frame-split`: интеграция
Разбор `RunLegacyPhysicsFrame` на именованные стадии; creature-файлы под CI-гейт «функция ≤300 строк»; обновить этот документ статусами.

## 5. Верификация (каждая фаза)

| Шаг | Команда/инструмент |
|---|---|
| Сборка (только Release) | `cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8` |
| Тесты | полный `ctest` (все новые тесты зарегистрированы) |
| Headless-поведение | AppRunner smoke-фикстуры спавна/выживания (`docs/CREATURE_SPAWN_SURVIVAL_VERIFICATION.md`) — база сравнения «до/после» |
| Визуально | `tools/flight_sim_run.py` против `bin/Cubatarium.exe` |
| Свежесть exe | `verify-windows-exe: PASS` |

## 6. Риски

- **Сохранение поведения**: характеризационные тесты фазы 0 + headless-фикстуры как «золотые». Фаза 1 — единственные намеренные изменения поведения, все задокументированы выше.
- **Унификация локомоции игрока (4.4)** — самый рискованный шаг: флаг, поэтапность, ручная проверка ощущений управления.
- **Параллельные потоки рефакторинга** на develop (World/GeometryEngine): короткие ветки, частые мержи; порядок фаз подобран, чтобы визуал (фаза 2) шёл раньше тяжёлых World-касаний.

## 7. Бэклог (осознанно вне плана)

Размножение, стаи/толпы, приручение, верховая езда, лут с трупов, деспавн по дистанции; вариативность особей (размер/окрас), LOD, освещение существ, блендинг анимаций; настоящие снаряды; спавн по биомам/свету/времени суток; O(1)-кэш ground-запросов; полный разбор связки UWorld⇄Environment за узкими интерфейсами.
