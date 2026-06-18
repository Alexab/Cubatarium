# Third-party notices

## Resource pack assets

### Kenney Voxel Pack (CC0-1.0)

- **Pack IDs:** `cubatarium_cc0_base`, `kenney_voxel_16`, `kenney_voxel_128`
- **Source:** [Kenney Voxel Pack](https://kenney.nl/assets/voxel-pack)
- **License:** CC0 1.0 Universal (public domain dedication)
- **Build:** `python tools/rebuild_release_resource_packs.py`

### Kenney Pattern Pack Pixel (CC0-1.0)

- **Pack ID:** `kenney_pattern_pixel_16`
- **Source:** [Kenney Pattern Pack Pixel](https://kenney.nl/assets/pattern-pack-pixel)
- **License:** CC0 1.0

### Kenney Pattern Pack 2 / Lines (CC0-1.0)

- **Pack ID:** `kenney_pattern_lines_16`
- **Source:** [Kenney Pattern Pack 2](https://kenney-assets.itch.io/pattern-pack-2)
- **License:** CC0 1.0

### Seamless Pattern Pack (CC0-1.0)

- **Pack ID:** `seamless_patterns_16`
- **Source:** [Seamless Pattern Pack](https://opengameart.org/content/seamless-pattern-pack) (Ragnar Random)
- **License:** CC0 1.0

### Goncalo Pixel Patterns (CC0-1.0)

- **Pack ID:** `goncalo_patterns_16`
- **Source:** [Pixel Patterns](https://goncalomcoliveira.itch.io/pixel-patterns)
- **License:** CC0 1.0

### SBS Sandbox Terrain Pack (CC0-1.0)

- **Pack ID:** `sbs_sandbox_terrain_16`
- **Source:** [SBS Sandbox Terrain Pack](https://screamingbrainstudios.itch.io/sbst-pack)
- **License:** CC0 1.0

### OGA Minecraft-inspired textures (CC0-1.0)

- **Pack ID:** `oga_mc_inspired_16`
- **Source:** [CC0 Minecraft-inspired textures](https://opengameart.org/content/cc0-minecraft-inspired-textures)
- **License:** CC0 1.0

### Minetest Game default textures (CC BY-SA 3.0)

- **Pack ID:** `minetest_default_16`
- **Source:** [minetest-game/default](https://github.com/minetest-game/default)
- **Copyright:** celeron55, Perttu Ahola et al.
- **License:** [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/)
- **Note:** Repackaged textures and derivatives are licensed under CC BY-SA 3.0. Attribution required.

Research packs are built via `python tools/build_research_resource_packs.py` from assets downloaded by `python tools/download_texture_packs.py`.

## Removed from repository

Minecraft-derived block textures and JSON under `textures/blocks/` and `models/blocks/` were removed from version control as part of the resource-pack migration. They may be regenerated locally for personal use only via `tools/migrate_to_resource_pack.ps1` into `resource_packs/minecraft_legacy_16/` (gitignored). **Do not redistribute** Minecraft-derived assets.

## Other dependencies

See project `CMakeLists.txt` and vcpkg manifest for C++ library licenses (GLFW, GLEW, GLM, FreeType, nlohmann/json, stb, etc.).
