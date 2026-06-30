# glTF skeleton backend (`gltf_skeleton`)

Runtime: `UCreatureVisualGltf` + loader in `src/Creatures/Visual/Gltf/`.

## creature.json

```json
"visual": {
  "backend": "gltf_skeleton",
  "gltf": {
    "model": "model.gltf",
    "textures": ["body", "face", "leg"],
    "model_scale": 1.0,
    "model_yaw_offset_deg": 0
  },
  "animation": {
    "clips": {
      "idle": { "start": 0, "end": 1, "loop": true },
      "walk": { "start": 0, "end": 1, "loop": true, "speed": 1.0 }
    },
    "state_map": {
      "Idle": "idle",
      "Walk": "walk",
      "Run": "walk",
      "Swim": "walk",
      "Fly": "idle"
    }
  },
  "default_texture": "body"
}
```

- `visual.gltf.model` — path relative to `models/creatures/<id>/`
- `textures` — texture stems resolved via `UCreatureTextureStorage`
  (`<id>/<stem>.png`)
- `model_scale` — uniform scale applied at draw time (block units in mesh)
- `model_yaw_offset_deg` — added to entity yaw (same as skeletal)

Clip ids in `state_map` must match glTF `animations[].name`.

## Asset layout

```
models/creatures/<id>/
  creature.json
  model.gltf
  model.bin          # optional external buffer
  textures/
    body.png
    face.png
    ...
```

Meshes are exported in **block space** (Y-up, 1 unit = 1 block). Luanti b3d
glTF uses `GltfEntityConventionMatrix()` (identity); `bone_skeleton` keeps
`BoneSkeletonEntityConventionMatrix()` Z-flip at draw time.

## Offline toolchain

| Tool | Purpose |
|------|---------|
| `tools/convert_creature_mesh_to_gltf.py` | Build `model.gltf` from `visual.parts[]` or Luanti `.b3d` |
| `tools/validate_gltf_creature.py` | Validate glTF + texture refs for a species |
| `tools/migrate_creature_to_gltf.py` | Batch update `creature.json` + export assets |

Luanti source paths: `tools/creature_luanti_sources.yaml` (provenance only).

## Luanti provenance

Cubatarium does not ship Luanti mob mods. Assets are traced from a local clone of
ContentDB mods in `CubatariumTextureResearch` (default
`E:/Work/Home/CubatariumTextureResearch`).

| Layer | Location | Role |
|-------|----------|------|
| mobs_redo API | [ContentDB mobs_redo](https://content.luanti.org/packages/TenPlus1/mobs_redo/) | `mobs:register_mob` + `animation = { stand_start, walk_start, … }` |
| Content mods | `mobs_animal`, `mobs_monster`, `dmobs`, `animalworld` in research | `.lua`, `.b3d`, textures |
| b3d format | [Luanti b3d spec](https://docs.luanti.org/for-creators/models/b3d-spec/) | Parsed by `tools/b3d_read.py` |
| Object properties | [deepwiki Object Properties](https://deepwiki.com/luanti-org/luanti/Object_Properties) | `visual`, `mesh`, `collisionbox`, `textures` |
| Provenance yaml | `tools/creature_luanti_sources.yaml` | `model:` + texture paths per species |
| Animation parser | `tools/luanti_mob_animation.py` | idle/walk/run/fly/punch/die frame ranges |
| Backend matrix | `docs/CREATURE_BACKEND_MATRIX.md` | `tools/audit_creature_backends.py` |

Export pipeline:

```
ContentDB mod → CubatariumTextureResearch
  → mob .lua (collisionbox, visual, animation)
  → .b3d + textures
  → luanti_mob_animation.py + b3d_export_gltf.py
  → models/creatures/<id>/model.gltf
```

**Visual routing**

| Luanti `visual` | Cubatarium action |
|-----------------|-------------------|
| `mesh` + `.b3d` | Primary glTF export (`convert_creature_mesh_to_gltf.py`) |
| `mesh` texture-only | bedrock_geo manual_uv or parts glTF |
| `sprite` | Not b3d; see TD-CRE-028 (`fire_spirit`) |

Sync helpers: `sync_creature_yaml_b3d_models.py`, `sync_gltf_creature_animation.py`.

## Runtime draw path

1. `CreatureGltfCache` loads and caches parsed mesh + animation metadata.
2. `UCreatureVisualGltf::UpdatePose` resolves locomotion state → clip id →
   samples root node TRS from glTF animation.
3. `SubmitDraw` binds per-primitive textures and calls
   `UGeometryEngine::DrawCreatureGltfMesh` (same vertex layout as skeletal
   cubes: `xyz` + `uv`).

Skinned meshes (joints + weights) use the same draw entry with bone palette
when present; static multi-primitive exports work without skinning.

## Tests

```bash
cmake --build build/desktop-msvc --config Debug --target creature_gltf_loader_test
```

## See also

- [CREATURE_BACKENDS.md](CREATURE_BACKENDS.md)
- [CREATURE_MIGRATION_GLTF.md](CREATURE_MIGRATION_GLTF.md)
- TD-CRE-001 and TD-CRE-022 (closed) in [TECH_DEBT_CREATURES.md](TECH_DEBT_CREATURES.md)
- TD-CRE-023…027 (closed): animation parser, batch export, state_map sync, validate/test
- b3d export: `python tools/convert_creature_mesh_to_gltf.py --species <id>` reads Luanti `animation = { stand/walk frames }` from `CubatariumTextureResearch` Lua via `luanti_mob_animation.py`
