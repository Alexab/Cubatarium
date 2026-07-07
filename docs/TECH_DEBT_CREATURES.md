# Tech debt: Creatures (visual + catalog)

> Review at end of phases 0, 1, 2, 3, 4, 5. Close items when implemented or explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-CRE-003 | 0 | `visual.rig` parsed but does not select pose presenter | `locomotion_archetype` is sufficient | backlog |
| TD-CRE-006 | 1 | `AerialPosePresenter` — full b3d clip playback for flying birds | Ground chicken walk+peck done; fly IK deferred | backlog |
| TD-CRE-010 | 3 | FP viewmodel arms (`fp_parts[]`) | Not a blocker | backlog |
| TD-CRE-017 | audit | Wave bake coverage (~42 Luanti mobs) | `creature_luanti_sources.yaml` + `bake_rigid_creature_textures.py`; partial until all research textures present | 3 |
| TD-CRE-026 | gltf | Parts-only glTF without b3d — per-bone upgrade vs root bob | Policy in `CREATURE_BACKENDS.md`; 2 species remain parts-only (`fire_spirit`, `octopus`) | backlog |
| TD-CRE-028 | gltf | Sprite visuals (`fire_spirit` glow billboard) | Luanti `visual=sprite`; procedural texture placeholder | backlog |
| TD-CRE-034 | visual-regression | `puffin` 3D model is incorrect in world and inventory preview | Suspected wrong source model/transform mapping after catalog sync; needs model remap + icon cache regeneration | backlog |
| TD-CRE-035 | visual-regression | `manatee` 3D model has invalid geometry/scale/offset | Verify source in `tools/creature_luanti_sources.yaml`, then fix geometry transform and regenerate icon cache | backlog |

### TD-CRE-034 acceptance notes (`puffin`)
- Repro: spawn `puffin` in world and inspect inventory preview icon.
- Suspected source: wrong model asset binding or transform remap in visual catalog.
- Close when: world model is visually correct, preview matches world, icon cache regenerated and manually verified.

### TD-CRE-035 acceptance notes (`manatee`)
- Repro: spawn `manatee` in world and inspect inventory preview icon.
- Suspected source: mismatch in source metadata (`tools/creature_luanti_sources.yaml`) and model transform (scale/offset).
- Close when: geometry/scale/offset are corrected, world+preview are visually consistent, icon cache regenerated and manually verified.

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-CRE-030 | AI-2 | Incremental `CreatureSpatialIndex` (`Upsert`/`Remove`/`PruneExcept`) |
| TD-CRE-031 | AI-2 | `gameplay.activity_tick_hz` в `config.json` + `UWorld::SetActivityTickHz` |
| TD-CRE-032 | AI-2 | Activity culling через `UChunkStreamer::IsPositionInActiveRing` |
| TD-CRE-033 | AI-3 | Flee steering в `USimpleFsmBrain`; hostile JSON: skeleton, dungeon_master |
| TD-CRE-008 | AI | `FleeActivityAgent`, `MeleeAttackActivityAgent`, `IUAgentBrain` / `USimpleFsmBrain` |
| TD-CRE-029 | AI | Wander mob crowding: `CreatureActivitySteering` + `TryDepenetrateSpawnOrigin` |
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
| TD-CRE-009 | 2 | Spider / 8 legs rigid approximation | 6-leg template (`leg_ml`/`leg_mr`) + quadruped pose |
| TD-CRE-015 | habitat | Amphibious habitat (`penguin`, `seal`) | `CreatureHabitat::Amphibious` + spawn/move rules |
| TD-CRE-016 | habitat | Lava fluid habitat for `lava_flan` | `CreatureHabitat::Lava` + lava block probe |
| TD-CRE-018 | audit | Per-species aquatic rigid parts (seahorse ≠ dolphin template) | `creature_rigid_parts.yaml` + `sync_creature_parts_from_rigid.py` |
| TD-CRE-019 | audit | Icon placeholder fallback (`parts_preview` → FBO; baked `species_texture` → `icon.png`) | `CreatureIconCache.cpp` + bake `patch_creature_icon_mode` |
| TD-CRE-020 | spawn | Universal spawn probe + habitat snap (50 spawnable) | `CreatureEnvironment.cpp`, `WorldCreatures.cpp` |
| TD-CRE-021 | audit | 8 placeholder species — research textures imported + baked | `sync_luanti_research_textures.py` + `bake_rigid_creature_textures.py` |

## Execution progress (2026-07-07)

- Packet `P0.1` tracker normalization committed (`15bbb00`): статусная рассинхронизация Open/Closed устранена для закрытых historical пунктов.
- Packet `R2` persistent icon invalidation committed (`17de9ca`): `ClearRenderedIcons` теперь инвалидация `creature/skin` на диске, что упрощает повторную валидацию после model fixes.
- `TD-CRE-034` (`puffin`): glTF re-exported from `animalworld/models/Puffin.b3d` (226 verts, mesh Y 0.0–0.586); awaiting manual visual sign-off.
- `TD-CRE-035` (`manatee`): glTF re-exported from `animalworld/models/Manatee.b3d` + bounds normalized to rigid envelope; awaiting manual visual sign-off.
