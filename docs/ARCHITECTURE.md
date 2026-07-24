# Cubatarium Architecture

## Render data flow

```
BlockWorld → ChunkMeshCache (GreedyMesher) → GreedyMeshBatch[] → GeometryEngine (opaque + `GreedyTransparentPipeline`) → `vshader_greedy.glsl`
```

Legacy path (`greedy_meshing: false`): instanced cubes via `vshader_instanced.glsl`.

World geometry lives only in `BlockWorld`. `ChunkMeshCache` rebuilds mesh per chunk. **`config.json` → `render`** toggles optimizations (see below).

| Flag | Effect |
|------|--------|
| `greedy_meshing` | `true` (default): GreedyMesher merged quads; **required** for water, lava, fire. `false`: legacy instanced cubes (solids only). |
| `face_quads` | `true` (default). **Requires `greedy_meshing: true`** (auto-enabled if missing). Greedy mesh as world-space triangles with baked UV. |
| `frustum_culling` | `true` (default): skip chunks outside view frustum. Chunk AABB expanded by 2 blocks. |
| `batch_cache` | `true` (default): reuse prepared draw batches when mesh revision unchanged (legacy instanced path). |
| `performance_preset` | `performance` / `fast` / `balanced` (default) / `quality` — seeds fog/sky/async and selects CPU lighting backend (`Flat` for `performance`, `Full` otherwise). `fast` enables boundary `distance_fog` but disables fog pull-in / water-unfinished near fog. Individual `render.*` keys override the preset. Selectable in Settings as **Graphics quality**. |
| `async_meshing` | Background mesh rebuild; see `RenderSettings.AsyncMeshing`. |
| `distance_fog` | Distance fog using `FogHorizonBlocks` / `RenderHorizonBlocks`. |

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
| `chunks/` | Per-chunk binary `cx_cy_cz.cchunk` (primary); legacy `*.json` read for migration |
| `chunks.json` | Marker `{ "format_version": 3, "storage": "per_file" }` |
| `users.json` | Per user: `position[3]`, `yaw`, `pitch` |
| `world_data.json` | `world_name`, `spawn_point`, `procedural`, `worldgen_sets` |

## Load order (`WorldCooperativeOps`)

1. `world_data.json` (requires `worldgen_sets`), `users.json`, `creatures.json`
2. `chunks.json` storage marker → scan `chunks/` per-file (`.cchunk` / `.json`)
3. If no persisted terrain and world is new: procedural fill
4. `RebuildBlockMesh`; camera restored from user data (not spawn reset)

Legacy `blocks.json`, monolithic `chunks.json` arrays, and save `objects.json` are **not** read.

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

Default release packs: `kenney_voxel_16` + `cubatarium_cc0_base`. For full legacy block visuals (`minecraft_legacy_16`), generate a local pack (gitignored):

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

Trees and structures: world objects in [`objects/`](../objects/) use canonical block names (`tree_log`, `tree_leaves`, `stone`, `wood`, …) so they resolve in any primary resource pack. Runtime data under `content/` is **JSON only** (C++ uses `nlohmann::json`; YAML in `tools/` is for authoring scripts). Global defaults live in [`content/object_features.json`](../content/object_features.json); per-world overrides in `world_data.json` → `worldgen_sets` (edited in-game with **G**). See [OBJECT_WORLDGEN.md](OBJECT_WORLDGEN.md). Feature toggles: `procedural.trees` (vegetation), `procedural.decoration`, `procedural.structures`. Per-world density multipliers live in `procedural.tuning` (`vegetation_density`, `decoration_density`, `structure_density`, `biome_*_weight`, `biome_blend_radius`, `ore_density`, `terrain_erosion`). Biome height, surface `palette`, `sub_biomes`, feature weights, and pipeline stages load from [`content/worldgen_packs/<pack_id>/`](content/worldgen_packs/default/) (`pack.json`, `pipeline.json`, `biomes/*.json`; `WorldGenPack`, `procedural.worldgen_pack_id`). Hot-reload: console `worldgen reload` (new chunks only). Cave tuning: `procedural.cave_params` (`threshold`, `min_y`, `scale`, `max_depth_below_surface`, `style`: `noise` or `worm`). Bedrock thickness: `procedural.bedrock_top_y`.

