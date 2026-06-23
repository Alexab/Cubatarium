# Tech debt: Creatures (visual + catalog)

> Review at end of phases 0, 1, 2, 3, 4, 5. Close items when implemented or explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-CRE-001 | 0 | Full glTF backend (cgltf, skinned shader, clip playback) | Primary path is rigid_voxels | 5 |
| TD-CRE-003 | 0 | `visual.rig` parsed but does not select pose presenter | `locomotion_archetype` is sufficient | backlog |
| TD-CRE-006 | 1 | `AerialPosePresenter` — full b3d clip playback for flying birds | Ground chicken walk+peck done; fly IK deferred | backlog |
| TD-CRE-008 | 2 | `FleeActivityAgent`, `MeleeAttackActivityAgent` | Visual scope, not AI | backlog |
| TD-CRE-009 | 2 | Spider / 8 legs rigid approximation | 6-leg template (`leg_ml`/`leg_mr`) + quadruped pose | done |
| TD-CRE-010 | 3 | FP viewmodel arms (`fp_parts[]`) | Not a blocker | backlog |
| TD-CRE-015 | habitat | Amphibious habitat (`penguin`, `seal`) | `CreatureHabitat::Amphibious` + spawn/move rules | done |
| TD-CRE-016 | habitat | Lava fluid habitat for `lava_flan` | `CreatureHabitat::Lava` + lava block probe | done |
| TD-CRE-017 | audit | Wave bake coverage (~42 Luanti mobs) | `creature_luanti_sources.yaml` + `bake_rigid_creature_textures.py`; partial until all research textures present | 3 |
| TD-CRE-018 | audit | Per-species aquatic rigid parts (seahorse ≠ dolphin template) | `creature_rigid_parts.yaml` + `sync_creature_parts_from_rigid.py` | done |
| TD-CRE-019 | audit | Icon placeholder fallback (`parts_preview` → FBO; baked `species_texture` → `icon.png`) | `CreatureIconCache.cpp` + bake `patch_creature_icon_mode` | done |
| TD-CRE-020 | spawn | Universal spawn probe + habitat snap (50 spawnable) | `CreatureEnvironment.cpp`, `WorldCreatures.cpp` | done |
| TD-CRE-021 | audit | 8 placeholder species (dolphin, whale, octopus, kitten, warthog, mese_monster, lava_flan, water_dragon) — textures missing in `CubatariumTextureResearch` | Import animalworld/mobs_* PNGs then `bake_rigid_creature_textures.py` | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-CRE-013 | 6 | `CreaturePartMesh::RigidHead` — face stem on +Z; snout/beak/head/comb |
| TD-CRE-014 | 7 | IK foot placement on uneven terrain for rigid mobs | Procedural sin gait sufficient on flat ground | backlog |
| TD-CRE-012 | 0 | `CreatureVisualFactory` uses `std::cerr` once-per-species for glTF stub |
| TD-CRE-002 | 1 | Controlled head follows camera pitch via biped presenter |
| TD-CRE-005 | 2 | Ship-set creature JSON uses explicit pivot/limb fields |
| TD-CRE-004 | 3 | Icons for all ship-set species (`icon.png` or `parts_preview` FBO) |
| TD-CRE-011 | 4 | Creature resource packs merge via `ApplyCreaturePackOverlays` |
| TD-CRE-007 | habitat | `AquaticPosePresenter` / `SerpentinePosePresenter` + habitat spawn/move UI | Implemented in habitat wave |
