# Cubatarium

A Minecraft-like voxel sandbox engine using C++17, OpenGL, and GLFW. Uses Minecraft-style block textures.

## Build

- CMake 3.10+, C++17 compiler
- Dependencies via vcpkg: GLFW, GLEW, GLM, FreeType, nlohmann/json
- Configure with `CMAKE_BINARY_DIR` pointing to `bin/` (see root `CMakeLists.txt`)
- Build with Visual Studio or MSBuild: `bin/Cubatarium.sln`, Configuration **Release**
- Run from `bin/` so `shaders/`, `textures/`, and `config.json` resolve correctly

```powershell
cmake -S . -B bin
& "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "bin\Cubatarium.sln" /p:Configuration=Release /m /v:minimal
```

## Controls

| Key | Action |
|-----|--------|
| WASD | Move |
| Space | Jump |
| Q/E | Up/down (creative-style vertical nudge) |
| Mouse (RMB drag) | Look (Classic: click RMB to place when not dragging) |
| LMB (Classic, default) | Hold to break block (animated); creature/skin slots use tap |
| RMB click (Classic) | Place block, prefab (Alt), or hotbar object |
| LMB (Cubatarium profile) | Tap to place; hold to break; dead zone between tap and hold |
| RMB (Cubatarium) | Look only (drag) |
| 0–9 | Hotbar (slot 9 = `tree_small` prefab) |
| F7 | Place test tree prefab at crosshair |
| F12 | Show new-world hint |
| Shift+F12 | Create new procedural world (saves current) |
| Delete | Remove targeted block (instant) |
| Settings | Block input profile: Classic (default) or Cubatarium |
| F9 | Toggle HUD |
| F10 | Performance overlay |
| F11 | Crosshair |
| F1–F8 | Sky presets |

World autosaves every **60 seconds**. Closing the window saves config and world.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for block/chunk pipeline, prefabs, streaming, and save formats.