## World generation

Generators implement `IUWorldGenPipeline` and are registered in `UWorldGeneratorRegistry` (`WorldGeneratorDescriptor.h`). Factory entry: `UProceduralWorldGenFactory::Create`.

| Generator id | Description |
|--------------|-------------|
| `flat` | Flat grass platform |
| `heightmap` | Legacy hash hills |
| `overworld` | Biomes + caves + ores + vegetation/decor/structures (default, stage checkboxes) |
| `hills` / `mountains` | Noise terrain presets |
| `beta_retro` | Overworld (BetaRetro): beta-style cliffs with biomes |

`UComposableWorldGenerator` composes column stages via `UColumnGenerationService` and `WorldGenStageMask` (pack pipeline × generator × procedural settings). Stage order comes from `pipeline.json` `stages[]`. Builtin lava/fire features run through `IUBuiltinWorldGenFeature`. Block slots resolve through `WorldGenBlockResolver` on `WorldGenContext`. **Worldgen places blocks and prefabs only** — creatures spawn separately via `World::SpawnCreature` / `AddUser`.

Defaults: `sea_level` 48, `max_height` 128, `generator` `overworld`. Compact presets for `flat`/`heightmap` use low-height defaults (`sea_level` ~5, `max_height` ~15). Legacy `indev_retro` loads as `heightmap`.

Settings persist in `config.json` (template: **generator + seed only**) and per-world `world_data.json` under full `procedural` + `procedural.tuning`. Sea level, height, and tuning are reset from generator defaults on each app start; per-world overrides live only in `world_data.json`.

New World UI: generator list picker, context-sensitive options, density/biome tuning fields.

### Animated blocks and fluids

Block metadata lives in `resource_packs/*/blocks/*.json` (`animation`, `render`, `physics`) parsed by `BlockDefinitionStorage`. Flipbook atlases are built in `TextureCubeStorage::CreateCubeTexture` (vertical strip for uniform six-face blocks like `water`/`lava`; multi-face rows for `fire`).

| Module | Role |
|--------|------|
| `AnimationClock` | Global elapsed time; per-block `frametime` from block JSON (20 ticks/s) drives `uAnimFrame` |
| `BlockPhysicsProfile` | `occupancy`, drag, sink/rise; presets `water`, `lava`, `fire` |
| `BlockRegistry::BlocksMovement` | Collision and raycast (only `occupancy >= 1`) |
| Greedy mesh | Opaque pass (solid, then cutout), then multi-pass transparent (see below); fluid–fluid faces kept; opaque↔transparent face rules (below) |
| Worldgen | `fill_water`, `fill_lava`, `fill_fire` in `ProceduralSettings`; `FillFluidColumn` to `sea_level`; with `fill_water`, `AdjustSurfaceYForSpawnIsland` raises terrain in a ~48-block-radius disk (+16-block blend) around spawn (0,0) so the player starts on dry land |

### Block alpha taxonomy (resource packs)

| Texture alpha | `render` JSON | Render pass | Examples |
|---------------|---------------|-------------|----------|
| Fully opaque | (default) | Opaque solid | stone, dirt |
| Alpha holes (cutout) | `style: cutout`, `occupancy: 0` | Opaque + `uAlphaCutout` | `tree_leaves`, `web` |
| True blend | `transparent: true` | `GreedyTransparentPipeline` | glass, ice, water, lava |
| Billboard | `transparent: true`, `style: cross` | Transparent + cross sprite | `tall_grass`, `reeds`, `fire` |

Name heuristics in [`tools/canonical_blocks.yaml`](../tools/canonical_blocks.yaml): `cutout_name_patterns` (`*leaves*`, `web`) and `blend_name_patterns` (`*glass*`, `*_ice`, `ice`). Applied to pack JSON by [`tools/apply_canonical_types.py`](../tools/apply_canonical_types.py). **Do not** mark leaf cubes as `render.transparent: true` — that sends them to the blend pass and causes x-ray through the world.

### Greedy mesh: opaque ↔ transparent culling

Implementation: [`GreedyMesher.cpp`](../src/Render/Mesh/GreedyMesher.cpp) (`NeighborHidesFace`).

