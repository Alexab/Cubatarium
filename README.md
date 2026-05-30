# Cubatarium

A Minecraft-like voxel sandbox engine using C++17, OpenGL, and GLFW. Uses Minecraft-style block textures.

## Build

- CMake 3.10+, C++17 compiler
- Dependencies via vcpkg: GLFW, GLEW, GLM, FreeType, nlohmann/json
- Configure with `CMAKE_BINARY_DIR` pointing to `bin/` (see root `CMakeLists.txt`)
- Build with Visual Studio or MSBuild: `bin/Cubatarium.sln`, Configuration **Release**
- Run from `bin/` so `shaders/`, `textures/`, and `config.json` resolve correctly

## Controls

- **WASD + Q/E** — move (English keyboard layout)
- **Mouse** — look
- **LMB (short)** — place block; **LMB (hold)** — remove block
- **0–9** — select block type in hotbar
- **Delete** — remove targeted block

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for engine layers and the block-grid roadmap.
