# Cubatarium Architecture

## Render data flow

```
BlockWorld → ChunkMeshCache (GreedyMesher) → GreedyMeshBatch[] (world verts + baked UV) → GeometryEngine::DrawGreedyMeshBatches → vshader.glsl
```

Legacy path (`greedy_meshing: false`): instanced cubes via `vshader_instanced.glsl`.

World geometry lives only in `BlockWorld`. `ChunkMeshCache` rebuilds mesh per chunk. **`config.json` → `render`** toggles optimizations (see below). Default in `config.json.example` is legacy (all `false`).

| Flag | Effect |
|------|--------|
| `greedy_meshing` | `true` (default): GreedyMesher merged quads; **required** for water, lava, fire. `false`: legacy instanced cubes (solids only). |
| `face_quads` | `true` (default). **Requires `greedy_meshing: true`** (auto-enabled if missing). Greedy mesh as world-space triangles with baked UV. |
| `frustum_culling` | `true` (default): skip chunks outside view frustum. |
| `batch_cache` | `true` (default): reuse prepared draw batches when mesh revision unchanged. |
| `frustum_culling` | Skip off-screen chunks in the instance list. Skips near plane; chunk AABB expanded by 2 blocks. |
| `batch_cache` | Skip rebuilding texture batches when mesh revision unchanged (legacy instanced path only). |

**Shaders:** legacy blocks use `vshader_instanced.glsl`. Greedy mesh uses `vshader.glsl` with vertices in world space and atlas UV baked on the CPU (`BlockAtlasUV.h`, same layout as `CubeGL`).

Bisect: start with all `false`, enable one flag at a time, restart game. Console prints `Render: greedy=...` on startup.

## Runtime paths (next to executable)

| Path | Role |
|------|------|
| `<exe_dir>/config.json` | Only config read/written at runtime (`default_world`, `default_user`, terrain settings) |
| `<exe_dir>/worlds/World_NNN/` | Saved worlds (`World_001`, `World_002`, …) |

Assets (textures, models, prefabs) resolve via `FindProjectRoot()` from the repo / project directory.

## Save files (`worlds/World_NNN/`)

| File | Content |
|------|---------|
| `chunks/` | Per-chunk JSON (`cx_cy_cz.json`), format_version 2, sparse voxels |
| `chunks.json` | Marker `{ "format_version": 3, "storage": "per_file" }` |
| `users.json` | Per user: `position[3]`, `yaw`, `pitch` |
| `world_data.json` | `world_name`, `spawn_point` |
| `objects.json` | Legacy; read-only migration source |
| `blocks.json` | Legacy flat block list (import if chunks empty) |

## Load order (`World::Load`)

1. `world_data.json`, `users.json`
2. `chunks/` directory if present, else monolithic `chunks.json` (+ migrate to per-file)
3. If `CountNonAir == 0`: `blocks.json`
4. If still empty: `MigrateObjectsFromJson(objects.json)`
5. If still empty: procedural terrain (`GenerateHeightmap` or `GenerateFlat`)
6. `RebuildBlockMesh`; camera restored from user data (not spawn reset)

## Blocks

| Path | Role |
|------|------|
| `textures/blocks/*.png` | Base face textures (`TextureBaseStorage`; stem = filename without `.png`) |
| `models/blocks/*.json` | Block types → `TextureCubeStorage` → `BlockRegistry` |

Each block JSON requires `name`, `id` (non-zero), and `textures`: six strings in face order **[+Z, +X, −Z, −X, +Y, −Y]**.

Import or refresh blocks from an external pack:

```powershell
powershell -ExecutionPolicy Bypass -File tools/import_blocks.ps1
```

Manifest: `tools/block_manifest.json` (+ optional `tools/block_manifest_supplement.json` for extended blocks). Run `tools/import_blocks.ps1` to copy static PNGs from the external pack and regenerate `models/blocks/*.json`. CMake copies `textures/` to `bin/textures` on build (like `models/`).

### Worldgen surface blocks (biomes)

