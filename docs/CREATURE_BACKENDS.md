# Creature visual backends

Cubatarium supports three equal visual backends selected by `visual.backend` in
`models/creatures/<id>/creature.json`.

| Backend | JSON key | Runtime class | Purpose |
|---------|----------|---------------|---------|
| Rigid voxels | `rigid_voxels` | `UCreatureVisualRigid` | Box parts. Three demo mobs plus `fire_spirit`, which is drawn as a billboard before this backend |
| Bone skeleton | `bone_skeleton` | `UCreatureVisualBoneSkeleton` | Box-UV geometry, 17 species including `bot` |
| glTF skeleton | `gltf_skeleton` | `UCreatureVisualGltf` | Skinned or static glTF, 35 species |

Factory entry point: `CreateCreatureVisual()` in
`src/Creatures/Visual/CreatureVisualFactory.cpp`.

## Migration policy (rigid → glTF)

All production mobs that previously used `rigid_voxels` are migrated to
`gltf_skeleton` with offline-generated `model.gltf` assets. `rigid_voxels` is
**not** deprecated — it remains for reference implementations and new simple mobs.

After migration:

- **4** species on `rigid_voxels`: `rigid_demo_walker`, `rigid_demo_flyer`,
  `rigid_demo_swimmer`, and `fire_spirit` (billboard sprite, not a fourth backend)
- **17** species on `bone_skeleton`, including `bot`
- **35** species on `gltf_skeleton`
- Authoring source is CreatureGen (`tools/creaturegen.py`), not Bedrock or Luanti imports

See [CREATURE_MIGRATION_GLTF.md](CREATURE_MIGRATION_GLTF.md) for per-species
status and wave order.

## Canonical rigid demo mobs

| Id | Habitat | Archetype | Presenter |
|----|---------|-----------|-----------|
| `rigid_demo_walker` | terrestrial | `terrestrial_quadruped` | `TerrestrialQuadrupedPosePresenter` |
| `rigid_demo_flyer` | aerial | `aerial` | `AerialPosePresenter` |
| `rigid_demo_swimmer` | aquatic | `aquatic` | `AquaticPosePresenter` |

These mobs document the rigid pipeline (`visual.parts[]`, pose presenters,
`docs/CREATURE_TEXTURED.md`) while the main catalog uses glTF.

## Naming

- Code and JSON use **`gltf`** only (`gltf_skeleton`, `UCreatureVisualGltf`).
- Luanti provenance belongs in `tools/` (e.g. `creature_luanti_sources.yaml`),
  not in `src/` or enum names.

## Related docs

- [CREATURE_GLTF.md](CREATURE_GLTF.md) — glTF asset layout and runtime
- [CREATURE_BONE_SKELETON.md](CREATURE_BONE_SKELETON.md) — bone_skeleton backend
- [CREATURE_TEXTURED.md](CREATURE_TEXTURED.md) — rigid parts and textures
- [CREATURE_IMPLEMENTATION.md](CREATURE_IMPLEMENTATION.md) — architecture §11
- [CREATURE_BACKEND_MATRIX.md](CREATURE_BACKEND_MATRIX.md) — per-species backend audit

## Special visual policies (TD-CRE-026 / TD-CRE-028)

| Case | Species | Policy |
|------|---------|--------|
| Luanti `visual="sprite"` | `fire_spirit` | Keep `gltf_skeleton` box mesh + procedural textures; **do not** run b3d export. Future: billboard/glow render flag (TD-CRE-028). |
| Missing upstream mod | `octopus` | `marinaramobs` not in research — texture-only parts glTF until mod cloned or replaced. |
| Mesh reuse in Luanti | `dirt_monster`, `land_guard` | Luanti reuses `mobs_stone_monster.b3d` / `mobs_dungeon_master.b3d`; Cubatarium exports those b3d paths explicitly in yaml. |
| Parts-only fallback | Species without b3d on disk | `convert_creature_mesh_to_gltf.py --parts-only` retains box primitives + root TRS bob (TD-CRE-026 backlog for per-bone upgrade). |
| Skinned b3d | 34+ gltf species with `skins[]` | Primary path: yaml `model:` → `b3d_export_gltf.py` + Luanti animation clips. |

Audit: `python tools/audit_creature_backends.py` → `docs/CREATURE_BACKEND_MATRIX.md`.
