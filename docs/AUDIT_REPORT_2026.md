# Cubatarium Audit Report 2026

- **Commit:** `9bf664f57d289c3d68a94bf598d4613201e97b9c`
- **Generated:** 2026-06-27T17:19:26+00:00
- **Status:** approved

## Executive Summary

Open findings (done excluded):

| Priority | Open |
|----------|------|
| P0 | 0 |
| P1 | 0 |
| P2 | 15 |
| P3 | 37 |

Closed: **19** | Rejected (false positive): **1**.

## Open Findings by Priority

### P2

- **AUDIT-APP-001** [architecture] UApplication ~1788 LOC combines game loop, screens, and input routing
  - Module: App; Files: src/App/Application.cpp, src/App/Application.h
  - Action: incremental extract UScreenNavigator (menu subview transitions) and UInputRouter (RouteKey/RouteMouse/scroll); keep UApplication as thin coordinator
  - Evidence: baseline god_class_lines=1788; owns MainMenu/HUD/Console/Palette/Progress screens plus MenuSubview state machine; RouteKey/RouteMouse* methods ~400 LOC; couples App+Gui+World+Render includes in single
- **AUDIT-APP-002** [architecture] Legacy config shims scattered across Core.cpp and config I/O
  - Module: App; Files: src/App/Core.cpp, src/App/Core.h, src/WorldGen/Core/ProceduralConfigIO.cpp
  - Action: group migration paths in LegacyConfigAdapter (read ui aliases, terrain root key, legacy pack list); document supported aliases; stop writing deprecated keys on save where safe
  - Evidence: Core.cpp reads ui.legacy_hud, ui.block_input_profile (alias for control_scheme), root terrain string synced with ProceduralTemplate.Generator via TerrainType field (10+ assignments); SetupNewWorldForC
