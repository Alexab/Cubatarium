# Skeletal geo creature backend

Hierarchical `geometry.geo.json` models with a single diffuse atlas and procedural bone animation.

## Backend selection

```json
"visual": {
  "backend": "skeletal_geo",
  "geometry": "geometry.cow.v1.8",
  "geometry_file": "geometry.geo.json",
  "texture": "diffuse",
  "texture_size": [64, 32],
  "animation_profile": "quadruped",
  "animation": {
    "walk_cycle_hz": 2.0,
    "leg_swing_deg": 45,
    "look_at_deg": 30
  }
}
```

`bedrock_geo` is accepted as a deprecated alias for `skeletal_geo`.

## Asset layout

```
models/creatures/cow/
  creature.json
  geometry.geo.json
  textures/diffuse.png
```

Source geo files: `models/creatures/_sources/bedrock_geo/` (from [Mojang/bedrock-samples](https://github.com/Mojang/bedrock-samples)).

## Dual backend

- `skeletal_geo` — hierarchical skeleton, Box UV, bone animation (default for Tier A/B mobs).
- `rigid_voxels` — legacy Luanti-derived parts; kept for custom mobs (oerkki, sand_monster, …).

Per-species `visual.backend` in `creature.json`.

## Toolchain

```powershell
python tools/setup_bedrock_creature_sources.py
python tools/import_bedrock_creature.py --species cow --species pig
python tools/validate_bedrock_creature.py
python tools/smoke_creature_fidelity.py
```

Catalog: [`tools/bedrock_geo_catalog.yaml`](../tools/bedrock_geo_catalog.yaml)

## Coordinate convention

| Space | Rule |
|-------|------|
| geo.json | Model units (1/16 block), Y-up, entity forward **−Z** |
| bone pivot | Absolute model-space for quadruped limbs under bind-rotated parents; humanoid uses parent spatial chain |
| humanoid chain | `parentMat * T(child.pivot − parent.pivot)` when no `bind_pose_rotation` on ancestors |
| bind_pose_rotation | Geo degrees mapped via `SkeletalBindPoseRotationDeg` (−X, −Y, +Z) for model Y-up |
| bone local | `T(pivot + offset) * R_bind * R_pose` — `restLocal` carries `(meshCenter − pivot)`; bind and pose rotations are separate |
| animation chain | `T(offset)` then `T(pivot)*R*T(-pivot)` for pose-only parent rotation |
| entity draw | `SkeletalEntityConventionMatrix()` = `scale(1,1,−1)`; world draw uses `glFrontFace(GL_CW)` |
| sides | +X = mob **right**, −X = mob **left** (east/west) |
| cube origin | Model-space absolute; mesh offset = `(origin + size/2) − bone.pivot` |
| box UV | Mojang unfold (`south` at `u+d+w+d`, `east` at `u+d+w`) |
| mirror | East/west faces (+X/−X) flip U only |

Diagnostic: `python tools/diagnose_bedrock_box_uv.py models/creatures/bee/geometry.geo.json`

## Runtime modules

| Module | Role |
|--------|------|
| `CreatureSkeletalGeoLoader` | Parse geo.json 1.8 / 1.12 / 1.21 |
| `SkeletalCubeMeshBuilder` | Box UV per cube |
| `CreatureBoneHierarchy` | Pivot + bind pose + animation |
| `SkeletalBonePoseEngine` | Procedural profiles (quadruped, humanoid, chicken, …) |
| `UCreatureVisualSkeletalGeo` | Render path |

## Animation profiles

Defined in code (`SkeletalBonePoseEngine`) and calibrated via `tools/extract_bedrock_animation_hints.py` from upstream animation JSON.

| Profile | Bones |
|---------|-------|
| quadruped | leg0–leg3, body, head, tail |
| humanoid | rightArm, leftArm, rightLeg, leftLeg, waist (body bob), head |
| chicken | leg0/1, wing0/1, head (idle peck) |
| aquatic | body, tail undulation |
| aerial | wing flap, body pitch |

### Humanoid / player

- Geometry: `geometry.skeleton.v1.8` (Steve) or `geometry.zombie.v1.8`; hierarchy `waist` → `body` → head/limbs.
- No `bind_pose_rotation` on humanoid bones — each part uses absolute model-space pivot (same approach as quadruped limbs).
- Walk body bob applies to **`waist` only** (skeleton root), not duplicated on `body`, so torso and limbs stay aligned.
- Player (`human`): `texture_layout: player_skin_atlas` in JSON is legacy rigid metadata; `skeletal_geo` draws **box UV** from geo with a **64×32** diffuse atlas (`import_bedrock_creature.py` crops 64×64 skins to the top half). `UCreatureTextureStorage` also crops 64×64 PNGs on GPU upload. World/preview resolve `skin/<id>/diffuse` when a skin is equipped.

## Textures

Tier A/B mobs use Java entity PNGs (study/reference) via `import_bedrock_creature.py`. For production packs, replace with upstream atlas from Vanilla-Mob-Variants `.mcpack` (personal use).

## Resource packs

Creature pack overlay merges `creature.json`, `geometry.geo.json`, and `textures/diffuse.png` via existing `ApplyCreaturePackOverlays`.
