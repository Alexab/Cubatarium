# Cubatarium Architecture

## Render data flow

```
BlockWorld → ChunkMeshCache (GreedyMesher) → GreedyMeshBatch[] → GeometryEngine (opaque + `GreedyTransparentPipeline`) → `vshader_greedy.glsl`
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

Block types and face textures load from **resource packs** under `resource_packs/` (see [RESOURCE_PACKS.md](RESOURCE_PACKS.md)). `UCore::LoadConfig` resolves enabled packs via `UResourcePackResolver`, merges definitions in `UBlockMergeRegistry`, and builds GPU textures in `UTextureCubeStorage`.

| Component | Role |
|-----------|------|
| `resource_packs/*/pack.json` | Pack id, priority, license, resolution |
| `resource_packs/*/blocks/*.json` | Block physics, render, animation, six (or twelve) texture stems |
| `resource_packs/*/textures/blocks/*.png` | Face PNGs (stems referenced by block JSON) |
| `UBlockMergeRegistry` | Union + merge by block `name`; runtime `BlockId` after lexicographic sort |
| `UPlaceholderTextureCache` | Programmatic PNG when a stem is missing across the pack chain |

Face order in block JSON: **[+Z, +X, −Z, −X, +Y, −Y]** (same as `BlockAtlasUV.h`).

Default release packs: `kenney_voxel_16` + `cubatarium_cc0_base`. For full Minecraft-parity visuals, generate a local legacy pack (gitignored):

```powershell
.\tools\migrate_to_resource_pack.ps1
```

Validate a pack: `python tools/validate_resource_pack.py resource_packs/cubatarium_cc0_base`

Legacy `models/blocks/` and `textures/blocks/` at repo root are **deprecated**; use resource packs instead.

### Worldgen surface blocks (biomes)

| Biome | Surface | Subsurface |
|-------|---------|------------|
| Plains / Forest | grass | dirt |
| Desert | sand | sandstone (fallback sand) |
| Hills | stone | gravel (fallback stone) |
| Tundra | snow (fallback stone) | dirt |

Trees: prefabs `tree_small` / `tree_large` use block types `tree_log` (bark `tree_side`, rings `tree_top`) and `tree_leaves` (`leaves_opaque`). Requires `procedural.trees: true` in config.

### Animated blocks and fluids

Block metadata lives in `resource_packs/*/blocks/*.json` (`animation`, `render`, `physics`) parsed by `BlockDefinitionStorage`. Flipbook atlases are built in `TextureCubeStorage::CreateCubeTexture` (vertical strip for uniform six-face blocks like `water`/`lava`; multi-face rows for `fire`).

| Module | Role |
|--------|------|
| `AnimationClock` | Global elapsed time; per-block `frametime` from block JSON (20 ticks/s) drives `uAnimFrame` |
| `BlockPhysicsProfile` | `occupancy`, drag, sink/rise; presets `water`, `lava`, `fire` |
| `BlockRegistry::BlocksMovement` | Collision and raycast (only `occupancy >= 1`) |
| Greedy mesh | Opaque pass, then multi-pass transparent (see below); fluid–fluid faces kept |
| Worldgen | `fill_water`, `fill_lava`, `fill_fire` in `ProceduralSettings`; `FillFluidColumn` to `sea_level`; with `fill_water`, `AdjustSurfaceYForSpawnIsland` raises terrain in a ~48-block-radius disk (+16-block blend) around spawn (0,0) so the player starts on dry land |

### Greedy transparent passes

Documented in [`src/Render/Pipeline/README.md`](../src/Render/Pipeline/README.md). Implementation: [`GreedyTransparentPipeline.cpp`](../src/Render/Pipeline/GreedyTransparentPipeline.cpp).

1. **ShellDepth** — depth prepass (α ≥ `shellAlpha`, default 0.35) + stencil mark.
2. **BehindShell** — color, `GL_GREATER`, stencil == 1 (layers behind the shell; not through solid blocks).
3. **ShellSurface** — color, `GL_LEQUAL`, stencil == 1.
4. **FuzzyEdges** — color, `GL_LESS`, stencil != 1 (soft edges only).

Frame setup: `Application::RenderFrame` clears **color, depth, and stencil** before `GeometryEngine::Paint`. FBO prefab icons use `GlStateScope` so GUI does not leak GL state into the world pass.

Import animated types: water/lava (4-frame vertical strips) and fire (2-frame, 12 stems) ship in CC0 packs. QA: new world with `overworld_biomes`, `fill_water` / `fill_fire` true; spawn fire prefab `fire_patch`.

## Asset paths

| Path | Role |
|------|------|
| `resource_packs/` | Block packs (definitions + textures); copied to `bin/resource_packs/` on build |
| `models/objects/` | Legacy brush prototypes (`SingleCube` only) |
| `textures/` | Non-block textures (creatures, UI, etc.) |
| `prefabs/` | Multi-block templates → `PrefabLibrary` |
| `prefabs/user/` | Drop-in user prefabs (optional) |

Prefab assets load at startup via `Core::LoadSystem`. They are **not** stored in world saves — placed blocks persist in chunk files only.

## PrefabLibrary vs ObjectStorage

- **ObjectStorage** — legacy single-block brush catalog (`TakeObject` deprecated).
- **PrefabLibrary** — JSON templates with sparse `blocks[]` and `anchor`; placement via `World::PlacePrefab`.
- **Hotbar** — `0–9` primary bar; second bar (prefabs) via HUD when `hotbar_count` is 2 (`SetPrefabHotbar` from `PrefabLibrary::ListNames()`).

## Streaming

Default: `streaming_enabled: true` in `config.json`.

`ChunkStreamer` around the camera each frame:

1. For each chunk in render radius — try `chunks/cx_cy_cz.json` on disk.
2. If missing — generate columns for that chunk using the world's **terrain** mode:
   - `heightmap` — noise height column (`GenerateColumn`)
   - `flat` — bedrock / stone / grass at fixed height (`GenerateFlatColumn`)
3. Unload distant chunks (save to disk, drop from memory).

Initial area on new world: chunk-aligned patch centered at spawn, radius `render_distance_chunks` in blocks (`GenerateSpawnPatch` / `GenerateFullPatch` fill every column in each touched chunk). `ChunkStreamer` backfills empty columns in partially filled ground chunks (`y == 0`). Empty chunk JSON (`voxels: []`) is not treated as a successful load.

`terrain` and `world_seed` are stored in `world_data.json` per world so reload uses the same generator as creation.

`render_distance_chunks` — radius in chunks (default 4).

## Config (`<exe_dir>/config.json`)

- `default_world` — folder name under `worlds/` (e.g. `World_001`)
- `default_user`, `world_seed`, `terrain` (`heightmap` | `flat`)
- `render_distance_chunks`, `streaming_enabled`
- Autosave every 60s; exit saves world + config

## Startup (`Core::LoadConfig` / `Core::EnterGame`)

1. **`LoadConfig`** — read `config.json`, load assets (textures, block models, objects, prefabs). Does **not** load the world.
2. **Main menu** — `Application` shows GUI until the user loads or creates a world.
3. **`Load Last World`** — `EnterGame()`: if `default_world` is missing → `CreateWorld()`; otherwise → `LoadLastWorld()`.
4. **`Load World` / `New World`** — pick a save or create `World_NNN` from the procedural template; optional save prompt if a session is already in memory.

`LoadSystem` = `LoadConfig` + `EnterGame` (used by `--validate-load`).

Shift+F11 / Shift+F12 create the next `World_NNN`, save immediately, and set `default_world` so the next launch opens that world.

## GUI (`src/Gui/`)

Retained-mode 2D UI (OpenGL + FreeType via `GuiRenderer` / `TextRenderer`). Game code talks to screens through interfaces in `src/Gui/Interfaces/`; `GameSession` implements them. `WindowManager` delegates input/render to `Application`.

| Layer | Role |
|-------|------|
| `GuiWidget` / layout / primitives | Panels, buttons, text input, lists, tabs, scroll, slots |
| `GuiLayout` | Anchors (`TopCenter`, `BottomCenter`, …), `LayoutHotbarRows` for dual hotbar |
| `GuiContext` | Active screen, input router, render pass; `OnViewportChanged` on resize |
| `GuiRenderer` | Solid quads (`UiQuadBatch`) + textured quads (`UiTexturedQuadBatch`) + FreeType text |
| `GuiIconSource` / `PrefabIconCache` | Block icons from `TextureCubeStorage`; prefab icons via FBO voxel preview (cached) |
| `Application` | `AppState`, main menu vs in-game, console/palette overlays |
| `ContentTypeRegistry` | Block/object categories (`content/types.json`, optional `"types"` on blocks) |
| `CommandRegistry` | In-game console (`help`, `give`, `tp`, `fly`, `time`) |

**Layout on resize:** every screen implements `GuiScreenBase::OnViewportChanged`. Main menu title uses `TopCenter` + centered label text; in-game HUD places the **block** row above the **prefab** row, both bottom-centered. Console and creative palette anchor to the window edges using framebuffer size from `Application::RenderFrame`.

**Hotbar UI:** slots show block/prefab textures when available; tooltips show the active or hovered item name (block label above the block row, prefab label below the prefab row). Selected slots use a stronger border/fill from `GuiTheme`.

**Main menu:** `Load Last World` (or `Resume` after Esc), `Load World`, `New World`, `Settings`, `Quit`.

**Settings (`SettingsScreen`):** tab **Application** — `default_user`, `default_world`, streaming, render distance, `render.*`, `gameplay.step_up`, `ui.*` (written to `config.json` via `Core::SaveConfigFile`). Tab **World defaults** — `procedural.*` template for the next new worlds only (does not change an already loaded world's `world_data.json`).

**Config (`ui` section):** `legacy_hud` (GeometryEngine text HUD), `console_key` (default `` ` ``), `palette_key` (default `b`). Saved from Settings together with other app keys.

**Input:** UI capture blocks world mouse/keyboard when the main menu, console, or palette is active. Hotbar keys `0–9` in `Application::RouteKey`. Left Alt toggles free cursor for HUD.

**Manual check (GUI):** resize main menu and in-game window; verify hotbar centering and row order; block/prefab icons and tooltips on hover and slot selection; F9 palette and console panel edges on resize.
