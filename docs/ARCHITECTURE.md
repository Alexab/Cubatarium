# Cubatarium Architecture

## Render data flow

```
BlockWorld → ChunkManager → ChunkMeshCache (GreedyMesher) → FaceInstance[] → GeometryEngine::PrepareRenderBatchesFromBlocks → instanced face quads
```

World geometry lives only in `BlockWorld`. `ChunkMeshCache` rebuilds mesh per chunk. **`config.json` → `render`** toggles optimizations (see below). Default in `config.json.example` is legacy (all `false`).

| Flag | Effect |
|------|--------|
| `greedy_meshing` | `false`: one instanced **cube** per exposed solid block (old). `true`: GreedyMesher merged quads. |
| `face_quads` | **Requires `greedy_meshing: true`.** Draw unit quads via `vshader_instanced_face.glsl`. Alone has no effect. |

**Shaders:** legacy blocks use `vshader_instanced.glsl`. Greedy face quads use `vshader_instanced_face.glsl` (cube face index 0–5 + atlas tiling). Frustum culling skips the near plane and expands chunk AABB by 2 blocks to avoid holes when the camera is inside a chunk.
| `frustum_culling` | Skip off-screen chunks in the instance list. |
| `batch_cache` | Skip rebuilding texture batches when mesh revision unchanged. |

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

## Asset paths

| Path | Role |
|------|------|
| `models/blocks/` | Block types → `BlockRegistry` |
| `models/objects/` | Legacy brush prototypes (`SingleCube` only) |
| `prefabs/` | Multi-block templates → `PrefabLibrary` |
| `prefabs/user/` | Drop-in user prefabs (optional) |

Prefab assets load at startup via `Core::LoadSystem`. They are **not** stored in world saves — placed blocks persist in chunk files only.

## PrefabLibrary vs ObjectStorage

- **ObjectStorage** — legacy single-block brush catalog (`TakeObject` deprecated).
- **PrefabLibrary** — JSON templates with sparse `blocks[]` and `anchor`; placement via `World::PlacePrefab`.
- **Hotbar** — `User` slots `{ kind: block|prefab, id }`; slot 9 defaults to prefab `tree_small`.

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
