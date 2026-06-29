# Textured creature parts (rigid voxels)

Multi-part textured cubes for `visual.backend: rigid_voxels`. Replaces the default single wireframe AABB with several diffuse-textured boxes defined in JSON.

## Overview

```
creature.json visual.parts  →  ResolveCreatureAppearance  →  CreatureVisualRigid  →  GeometryEngine::DrawCreatureTexturedPart
skin.json texture_map       →  per-part texture keys      →  CreatureTextureStorage
```

Wireframe is **not** drawn in normal play. Use debug settings for bounds or overlay.

## Luanti mesh atlas vs texture layouts

Luanti mobs use `visual = "mesh"`: a single PNG atlas (often 128×128) with per-vertex UV from `.b3d`. Cubatarium `rigid_voxels` draws axis-aligned boxes with one texture stem per part.

| Layout | Species | UV in renderer | PNG source |
|--------|---------|----------------|------------|
| `player_skin_atlas` | `human` | `Head` / `Body` VAO crops (face on +Z) | 64×32 skin sliced into player-atlas layout |
| `box_uv` | mobs with `.b3d` model | `BoxDynamic` — per-face regions from `BuildCreatureBoxUvTexCoords(size)` | Box-unfold PNG per stem from bake v2 |
| `rigid_crop` | mobs without `.b3d` (e.g. wolf/pig manual atlas) | `Box` full 0–1; `RigidHead` for `/face` on +Z only | 64×64 square crop per stem (legacy) |

**Without bake:** importing a mesh atlas into every stem and applying full UV on all cube faces produces stretched “face on torso” artifacts.

**Bake pipeline (default):** `python tools/bake_rigid_creature_textures.py`

- Species **with** Luanti `.b3d`: `box_uv` unfold via `tools/b3d_face_groups.py` (`texels_per_block` in `creature_rigid_uv_maps.yaml`).
- Species **without** model (manual `manual_uv` only): falls back to `rigid_crop` square crops.
- `--legacy-crop` forces square crops for all mobs.
- Sidecar `textures/<stem>.uv.json` documents unfold metadata (validation only).

Override failed auto-crops via `manual_uv` in `tools/creature_luanti_sources.yaml`. UV validation: `python tools/debug_creature_uv_crops.py --tier-a --auto`.

Set in JSON: `"texture_layout": "box_uv"` | `"rigid_crop"` | `"player_skin_atlas"` (human). Runtime: `CreatureVisualRigid::MeshForPart` — human uses Head/Body; `box_uv` uses dynamic box unfold; `rigid_crop` uses Box / RigidHead.

### Quadruped face parts

Per-species geometry in [`tools/creature_rigid_parts.yaml`](../../tools/creature_rigid_parts.yaml):

- `head` — skull/wool (`body` texture)
- `snout` — forward box (`face` texture, `RigidHead` UV)
- `ear_l` / `ear_r` — optional (`ear` texture stem)
- `tail` — optional (`tail` texture stem)
- Chicken: `neck`, `head` + `comb` + `beak` (`face` stem), wings, legs

Part geometry source: [`tools/creature_rigid_parts.yaml`](../../tools/creature_rigid_parts.yaml). Proportion hints: `python tools/derive_rigid_proportions.py`; refine from `.b3d`: `python tools/derive_rigid_parts_v2.py --species <id>` (use `--write` only after review).

Tier A baseline gallery: `python tools/render_creature_tier_a_gallery.py`

## `visual.parts` in `creature.json`

```json
"visual": {
  "backend": "rigid_voxels",
  "default_texture": "body",
  "parts": [
    { "id": "torso", "offset": [0.0, 0.9, 0.0], "size": [0.7, 0.9, 0.45], "texture": "body" },
    { "id": "head",  "offset": [0.0, 1.55, 0.0], "size": [0.45, 0.45, 0.45], "texture": "face" },
    { "id": "leg_l", "offset": [-0.2, 0.35, 0.0], "size": [0.25, 0.7, 0.25], "texture": "leg",
      "pivot": [-0.2, 0.7, 0.0], "limb": "leg" }
  ],
  "icon": { "mode": "parts_preview", "color": [1.0, 0.55, 0.1, 1.0] }
}
```

| Field | Meaning |
|-------|---------|
| `offset` | Part center in **local blocks** from `bodyOrigin` (same space as bounds center) |
| `size` | Cube scale in blocks |
| `texture` | Stem → `models/creatures/<species>/textures/<stem>.png` → key `<species_id>/<stem>` |
| `pivot` | Optional limb hinge in local blocks (enables pose rotation) |
| `limb` | `leg` or `arm` — selects swing axis in pose presenter |

Ship-set stems: `body`, `face`, `leg`, `ear`, `tail` (quadrupeds), `arm` (bipeds). Human uses `player_skin_atlas` via `tools/import_luanti_creature_textures.py` (not mob bake). Player UV audit: `python tools/debug_player_skin_uv.py`.

Head mesh uses atlas UVs (`creatureHeadPartVAO`) so the face panel is not repeated on all four horizontal sides — **only when** `visual.texture_layout` is `player_skin_atlas` (human). Mobs with `rigid_crop` use the box mesh on every part.

