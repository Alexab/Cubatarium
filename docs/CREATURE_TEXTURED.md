# Textured creature parts (rigid voxels)

Multi-part textured cubes for `visual.backend: rigid_voxels`. Replaces the default single wireframe AABB with several diffuse-textured boxes defined in JSON.

## Overview

```
creature.json visual.parts  →  ResolveCreatureAppearance  →  CreatureVisualRigid  →  GeometryEngine::DrawCreatureTexturedPart
skin.json texture_map       →  per-part texture keys      →  CreatureTextureStorage
```

Wireframe is **not** drawn in normal play. Use debug settings for bounds or overlay.

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

Ship-set stems: `body` (torso, chest/belt bands), `face` (head atlas: eyes only on +X forward), `leg` (striped, brown-tinted), `arm` (shoulder/cuff bands). Parts `arm_l` / `arm_r` in JSON.

Head mesh uses atlas UVs (`creatureHeadPartVAO`) so the face panel is not repeated on all four horizontal sides.

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

## Body yaw (facing)

- **Mobs** (`wander`): `Creature::ApplyIntent` sets `yaw` from `moveDirWorld` via `atan2(z, x)` (degrees, same XZ convention as the camera). Last yaw kept when idle.
- **Controlled** (`human`): `World` syncs `yaw` from the camera each frame (unchanged).
- **Render**: `CreatureVisualRigid` applies `rotate(Y, yaw)` around `bodyOrigin` before each part offset (local +X = forward).

`yaw` is saved in `creatures.json` per instance.

## Regenerate PNG assets

```powershell
Set-Location "e:\Work\Home\Cubatarium"
python tools/generate_luanti_creature_catalog.py
```

Writes placeholder Luanti-style PNGs for all ship-set species and skins. Replace with CC-licensed imports via `tools/import_luanti_rigid_creature.py`.

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

1. Sheep in world: quadruped gait; model turns when wander direction changes.
2. `sheep_wool_golden` on sheep → parts use golden skin textures.
3. Cow taller/wider than human — part scales match bounds.
4. F5 3rd person: human with parts; 1st person: no body (unchanged).
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