Greedy mesh hides shared faces between solid blocks as before. At **opaque ↔ solid transparent** boundaries (glass, ice) an extra **two-hop** check avoids x-ray into open air volumes.

**Flow-level fluids** (`FluidCellState`: source level 0, flowing 1–7): mesh height follows level on the top face; side faces use basin heuristic (enclosed pit) and level compare fluid↔fluid. See [FLUID_ARCHITECTURE.md](FLUID_ARCHITECTURE.md).

**Opaque ↔ fluid (cliff):** solid face kept at terrain; fluid face toward opaque culled on open cliffs. **Basin / pit:** fluid side faces toward stone are drawn (truncated by level). **Fluid ↔ fluid:** hide face when neighbor level ≥ self.

- `stone | glass | air` (window) — opaque face toward glass stays culled; room stays visible through the pane.
- `air | glass | stone` (glass on a solid facade) — opaque face toward glass is **kept** so depth/color behind glass is the adjacent stone, not sky or distant caves.
- `stone | glass | stone` (embedded glass) — opaque faces kept on both sides.

**Cutout** blocks (`render.style: cutout`, e.g. `tree_leaves`) use the opaque pass with alpha discard (`uAlphaCutout`); **shared faces between cutout neighbors are kept** so leaf clusters are not hollow shells. Opaque faces toward cutout neighbors (logs, stone, sand) are **kept** — cutout blocks do not block movement, so the mesher does not treat them as solid occluders. The cutout pass draws with face culling disabled. **Cross** plants (`render.style: cross`, e.g. `reeds`, `fire`) are billboards in the transparent pass.

Transparent↔transparent shared faces are culled. Audit: [`tools/audit_resource_packs.py`](../tools/audit_resource_packs.py) flags cube blend on alpha-hole blocks and cutout blocks missing `occupancy: 0`.

Greedy mesh culling at **column** boundaries depends on both columns (e.g. cave carved in column B must remesh column A). [`WorldGenContext::MarkDirtyColumn`](../src/WorldGen/Core/WorldGenContext.cpp) marks dirty a **3×3 column neighborhood** (±1 X/Z). [`UWorld::GenerateWorldBlocks`](../src/World/Core/World.cpp) finishes with `RebuildBlockMesh()` so the spawn patch is fully consistent before the first frame.

### Greedy transparent passes

Documented in [`src/Render/Pipeline/README.md`](../src/Render/Pipeline/README.md). Implementation: [`GreedyTransparentPipeline.cpp`](../src/Render/Pipeline/GreedyTransparentPipeline.cpp).

1. **ShellDepth** — depth prepass (α ≥ `shellAlpha`, default 0.35) + stencil mark.
2. **BehindShell** — color, `GL_GREATER`, stencil == 1 (layers behind the shell; not through solid blocks).
3. **ShellSurface** — color, `GL_LEQUAL`, stencil == 1.
4. **FuzzyEdges** — color, `GL_LESS`, stencil != 1 (soft edges only).

After opaque + cutout passes, **`UOpaqueDepthCapture`** snapshots the depth buffer; color/fuzzy transparent passes discard fragments behind that opaque depth on the same pixel (`uOpaqueDepthGuard` in `fshader_greedy.glsl`).

Frame setup: `Application::RenderFrame` clears **color, depth, and stencil** before `GeometryEngine::Paint`. FBO prefab icons use `GlStateScope` so GUI does not leak GL state into the world pass.

Import animated types: water/lava (4-frame vertical strips) and fire (2-frame, 12 stems) ship in CC0 packs. QA: new world with `overworld`, `fill_water` / `fill_fire` true; spawn fire prefab `fire_patch`.

## Asset paths

| Path | Role |
|------|------|
| `resource_packs/` | Block packs (definitions + textures); copied to `bin/resource_packs/` on build |
| `textures/` | Non-block textures (creatures, UI, etc.) |
| `objects/` | Multi-block templates → `UObjectLibrary` |
| `objects/user/` | Drop-in user objects (optional) |

Object assets load at startup via `Core::LoadSystem`. They are **not** stored in world saves — placed blocks persist in chunk files only.

## UObjectLibrary

