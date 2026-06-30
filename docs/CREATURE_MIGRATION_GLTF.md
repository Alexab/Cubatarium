# Rigid → glTF migration table

36 species migrated from `rigid_voxels` to `gltf_skeleton`. Wave order matches
`tools/creature_luanti_sources.yaml` and TD-CRE-021 blockers.

## Wave A — Luanti `.b3d` in yaml (pilot first)

| Species | b3d source | Status |
|---------|------------|--------|
| oerkki | mobs_oerkki.b3d | done (skinned) |
| sand_monster | mobs_sand_monster.b3d | done (skinned) |
| badger | mobs_badger.b3d | done (skinned) |
| hedgehog | mobs_hedgehog.b3d | done (skinned) |
| penguin | mobs_penguin.b3d | done (skinned) |

## Wave A — rigid with yaml entry (manual_uv / texture only)

| Species | Notes | Status |
|---------|-------|--------|
| badger, hedgehog, penguin | see above | done |
| butterfly, crab, dirt_monster, dungeon_master, fire_spirit, golem, hermitcrab, land_guard, lobster, manatee, ogre, orc, owl, panda, puffin, rat, seahorse, seal, shark, stingray, stone_monster, tree_monster, treeman, wasp | parts → glTF export | done |

## Wave B — manual_uv only (no b3d)

All remaining rigid species without `model:` in yaml — mesh from `visual.parts[]`.

## Wave C — TD-CRE-021 texture blockers

All eight species now have research atlas paths (`sync_luanti_research_textures.py`) and baked `models/creatures/<id>/textures/*.png`.

| Species | Source |
|---------|--------|
| kitten | mobs_kitten_striped.png (mobs_animal) |
| warthog | mobs_pumba.png (mobs_animal) |
| mese_monster | mobs_mese_monster_purple.png |
| lava_flan | zmobs_lava_flan.png |
| dolphin | Minecraft entity dolphin (stand-in atlas) |
| whale | dmobs_whale.png |
| octopus | marinaramobs textureoctopus.png |
| water_dragon | dmobs_waterdragon.png |

## Not migrated (stay on current backend)

| Backend | Species |
|---------|---------|
| `skeletal_geo` | 16 Tier A/B (cow, sheep, human, …) |
| `rigid_voxels` | `rigid_demo_walker`, `rigid_demo_flyer`, `rigid_demo_swimmer` |

## Per-species checklist

1. `python tools/convert_creature_mesh_to_gltf.py --species <id>`
2. `python tools/validate_gltf_creature.py --species <id>`
3. `creature.json`: `backend: gltf_skeleton`, `visual.gltf`, clips/state_map
4. Remove `visual.parts[]` from production JSON
5. Preview smoke + in-world spawn