| Biome | Surface | Subsurface |
|-------|---------|------------|
| Plains / Forest | grass | dirt |
| Desert | sand | sandstone (fallback sand) |
| Hills | stone | gravel (fallback stone) |
| Tundra | snow (fallback stone) | dirt |

Trees: prefabs `tree_small` / `tree_large` use block types `tree_log` (bark `tree_side`, rings `tree_top`) and `tree_leaves` (`leaves_opaque`). Requires `procedural.trees: true` in config.

### Animated blocks and fluids

Block metadata lives in `models/blocks/*.json` (`animation`, `render`, `physics`) parsed by `BlockDefinitionStorage`. Flipbook atlases are built in `TextureCubeStorage::CreateCubeTexture` (vertical strip for uniform six-face blocks like `water`/`lava`; multi-face rows for `fire`).

| Module | Role |
|--------|------|
| `AnimationClock` | Global 20 TPS tick; `uAnimFrame` / `uAnimFrameCount` in greedy and instanced shaders |
| `BlockPhysicsProfile` | `occupancy`, drag, sink/rise; presets `water`, `lava`, `fire` |
| `BlockRegistry::BlocksMovement` | Collision and raycast (only `occupancy >= 1`) |
| Greedy mesh | Opaque pass then transparent (`render.transparent`); fluid–fluid faces kept |
| Worldgen | `fill_water`, `fill_lava`, `fill_fire` in `ProceduralSettings`; `FillFluidColumn` to `sea_level` |

Import animated types: `tools/block_manifest_animated.json` (ids 170–172) via `tools/import_blocks.ps1`. QA: new world with `overworld_biomes`, `fill_water` / `fill_fire` true; spawn fire prefab `fire_patch`.

## Asset paths

| Path | Role |
|------|------|
| `models/blocks/` | Block types → `BlockRegistry` |
| `textures/blocks/` | Per-face PNG atlases for blocks |
| `models/objects/` | Legacy brush prototypes (`SingleCube` only) |
| `prefabs/` | Multi-block templates → `PrefabLibrary` |
| `prefabs/user/` | Drop-in user prefabs (optional) |

Prefab assets load at startup via `Core::LoadSystem`. They are **not** stored in world saves — placed blocks persist in chunk files only.

## PrefabLibrary vs ObjectStorage

- **ObjectStorage** — legacy single-block brush catalog (`TakeObject` deprecated).
- **PrefabLibrary** — JSON templates with sparse `blocks[]` and `anchor`; placement via `World::PlacePrefab`.
- **Hotbar** — `0–9` block hotbar; `Alt+0–9` prefab hotbar (`SetPrefabHotbar` from `PrefabLibrary::ListNames()`).

## Streaming

Default: `streaming_enabled: true` in `config.json`.

`ChunkStreamer` around the camera each frame:

1. For each chunk in render radius — try `chunks/cx_cy_cz.json` on disk.
2. If missing — generate columns for that chunk using the world's **terrain** mode:
   - `heightmap` — noise height column (`GenerateColumn`)
   - `flat` — bedrock / stone / grass at fixed height (`GenerateFlatColumn`)
3. Unload distant chunks (save to disk, drop from memory).

Initial area on new world: 2 chunk radius (`GenerateSpawnArea` or `GenerateFlatArea`). Without streaming, a fixed 33×33 region is generated at once.

`terrain` and `world_seed` are stored in `world_data.json` per world so reload uses the same generator as creation.

`render_distance_chunks` — radius in chunks (default 4).

## Config (`<exe_dir>/config.json`)

- `default_world` — folder name under `worlds/` (e.g. `World_001`)
- `default_user`, `world_seed`, `terrain` (`heightmap` | `flat`)
- `render_distance_chunks`, `streaming_enabled`
- Autosave every 60s; exit saves world + config

## Startup (`Core::LoadSystem`)

1. Read config next to the executable.
2. If `worlds/` is missing, empty, or `default_world` folder does not exist → `CreateWorld()` (next `World_NNN` + procedural terrain) and write config.
3. Otherwise → `LoadLastWorld()` from `default_world`.

Shift+F11 / Shift+F12 create the next `World_NNN`, save immediately, and set `default_world` so the next launch opens that world.