- **AUDIT-APP-003** [architecture] UCore ~1567 LOC god-class spans config, worlds, and resource packs
  - Module: App; Files: src/App/Core.cpp, src/App/Core.h
  - Action: extract UWorldLifecycleFacade (create/load/save/list) and UResourcePackBootstrap; leave UCore as wiring/DI root
  - Evidence: Core.cpp ~1567 LOC; 35+ public methods mixing LoadConfig/SaveSystem, world CRUD, resource-pack apply/reload, runtime block overlay, headless CreateWorldCli; duplicates platform path discovery (IsGameD
- **AUDIT-APP-004** [architecture] UWindowManager ~907 LOC owns GLFW loop, input fan-out, and legacy help overlay
  - Module: App; Files: src/App/Platform/WindowManager.cpp, src/App/Platform/WindowManager.h
  - Action: move help overlay to Gui screen or dev-only overlay; share GL state scope guard with Render; slim WindowManager to platform callbacks only
  - Evidence: ~907 LOC; Run/Update/Render/ProcessInput/HandleKeyEvent coordinate Core+Application+GeometryEngine; RenderHelpText embeds hard-coded F-key shortcuts; duplicates OpenGL depth/blend save-restore blocks 
- **AUDIT-ARCH-001** [architecture] UWorld combines persistence, streaming, mesh, creatures
  - Module: World; Files: src/World/Core/World.cpp, src/World/Core/World.h
  - Action: incremental extract UWorldPersistence / UWorldStreaming facades
  - Evidence: ~3500 LOC god class
- **AUDIT-CON-001** [architecture] RegisterCommands ~340 LOC lives in GameSession, not Commands module
  - Module: Commands; Files: src/Game/GameSession.cpp, src/Commands/CommandRegistry.cpp, src/Commands/CommandRegistry.h
  - Action: extract command modules (worldgen, creature, player) or CommandRegistrar helper
  - Evidence: UCommandRegistry is 45 LOC dispatcher; all handlers registered in UGameSession::RegisterCommands with heavy World/Creature coupling.
- **AUDIT-CRE-002** [duplication] Duplicate catalog sort comparator in CreatureDefinitionStorage and SkinDefinitionStorage
  - Module: Creatures; Files: src/Creatures/Definition/CreatureDefinitionStorage.cpp, src/Creatures/Definition/SkinDefinitionStorage.cpp
  - Action: extract shared SortDefinitionIdsByCatalogOrder helper
  - Evidence: scan_duplicates.json: identical 15-line sort-by-sortOrder lambdas in ListIds/ListSpawnable (creature) and ListEquippable (skin).
- **AUDIT-CRE-009** [architecture] WorldCreatures.cpp combines spawn, habitat, pack overlay (~776 LOC)
  - Module: Creatures; Files: src/Creatures/Core/WorldCreatures.cpp
  - Action: incremental extract spawn vs environment facades when touching this file
  - Evidence: 5975 LOC module total; WorldCreatures is largest file and couples spawn probe, habitat snap, pack overlay refresh.
- **AUDIT-RENDER-001** [architecture] GeometryEngine ~2618 LOC with World+Creatures+WorldGen coupling
  - Module: Render; Files: src/Render/Engine/GeometryEngine.cpp, src/Render/Engine/GeometryEngine.h
  - Action: Enforce Pipeline include rules; extract GreedyGpuBackend and CreatureDrawPass incrementally.
  - Evidence: baseline.json god_class_lines GeometryEngine.cpp=2618. Includes Creatures/*, Pose/*, WorldGen/*, App/Core.h, UWorld shared_ptr. DrawScene orchestrates blocks, creatures, sky, UI overlays, fog.
- **AUDIT-RENDER-009** [duplication] GreedyMesher BuildChunkMesh overloads duplicate ~140 lines
  - Module: Render; Files: src/Render/Mesh/GreedyMesher.cpp
  - Action: Extract template or IChunkMeshReader interface for block/neighbor queries; single meshing loop.
  - Evidence: Two overloads (UBlockWorld chunk vs ChunkMeshSnapshot) share identical greedy mask/quad emission loops; differ only in block access (GetBlockLocal vs snapshot) and maxSolidY source.
- **AUDIT-RENDER-010** [performance] Greedy GPU batches recreated with glBufferData on mesh revision
  - Module: Render; Files: src/Render/Engine/GeometryEngine.cpp
  - Action: Deferred: persistent GPU VBO / vertex pooling per TD-CS-016.
  - Evidence: UploadGreedyGpuBatches glGenBuffers + glBufferData GL_STATIC_DRAW per batch on rebuild. Per-draw instance path also glBufferData each batch (620-628). No persistent VBO pool.
- **AUDIT-WG-003** [architecture] UBiomeSampler translation unit ~1137 LOC — per-column hot path god-module
  - Module: WorldGen; Files: src/WorldGen/Sampling/BiomeSampler.cpp, src/WorldGen/Sampling/BiomeSampler.h
  - Action: split into BiomeClassifier, BiomeHeightRefiner, BiomeSurfaceRules; keep UBiomeSampler as facade; profile before micro-opts
  - Evidence: 8177 LOC WorldGen module; BiomeSampler.cpp alone ~1137 lines with 30+ free functions (ClassifyBiome, ComputeBiomeWeights, BlendedBiomeWeights, RefineSurfaceYWithBiomes, river/coast/erosion) all called
- **AUDIT-WG-004** [architecture] ProceduralConfigIO ~571 LOC mixes parse, legacy migration, and serialize
  - Module: WorldGen; Files: src/WorldGen/Core/ProceduralConfigIO.cpp, src/WorldGen/Core/ProceduralConfigIO.h
  - Action: split ProceduralConfigReader / ProceduralConfigWriter; centralize legacy field mapping table
  - Evidence: single file handles ParseProceduralSettings, ParseTuning, legacy vertical/generator shims, duplicate keys (caves + enable_caves, trees booleans), WriteProceduralSettings and WriteUiSettings; writes bo
- **AUDIT-WORLD-005** [architecture] UWorld god-class (~3390 LOC) mixes persistence, streaming, mesh, creatures, collision
  - Module: World; Files: src/World/Core/World.cpp, src/World/Core/World.h
  - Action: Incremental facade extract: UWorldPersistence, UWorldStreaming, UWorldCollision (per TD-AUD-010).
  - Evidence: baseline.json god_class_lines World.cpp=3390. World.h pulls ChunkMeshCache, Creature, Activity, WorldGen, 100+ public methods. Partial extract exists (WorldCooperativeOps, LegacyChunkJsonLoader, Chunk
- **AUDIT-WORLD-006** [architecture] World module depends on Render headers (mesh cache, camera, fog, cube)
  - Module: World; Files: src/World/Core/World.h, src/World/Core/World.cpp
  - Action: Introduce IWorldMeshSink or move mesh dirty API behind interface to invert dependency (World → abstraction ← Render).
  - Evidence: World.h includes Render/Mesh/ChunkMeshCache.h. World.cpp includes Render/Camera/*, Render/Engine/DistanceFog.h, Render/Primitives/Cube.h. UWorld owns UChunkMeshCache MeshCache member and drives frustu

### P3

- **AUDIT-APP-007** [duplication] List widget scroll/selection logic duplicated across CheckList and ListView
  - Module: Gui; Files: src/Gui/Widgets/GuiCheckList.cpp, src/Gui/Widgets/GuiListView.cpp
  - Action: extract GuiScrollableListMixin or shared GuiListScrollController base used by both widgets
  - Evidence: scan_duplicates: 14 clusters between GuiCheckList.cpp and GuiListView.cpp (e.g. hashes 659f072b, 7ea56a8f, ea4abfe6, e6550ace) covering ApplyMinimumBounds, scrollbar drag, row hit-test patterns; Gui m
- **AUDIT-APP-008** [duplication] Icon cache GL teardown blocks duplicated in CreatureIconCache and PrefabIconCache
  - Module: Gui; Files: src/Gui/Cache/CreatureIconCache.cpp, src/Gui/Cache/PrefabIconCache.cpp
  - Action: shared GuiOffscreenIconCacheBase or small GlResourceBundle helper for icon render targets
  - Evidence: duplicate clusters 0a194e54, 0dc88ee5, dc40cc38: matching FBO/VBO/VAO/texture delete sequences at CreatureIconCache.cpp:241-285 and PrefabIconCache.cpp:95-110
- **AUDIT-APP-009** [duplication] Focus/scroll hit-test boilerplate duplicated in GuiFocus and GuiScrollView
  - Module: Gui; Files: src/Gui/Core/GuiFocus.cpp, src/Gui/Widgets/GuiScrollView.cpp
  - Action: consolidate pointer-to-widget routing helpers in GuiFocus or GuiHitTest util
  - Evidence: scan_duplicates: 10 clusters (06219a43, 158d0db9, 3880a223, etc.) between GuiFocus.cpp:8-19 and GuiScrollView.cpp:10-20
- **AUDIT-CON-002** [architecture] Console code split across src/Commands, src/Console, Gui/ConsoleScreen
  - Module: Commands; Files: src/Commands/CommandRegistry.cpp, src/Console/ConsoleCommandHistory.cpp, src/Console/ConsoleInputSanitize.cpp, src/Gui/Screens/ConsoleScreen.cpp
  - Action: document module boundaries in ARCHITECTURE.md or consolidate under Commands/
  - Evidence: Module mapped to src/Commands (81 LOC) but UI/history/sanitize in separate trees; baseline shows src/Console 193 LOC.
- **AUDIT-CON-003** [docs] help command uses hardcoded command list
  - Module: Commands; Files: src/Game/GameSession.cpp
  - Action: generate help from UCommandRegistry or add compile-time assert
  - Evidence: Static string may drift from Handlers map; no auto-enumeration of registered commands.
- **AUDIT-CON-004** [architecture] Stub commands: time, worldgen debug overlay
  - Module: Commands; Files: src/Game/GameSession.cpp
  - Action: implement or remove from help until ready
  - Evidence: time returns 'not implemented'; worldgen debug stores flag but message says 'visual overlay not wired yet'.
- **AUDIT-CRE-001** [architecture] glTF backend stub (TD-CRE-001)
  - Module: Creatures; Files: src/Creatures/Visual/CreatureVisualGltf.cpp, src/Creatures/Visual/CreatureVisualFactory.cpp
  - Action: defer; full cgltf + skinned shader path is phase 5
  - Evidence: UCreatureVisualGltf::SubmitDraw logs once and draws debug wireframe only; CreateCreatureVisual routes gltf_skeleton to stub. Open in TECH_DEBT_CREATURES.md.
- **AUDIT-CRE-003** [architecture] visual.rig parsed but does not select pose presenter (TD-CRE-003)
  - Module: Creatures; Files: src/Creatures/Definition/CreatureDefinitionStorage.cpp
  - Action: defer or wire rig.template to pose registry
  - Evidence: rig.template/partIds loaded from JSON; pose selection uses locomotion_archetype only per TECH_DEBT_CREATURES.md.
- **AUDIT-CRE-004** [architecture] AerialPosePresenter: full b3d clip playback for flying birds deferred (TD-CRE-006)
  - Module: Creatures; Files: src/Pose/AerialPosePresenter.cpp
  - Action: defer
  - Evidence: Ground chicken walk+peck done; fly IK and clip playback backlog per TECH_DEBT_CREATURES.md.
- **AUDIT-CRE-005** [architecture] FleeActivityAgent / MeleeAttackActivityAgent — **superseded / closed** (TD-CRE-008 closed in TECH_DEBT_CREATURES; see CREATURE_AGENTS.md)
  - Module: Creatures; Files: src/Activity/Agents/*
  - Action: none (implemented)
  - Evidence: Flee/Melee + USimpleFsmBrain shipped; stale defer note kept for historical audit trail.
- **AUDIT-CRE-006** [architecture] FP viewmodel arms (fp_parts[]) not implemented (TD-CRE-010)
  - Module: Creatures; Files: docs/TECH_DEBT_CREATURES.md
  - Action: defer
  - Evidence: First-person arm parts deferred; not a blocker.
- **AUDIT-CRE-007** [architecture] Wave bake coverage incomplete for ~42 Luanti mobs (TD-CRE-017)
  - Module: Creatures; Files: tools/creature_luanti_sources.yaml, tools/bake_rigid_creature_textures.py, docs/TECH_DEBT_CREATURES.md
  - Action: import missing PNGs then re-run bake pipeline
  - Evidence: Partial until all research textures present in CubatariumTextureResearch.
- **AUDIT-CRE-008** [architecture] 8 placeholder species missing research textures (TD-CRE-021)
  - Module: Creatures; Files: docs/TECH_DEBT_CREATURES.md
  - Action: import animalworld/mobs_* PNGs then bake_rigid_creature_textures.py
  - Evidence: dolphin, whale, octopus, kitten, warthog, mese_monster, lava_flan, water_dragon — textures missing in CubatariumTextureResearch.
- **AUDIT-PACK-001** [performance] TD-002: RegisterRuntimeBlock still triggers full Rebuild via FlushRuntimeOverlay
  - Module: ResourcePacks; Files: src/ResourcePacks/BlockMergeRegistry.cpp, src/ResourcePacks/BlockMergeRegistry.h, docs/TECH_DEBT_RESOURCE_PACKS.md
  - Action: defer; implement incremental atlas rebuild when overlay batching is insufficient
  - Evidence: RegisterRuntimeBlock batches overlay dirty flag; FlushRuntimeOverlay calls full Rebuild(). Incremental atlas + dirty-chunk path deferred in TD-002.
- **AUDIT-PACK-002** [performance] TD-005: disk placeholder cache (.placeholder_cache/) unused
  - Module: ResourcePacks; Files: src/ResourcePacks/PlaceholderTextureCache.cpp, src/ResourcePacks/PlaceholderTextureCache.h, docs/TECH_DEBT_RESOURCE_PACKS.md
  - Action: defer; add disk LRU or remove dead save/load paths
  - Evidence: SavePlaceholderFile/LoadPlaceholderFile exist; in-memory LRU (max 256) active; disk cache path still backlog per TD-005.
- **AUDIT-PACK-003** [architecture] TD-006: Android selective asset extraction not implemented
  - Module: ResourcePacks; Files: docs/TECH_DEBT_RESOURCE_PACKS.md
  - Action: defer to Android packaging milestone
  - Evidence: Open tech debt: whitelist extraction after TD-001; manifest+checksums deferred.
- **AUDIT-PACK-004** [docs] Manual verify checklist for hotbar and pack UI still open
  - Module: ResourcePacks; Files: docs/TECH_DEBT_RESOURCE_PACKS.md
  - Action: run manual QA checklist; close or file bugs
  - Evidence: Four unchecked manual-verify items (unknown hotbar block, World settings pack swap, Settings defaults, _example_creature_demo overlay).
- **AUDIT-PACK-005** [architecture] smoke_resource_packs tree_bark asset gap (TD-AUD-019)
  - Module: ResourcePacks; Files: tools/smoke_resource_packs.py, resource_packs/minetest_default_16/blocks/tree_bark.json, docs/TECH_DEBT_AUDIT.md
  - Action: fix primary-pack texture for tree_bark or adjust smoke strictness
  - Evidence: tree_bark referenced in prefabs/worldgen_refs; smoke may fail on missing texture in primary pack — tracked as TD-AUD-019.
- **AUDIT-PACK-006** [duplication] ParseHexColor helper duplicated with App/Core.cpp
  - Module: ResourcePacks; Files: src/ResourcePacks/PlaceholderTextureCache.cpp, src/App/Core.cpp
  - Action: extract shared color util or accept cross-module duplication
  - Evidence: scan_duplicates.json hash 24c24caec85e6538: 15-line hex color parser in both files.
- **AUDIT-RENDER-011** [performance] Cross vegetation merged per-chunk; no global GPU instancing
  - Module: Render; Files: src/Render/Mesh/ChunkMeshCache.cpp, src/Render/Mesh/CrossMeshEmitter.h
  - Action: Implement retained instance buffer + single draw for Cross vegetation.
  - Evidence: RebuildFlatGreedyBatches merges cross batches into merged_cross map then single batch per blockId; still one draw per block type, not retained instance buffer. TD-CS-014 open.
- **AUDIT-RENDER-012** [performance] Async meshing enabled by default; needs in-game validation
  - Module: Render; Files: src/App/Settings/RenderSettings.h, src/Render/Mesh/ChunkMeshCache.cpp, src/App/Core.cpp
  - Action: Run profiling bisect (TECH_DEBT_CHUNK_STREAMING); toggle render.async_meshing; export movement_diagnostics.v2.
  - Evidence: RenderSettings.AsyncMeshing{true}. ChunkMeshCache::RebuildDirtyChunks uses async when AsyncMeshing&&GreedyMeshing. TD-CS-010 backlog: validate via fly-through before treating as production-default.
- **AUDIT-RENDER-013** [performance] Conservative frustum fallback rebuilds all greedy batches
  - Module: Render; Files: src/Render/Mesh/ChunkMeshCache.cpp
  - Action: Defer; improve camera-chunk skip or incremental cull.
  - Evidence: When frustum cull yields empty GreedyBatches but GreedyCache non-empty, RebuildFlatGreedyBatches(nullptr) full merge. TD-CS-018: incremental frustum-only cull without full flat merge deferred.
- **AUDIT-RENDER-014** [performance] Sky uses fixed horizon fog band; radial sky fog optional
  - Module: Render; Files: src/Render/Engine/DistanceFog.cpp, src/Render/Engine/GeometryEngine.cpp
  - Action: Defer optional true radial sky fog; current horizon blend shipped.
  - Evidence: kHorizonMarginBlocks/kMinHorizonBlocks fixed constants. GeometryEngine sets FogHorizonBlend=1.0 when distance fog on. TD-CS-019 open.
- **AUDIT-TOOL-019** [architecture] smoke_resource_packs tree_bark missing (TD-AUD-019)
  - Module: tools; Files: tools/smoke_resource_packs.py, .github/workflows/resource-packs-smoke.yml
  - Action: fix asset or relax smoke; see also AUDIT-PACK-005
  - Evidence: Pre-existing asset gap; smoke may fail on tree_bark texture in primary pack.
- **AUDIT-TOOL-FIX-001** [architecture] One-shot fix_* refactor scripts unreferenced (7)
  - Module: tools; Files: tools/fix_animated_block_pngs.py, tools/fix_broken_header_includes.py, tools/fix_broken_includes.py, tools/fix_gui_rect_accessors.py, tools/fix_refactor_damage.py, tools/fix_remaining_types.py, tools/fix_worldgen_context.py
  - Action: move to tools/archive/ with README entry (pattern: import_blocks.ps1 TD-008)
  - Evidence: audit/tools_orphans.json: no references in CI/docs/scripts/README; one-time migration scripts from refactor waves.
- **AUDIT-TOOL-GEN-001** [architecture] Orphan generator/migration scripts (6)
  - Module: tools; Files: tools/generate_minetest_stem_map.py, tools/generate_worldgen_biome_map.py, tools/inject_display_names.py, tools/restore_tier_a_block.py, tools/restructure_src.py, tools/update_refactor_style.py
  - Action: document in tools/README or archive if superseded
  - Evidence: audit/tools_orphans.json: no CI/docs references; ad-hoc authoring utilities.
- **AUDIT-TOOL-IMPORT-001** [architecture] Orphan import/prefab utilities (3)
  - Module: tools; Files: tools/import_luanti_rigid_creature.py, tools/prefab_bounds.py, tools/prefab_import_common.py
  - Action: reclassify prefab_import_common as internal lib; document or archive the other two
  - Evidence: prefab_import_common imported by mts_to_prefab.py but flagged orphan (filename scan); import_luanti_rigid_creature and prefab_bounds have no external refs.
- **AUDIT-TOOL-SCAN-001** [architecture] scan_tools_usage.py uses filename-only rg heuristic
  - Module: tools; Files: tools/audit/scan_tools_usage.py, audit/tools_orphans.json
  - Action: extend scanner: module stem imports, tools/README inventory, exclude tools/audit/
  - Evidence: Misses 'from module import' references and docs mentioning tools/audit/ only inside audit/ folder; produces false orphans for libs and audit pipeline.
- **AUDIT-TOOL-SMOKE-001** [architecture] Orphan smoke/diagnostic scripts (4)
  - Module: tools; Files: tools/smoke_control_schemes.py, tools/smoke_creature_fidelity.py, tools/test_chunk_storage.py, tools/test_texture_overrides_yaml.py
  - Action: wire to workflow or document as manual-only in docs/CODING_STYLE.md
  - Evidence: audit/tools_orphans.json: not wired to CI; may be useful manual QA.
- **AUDIT-WG-001** [architecture] Legacy procedural config shims in ProceduralConfigIO
  - Module: WorldGen; Files: src/WorldGen/Core/ProceduralConfigIO.cpp, src/WorldGen/Core/ProceduralSettings.cpp
  - Action: document migration matrix in TECH_DEBT_WORLDGEN.md; keep shims until old configs retired; add config version field when breaking
  - Evidence: ApplyLegacyVerticalMode (compact/extended), ApplyLegacyOverworldProfile (generator overworld_biomes toggles caves/ores/fluids), root terrain string fallback when procedural absent, WARN when procedura
- **AUDIT-WG-005** [architecture] Dual config keys and deprecated generator ids retained for compatibility
  - Module: WorldGen; Files: src/WorldGen/Core/ProceduralConfigIO.cpp, src/WorldGen/Core/ProceduralSettings.cpp
  - Action: on save emit canonical keys only; keep read-side aliases documented; log once per session for deprecated ids
  - Evidence: WriteProceduralSettings emits both procedural.caves and procedural.enable_caves; Parse accepts trees/enable_trees variants; indev_retro maps to Heightmap with WARN; unknown generator falls back to Ove
- **AUDIT-WG-006** [architecture] UWorldGenPack loader ~669 LOC — pack/biome/pipeline parsing monolith
  - Module: WorldGen; Files: src/WorldGen/Core/WorldGenPack.cpp, src/WorldGen/Core/WorldGenPack.h
  - Action: optional split PackJsonLoader vs runtime ActivePack cache; low urgency — load-time not hot path
  - Evidence: WorldGenPack.cpp ~669 LOC; parses pack.json, pipeline.json stage order, biomes, height/climate layers; push_back on StageOrder without reserve (small arrays, load-time only)
- **AUDIT-WG-007** [performance] Column generation sorts full patch before pipeline.GenerateColumn
  - Module: WorldGen; Files: src/WorldGen/Core/IUWorldGenPipeline.cpp, src/WorldGen/Pipelines/ComposableWorldGenerator.cpp
  - Action: defer unless profiling shows sort overhead; prefer optimizing BiomeSampler hot path first; see TECH_DEBT_CHUNK_STREAMING for streaming context
  - Evidence: GenerateAllColumnsInChunkRange builds vector of all columns, sorts by dist2, then calls GenerateColumn per entry; for 16-chunk radius patch this is O(n log n) setup per batch — acceptable but BiomeSam
- **AUDIT-WG-008** [architecture] Deferred worldgen UX and debug features per TECH_DEBT_WORLDGEN
  - Module: WorldGen; Files: docs/TECH_DEBT_WORLDGEN.md
  - Action: defer to post-audit roadmap; not blocking PR-D/E
  - Evidence: open backlog: pack dropdown from pack.json scan, biome/climate debug overlay (worldgen debug on stub), generator presets UX; hot-reload only affects new chunks (documented limitation)
- **AUDIT-WG-009** [architecture] integration_test_worldgen fire_blocks threshold smoke failure
  - Module: WorldGen; Files: docs/TECH_DEBT_AUDIT.md
  - Action: fix threshold or tune generator in dedicated PR; not part of dead-code cleanup
  - Evidence: TD-AUD-018: pre-existing integration_test_worldgen fire_blocks threshold failure tracked separately from module refactor
- **AUDIT-WORLD-009** [performance] Chunk streamer ring gate disabled by default; tuning undocumented in-game
  - Module: World; Files: src/World/Chunks/ChunkStreamer.h, src/World/Chunks/ChunkStreamer.cpp
  - Action: Expose config toggle; profile fly-through with ring gate on/off; document in TECH_DEBT_CHUNK_STREAMING profiling bisect.
  - Evidence: RingGateEnabled defaults false (ChunkStreamer.h:120). SetRingGateEnabled exists but World.cpp never enables it. TD-CS-017 open.
- **AUDIT-WORLD-010** [architecture] TECH_DEBT claims async chunk gen/IO defaults off; code defaults on
  - Module: World; Files: src/WorldGen/Core/ProceduralSettings.h, src/App/Core.cpp, bin/config.json
  - Action: Update TECH_DEBT tracker to reflect current defaults OR flip defaults after validation fly-through.
  - Evidence: TD-CS-011/012 say defaults off. ProceduralSettings has AsyncChunkGeneration{true} AsyncChunkIo{true}. bin/config.json procedural.async_chunk_* = true. Only headless worldgen path forces false (Core.cp

## Closed Findings

- **AUDIT-APP-005** — IsGameDataRoot / project-root search duplicated in Core and DesktopPlatformPaths (`local-p0-fix`)
- **AUDIT-APP-006** — OpenGL 2D overlay state save/restore duplicated with GeometryEngine (`9bf664f57d289c3d68a94bf598d4613201e97b9c`)
- **AUDIT-RENDER-002** — StreamingHorizonBlocks deprecated API (`fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`)
- **AUDIT-RENDER-003** — GreedyMesher quads.reserve in BuildChunkMesh hot paths (`fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`)
- **AUDIT-RENDER-004** — RebuildFlatGreedyBatches push_back without vector reserve (`local-p0-fix`)
- **AUDIT-RENDER-005** — RebuildChunkLegacy face loop push_back with partial reserve (`9bf664f57d289c3d68a94bf598d4613201e97b9c`)
- **AUDIT-RENDER-006** — ChunkMeshCache dual RebuildChunkImmediate and MarkDirty paths (`9bf664f57d289c3d68a94bf598d4613201e97b9c`)
- **AUDIT-RENDER-007** — Orphan header Cube_GLM.h not in build (`local-p0-fix`)
- **AUDIT-RENDER-008** — FaceIndexFromGreedy duplicated in GreedyMeshEmitter and GreedyMeshMath (`9bf664f57d289c3d68a94bf598d4613201e97b9c`)
- **AUDIT-RENDER-015** — OpenGL depth/blend state restore duplicated with WindowManager (`9bf664f57d289c3d68a94bf598d4613201e97b9c`)
- **AUDIT-TEST-001** — chunk_load_priority_test not in CI (`fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`)
- **AUDIT-TOOL-DEV-001** — tools/audit/ pipeline is documented dev tooling (not orphan) (`fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`)
- **AUDIT-TOOL-LIB-001** — Internal library modules falsely flagged as orphans (filename-only scan) (`local`)
- **AUDIT-WORLD-001** — Monolithic world JSON migration parsing (LoadBlocks/LoadChunks; not per-chunk JSON) (`fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`)
- **AUDIT-WORLD-002** — MarkBlockChunkDirty uses RebuildChunkImmediate vs MarkDirty branches (`9bf664f57d289c3d68a94bf598d4613201e97b9c`)
- **AUDIT-WORLD-003** — Unused HasChunkJsonFiles helper (`local-p0-fix`)
- **AUDIT-WORLD-004** — Unused ResolveMovementAxisEye collision helper (`local-p0-fix`)
- **AUDIT-WORLD-007** — push_back in LoadWorldData resource-pack parse loop without reserve (`local-p0-fix`)
- **AUDIT-WORLD-008** — push_back in SaveMovementDiagnostics sample loop without reserve (`9bf664f57d289c3d68a94bf598d4613201e97b9c`)

## Rejected Findings (scan false positives)

- **AUDIT-WG-002** — Dead-code scan false positives: WorldGeneratorRegistry factory symbols

## Recommended PR Sequence

1. **PR-A:** P0 dead code + duplicate includes + auto-fixable style
2. **PR-B:** World/IO — legacy JSON loader extract, chunk dirty helper
3. **PR-C:** Render — fog API cleanup, include hygiene
4. **PR-D:** App/Gui — incremental Application extractions
5. **PR-E:** Perf micro-optimizations with smoke metrics
6. **PR-F:** Documentation sync + CI style gate

## Human Gate

Set `audit/findings.json` → `"status": "approved"` and optional `approved_ids` before Fix agents run.
