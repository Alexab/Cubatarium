# Creature catalog and skins

Ship set: **51 species** (1 player + 50 mobs) and **5 skins**, Luanti-style rigid voxels, data-driven spawn with **habitat** gating, palette tabs, and appearance resolution.

## Ship set (original 9)

### Species

| id | role | archetype | habitat | tags | notes |
|----|------|-----------|---------|------|-------|
| `human` | controlled_default | terrestrial_biped | — | humanoid | Player spawn; not in Creatures palette |
| `sheep` | mob | terrestrial_quadruped | terrestrial | mobs, passive | |
| `wolf` | mob | terrestrial_quadruped | terrestrial | mobs, hostile | |
| `pig` | mob | terrestrial_quadruped | terrestrial | mobs, passive | |
| `cow` | mob | terrestrial_quadruped | terrestrial | mobs, passive | |
| `chicken` | mob | aerial | terrestrial | mobs, passive | Ground bird; wing flap only when `can_fly` |
| `oerkki` | mob | terrestrial_biped | terrestrial | mobs, hostile | |
| `skeleton` | mob | terrestrial_biped | terrestrial | mobs, hostile | |
| `sand_monster` | mob | terrestrial_biped | terrestrial | mobs, hostile | |

### Extended catalog (waves)

| Wave | Species |
|------|---------|
| mobs_animal | `bunny`, `rat`, `panda`, `kitten`, `penguin`, `bee`, `warthog` |
| mobs_monster | `spider`, `stone_monster`, `tree_monster`, `mese_monster`, `dirt_monster`, `dungeon_master`, `fire_spirit`, `land_guard`, `lava_flan` |
| dmobs | `fox`, `badger`, `hedgehog`, `tortoise`, `orc`, `ogre`, `golem`, `treeman`, `butterfly`, `owl`, `wasp` |
| marine | `trout`, `shark`, `squid`, `stingray`, `seahorse`, `manatee`, `lobster`, `hermitcrab`, `seal`, `dolphin`, `whale`, `water_dragon`, `crab`, `octopus`, `puffin` |

Regenerate: `python tools/generate_luanti_creature_catalog.py` (merges [`tools/extra_creature_species.py`](../tools/extra_creature_species.py)).

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

## Visual backends (ship set)

| Backend | Count | Species | Notes |
|---------|-------|---------|-------|
| `gltf_skeleton` | 36 | Luanti mobs migrated from rigid boxes | `model.gltf` + `model.bin`; 5 yaml `.b3d` species use skinned mesh export |
| `bone_skeleton` | 16 | Tier A/B (cow, sheep, human, zombie, …) | Bedrock-style `geometry.geo.json` |
| `rigid_voxels` | 3 | `rigid_demo_walker`, `rigid_demo_flyer`, `rigid_demo_swimmer` | Canonical rigid reference; see [CREATURE_TEXTURED.md](CREATURE_TEXTURED.md) |

Regenerate glTF from b3d: `python tools/convert_creature_mesh_to_gltf.py --all-with-b3d`. Smoke: `python tools/smoke_creature_rigid_demo.py`, `python tools/render_creature_preview_gltf.py`.

## JSON reference

### `creature.json`

