# Creature glTF backend (contract)

> Full loader implementation deferred — see [TECH_DEBT_CREATURES.md](TECH_DEBT_CREATURES.md) TD-CRE-001.

## Backend selection

In `creature.json`:

```json
"visual": {
  "backend": "gltf_skeleton",
  "gltf": {
    "model": "meshes/creature.glb",
    "textures": ["textures/diffuse.png"],
    "model_scale": 1.0,
    "model_yaw_offset_deg": 0
  },
  "animation": {
    "clips": {
      "idle": { "start": 0.0, "end": 1.0, "loop": true, "speed": 1.0 },
      "walk": { "start": 1.0, "end": 2.0, "loop": true, "speed": 1.0 }
    },
    "state_map": {
      "idle": "idle",
      "walk": "walk",
      "run": "walk"
    }
  }
}
```

## Luanti compatibility

- Clip ranges use **seconds** on a single timeline (see [Luanti models](https://docs.luanti.org/for-creators/models/)).
- Textures are **not** embedded in `.glb`; list them in `visual.gltf.textures`.
- Export from Blockbench with scale **1.6** for Luanti-style units.

## Runtime (current)

- `UCreatureVisualGltf` is a **stub**: logs once, optional debug bounds wireframe.
- `DrawCreatureSkinnedMesh` in `GeometryEngine` is empty until TD-CRE-001 is closed.
- `rigid_voxels` remains the production backend for all ship-set species.

## Dual-backend

Each species picks one `visual.backend`. Motor, locomotion, and activity agents are backend-agnostic.
