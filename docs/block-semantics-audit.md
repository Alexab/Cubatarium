# Block semantics audit

Generated: 2026-06-18T19:02:04Z

## Summary

| Pack | Blocks | Tier A | Semantics OK | Score | Role |
|------|--------|--------|--------------|-------|------|
| `cubatarium_cc0_base` | 15 | 15/17 | 15/17 | 88.2% | secondary |
| `goncalo_patterns_16` | 10 | 6/17 | 6/17 | 35.3% | secondary |
| `kenney_pattern_lines_16` | 9 | 5/17 | 5/17 | 29.4% | secondary |
| `kenney_pattern_pixel_16` | 17 | 6/17 | 6/17 | 35.3% | secondary |
| `kenney_voxel_128` | 58 | 17/17 | 17/17 | 100.0% | primary |
| `kenney_voxel_16` | 58 | 17/17 | 17/17 | 100.0% | primary |
| `minecraft_legacy_16` | 173 | 17/17 | 15/17 | 88.2% | secondary |
| `minetest_default_16` | 251 | 12/17 | 9/17 | 52.9% | secondary |
| `oga_mc_inspired_16` | 5 | 2/17 | 2/17 | 11.8% | secondary |
| `programmer_art_16` | 128 | 16/17 | 13/17 | 76.5% | secondary |
| `refi_textures_16` | 108 | 15/17 | 13/17 | 76.5% | secondary |
| `sbs_sandbox_terrain_16` | 13 | 9/17 | 9/17 | 52.9% | secondary |
| `seamless_patterns_16` | 28 | 14/17 | 14/17 | 82.4% | secondary |
| `snez_16` | 88 | 14/17 | 12/17 | 70.6% | secondary |
| `too_many_stones_16` | 38 | 10/17 | 10/17 | 58.8% | secondary |

## Global warnings

- resolution_mismatch across installed packs — 16px: cubatarium_cc0_base, goncalo_patterns_16, kenney_pattern_lines_16, kenney_pattern_pixel_16, kenney_voxel_16, minecraft_legacy_16, minetest_default_16, oga_mc_inspired_16, programmer_art_16, refi_textures_16, sbs_sandbox_terrain_16, seamless_patterns_16, snez_16, too_many_stones_16, 128px: kenney_voxel_128

## Per-pack issues

### `cubatarium_cc0_base`

**Errors / semantics:**

- missing tier_a block: tree_log
- missing tier_a block: tree_leaves

### `goncalo_patterns_16`

**Errors / semantics:**

- missing tier_a block: bedrock
- missing tier_a block: sandstone
- missing tier_a block: snow
- missing tier_a block: clay
- missing tier_a block: ice
- missing tier_a block: hellrock
- missing tier_a block: water
- missing tier_a block: lava
- missing tier_a block: fire
- missing tier_a block: tree_log
- missing tier_a block: tree_leaves

### `kenney_pattern_lines_16`

**Errors / semantics:**

- missing tier_a block: bedrock
- missing tier_a block: sandstone
- missing tier_a block: gravel
- missing tier_a block: snow
- missing tier_a block: clay
- missing tier_a block: ice
- missing tier_a block: hellrock
- missing tier_a block: water
- missing tier_a block: lava
- missing tier_a block: fire
- missing tier_a block: tree_log
- missing tier_a block: tree_leaves

### `kenney_pattern_pixel_16`

**Errors / semantics:**

- missing tier_a block: bedrock
- missing tier_a block: sandstone
- missing tier_a block: snow
- missing tier_a block: clay
- missing tier_a block: ice
- missing tier_a block: hellrock
- missing tier_a block: water
- missing tier_a block: lava
- missing tier_a block: fire
- missing tier_a block: tree_log
- missing tier_a block: tree_leaves

### `minecraft_legacy_16`

**Errors / semantics:**

- water: water: water.png height 512 != width×frames (16×4=64)
- water: water: water.png height 512 != width×frames (16×4=64)
- water: water: water.png height 512 != width×frames (16×4=64)
- water: water: water.png height 512 != width×frames (16×4=64)
- water: water: water.png height 512 != width×frames (16×4=64)
- water: water: water.png height 512 != width×frames (16×4=64)
- lava: lava: lava.png height 320 != width×frames (16×4=64)
- lava: lava: lava.png height 320 != width×frames (16×4=64)
- lava: lava: lava.png height 320 != width×frames (16×4=64)
- lava: lava: lava.png height 320 != width×frames (16×4=64)
- lava: lava: lava.png height 320 != width×frames (16×4=64)
- lava: lava: lava.png height 320 != width×frames (16×4=64)

**Warnings:**

- grass: textures differ from reference pack (ref=['grass_side', 'grass_side', 'grass_side', 'grass_side', 'grass_top', 'dirt'], actual=['grass_side', 'grass_side', 'grass_side', 'grass_side', 'grass_top_green', 'grass_top'])
- sandstone: textures differ from reference pack (ref=['sandstone', 'sandstone', 'sandstone', 'sandstone', 'sandstone', 'sandstone'], actual=['sandstone_side', 'sandstone_side', 'sandstone_side', 'sandstone_side', 'sandstone_top', 'sandstone_bottom'])
- fire: textures differ from reference pack (ref=['fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0'], actual=['fire', 'fire', 'fire', 'fire', 'fire', 'fire'])
- wood: textures differ from reference pack (ref=['tree_side', 'tree_side', 'tree_side', 'tree_side', 'tree_top', 'tree_top'], actual=['wood', 'wood', 'wood', 'wood', 'wood', 'wood'])

### `minetest_default_16`

**Errors / semantics:**

