# Creature catalog and skins

Ship set for the `creature` feature branch: **4 species** and **5 skins**, data-driven spawn, palette tabs, and appearance resolution.

## Ship set

### Species

| id | role | behavior | notes |
|----|------|----------|-------|
| `human` | controlled_default | none | Player spawn; not in Creatures palette |
| `scout` | mob | wander | Orange multi-part (torso/head/legs) |
| `brute` | mob | wander | Red multi-part, larger bounds |
| `drifter` | mob | wander | Green multi-part |

### Skins

| id | creature_id | use |
|----|-------------|-----|
| `human_adventurer` | human | Controlled appearance |
| `human_guard` | human | Controlled appearance |
| `scout_golden` | scout | LMB apply on mob |
| `brute_rust` | brute | LMB apply on mob |
| `drifter_ice` | drifter | LMB apply on mob |

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
- `bounds`, `eye_height`, `locomotion`
- `locomotion`: `can_fly`, `can_crouch`, `can_jump`, `jump_height` (feet rise in blocks; jump speed derived from shared gravity), `walk_speed` (m/s), `fly_speed` (m/s, defaults to `walk_speed`)
- `behavior`: `none` | `wander`
- `behavior_params`: `move_speed` (legacy wander fallback if `locomotion.walk_speed` omitted), `wander_interval_min`, `wander_interval_max`
- `visual`: `backend`, `default_texture`, `parts[]` (`id`, `offset`, `size`, `texture`), `icon` (`mode`: `parts_preview`, `color`)

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
- Migration: `test_mob` → `scout`, `player` creatures skipped on load

## Regenerate PNG assets

```powershell
Set-Location "e:\Work\Home\Cubatarium"
python tools/generate_creature_assets.py
```

## Smoke acceptance

1. New world → controlled species `human` (textured parts in 3rd person, F5).
2. Palette **Creatures** → spawn scout, brute, drifter (distinct part meshes, wander).
3. **Skins** → `scout_golden` → LMB on scout → yellow diffuse on parts.
4. Console: `select_skin human_adventurer` → controlled color changes.
5. Save world → reload → mob keeps `skin_id`.
6. Regression: blocks, prefabs, possess/depossess, F5, entity collision.

## Code map

| Layer | Types |
|-------|--------|
| Data | `models/creatures`, `models/skins` |
| Storage | `CreatureDefinitionStorage`, `SkinDefinitionStorage`, `CreatureTextureStorage` |
| Appearance | `ResolveCreatureAppearance`, `World::GetResolvedAppearance` |
| World | `SpawnCreature`, `SpawnCreatureByView`, `PickCreatureByView`, `TryApplySkin` |
| UI | `ContentTypeRegistry`, `CreativePaletteScreen`, `CreatureIconCache` |

See also: [CREATURE_TEXTURED.md](CREATURE_TEXTURED.md), [CREATURE_IMPLEMENTATION.md](CREATURE_IMPLEMENTATION.md), [CREATURE_POST_B.md](CREATURE_POST_B.md).
