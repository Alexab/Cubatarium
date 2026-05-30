# Cubatarium architecture

## Layers

| Layer | Main types | Role |
|-------|------------|------|
| **Core** | `Core` | Startup, paths, load textures/models/worlds, `config.json` |
| **World** | `World`, `User`, `Camera` | Block grid, raycast, place/delete, save/load |
| **Content** | `ObjectStorage`, `TextureCubeStorage` | JSON prototypes and block textures |
| **Render** | `GeometryEngine`, `ShaderManager` | Instanced cubes, sky, HUD, selection outline |
| **Input** | `WindowManager`, `InputManager` | GLFW loop, keyboard/mouse |

## World model

**Current:** voxels stored in `BlockWorld` / `ChunkManager` as `glm::ivec3 → BlockId`. Block center in world space is `(x, y, z)` with unit cube size 1.0.

**Legacy:** `std::vector<std::shared_ptr<Object>>` — one `SingleCube` per placed block; kept for API compatibility but no longer used for terrain or placement.

## Data files

- `models/blocks/*.json` — texture atlas id per block type
- `models/objects/*.json` — placeable type names (grass, stone, …)
- `worlds/<name>/blocks.json` or `chunks.json` — saved voxels
- `worlds/<name>/users.json` — player camera positions

## Rendering

Unit cube VAO + **instanced** draw (`vshader_instanced.glsl`), batches grouped by texture id. Optional chunk mesh cache skips fully enclosed blocks.