- **UObjectLibrary** — JSON templates with sparse voxels, `tags[]`, optional `displayName`, `placement.y_offset`; loaded from `objects/`, `objects/imported/`, `objects/user/`, and optional `resource_packs/*/objects/`; placement via `World::PlaceObject`.
- **Hotbar** — `0–9` primary bar; second bar (objects) via HUD when `hotbar_count` is 2 (`SetObjectHotbar` from `UObjectLibrary::ListNames()`).
- **Worldgen UI** — **G** opens `WorldGenPaletteScreen` to edit per-world `worldgen_sets` (objects, terrain slots, ores).

## Streaming

Default: `streaming_enabled: true` in `config.json`.

Companion streaming docs: `docs/streaming/README.md`. Memory budgets / overflow:
`docs/streaming/MEMORY_BUDGET.md` (Era 12).

Pipeline per frame:

```
UChunkStreamer (view-biased priority via ChunkLoadPriority)
  → UChunkLoadScheduler (async Populate, column origin sort)
  → commit → MarkDirty → UChunkMeshCache (async greedy + cross cutout)
```

`ChunkLoadPriority` — Chebyshev ring + view dot product + feet-neighborhood bonus; optional ring gate (inner ring before outer).

`UChunkLoadScheduler` commits with `max_chunk_commits_per_frame` (boosted when movement speed exceeds `movement_speed_boost_threshold`).

Altitude: `ComputeStreamingAltitude` shrinks effective `render_distance_chunks` and fog start when the camera rises (`render.altitude_adaptive_fog`).

Spatial load: cooperative `MeshWarmup` phase builds meshes on the loading screen before gameplay.

`ChunkStreamer` around the camera each frame:

1. For each chunk in render radius — try `chunks/cx_cy_cz.json` on disk.
2. If missing — generate columns for that chunk using the world's procedural pipeline (`PipelineChunkPopulator` / `IUWorldGenPipeline::GenerateColumn`).
3. Unload distant chunks (save to disk, drop from memory).

Initial area on new world: chunk-aligned patch centered at spawn, radius `render_distance_chunks` in blocks (`GenerateSpawnPatch` / `GenerateFullPatch` fill every column in each touched chunk). `ChunkStreamer` backfills empty columns in partially filled ground chunks (`y == 0`). Empty chunk JSON (`voxels: []`) is not treated as a successful load.

`terrain` and `world_seed` are stored in `world_data.json` per world so reload uses the same generator as creation.

`render_distance_chunks` — radius in chunks (default 4).

### Greedy draw categories

| Category | Blocks | Pass |
|----------|--------|------|
| Opaque | solid terrain | opaque |
| Cutout | leaves, glass discard | opaque + alpha cutout |
| CrossCutout | tall_grass, reeds | opaque cutout (not 4-pass stencil) |
| TransparentFluid | water, lava | `GreedyTransparentPipeline` |

Cross batches merge by `blockId` in `RebuildFlatGreedyBatches`. Scatter vegetation capped via `max_per_chunk` in `prefab_features.json`.

### Fog vs cull vs streaming

Distance fog uses horizontal (XZ) distance from the camera. Fog color comes from `ComputeAtmosphericSkyColors` (day/night, weather, sun/moon twilight). Sky horizon fog is **view-direction** radial when `horizon_fog_radial` is true (`fshader_sky.glsl`); celestial tint optional via `horizon_fog_celestial_tint`.

| System | Horizon |
|--------|---------|
| `ComputeDistanceFog` | `FogHorizonBlocks(fog_rd, end_margin)` — `fog_rd` = `EffectiveFogRenderDistance` |
| `ChunkMeshCache::MaxCullDistance` | `RenderHorizonBlocks(effective_render_distance)` |
| `UChunkStreamer` load square | Chebyshev `render_distance_chunks` + view-ahead prefetch |
| Altitude policy | `ground_y` from terrain surface (`altitude_use_terrain_surface`); horizontal cull above threshold |

