# Cubatarium

A Minecraft-like voxel sandbox engine using C++17, OpenGL, and GLFW. Uses Minecraft-style block textures.

## Resource packs

Blocks and block textures ship as **resource packs** under `resource_packs/`. See [docs/RESOURCE_PACKS.md](docs/RESOURCE_PACKS.md).

Default release packs: `kenney_voxel_16` + `cubatarium_cc0_base` (Kenney Voxel Pack, CC0). Regenerate textures with `python tools/rebuild_release_resource_packs.py`. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Licensing

- **Code license:** Cubatarium engine/game code is licensed under [MIT](LICENSE).
- **Bundled content licenses:** shipped assets include MIT, Apache-2.0, CC0, CC BY 4.0, and CC BY-SA (3.0/4.0), depending on pack/asset.
- **Compliance source:** attribution and per-component license details are maintained in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This means the project code stays MIT, while asset files keep their original licenses.

For full Minecraft-parity visuals, generate a local pack (gitignored):

```powershell
.\tools\migrate_to_resource_pack.ps1
```

Then pick `minecraft_legacy_16` in **Settings → Application** (default for new worlds) or **New World** (per-world). You can also set `"default_enabled": ["minecraft_legacy_16"]` in `config.json`.

## Build

- CMake 3.15+, C++17 compiler
- Dependencies via vcpkg (Windows) or system packages (Linux): GLFW, GLEW, GLM, FreeType, nlohmann/json
- **Build tree:** `build/desktop-msvc/` (Windows) or `build/desktop-linux/` (Linux)
- **Runtime:** `bin/` — executable and copied game data; run/debug with cwd `bin/`
- See [platforms/desktop/README.md](platforms/desktop/README.md) and [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md)

```powershell
.\scripts\branding\generate-app-icons.ps1
.\configure.ps1 -Config Release
cmake --build build\desktop-msvc --config Release
```

Linux:

```bash
./scripts/build/linux-configure.sh Release
cmake --build build/desktop-linux
./bin/Cubatarium
```

## Controls

Two **control schemes** (Settings → Application → Control scheme; `config.json` → `ui.control_scheme`):

### Classic (Minecraft) — default

| Input | Action |
|-------|--------|
| Mouse move | Look (captured cursor) |
| LMB hold | Break block (animated) |
| RMB click | Place block, prefab, spawn creature, or apply skin from active slot |
| Left Alt | Toggle free cursor for HUD clicks |

### Cubatarium

| Input | Action |
|-------|--------|
| RMB hold + drag | Look |
| LMB tap | Place block, prefab, spawn creature, or apply skin |
| LMB hold (≥ dead zone) | Break block (animated) |
| Dead zone (0.2–0.5 s release) | No action |

### Shared

| Key | Action |
|-----|--------|
| WASD | Move |
| Space | Jump |
| Q/E | Up/down (creative-style vertical nudge) |
| 0–9 | Hotbar (slot 9 = `tree_small` prefab) |
| Mouse wheel | Cycle primary hotbar (FPS/perspective); zoom in isometric |
| F7 | Place test tree prefab at crosshair |
| F12 | Show new-world hint |
| Shift+F12 | Create new procedural world (saves current) |
| Delete | Remove targeted block (instant) |
| ` / B / E | Console / palette (Blocks) / inventory (last tab) |
| F9 | Toggle HUD |
| F10 | Performance overlay |
| F11 | Crosshair |
| F1–F8 | Sky presets |

Legacy config key `ui.block_input_profile` is still read; saves write `ui.control_scheme`.

World autosaves every **60 seconds**. Closing the window saves config and world.

## Architecture

Cubatarium is a C++17 voxel sandbox. High-level data flow:

```
BlockWorld -> ChunkMeshCache (GreedyMesher) -> GeometryEngine -> GLSL shaders
```

- **World / streaming:** chunk load/save via `UChunkStorageService` (`.cchunk` binary); legacy JSON migration on load only.
- **Worldgen:** data-driven packs under `content/worldgen_packs/`.
- **Assets:** resource packs in `resource_packs/` (blocks, textures, creatures).

Full details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), render pipeline: [src/Render/Pipeline/README.md](src/Render/Pipeline/README.md).
