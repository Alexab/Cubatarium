# Creature catalog and skins

Ship set: **9 species** (1 player + 8 mobs) and **5 skins**, Luanti-style rigid voxels, data-driven spawn, palette tabs, and appearance resolution.

## Ship set

### Species

| id | role | archetype | tags | notes |
|----|------|-----------|------|-------|
| `human` | controlled_default | terrestrial_biped | humanoid | Player spawn; not in Creatures palette |
| `sheep` | mob | terrestrial_quadruped | mobs, passive | |
| `wolf` | mob | terrestrial_quadruped | mobs, hostile | |
| `pig` | mob | terrestrial_quadruped | mobs, passive | |
| `cow` | mob | terrestrial_quadruped | mobs, passive | |
| `chicken` | mob | aerial | mobs, passive | Wing flap via `AerialPosePresenter` |
| `oerkki` | mob | terrestrial_biped | mobs, hostile | |
| `skeleton` | mob | terrestrial_biped | mobs, hostile | |
| `sand_monster` | mob | terrestrial_biped | mobs, hostile | |

**Removed:** `scout`, `brute`, `drifter` (legacy placeholders).

### Skins

| id | creature_id | use |
|----|-------------|-----|
| `human_adventurer` | human | Controlled appearance (per-part textures) |
| `human_guard` | human | Controlled appearance |
| `sheep_wool_black` | sheep | LMB apply on mob |
| `sheep_wool_golden` | sheep | LMB apply on mob |
| `wolf_snow` | wolf | LMB apply on mob |

## Folder layout

```
models/creatures/<species_id>/creature.json
models/creatures/<species_id>/textures/body.png, face.png, leg.png, arm.png
models/skins/<skin_id>/skin.json
models/skins/<skin_id>/textures/diffuse.png
```

Texture keys at runtime: `<species_id>/<stem>` and `skin/<skin_id>/<stem>`.

## JSON reference

### `creature.json`