`end_margin` (`distance_fog_end_margin_blocks`, default **28**) is the strip that hides the unfinished streaming ring inside the visual/cull horizon. Mesh cull stays at full Effective RD; fog may pull in tighter via **fog-only** `EffectiveFogRenderDistance` when `fog_pull_in_enabled` (`VisualHoles>0`, any `UnfinishedVisual`, stream Yellow/Red, or wall hitch), floored by `fog_rd_min` (default 3). Under hole/unfinished debt the runtime also boosts end margin and lowers `EffectiveFogStartRatio` so incomplete mid-range decor (trees) is not left clear until fog “catches up”. **Water unfinished (A+B):** when debt coincides with near-fluid / low-over-sea (`fog_water_unfinished_boost`), pull-in is stronger (`FogWaterStartRatioCap` default 0.28, extra RD−1 / margin) and sky horizon elevation widens (`FogHorizonElevation` 0.35→0.22) so empty ocean columns read as fog rather than clear skydome. Mesh/gen RD is not changed by this lever. Saved `config.json` must not keep stale `distance_fog_start_ratio≈0.85` / margin 12 or defaults never apply.

At high altitude, mesh cull uses XZ distance (not 3D) to avoid a visible terrain disk under the camera. `horizon_boost` increases sky fog blend while flying.

**Underwater / fluid fog (v3):** per-fragment underwater fog in `fshader_greedy` when `vWorldPos.y < surfaceYAt(vWorldPos.xz)` using `UFluidSurfaceMap` (`GL_R16F` surface Y + `GL_R8UI` fluid index). Global underwater fog is a fallback only when the surface map is unavailable. **Air distance fog stays enabled while submerged** for non-fluid fragments (shore / streaming edge); fluid span still uses underwater uniforms. Sky pass suppresses celestial bodies when fully submerged; partial submerge uses a screen waterline split (`FluidUnderwaterFogLogic.h`).

Mesh commit marks dirty once via `ColumnMeshDirty` (Y bounds); `NotifyChunkCommitted` updates streamer state only.

## Config (`<exe_dir>/config.json`)

- `default_world` — folder name under `worlds/` (e.g. `World_001`)
- `default_user`, `world_seed`, `procedural` (generator, sea_level, max_height, tuning, …)
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
| `GuiContext` | Active screen, input router, render pass; `OnViewportChanged` on resize; `ApplyUiScale` updates `GuiMetrics` |
| `GuiMetrics` / `GuiScale` | Design px → device px; auto scale from DPI/resolution × user `ui_scale` multiplier |
| `GuiRenderer` | Solid quads (`UiQuadBatch`) + textured quads (`UiTexturedQuadBatch`) + FreeType text |
| `GuiIconSource` / `PrefabIconCache` | Block icons from `TextureCubeStorage`; prefab icons via FBO voxel preview (cached) |
| `Application` | `AppState`, main menu vs in-game, console/palette overlays |
| `ContentTypeRegistry` | Block/object categories (`content/types.json`, optional `"types"` on blocks) |
| `CommandRegistry` | In-game console; world commands in `RegisterWorldCommands` (`src/Commands/WorldCommands.cpp`) |

## Console / Commands

| Piece | Role |
|-------|------|
| `UCommandRegistry` | Name → handler map; `ExecuteLine` tokenizes input; `FormatHelpText()` lists registered names |
| `RegisterWorldCommands` | Registers gameplay commands (`give`, `tp`, `fly`, `spawn`, skin/possess, `worldgen`) on session init |
| `help` | Built after world commands; text comes from the registry, not a hardcoded list |
| `UGameSession` | Owns registry + `Execute()` for the in-game console overlay |
| History | `UConsoleCommandHistory` persists to `console_history.txt` under the game data root |

Console is toggled via `ui.console_key` (default grave). Chat log lines are appended by `GameSession::AddChatLine`.

**Layout on resize:** every screen implements `GuiScreenBase::OnViewportChanged`; `OnMetricsChanged` triggers relayout when UI scale changes. Sizes come from scaled `GuiTheme` fields (design px at 720p/160dpi baseline) or `GuiScreenBase::Scaled()` for screen-local layout constants. Dialog/card heights should use `GuiLayout::StackVerticalMeasure` when content height grows with scaled fonts (see `WorldProgressScreen`). Main menu title uses `TopCenter` + centered label text; in-game HUD places hotbar bottom-centered. Console and creative palette anchor to the window edges using framebuffer size from `Application::RenderFrame`.

**Scrollbar interaction:** list and scroll widgets share `GuiScrollbarController` for thumb drag and track page jumps; wheel and keyboard navigation remain on the widget.

