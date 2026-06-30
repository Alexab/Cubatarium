# Tech debt: Creatures (visual + catalog)

> Review at end of phases 0, 1, 2, 3, 4, 5. Close items when implemented or explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
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
| TD-CRE-021 | audit | 8 placeholder species — research textures imported + baked | `sync_luanti_research_textures.py` + `bake_rigid_creature_textures.py` | done |
| TD-CRE-026 | gltf | Parts-only glTF without b3d — per-bone upgrade vs root bob | Policy in `CREATURE_BACKENDS.md`; 2 species remain parts-only (`fire_spirit`, `octopus`) | backlog |
| TD-CRE-028 | gltf | Sprite visuals (`fire_spirit` glow billboard) | Luanti `visual=sprite`; procedural texture placeholder | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-CRE-023 | gltf-audit | mobs_redo animation parser: run/fly/punch/die + idle fallback | `luanti_mob_animation.py` |
| TD-CRE-024 | gltf-audit | Batch b3d → glTF re-export for yaml `model:` species | `convert_creature_mesh_to_gltf.py --all-with-b3d` (34 skinned) |
| TD-CRE-025 | gltf-audit | `state_map` Run/Fly/Swim from habitat + Luanti clips | `sync_gltf_creature_animation.py` + runtime `ResolveAnimationClipId` |
| TD-CRE-027 | gltf-audit | CI validate skinned glTF + bind-pose smoke | `validate_gltf_creature.py --skinned-only`, `test_gltf_skinned_bind_pose.py`, `creature_gltf_loader_test` |
| TD-CRE-013 | 6 | `CreaturePartMesh::RigidHead` — face stem on +Z; snout/beak/head/comb |
| TD-CRE-014 | 7 | IK foot placement on uneven terrain for rigid mobs | Procedural sin gait sufficient on flat ground | backlog |
| TD-CRE-012 | 0 | `CreatureVisualFactory` uses `std::cerr` once-per-species for glTF stub |
| TD-CRE-001 | gltf | glTF skeleton backend (JSON loader, skinned shader, bind-pose palette, root clip bob) | MVP shipped; full b3d clip export → TD-CRE-022 |
| TD-CRE-022 | gltf | b3d KEYS → glTF idle/walk clips from Luanti mob Lua | `luanti_mob_animation.py` + `b3d_export_gltf.py` | done |
| TD-CRE-002 | 1 | Controlled head follows camera pitch via biped presenter |
| TD-CRE-005 | 2 | Ship-set creature JSON uses explicit pivot/limb fields |
| TD-CRE-004 | 3 | Icons for all ship-set species (`icon.png` or `parts_preview` FBO) |
| TD-CRE-011 | 4 | Creature resource packs merge via `ApplyCreaturePackOverlays` |
| TD-CRE-007 | habitat | `AquaticPosePresenter` / `SerpentinePosePresenter` + habitat spawn/move UI | Implemented in habitat wave |
