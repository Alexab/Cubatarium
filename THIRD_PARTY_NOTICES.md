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

- **Pack ID:** `minetest_default_16` (version 2 — full upstream catalog)
- **Source:** [minetest-game/default](https://github.com/minetest-game/default)
- **Copyright:** celeron55, Perttu Ahola et al.
- **License:** [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/)
- **Note:** ~261 blocks (145 Cubatarium + 116 `mtg_*` upstream nodes), ~322 textures. Repackaged under CC BY-SA 3.0.

### REFI Textures (CC BY-SA 4.0)

- **Pack ID:** `refi_textures_16`
- **Source:** [MysticTempest/REFI_Textures](https://github.com/MysticTempest/REFI_Textures)
- **License:** [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
- **Author:** MysticTempest

### ProgrammerArt (CC BY 4.0)

- **Pack ID:** `programmer_art_16`
- **Source:** [deathcap/ProgrammerArt](https://github.com/deathcap/ProgrammerArt)
- **License:** [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)

### Snez texture pack (CC BY-SA 3.0)

- **Pack ID:** `snez_16`
- **Source:** [FrugalGamer/snez-texture-pack](https://github.com/FrugalGamer/snez-texture-pack)
- **License:** CC BY-SA (see `texture_pack.conf` in upstream)
- **Author:** TheFrugalGamer

### Too Many Stones (CC0 textures)

- **Pack ID:** `too_many_stones_16`
- **Source:** [asuna-mt/Too_Many_Stones](https://github.com/asuna-mt/Too_Many_Stones) (textures CC0)
- **License:** CC0 (textures); mod code LGPL (not redistributed)
- **Note:** Partial mapping to Cubatarium stone/ore blocks (~38 blocks).

Research packs are built via `python tools/build_research_resource_packs.py` from assets downloaded by `python tools/download_texture_packs.py`.

### Luanti mob textures (MIT / CC BY-SA 3.0) — creature catalog

- **Paths:** `models/creatures/*/textures/`, `models/creatures/*/LICENSE.txt`, `models/skins/*/LICENSE.txt`
- **Sources (cloned to `CubatariumTextureResearch/`):**
  - [tenplus1/mobs_animal](https://codeberg.org/tenplus1/mobs_animal) — sheep, cow, chicken (MIT)
  - [tenplus1/mobs_monster](https://codeberg.org/tenplus1/mobs_monster) — oerkki, sand_monster (MIT)
  - [tenplus1/dmobs](https://codeberg.org/tenplus1/dmobs) — skeleton (CC BY-SA 3.0, D00Med)
  - [Skandarella/animalworld](https://github.com/Skandarella/animalworld) — wolf, pig/boar (MIT)
  - [minetest/minetest_game](https://github.com/minetest/minetest_game) — `character.png` player skin (CC BY-SA 3.0)
- **Import:** `python tools/import_luanti_creature_textures.py --download`
- **Note:** Pig uses wild boar texture from animalworld as a Luanti-style stand-in. Rigid_voxels copies each mob mesh texture to all part stems.

### Creature resource packs

Packs may overlay `creatures/<species>/creature.json` and `textures/` under `resource_packs/<pack_id>/creatures/`. Applied after base `models/creatures` via `ApplyCreaturePackOverlays` in `Core::ApplyResourcePacks`.

## Removed from repository

Minecraft-derived block textures and JSON under `textures/blocks/` and `models/blocks/` were removed from version control as part of the resource-pack migration. They may be regenerated locally for personal use only via `tools/migrate_to_resource_pack.ps1` into `resource_packs/minecraft_legacy_16/` (gitignored). **Do not redistribute** Minecraft-derived assets.

## Other dependencies

See project `CMakeLists.txt` and vcpkg manifest for C++ library licenses (GLFW, GLEW, GLM, FreeType, nlohmann/json, stb, etc.).