**Fallback:** empty `parts` → one part synthesized from `bounds.rest` + `default_texture` (old JSON still works).

## Skin `texture_map`

```json
"visual": {
  "texture": "diffuse",
  "texture_map": { "body": "diffuse", "face": "diffuse", "leg": "diffuse" },
  "wireframe_color": [1.0, 0.9, 0.2, 1.0]
}
```

For a part with `texture: "body"` and active skin, resolve uses `skin/<skin_id>/<stem>` instead of `sheep/body`.

## Render settings (`config.json` → `render`)

| Key | Default | Effect |
|-----|---------|--------|
| `creature_textured_parts` | `true` | Draw resolved parts with diffuse |
| `creature_wireframe_overlay` | `false` | Wireframe on each part (debug) |
| `creature_debug_bounds` | `false` | Cyan max AABB wireframe (unchanged) |

Set `creature_textured_parts` to `false` to fall back to a single wireframe box per creature.

## Procedural animation (`visual.animation`)

Parsed into `CreatureAnimationParams` and used by pose presenters (`src/Pose/*`):

| Field | Default | Effect |
|-------|---------|--------|
| `walk_cycle_hz` | 2.0 | Leg/arm phase rate vs `walk_speed` |
| `leg_swing_deg` / `arm_swing_deg` | 25 / 15 | Limb swing amplitude |
| `fly_body_pitch_deg` | 10 | Torso pitch when flying |
| `body_bob_blocks` | 0.025 | Vertical torso/head bob on walk |
| `tail_swing_deg` | 12 | Quadruped tail wag |
| `run_speed_multiplier` | 1.3 | Faster swing above ~1.2× walk speed |
| `crouch_leg_bend_deg` | 25 | Human knee bend when crouching |
| `wing_idle_swing_deg` | 5 | Ground chicken wing tuck on walk |

Ground chicken (`can_fly: false`, aerial archetype): legs alternate, wings tucked, idle peck. Quadrupeds: tail wag + spine-offset torso bob.

## Body yaw (facing)

- **Mobs** (`wander`): `Creature::ApplyIntent` sets `yaw` from `moveDirWorld` via `atan2(z, x)` (degrees, same XZ convention as the camera). Last yaw kept when idle.
- **Controlled** (`human`): `World` syncs `yaw` from the camera each frame (unchanged).
- **Render**: `CreatureVisualRigid` applies `rotate(Y, yaw)` around `bodyOrigin` before each part offset (local +X = forward).

`yaw` is saved in `creatures.json` per instance.

## Regenerate PNG assets

Luanti mob crops (after research folder is present):

```powershell
Set-Location "e:\Work\Home\Cubatarium"
python tools/bake_rigid_creature_textures.py
```

Catalog JSON + placeholders:

```powershell
python tools/generate_luanti_creature_catalog.py
```

Writes placeholder Luanti-style PNGs for all ship-set species and skins. Replace with CC-licensed imports via `tools/import_luanti_creature_textures.py`.

## Geometry

- **Separate VAO** (`creaturePartVAO`) with UV 0–1 per face — not the block atlas cube.
- `GeometryEngine::SetCreatureTextureStorage` wired from `Core` at startup.
- Missing texture: skip part + one-time log; optional wireframe if `creature_wireframe_overlay`.

## Icons

`CreatureIconCache` renders species icons in a 64×64 FBO using resolved parts and `CreatureTextureStorage`. If `textures/icon.png` exists (32×32 inventory style), it is used directly. Skins reuse loaded `skin/<id>/<stem>` when present.

`icon.mode: parts_preview` in species JSON selects this path (solid `icon.color` is background tint only).

## Migration from wireframe-only

1. Add `parts[]` with optional `pivot`/`limb` to each species (ship set: human + 8 mobs).
2. Add `texture_map` to skins that override part stems.
3. Ensure PNGs exist under `textures/` (see `tools/generate_creature_assets.py`).
4. Keep `wireframe_color` for debug and icon fallback.

World/collision/spawn/palette behavior is unchanged.

## Smoke acceptance

1. Sheep in world: opaque wool, **eyes on snout**, visible ears/tail; chicken walks with leg swing (not wing flap).
2. `sheep_wool_golden` on sheep → parts use golden skin textures (incl. ear/tail stems).
3. Cow torso longer than sheep; wolf has ears and snout.
4. F5 3rd person: human face on head, torso front/back correct; crouch bends knees; walk bob.
5. `creature_debug_bounds`: cyan max AABB over mesh.
6. Save/reload: `skin_id`, wander, collision, palette OK.
7. ~10–20 mobs: no obvious FPS drop.

## Out of scope (later)

- Full glTF backend — see [CREATURE_GLTF.md](CREATURE_GLTF.md), [TECH_DEBT_CREATURES.md](TECH_DEBT_CREATURES.md) TD-CRE-001
- Skeletal animation, normal maps
- GPU instancing of all creature parts
- User skins under `models/skins/user/`
- Animated pose on palette icons (static parts preview only)

See also: [CREATURE_CATALOG.md](CREATURE_CATALOG.md).
