# Cubatarium Architecture

## Render data flow

```
BlockWorld → ChunkManager → ChunkMeshCache (GreedyMesher) → FaceInstance[] → GeometryEngine::PrepareRenderBatchesFromBlocks → instanced face quads
```

World geometry lives only in `BlockWorld`. `ChunkMeshCache` rebuilds visible faces per chunk using greedy meshing. `GeometryEngine` batches by block texture and draws instanced unit quads.

## Save files (`worlds/<name>/`)

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

When `streaming_enabled` is true in `config.json`, `ChunkStreamer` loads/saves chunks around the camera and generates missing columns procedurally. `render_distance_chunks` controls radius (default 4).

## Config (`config.json`)

- `world_seed`, `terrain` (`heightmap` | `flat`)
- `render_distance_chunks`, `streaming_enabled`
- Autosave every 60s; exit saves world + config

Executable in `bin/` saves to project root `worlds/` (via `FindProjectRoot`), not `bin/worlds/`.
