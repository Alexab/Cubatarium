# Bone skeleton creature backend

Hierarchical `geometry.geo.json` models with a single diffuse atlas and procedural bone animation.

## Backend selection

```json
"visual": {
  "backend": "bone_skeleton",
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

Legacy aliases accepted at load time: `bedrock_geo`, `skeletal_geo`, `skeletal` (one-time stderr warning).

## Asset layout

```
models/creatures/cow/
  creature.json
  geometry.geo.json
  textures/diffuse.png
```

Source geo files: `models/creatures/_sources/bedrock_geo/` (from [Mojang/bedrock-samples](https://github.com/Mojang/bedrock-samples)).

## Dual backend

- `bone_skeleton` — hierarchical skeleton, Box UV, bone animation (default for Tier A/B mobs).
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
| draw | `BoneSkeletonEntityConventionMatrix()` flips Z for Cubatarium +Z forward |
| preview | `BoneSkeletonPreviewRootMatrix()` centers on `visible_bounds_*` |

## Runtime types

| Role | Type |
|------|------|
| Parsed geo | `CreatureBoneSkeletonGeometry` |
| GPU mesh | `CreatureBoneSkeletonMeshAsset` |
| Pose chain | `BoneSkeletonHierarchy` |
| Locomotion pose | `BoneSkeletonPoseEngine` |
| Visual backend | `UCreatureVisualBoneSkeleton` |

glTF skinning uses a separate path (`ComputeGltfSkinMatrices`); do not share `BoneSkeletonHierarchy` with `gltf_skeleton`.

## Player skin

Player (`human`): `texture_layout: player_skin_atlas` in JSON is legacy rigid metadata; `bone_skeleton` draws **box UV** from geo with a **64×32** diffuse atlas (`import_bedrock_creature.py` crops 64×64 skins to the top half). `UCreatureTextureStorage` also crops 64×64 PNGs on GPU upload. World/preview resolve `skin/<id>/diffuse` when a skin is equipped.

## See also

- [CREATURE_BACKENDS.md](CREATURE_BACKENDS.md) — three-way backend comparison
- [CREATURE_IMPLEMENTATION.md](CREATURE_IMPLEMENTATION.md) — draw path and culling