- `id`, `display_name`
- `catalog`: `tags`, `spawnable`, `sort_order`
- `role`: `controlled_default` | `mob`
- `bounds`, `eye_height`, `locomotion_archetype`, `locomotion`
- `locomotion_archetype`: `terrestrial_biped` (default) | `terrestrial_quadruped` | `aerial` | `aquatic` | `serpentine` — выбор pose presenter и derive `LocomotionState`
- `locomotion`: `can_fly`, `can_crouch`, `can_jump`, `jump_height` (feet rise in blocks; jump speed derived from shared gravity), `walk_speed` (m/s), `fly_speed` (m/s, defaults to `walk_speed`)
- `behavior`: `none` | `wander` — см. [Activity agents](#activity-agents) ниже
- `behavior_params`: `move_speed` (legacy wander fallback if `locomotion.walk_speed` omitted), `wander_interval_min`, `wander_interval_max`
- `visual`: `backend` (`rigid_voxels` | `gltf_skeleton`), `animation`, `default_texture`, `parts[]` (`id`, `offset`, `size`, `texture`, optional `pivot`, `limb`, `limb_axis`), `icon` (`mode`: `parts_preview`, `color`)

### `skin.json`

- `id`, `display_name`, `creature_id`
- `catalog`: `tags`, `equippable`, `sort_order`
- `visual`: `texture`, `texture_map` (part stem → skin texture stem), `wireframe_color`, `icon.fallback_color`

## Commands

| Command | Description |
|---------|-------------|
| `spawn <species> [skin]` | Spawn mob in front of camera |
| `select_skin <skin_id>` | Apply skin to controlled creature + save on user |
| `apply_skin <skin_id>` | Apply to creature under crosshair |
| `select_appearance <id>` | Alias for `select_skin` (legacy) |

Removed: `spawn_test_mob`.

## Palette and hotbar

- **E** → Creative palette: Blocks, Objects, **Creatures**, **Skins**
- LMB with Creatures slot: spawn species ahead of view
- LMB with Skins slot: apply skin to targeted creature (ray-AABB, 8 m)

## Persistence

- `creatures.json`: `type`, optional `skin_id`
- `users.json`: `selected_skin_id` (fallback read: `selected_appearance_type`)
- Migration on load: `test_mob`/`scout` → `sheep`, `brute` → `sand_monster`, `drifter` → `wolf`; legacy skin ids remapped; `player` creatures skipped

## Regenerate PNG assets

Import Luanti CC textures (recommended after clone or when research folder exists):

```powershell
Set-Location "e:\Work\Home\Cubatarium"
python tools/import_luanti_creature_textures.py --download
# or, if E:\Work\Home\CubatariumTextureResearch already has mobs_animal etc.:
python tools/import_luanti_creature_textures.py
```

Regenerate JSON only (keeps imported PNGs when LICENSE is not a placeholder):

```powershell
python tools/generate_luanti_creature_catalog.py
```

Procedural placeholders (no upstream sources):

```powershell
python tools/generate_creature_assets.py
```

## Smoke acceptance

1. New world → controlled species `human` (textured parts in 3rd person, F5).
2. Palette **Creatures** → 8 mobs (sheep, wolf, pig, cow, chicken, oerkki, skeleton, sand_monster); no scout/brute/drifter.
3. **Skins** → `sheep_wool_golden` → LMB on sheep → golden wool on parts.
4. Console: `select_skin human_adventurer` → controlled color changes.
5. Load world with `creatures.json` type `scout` → mob becomes `sheep`.
6. Save world → reload → mob keeps `skin_id`.
7. Regression: blocks, prefabs, possess/depossess, F5, entity collision.

## Locomotion presentation

Анимация конечностей **не** в activity-агентах. Pipeline:

1. Motor: `Creature::ExecuteIntent` / `Camera::DoMovement` → `CreatureLocomotionFacts` (скорость, grounded, stance).
2. `DeriveLocomotionState` → `LocomotionState` (Idle, Walk, Run, Jump, Fall, Crouch, Fly, …).
3. `CreaturePosePresenterRegistry` → biped / quadruped / aerial presenters → `CreaturePoseParams` по part id.
4. `ICreatureVisual::UpdatePose` → `CreatureVisualRigid` рисует части.

Опционально в `CreatureIntent`: `lookAtWorld`, `lookAtWeight` (IK головы).

## Activity agents

Поле `behavior` в `creature.json` задаёт membership в [`CreatureActivityDirector`](CREATURE_AGENTS.md), не логику внутри `Creature`.

| `behavior` | При spawn / load | Движение каждый кадр |
|------------|------------------|----------------------|
| `wander` | `OnCreatureAdded` → **`WanderActivityAgent`** (таймер, направление в агенте) | Агент `SetIntent` в `DoMovement`, затем `Creature::ExecuteIntent` |
| `none` | Нет membership в директоре | Только `ExecuteIntent` (гравитация, коллизии; intent пустой, если не задан иначе) |

Controlled (`human`, `controlled_default`) использует `behavior: none` и **не** тикается агентами; ввод — `Camera::DoMovement`.

Параметры wander читаются агентом через `ICreatureActivitySink::GetBehaviorSnapshot` (`wander_interval_*`, `locomotion.walk_speed` / `move_speed`).

## Code map

| Layer | Types |
|-------|--------|
| Data | `models/creatures`, `models/skins` |
| Storage | `CreatureDefinitionStorage`, `SkinDefinitionStorage`, `CreatureTextureStorage` |
| Appearance | `ResolveCreatureAppearance`, `World::GetResolvedAppearance` |
| Activity | `src/Activity/*`, `CreatureActivityDirector`, `WanderActivityAgent` |
| Presentation | `src/Pose/*`, `CreatureLocomotionFacts`, `LocomotionStateDerive` |
| World | `SpawnCreature`, `SpawnCreatureByView`, `PickCreatureByView`, `TryApplySkin`, `DoMovement` |
| UI | `ContentTypeRegistry`, `CreativePaletteScreen`, `CreatureIconCache` |

See also: [CREATURE_AGENTS.md](CREATURE_AGENTS.md), [CREATURE_TEXTURED.md](CREATURE_TEXTURED.md), [CREATURE_GLTF.md](CREATURE_GLTF.md), [CREATURE_IMPLEMENTATION.md](CREATURE_IMPLEMENTATION.md), [TECH_DEBT_CREATURES.md](TECH_DEBT_CREATURES.md).