- `id`, `display_name`
- `catalog`: `tags`, `spawnable`, `sort_order`
- `role`: `controlled_default` | `mob`
- `bounds`, `eye_height`, `habitat`, `locomotion_archetype`, `locomotion`
- `habitat`: `terrestrial` | `aquatic` | `aerial` | `amphibious` | `lava` — spawn zone and wander constraints (see [Habitat](#habitat))
- `locomotion_archetype`: `terrestrial_biped` (default) | `terrestrial_quadruped` | `aerial` | `aquatic` | `serpentine` — выбор pose presenter и derive `LocomotionState`
- `locomotion`: `can_fly`, `can_crouch`, `can_jump`, `jump_height` (feet rise in blocks; jump speed derived from shared gravity), `walk_speed` (m/s), `fly_speed` (m/s, defaults to `walk_speed`)
- `behavior`: `none` | `wander` — см. [Activity agents](#activity-agents) ниже
- `behavior_params`: `move_speed` (legacy wander fallback if `locomotion.walk_speed` omitted), `wander_interval_min`, `wander_interval_max`
- `visual`: `backend` (`rigid_voxels` | `gltf_skeleton`), `texture_layout` (`player_skin_atlas` | `rigid_crop`), `animation` (`walk_cycle_hz`, `leg_swing_deg`, `arm_swing_deg`, `fly_body_pitch_deg`, `body_bob_blocks`, `tail_swing_deg`, `run_speed_multiplier`, `crouch_leg_bend_deg`, `wing_idle_swing_deg`), `default_texture`, `parts[]` (`id`, `offset`, `size`, `texture`, optional `pivot`, `limb`, `limb_axis`), `icon` (`mode`: `parts_preview`, `species_texture`, `color`)

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
- Use active slot (ПКМ Classic / tap ЛКМ Cubatarium): spawn species ahead of view
- **Creatures** icons are **dimmed** when spawn is impossible in the current view zone (wrong habitat)
- Tooltip on dimmed entry: habitat hint in Russian (`Нужна вода`, `Нужно открытое небо`, …)
- Skins slot: apply skin to targeted creature (ray-AABB, 8 m)

## Habitat

| `habitat` | Spawn requires | Movement |
|-----------|----------------|----------|
| `terrestrial` | solid ground, not in fluid | XZ wander; blocked leaving land |
| `aquatic` | body in fluid (water) | 3D wander in water; cannot surface on dry land |
| `aerial` | open air, not in fluid, ≥1 block clearance above | fly mode wander with light vertical drift |
| `amphibious` | solid ground **or** water | XZ on land; 3D in water (`penguin`, `seal`) |
| `lava` | body in lava fluid | 3D wander in lava (`lava_flan`) |

Runtime: [`CreatureEnvironment`](../src/Creatures/Environment/CreatureEnvironment.cpp), `UWorld::CanSpawnCreatureByView`, `WanderActivityAgent` habitat probe.

Derive from Luanti `.lua`: `python tools/derive_creature_habitat_from_luanti.py` → `tools/creature_habitat_map.json`.

Smoke: `python tools/smoke_creature_habitat.py`.

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

Bake per-part rigid crops from Luanti mesh atlases (mobs use `rigid_crop`; human keeps `player_skin_atlas`):

```powershell
python tools/bake_rigid_creature_textures.py
# optional: --species sheep --skin sheep_wool_black
```

Sources: `tools/creature_luanti_sources.yaml`, UV mapping: `tools/creature_rigid_uv_maps.yaml`, part geometry: `tools/creature_rigid_parts.yaml`. Requires `E:\Work\Home\CubatariumTextureResearch` with `.b3d` models and textures.

Audit baseline (asset tier, icons, habitat): `python tools/audit_creature_catalog.py` → `docs/CREATURE_AUDIT_REPORT.md`, `tools/creature_audit_status.yaml`.

Spawn / runtime tracking: `python tools/diagnose_creature_spawn.py`, `python tools/verify_creature_spawn.py`, `python tools/sync_creature_resolution.py` → `tools/creature_resolution_log.yaml`, `docs/CREATURE_RESOLUTION_LOG.md`, `docs/CREATURE_SPAWN_MATRIX.md`, `docs/CREATURE_VERIFICATION_MATRIX.md`. Manual spawn checklist: `docs/CREATURE_MANUAL_SPAWN_CHECKLIST.md`. **Remaining work:** [`docs/CREATURE_DEBT_PLAN.md`](CREATURE_DEBT_PLAN.md).

Sync custom part geometry into JSON after editing `creature_rigid_parts.yaml`:

```powershell
python tools/sync_creature_parts_from_rigid.py --species seahorse shark
```

Debug / tuning:

```powershell
python tools/debug_creature_uv_crops.py --species sheep
python tools/debug_player_skin_uv.py
python tools/derive_rigid_proportions.py
python tools/sample_b3d_pose_curves.py
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
4. `IUCreatureVisual::UpdatePose` → `CreatureVisualRigid` рисует части.

Опционально в `CreatureIntent`: `lookAtWorld`, `lookAtWeight` (IK головы).

## Activity agents

Поле `behavior` в `creature.json` задаёт membership в [`CreatureActivityDirector`](CREATURE_AGENTS.md), не логику внутри `Creature`.

| `behavior` | При spawn / load | Движение каждый кадр |
|------------|------------------|----------------------|
| `wander` | `OnCreatureAdded` → **`WanderActivityAgent`** (таймер, направление в агенте) | Агент `SetIntent` в `DoMovement`, затем `Creature::ExecuteIntent` |
| `flee` | **`FleeActivityAgent`** + `USimpleFsmBrain` | Убегание от controlled; navigation + steering |
| `melee_attack` | **`MeleeAttackActivityAgent`** + `USimpleFsmBrain` | zombie, skeleton, dungeon_master — преследование и `attackTargetId` в intent |
| `none` | Нет membership в директоре | Только `ExecuteIntent` (гравитация, коллизии; intent пустой, если не задан иначе) |

Controlled (`human`, `controlled_default`) использует `behavior: none` и **не** тикается агентами; ввод — `Camera::DoMovement`.

Параметры wander читаются агентом через `IUCreatureActivitySink::GetBehaviorSnapshot` (`wander_interval_*`, `locomotion.walk_speed` / `move_speed`).

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