**Text input:** `GuiTextInput` vertically centers text within its bounds, clips long lines with horizontal scroll-to-caret, and routes mouse caret/selection through `GuiInputRouter` (console overlay keeps a thin routing shim because it bypasses the router while open).

**UI scale:** `Application::UpdateUiScale` computes `EffectiveScale = AutoScale × ui_scale` (Android: DPI + short edge; desktop: GLFW content scale + resolution). User multiplier `ui_scale` (0.5–2.0, default 1.0) is editable in Settings with live preview.

**Hotbar UI:** slots show block/prefab textures when available; tooltips show the active or hovered item name (block label above the block row, prefab label below the prefab row). Selected slots use a stronger border/fill from `GuiTheme`.

**Main menu:** `Load Last World` (or `Resume` after Esc), `Load World`, `New World`, `Settings`, `Quit`.

**Settings (`SettingsScreen`):** tab **Application** — `default_user`, `default_world`, streaming, render distance, **Graphics quality** (`render.performance_preset`), meshing `render.*` toggles, `gameplay.step_up`, `ui.*` (written to `config.json` via `Core::SaveConfigFile`). Tab **World defaults** — `procedural.*` template for the next new worlds only (does not change an already loaded world's `world_data.json`).

**Config (`ui` section):** `legacy_hud` (GeometryEngine text HUD), `console_key` (default `` ` ``), `palette_key` (default `b`, opens Blocks), `inventory_key` (default `e`, toggles palette on last main tab; default Blocks), `ui_scale` (interface scale multiplier, default `1.0`). Saved from Settings together with other app keys.

**Input:** UI capture blocks world mouse/keyboard when the main menu, console, or palette is active. Hotbar keys `0–9` in `Application::RouteKey`. Left Alt toggles free cursor for HUD.

**Manual check (GUI):** resize main menu and in-game window; verify hotbar centering and row order; block/prefab icons and tooltips on hover and slot selection; F9 palette and console panel edges on resize.

## Module boundaries

Layer dependency rules (enforced by `tools/audit/check_include_rules.py` in CI):

```
App → Game, Gui, World (facade), Render (facade), Core
World → Blocks, WorldGen, Creatures (IU-interfaces only)
Render → World (IUBlockWorld read-only), Blocks (defs)
World ↛ Render   (no #include Render/* from World headers)
```

| Module | May include | Must not include |
|--------|-------------|------------------|
| `src/World/` | Blocks, WorldGen, Creatures, Core | `Render/*` except `src/World/Mesh/` adapter |
| `src/Render/` | Blocks, World interfaces | `Gui/*`, `App/Application.h` (Pipeline: see [`src/Render/Pipeline/README.md`](../src/Render/Pipeline/README.md)) |
| `src/App/` | all facades | — |
| `src/Gui/` | App interfaces, Blocks icons | direct `World.h` (use view-models) |

New interfaces use **`IU*`** naming (`IUWorldMeshSink`, `IUGameContent`); see [`CODING_STYLE.md`](CODING_STYLE.md).

### World facades (2026-07)

| Module | Class | Role |
|--------|-------|------|
| `World/Mesh/` | `UWorldMeshService` | Chunk mesh cache, dirty API (`IUWorldMeshSink`) |
| `World/Persistence/` | `UWorldPersistence` | users/world_data/chunk I/O |
| `World/Streaming/` | `UWorldStreaming` | `UChunkStreamer`, scheduler, emerge |
| `World/Streaming/` | `UChunkEmergeCoordinator` | Per-frame chunk/mesh budgets |
| `World/Environment/` | `UWorldEnvironment` | Creatures, activity, poses |
| `World/Collision/` | `UWorldCollision` | Movement, collision, step-up |
| `Game/Interfaces/` | `IUGameContent` | Read-only defs (blocks, objects, creatures) |

## Console / Commands module layout

| Path | Role |
|------|------|
| `src/Commands/` | `UCommandRegistry`, `RegisterWorldCommands`, command handlers |
| `src/Console/` | Input history (`UConsoleCommandHistory`), sanitization |
| `src/Gui/Screens/ConsoleScreen.cpp` | Console overlay UI only |

Gameplay commands register in `src/Commands/WorldCommands.cpp` on session init; `UGameSession` owns the registry and delegates `Execute()` to the overlay.