- missing tier_a block: bedrock
- missing tier_a block: stone
- missing tier_a block: grass
- missing tier_a block: sandstone
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- missing tier_a block: wood

**Warnings:**

- fire: textures differ from reference pack (ref=['fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0'], actual=['fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_1', 'fire_1', 'fire_1', 'fire_1', 'fire_1', 'fire_1'])

### `oga_mc_inspired_16`

**Errors / semantics:**

- missing tier_a block: bedrock
- missing tier_a block: dirt
- missing tier_a block: grass
- missing tier_a block: sand
- missing tier_a block: sandstone
- missing tier_a block: gravel
- missing tier_a block: snow
- missing tier_a block: clay
- missing tier_a block: ice
- missing tier_a block: water
- missing tier_a block: lava
- missing tier_a block: fire
- missing tier_a block: wood
- missing tier_a block: tree_log
- missing tier_a block: tree_leaves

### `programmer_art_16`

**Errors / semantics:**

- missing tier_a block: sandstone
- water: water: water.png height 32 != width×frames (16×4=64)
- water: water: water.png height 32 != width×frames (16×4=64)
- water: water: water.png height 32 != width×frames (16×4=64)
- water: water: water.png height 32 != width×frames (16×4=64)
- water: water: water.png height 32 != width×frames (16×4=64)
- water: water: water.png height 32 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)
- fire: fire: fire_0.png height 16 != width×frames (16×2=32)

**Warnings:**

- grass: textures differ from reference pack (ref=['grass_side', 'grass_side', 'grass_side', 'grass_side', 'grass_top', 'dirt'], actual=['grass_side', 'grass_side', 'grass_side', 'grass_side', 'grass_top_green', 'grass_top'])
- fire: textures differ from reference pack (ref=['fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0'], actual=['fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_0', 'fire_1', 'fire_1', 'fire_1', 'fire_1', 'fire_1', 'fire_1'])
- wood: textures differ from reference pack (ref=['tree_side', 'tree_side', 'tree_side', 'tree_side', 'tree_top', 'tree_top'], actual=['wood', 'wood', 'wood', 'wood', 'wood', 'wood'])

### `refi_textures_16`

**Errors / semantics:**

- missing tier_a block: hellrock
- water: water: water.png height 16 != width×frames (16×4=64)
- water: water: water.png height 16 != width×frames (16×4=64)
- water: water: water.png height 16 != width×frames (16×4=64)
- water: water: water.png height 16 != width×frames (16×4=64)
- water: water: water.png height 16 != width×frames (16×4=64)
- water: water: water.png height 16 != width×frames (16×4=64)
- lava: lava: lava.png height 16 != width×frames (16×4=64)
- lava: lava: lava.png height 16 != width×frames (16×4=64)
- lava: lava: lava.png height 16 != width×frames (16×4=64)
- lava: lava: lava.png height 16 != width×frames (16×4=64)
- lava: lava: lava.png height 16 != width×frames (16×4=64)
- lava: lava: lava.png height 16 != width×frames (16×4=64)
- missing tier_a block: fire

**Warnings:**

- grass: textures differ from reference pack (ref=['grass_side', 'grass_side', 'grass_side', 'grass_side', 'grass_top', 'dirt'], actual=['grass_side', 'grass_side', 'grass_side', 'grass_side', 'grass_top_green', 'grass_top'])
- sandstone: textures differ from reference pack (ref=['sandstone', 'sandstone', 'sandstone', 'sandstone', 'sandstone', 'sandstone'], actual=['sandstone_side', 'sandstone_side', 'sandstone_side', 'sandstone_side', 'sandstone_top', 'sandstone_bottom'])
- wood: textures differ from reference pack (ref=['tree_side', 'tree_side', 'tree_side', 'tree_side', 'tree_top', 'tree_top'], actual=['wood', 'wood', 'wood', 'wood', 'wood', 'wood'])

### `sbs_sandbox_terrain_16`

**Errors / semantics:**

- missing tier_a block: bedrock
- missing tier_a block: sandstone
- missing tier_a block: clay
- missing tier_a block: ice
- missing tier_a block: water
- missing tier_a block: lava
- missing tier_a block: fire
- missing tier_a block: tree_leaves

### `seamless_patterns_16`

**Errors / semantics:**

- missing tier_a block: water
- missing tier_a block: lava
- missing tier_a block: fire

### `snez_16`

**Errors / semantics:**

- missing tier_a block: hellrock
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- water: water: water.png height 256 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- lava: lava: lava.png height 128 != width×frames (16×4=64)
- missing tier_a block: fire
- missing tier_a block: tree_leaves

**Warnings:**

- sandstone: textures differ from reference pack (ref=['sandstone', 'sandstone', 'sandstone', 'sandstone', 'sandstone', 'sandstone'], actual=['sandstone_side', 'sandstone_side', 'sandstone_side', 'sandstone_side', 'sandstone_top', 'sandstone_bottom'])
- wood: textures differ from reference pack (ref=['tree_side', 'tree_side', 'tree_side', 'tree_side', 'tree_top', 'tree_top'], actual=['wood', 'wood', 'wood', 'wood', 'wood', 'wood'])

### `too_many_stones_16`

**Errors / semantics:**

- missing tier_a block: grass
- missing tier_a block: water
- missing tier_a block: lava
- missing tier_a block: fire
- missing tier_a block: wood
- missing tier_a block: tree_log
- missing tier_a block: tree_leaves

**Warnings:**

- sandstone: textures differ from reference pack (ref=['sandstone', 'sandstone', 'sandstone', 'sandstone', 'sandstone', 'sandstone'], actual=['sandstone_side', 'sandstone_side', 'sandstone_side', 'sandstone_side', 'sandstone_top', 'sandstone_bottom'])

