# Cubatarium

A Minecraft-like voxel sandbox engine using C++17, OpenGL, and GLFW. Uses Minecraft-style block textures.

## Resource packs

Blocks and block textures ship as **resource packs** under `resource_packs/`. See [docs/RESOURCE_PACKS.md](docs/RESOURCE_PACKS.md).

Default release packs: `kenney_voxel_16` + `cubatarium_cc0_base` (CC0). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

For full Minecraft-parity visuals, generate a local pack (gitignored):

```powershell
.\tools\migrate_to_resource_pack.ps1
```

Then set `"enabled": ["minecraft_legacy_16"]` in `config.json`.

## Build

- CMake 3.15+, C++17 compiler
- Dependencies via vcpkg: GLFW, GLEW, GLM, FreeType, nlohmann/json
- Configure with `CMAKE_BINARY_DIR` pointing to `bin/` (see root `CMakeLists.txt`)
- Build with Visual Studio or MSBuild: `bin/Cubatarium.sln`, Configuration **Release**
- Run from `bin/` so `shaders/`, `textures/`, and `config.json` resolve correctly

```powershell
cmake -S . -B bin
& "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "bin\Cubatarium.sln" /p:Configuration=Release /m /v:minimal
```

## Controls

Two **control schemes** (Settings → Application → Control scheme; `config.json` → `ui.control_scheme`):

### Classic (Minecraft) — default

| Input | Action |
|-------|--------|
| Mouse move | Look (captured cursor) |
| LMB hold | Break block (animated) |
| RMB click | Place block or prefab from active hotbar slot |
| Left Alt | Toggle free cursor for HUD clicks |
| Creature/Skin hotbar | LMB tap to spawn / apply skin |

### Cubatarium

| Input | Action |
|-------|--------|
| RMB hold + drag | Look |
| LMB tap | Place block |
| LMB hold (≥ dead zone) | Break block (animated) |
| Dead zone (0.2–0.5 s release) | No action |

### Shared

| Key | Action |
|-----|--------|
| WASD | Move |
| Space | Jump |
| Q/E | Up/down (creative-style vertical nudge) |
| 0–9 | Hotbar (slot 9 = `tree_small` prefab) |
| F7 | Place test tree prefab at crosshair |
| F12 | Show new-world hint |
| Shift+F12 | Create new procedural world (saves current) |
| Delete | Remove targeted block (instant) |
| ` / B / E | Console / palette / inventory |
| F9 | Toggle HUD |
| F10 | Performance overlay |
| F11 | Crosshair |
| F1–F8 | Sky presets |

Legacy config key `ui.block_input_profile` is still read; saves write `ui.control_scheme`.

World autosaves every **60 seconds**. Closing the window saves config and world.

## Architecture
