# Third-party notices

Attribution for open-source **libraries**, **runtime assets** shipped in Cubatarium builds (Windows installer, Android APK/AAB), and **game content** bundled with the app.

| Section | Contents |
|---------|----------|
| [Application code](#application-code) | Cubatarium (MIT) |
| [Software libraries](#software-libraries) | GLFW, GLEW, GLM, FreeType, JSON, stb, AndroidX, … |
| [Shipped runtime data](#shipped-runtime-data) | What goes into APK / Windows installer |
| [Resource pack assets](#resource-pack-assets) | Texture packs (CC0 / CC BY-SA) |
| [Worldgen schematics](#worldgen-schematics) | Luanti `.mts` schematics |
| [Creature textures](#creature-textures) | Mob / skin PNGs |
| [Removed](#removed-from-repository) | Not redistributed |

---

## Application code

**Cubatarium** game and engine source: [MIT License](LICENSE) — Copyright (c) 2024 Alexandr Bakhshiev.

---

## Software libraries

Libraries are **statically linked** into the Windows desktop executable (`x64-windows-static` / `/MT`). The Android APK/AAB links native code into `libcubatarium.so` plus small Java dependencies from Gradle.

### Desktop (Windows / Linux) — vcpkg manifest

Declared in [`vcpkg.json`](vcpkg.json), resolved at configure time:

| Component | License | Source |
|-----------|---------|--------|
| [GLFW](https://www.glfw.org/) | [zlib/libpng-style](https://github.com/glfw/glfw/blob/master/LICENSE.md) | Window / input (desktop) |
| [GLEW](https://glew.sourceforge.net/) | [BSD / MIT / ISC](https://github.com/nigels-com/glew/blob/master/LICENSE.txt) | OpenGL extension loading |
| [GLM](https://github.com/g-truc/glm) | [MIT](https://github.com/g-truc/glm/blob/master/copying.txt) | Math |
| [FreeType](https://freetype.org/) | [FTL / GPLv2](https://github.com/freetype/freetype/blob/master/LICENSE.TXT) (used under FTL in this project) | Font rasterization |
| [nlohmann/json](https://github.com/nlohmann/json) | [MIT](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT) | JSON parsing |

Transitive libraries pulled by vcpkg for the static Windows build (linked into `Cubatarium.exe`):

| Component | License | Notes |
|-----------|---------|--------|
| [zlib](https://zlib.net/) | [zlib](https://zlib.net/zlib_license.html) | via FreeType / libpng |
| [libpng](http://www.libpng.org/) | [libpng License](https://github.com/pnggroup/libpng/blob/master/LICENSE) | via FreeType |
| [bzip2](https://sourceware.org/bzip2/) | [bzip2](https://gitlab.com/bzip2/bzip2/-/blob/master/LICENSE?ref_type=heads) | via FreeType |
| [brotli](https://github.com/google/brotli) | [MIT](https://github.com/google/brotli/blob/master/LICENSE) | via FreeType |

**System libraries (not redistributed):** OpenGL32, Win32 API (Windows); OpenGL, X11/Wayland via GLFW (Linux).

### Android — native (CMake FetchContent)

| Component | Version (pin) | License |
|-----------|---------------|---------|
| [GLM](https://github.com/g-truc/glm) | 1.0.1 | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | MIT |
| [FreeType](https://freetype.org/) | VER-2-13-2 | FTL / GPLv2 (FTL) |

Android NDK / platform (linked, not shipped as separate APK libs): `EGL`, `GLESv3`, `android`, `log`.

### Android — Java (Gradle)

From [`platforms/android/app/build.gradle`](platforms/android/app/build.gradle):

| Component | Version | License |
|-----------|---------|---------|
| [AndroidX Games Activity](https://developer.android.com/games/agdk/game-activity) | 3.0.5 | [Apache 2.0](https://github.com/android/games-samples/blob/main/LICENSE) |
| [AndroidX AppCompat](https://developer.android.com/jetpack/androidx/releases/appcompat) | 1.7.0 | Apache 2.0 |
| [AndroidX Core](https://developer.android.com/jetpack/androidx/releases/core) | 1.15.0 | Apache 2.0 |

Transitive Maven dependencies of the above are standard AndroidX libraries (also Apache 2.0). Full dependency tree: `./gradlew :app:dependencies` in `platforms/android/`.

### Vendored in source tree

| Component | Path | License |
|-----------|------|---------|
| [stb_image](https://github.com/nothings/stb) | `src/ThirdParty/stb_image.h` | [MIT / public domain](https://github.com/nothings/stb/blob/master/LICENSE) |
| [Roboto](https://github.com/googlefonts/roboto) | `fonts/Roboto-Regular.ttf` | [Apache 2.0](fonts/LICENSE-Roboto.txt) |

---

## Shipped runtime data

### Android APK / AAB

Gradle task `syncAssets` copies into the app package (see [`platforms/android/app/build.gradle`](platforms/android/app/build.gradle)):

| Path in package | Origin |
|-----------------|--------|
| `assets/shaders/gles/` | `shaders/gles/` |
| `assets/prefabs/` | `prefabs/` |
| `assets/models/` | `models/` (creatures, blocks, …) |
| `assets/content/` | `content/` |
| `assets/resource_packs/` | `resource_packs/` (except gitignored `minecraft_legacy_16`) |
| `assets/fonts/` | `fonts/` |
| `assets/config.json.example` | template config |

Native code: `libcubatarium.so` (armeabi-v7a + arm64-v8a), C++ shared runtime `libc++_shared.so` from the NDK.

### Windows installer

Staging via [`packaging/windows/installer/prepare-installer.ps1`](packaging/windows/installer/prepare-installer.ps1): `Cubatarium.exe` plus `shaders/`, `prefabs/`, `models/`, `content/`, `objects/`, `resource_packs/`, `fonts/`, `config.json`. Same asset licensing as Android; no separate third-party DLLs (static build).

### Fonts

| File | License | Notes |
|------|---------|-------|
| `fonts/Roboto-Regular.ttf` | [Apache 2.0](fonts/LICENSE-Roboto.txt) | Bundled UI font; see [`fonts/README.md`](fonts/README.md) |

---

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

---

## Creature textures

### Luanti mob textures (MIT / CC BY-SA 3.0)

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

Per-pack `LICENSE.txt` files: `resource_packs/*/LICENSE.txt`.

## Worldgen schematics

Luanti schematic files in `third_party/schematics/` (also used from `prefabs/` / worldgen). Provenance in [`third_party/schematics/SOURCES.json`](third_party/schematics/SOURCES.json):

| Upstream | License | Files |
|----------|---------|-------|
| [minetest-game/default](https://github.com/minetest-game/default) | [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/) | `mtg/*.mts` (trees, bushes, …) |
| [X-DE1/ruined_structures](https://github.com/X-DE1/ruined_structures) | [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) | `ruined_structures/*.mts` |

## Item / armor models

Curated and imported item/armor assets under [`models/items/`](models/items/) (`parts_v1` JSON + folder-per-id `model.gltf`). Import via [`tools/import_item_models.py`](tools/import_item_models.py); stand-in glTF via [`tools/parts_to_gltf.py`](tools/parts_to_gltf.py). Manifest: [`tools/item_model_manifest.json`](tools/item_model_manifest.json).

| Source | License | Notes |
|--------|---------|-------|
| Cubatarium authored `parts[]` / stand-in glTF | CC0-1.0 | Shipped defaults and educational box glTF |
| [Kenney Survival Kit](https://www.kenney.nl/assets/survival-kit) | CC0-1.0 | Imported tools (axe/pickaxe/shovel/hammer/hoe); GLB converted to glTF |
| [KayKit RPG Tools Bits](https://kaylousberg.itch.io/rpg-tools-bits) | CC0-1.0 | Optional; place free tier under `third_party/asset_cache/kaykit_rpg_tools/` |
| [KayKit Fantasy Weapons Bits](https://kaylousberg.itch.io/fantasy-weapons-bits) | CC0-1.0 | Optional; place free tier under `third_party/asset_cache/kaykit_fantasy_weapons/` |
| [Quaternius](https://quaternius.com/) Fantasy Props / Ultimate RPG Items | CC0-1.0 | Optional armor/weapon props |

Imported item ids (Kenney + catalog): see `tools/item_model_manifest.json` and per-folder `ATTRIBUTION.json`. Do not commit raw pack zips (`third_party/asset_cache/` is gitignored).

Details: [`docs/ITEM_ASSETS.md`](docs/ITEM_ASSETS.md).

## Removed from repository

Minecraft-derived block textures and JSON under `textures/blocks/` and `models/blocks/` were removed from version control as part of the resource-pack migration. They may be regenerated locally for personal use only via `tools/migrate_to_resource_pack.ps1` into `resource_packs/minecraft_legacy_16/` (gitignored). **Do not redistribute** Minecraft-derived assets.

---

*Last updated: 2026-08-04. For privacy practices see [`packaging/android/store-assets/PRIVACY_POLICY.md`](packaging/android/store-assets/PRIVACY_POLICY.md).*
