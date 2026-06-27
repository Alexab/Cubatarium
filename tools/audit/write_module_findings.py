#!/usr/bin/env python3
"""One-shot writer for module audit JSON (manual agent pass output)."""

from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
AUDIT = REPO / "audit" / "modules"
TS = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
COMMIT = "0815e34"

MODULES: dict[str, list[dict]] = {
    "world": [
        {
            "id": "AUDIT-WORLD-001",
            "category": "duplication",
            "priority": "P1",
            "module": "World",
            "title": "Monolithic world JSON migration parsing (LoadBlocks/LoadChunks; not per-chunk JSON)",
            "files": [
                "src/World/Core/World.cpp",
                "src/World/IO/LegacyChunkJsonLoader.cpp",
                "src/World/IO/LegacyChunkJsonLoader.h"
            ],
            "lines": [
                3313,
                3328
            ],
            "evidence": "LoadBlocks/LoadChunks delegate to ULegacyChunkJsonLoader::LoadBlocksFile and LoadMonolithicChunksFile; shared parser extracted from World.cpp.",
            "action": "No further action; keep loader as single entry point for legacy JSON.",
            "risk": "low",
            "status": "done",
            "implemented_in": "fca6e21cdcb71f42f9e880bd02cbf3fbccea753e",
            "tech_debt_ref": "TD-AUD-004"
        },
        {
            "id": "AUDIT-WORLD-002",
            "category": "performance",
            "priority": "P1",
            "module": "World",
            "title": "MarkBlockChunkDirty uses RebuildChunkImmediate vs MarkDirty branches",
            "files": [
                "src/World/Core/World.cpp"
            ],
            "lines": [
                3288,
                3310
            ],
            "evidence": "MarkBlockChunkDirty sets immediate=BlockRegistry!=nullptr; immediate path calls MeshCache.RebuildChunkImmediate, else MeshCache.MarkDirty; propagates to chunk + NEIGHBOR_OFFSETS. perf_hints flagged dual paths at file level.",
            "action": "Document contract (sync mesh when registry ready, defer when null); consider unifying to MarkDirty + frame budget if hitches appear on block edit.",
            "risk": "medium",
            "status": "open",
            "tech_debt_ref": "TD-AUD-013"
        },
        {
            "id": "AUDIT-WORLD-003",
            "category": "dead_code",
            "priority": "P0",
            "module": "World",
            "title": "Unused HasChunkJsonFiles helper",
            "files": [
                "src/World/Core/World.cpp"
            ],
            "lines": [
                69,
                84
            ],
            "evidence": "dead_code.json symbol HasChunkJsonFiles callers=1 (definition only). rg finds zero call sites. HasPersistedTerrainOnDisk uses HasChunkDataFiles (line 116) and UChunkStorageService::HasChunkFilesOnDisk instead.",
            "action": "Remove HasChunkJsonFiles or wire it into HasPersistedTerrainOnDisk if .json per-file detection is still needed.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-015"
        },
        {
            "id": "AUDIT-WORLD-004",
            "category": "dead_code",
            "priority": "P0",
            "module": "World",
            "title": "Unused ResolveMovementAxisEye collision helper",
            "files": [
                "src/World/Core/World.cpp"
            ],
            "lines": [
                2036,
                2088
            ],
            "evidence": "dead_code.json callers=1. rg finds zero invocations. UWorld::ResolveMovement (2169) converts eye→body and calls ResolveMovementBody→ResolveMovementAxisBody (2091); eye-axis solver superseded.",
            "action": "Delete ResolveMovementAxisEye (~53 LOC) or refactor ResolveMovement to use it consistently with body path.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-015"
        },
        {
            "id": "AUDIT-WORLD-005",
            "category": "architecture",
            "priority": "P2",
            "module": "World",
            "title": "UWorld god-class (~3390 LOC) mixes persistence, streaming, mesh, creatures, collision",
            "files": [
                "src/World/Core/World.cpp",
                "src/World/Core/World.h"
            ],
            "lines": [
                0,
                3390
            ],
            "evidence": "baseline.json god_class_lines World.cpp=3390. World.h pulls ChunkMeshCache, Creature, Activity, WorldGen, 100+ public methods. Partial extract exists (WorldCooperativeOps, LegacyChunkJsonLoader, ChunkStreamer) but core class remains monolithic.",
            "action": "Incremental facade extract: UWorldPersistence, UWorldStreaming, UWorldCollision (per TD-AUD-010).",
            "risk": "medium",
            "status": "open",
            "tech_debt_ref": "TD-AUD-010"
        },
        {
            "id": "AUDIT-WORLD-006",
            "category": "architecture",
            "priority": "P2",
            "module": "World",
            "title": "World module depends on Render headers (mesh cache, camera, fog, cube)",
            "files": [
                "src/World/Core/World.h",
                "src/World/Core/World.cpp"
            ],
            "lines": [
                15,
                24
            ],
            "evidence": "World.h includes Render/Mesh/ChunkMeshCache.h. World.cpp includes Render/Camera/*, Render/Engine/DistanceFog.h, Render/Primitives/Cube.h. UWorld owns UChunkMeshCache MeshCache member and drives frustum warmup.",
            "action": "Introduce IWorldMeshSink or move mesh dirty API behind interface to invert dependency (World → abstraction ← Render).",
            "risk": "medium",
            "status": "open",
            "tech_debt_ref": "TD-AUD-012"
        },
        {
            "id": "AUDIT-WORLD-007",
            "category": "performance",
            "priority": "P3",
            "module": "World",
            "title": "push_back in LoadWorldData resource-pack parse loop without reserve",
            "files": [
                "src/World/Core/World.cpp"
            ],
            "lines": [
                2635,
                2640
            ],
            "evidence": "perf_hints.json line 2638. Lambda parseIds pushes to out vector while parsing JSON arrays; runs once per world load.",
            "action": "Optional: reserve from array size if profiling shows load hitch (micro-opt).",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-014"
        },
        {
            "id": "AUDIT-WORLD-008",
            "category": "performance",
            "priority": "P3",
            "module": "World",
            "title": "push_back in SaveMovementDiagnostics sample loop without reserve",
            "files": [
                "src/World/Core/World.cpp"
            ],
            "lines": [
                2734,
                2755
            ],
            "evidence": "perf_hints.json line 2735. samples.push_back in loop over MovementDiagHistory; diagnostic export only.",
            "action": "samples.reserve(MovementDiagHistory.size()) before loop.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-014"
        },
        {
            "id": "AUDIT-WORLD-009",
            "category": "performance",
            "priority": "P3",
            "module": "World",
            "title": "Chunk streamer ring gate disabled by default; tuning undocumented in-game",
            "files": [
                "src/World/Chunks/ChunkStreamer.h",
                "src/World/Chunks/ChunkStreamer.cpp"
            ],
            "lines": [
                70,
                140
            ],
            "evidence": "RingGateEnabled defaults false (ChunkStreamer.h:120). SetRingGateEnabled exists but World.cpp never enables it. TD-CS-017 open.",
            "action": "Expose config toggle; profile fly-through with ring gate on/off; document in TECH_DEBT_CHUNK_STREAMING profiling bisect.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CS-017"
        },
        {
            "id": "AUDIT-WORLD-010",
            "category": "architecture",
            "priority": "P3",
            "module": "World",
            "title": "TECH_DEBT claims async chunk gen/IO defaults off; code defaults on",
            "files": [
                "src/WorldGen/Core/ProceduralSettings.h",
                "src/App/Core.cpp",
                "bin/config.json"
            ],
            "lines": [
                88,
                89
            ],
            "evidence": "TD-CS-011/012 say defaults off. ProceduralSettings has AsyncChunkGeneration{true} AsyncChunkIo{true}. bin/config.json procedural.async_chunk_* = true. Only headless worldgen path forces false (Core.cpp:1505-1506).",
            "action": "Update TECH_DEBT tracker to reflect current defaults OR flip defaults after validation fly-through.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CS-011"
        }
    ],
    "render": [
        {
            "id": "AUDIT-RENDER-001",
            "category": "architecture",
            "priority": "P2",
            "module": "Render",
            "title": "GeometryEngine ~2618 LOC with World+Creatures+WorldGen coupling",
            "files": [
                "src/Render/Engine/GeometryEngine.cpp",
                "src/Render/Engine/GeometryEngine.h"
            ],
            "lines": [
                0,
                2618
            ],
            "evidence": "baseline.json god_class_lines GeometryEngine.cpp=2618. Includes Creatures/*, Pose/*, WorldGen/*, App/Core.h, UWorld shared_ptr. DrawScene orchestrates blocks, creatures, sky, UI overlays, fog.",
            "action": "Enforce Pipeline include rules; extract GreedyGpuBackend and CreatureDrawPass incrementally.",
            "risk": "medium",
            "status": "open",
            "tech_debt_ref": "TD-AUD-012"
        },
        {
            "id": "AUDIT-RENDER-002",
            "category": "architecture",
            "priority": "P2",
            "module": "Render",
            "title": "StreamingHorizonBlocks deprecated API",
            "files": [
                "src/Render/Engine/DistanceFog.h",
                "src/World/Core/World.cpp"
            ],
            "lines": [],
            "evidence": "merge_findings auto_resolved: StreamingHorizonBlocks absent from DistanceFog.h and entire src/. Replaced by RenderHorizonBlocks/FogHorizonBlocks.",
            "action": "None.",
            "risk": "low",
            "status": "done",
            "implemented_in": "fca6e21cdcb71f42f9e880bd02cbf3fbccea753e",
            "tech_debt_ref": "TD-AUD-009"
        },
        {
            "id": "AUDIT-RENDER-003",
            "category": "performance",
            "priority": "P1",
            "module": "Render",
            "title": "GreedyMesher quads.reserve in BuildChunkMesh hot paths",
            "files": [
                "src/Render/Mesh/GreedyMesher.cpp"
            ],
            "lines": [
                197,
                340
            ],
            "evidence": "perf_hints flagged lines 315/436 push_back in loop. Both BuildChunkMesh overloads call quads.reserve(512) at function entry (197, 340). TD-AUD-008 closed as partial PR-E.",
            "action": "No further action unless profiling shows realloc; consider adaptive reserve from maxSolidY.",
            "risk": "low",
            "status": "done",
            "implemented_in": "fca6e21cdcb71f42f9e880bd02cbf3fbccea753e",
            "tech_debt_ref": "TD-AUD-008"
        },
        {
            "id": "AUDIT-RENDER-004",
            "category": "performance",
            "priority": "P1",
            "module": "Render",
            "title": "RebuildFlatGreedyBatches push_back without vector reserve",
            "files": [
                "src/Render/Mesh/ChunkMeshCache.cpp"
            ],
            "lines": [
                290,
                295
            ],
            "evidence": "perf_hints.json lines 289/294. GreedyBatches.push_back(chunk_batch) and merged_cross iteration push_back in flat rebuild loop; no GreedyBatches.reserve(GreedyCache.size()).",
            "action": "Reserve GreedyBatches from GreedyCache entry count or upper-bound batch count before loops.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-014"
        },
        {
            "id": "AUDIT-RENDER-005",
            "category": "performance",
            "priority": "P1",
            "module": "Render",
            "title": "RebuildChunkLegacy face loop push_back with partial reserve",
            "files": [
                "src/Render/Mesh/ChunkMeshCache.cpp"
            ],
            "lines": [
                496,
                548
            ],
            "evidence": "perf_hints line 497. Triple nested loop chunkInstances.push_back. Caller reserves 256 (547) but dense chunks may reallocate.",
            "action": "Tune reserve heuristic (e.g. 512) or skip legacy path when GreedyMeshing always on.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-014"
        },
        {
            "id": "AUDIT-RENDER-006",
            "category": "performance",
            "priority": "P1",
            "module": "Render",
            "title": "ChunkMeshCache dual RebuildChunkImmediate and MarkDirty paths",
            "files": [
                "src/Render/Mesh/ChunkMeshCache.cpp",
                "src/Render/Mesh/ChunkMeshCache.h"
            ],
            "lines": [
                202,
                464
            ],
            "evidence": "perf_hints file-level hint. MarkDirty queues dirty set; RebuildChunkImmediate calls RebuildChunk synchronously. World::MarkBlockChunkDirty selects path. Async path in RebuildDirtyChunks when Render.AsyncMeshing.",
            "action": "Keep intentional split; document when each path is used; align with TD-AUD-013 review.",
            "risk": "medium",
            "status": "open",
            "tech_debt_ref": "TD-AUD-013"
        },
        {
            "id": "AUDIT-RENDER-007",
            "category": "dead_code",
            "priority": "P0",
            "module": "Render",
            "title": "Orphan header Cube_GLM.h not in build",
            "files": [
                "src/Render/Primitives/Cube_GLM.h",
                "src/Render/Primitives/Cube.h"
            ],
            "lines": [
                1,
                86
            ],
            "evidence": "duplicates.json: 30+ clusters between Cube.h and Cube_GLM.h. rg: Cube_GLM only in tools/src_restructure_mapping.json; not in CMakeLists.txt; active code uses Cube.h + CubeGL.h.",
            "action": "Delete Cube_GLM.h or merge into Cube.h to eliminate duplicate UCube/CubeSide definitions.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        },
        {
            "id": "AUDIT-RENDER-008",
            "category": "duplication",
            "priority": "P1",
            "module": "Render",
            "title": "FaceIndexFromGreedy duplicated in GreedyMeshEmitter and GreedyMeshMath",
            "files": [
                "src/Render/Mesh/GreedyMeshEmitter.h",
                "src/Render/Mesh/GreedyMeshMath.h"
            ],
            "lines": [
                17,
                26
            ],
            "evidence": "duplicates.json clusters 6b612f97 and 9b687a17. Identical inline FaceIndexFromGreedy in anonymous namespaces of both headers.",
            "action": "Move to GreedyMesher.h or shared GreedyMeshCommon.h; include from both emitters.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        },
        {
            "id": "AUDIT-RENDER-009",
            "category": "duplication",
            "priority": "P2",
            "module": "Render",
            "title": "GreedyMesher BuildChunkMesh overloads duplicate ~140 lines",
            "files": [
                "src/Render/Mesh/GreedyMesher.cpp"
            ],
            "lines": [
                192,
                455
            ],
            "evidence": "Two overloads (UBlockWorld chunk vs ChunkMeshSnapshot) share identical greedy mask/quad emission loops; differ only in block access (GetBlockLocal vs snapshot) and maxSolidY source.",
            "action": "Extract template or IChunkMeshReader interface for block/neighbor queries; single meshing loop.",
            "risk": "medium",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        },
        {
            "id": "AUDIT-RENDER-010",
            "category": "performance",
            "priority": "P2",
            "module": "Render",
            "title": "Greedy GPU batches recreated with glBufferData on mesh revision",
            "files": [
                "src/Render/Engine/GeometryEngine.cpp"
            ],
            "lines": [
                684,
                708
            ],
            "evidence": "UploadGreedyGpuBatches glGenBuffers + glBufferData GL_STATIC_DRAW per batch on rebuild. Per-draw instance path also glBufferData each batch (620-628). No persistent VBO pool.",
            "action": "Deferred: persistent GPU VBO / vertex pooling per TD-CS-016.",
            "risk": "medium",
            "status": "open",
            "tech_debt_ref": "TD-CS-016"
        },
        {
            "id": "AUDIT-RENDER-011",
            "category": "performance",
            "priority": "P3",
            "module": "Render",
            "title": "Cross vegetation merged per-chunk; no global GPU instancing",
            "files": [
                "src/Render/Mesh/ChunkMeshCache.cpp",
                "src/Render/Mesh/CrossMeshEmitter.h"
            ],
            "lines": [
                277,
                296
            ],
            "evidence": "RebuildFlatGreedyBatches merges cross batches into merged_cross map then single batch per blockId; still one draw per block type, not retained instance buffer. TD-CS-014 open.",
            "action": "Implement retained instance buffer + single draw for Cross vegetation.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CS-014"
        },
        {
            "id": "AUDIT-RENDER-012",
            "category": "performance",
            "priority": "P3",
            "module": "Render",
            "title": "Async meshing enabled by default; needs in-game validation",
            "files": [
                "src/App/Settings/RenderSettings.h",
                "src/Render/Mesh/ChunkMeshCache.cpp",
                "src/App/Core.cpp"
            ],
            "lines": [
                12,
                373
            ],
            "evidence": "RenderSettings.AsyncMeshing{true}. ChunkMeshCache::RebuildDirtyChunks uses async when AsyncMeshing&&GreedyMeshing. TD-CS-010 backlog: validate via fly-through before treating as production-default.",
            "action": "Run profiling bisect (TECH_DEBT_CHUNK_STREAMING); toggle render.async_meshing; export movement_diagnostics.v2.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CS-010"
        },
        {
            "id": "AUDIT-RENDER-013",
            "category": "performance",
            "priority": "P3",
            "module": "Render",
            "title": "Conservative frustum fallback rebuilds all greedy batches",
            "files": [
                "src/Render/Mesh/ChunkMeshCache.cpp"
            ],
            "lines": [
                297,
                301
            ],
            "evidence": "When frustum cull yields empty GreedyBatches but GreedyCache non-empty, RebuildFlatGreedyBatches(nullptr) full merge. TD-CS-018: incremental frustum-only cull without full flat merge deferred.",
            "action": "Defer; improve camera-chunk skip or incremental cull.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CS-018"
        },
        {
            "id": "AUDIT-RENDER-014",
            "category": "performance",
            "priority": "P3",
            "module": "Render",
            "title": "Sky uses fixed horizon fog band; radial sky fog optional",
            "files": [
                "src/Render/Engine/DistanceFog.cpp",
                "src/Render/Engine/GeometryEngine.cpp"
            ],
            "lines": [
                13,
                26,
                802,
                803
            ],
            "evidence": "kHorizonMarginBlocks/kMinHorizonBlocks fixed constants. GeometryEngine sets FogHorizonBlend=1.0 when distance fog on. TD-CS-019 open.",
            "action": "Defer optional true radial sky fog; current horizon blend shipped.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CS-019"
        },
        {
            "id": "AUDIT-RENDER-015",
            "category": "duplication",
            "priority": "P2",
            "module": "Render",
            "title": "OpenGL depth/blend state restore duplicated with WindowManager",
            "files": [
                "src/Render/Engine/GeometryEngine.cpp",
                "src/App/Platform/WindowManager.cpp"
            ],
            "lines": [
                433,
                447,
                1584,
                1600
            ],
            "evidence": "duplicates.json clusters a47f0843/dc5b4bbe: identical if(depthTestEnabled) glEnable/Disable GL_DEPTH_TEST and blend blocks in GeometryEngine::DrawScene and UI draw teardown.",
            "action": "Extract GlStateScope restore helper shared with Render/Pipeline/GlStateScope.",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        }
    ],
    "app_gui": [
        {
            "id": "AUDIT-APP-001",
            "category": "architecture",
            "priority": "P2",
            "module": "App",
            "title": "UApplication ~1788 LOC combines game loop, screens, and input routing",
            "files": [
                "src/App/Application.cpp",
                "src/App/Application.h"
            ],
            "evidence": "baseline god_class_lines=1788; owns MainMenu/HUD/Console/Palette/Progress screens plus MenuSubview state machine; RouteKey/RouteMouse* methods ~400 LOC; couples App+Gui+World+Render includes in single TU; WorldOperationRunner already extracted but screen transitions and overlay capture remain inline",
            "action": "incremental extract UScreenNavigator (menu subview transitions) and UInputRouter (RouteKey/RouteMouse/scroll); keep UApplication as thin coordinator",
            "risk": "medium",
            "status": "open",
            "tech_debt_ref": "TD-AUD-011"
        },
        {
            "id": "AUDIT-APP-002",
            "category": "architecture",
            "priority": "P2",
            "module": "App",
            "title": "Legacy config shims scattered across Core.cpp and config I/O",
            "files": [
                "src/App/Core.cpp",
                "src/App/Core.h",
                "src/WorldGen/Core/ProceduralConfigIO.cpp"
            ],
            "evidence": "Core.cpp reads ui.legacy_hud, ui.block_input_profile (alias for control_scheme), root terrain string synced with ProceduralTemplate.Generator via TerrainType field (10+ assignments); SetupNewWorldForCreation migrates PendingNewWorldResourcePacks legacy vector; ProceduralConfigIO::WriteUiSettings still writes legacy_hud",
            "action": "group migration paths in LegacyConfigAdapter (read ui aliases, terrain root key, legacy pack list); document supported aliases; stop writing deprecated keys on save where safe",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-APP-003",
            "category": "architecture",
            "priority": "P2",
            "module": "App",
            "title": "UCore ~1567 LOC god-class spans config, worlds, and resource packs",
            "files": [
                "src/App/Core.cpp",
                "src/App/Core.h"
            ],
            "evidence": "Core.cpp ~1567 LOC; 35+ public methods mixing LoadConfig/SaveSystem, world CRUD, resource-pack apply/reload, runtime block overlay, headless CreateWorldCli; duplicates platform path discovery (IsGameDataRoot) already present in DesktopPlatformPaths",
            "action": "extract UWorldLifecycleFacade (create/load/save/list) and UResourcePackBootstrap; leave UCore as wiring/DI root",
            "risk": "medium",
            "status": "open"
        },
        {
            "id": "AUDIT-APP-004",
            "category": "architecture",
            "priority": "P2",
            "module": "App",
            "title": "UWindowManager ~907 LOC owns GLFW loop, input fan-out, and legacy help overlay",
            "files": [
                "src/App/Platform/WindowManager.cpp",
                "src/App/Platform/WindowManager.h"
            ],
            "evidence": "~907 LOC; Run/Update/Render/ProcessInput/HandleKeyEvent coordinate Core+Application+GeometryEngine; RenderHelpText embeds hard-coded F-key shortcuts; duplicates OpenGL depth/blend save-restore blocks also found in GeometryEngine.cpp (duplicate clusters a47f0843, dc5b4bbe)",
            "action": "move help overlay to Gui screen or dev-only overlay; share GL state scope guard with Render; slim WindowManager to platform callbacks only",
            "risk": "medium",
            "status": "open"
        },
        {
            "id": "AUDIT-APP-005",
            "category": "duplication",
            "priority": "P1",
            "module": "App",
            "title": "IsGameDataRoot / project-root search duplicated in Core and DesktopPlatformPaths",
            "files": [
                "src/App/Core.cpp",
                "src/App/Platform/DesktopPlatformPaths.cpp"
            ],
            "evidence": "scan_duplicates clusters ddc1098e, 7b48503d, 89399d99: identical 15-line IsGameDataRoot blocks at Core.cpp:93-109 and DesktopPlatformPaths.cpp:14-30; both walk parent dirs up to depth 8",
            "action": "single GameDataRootLocator in App/Platform used by Core and UDesktopPlatformPaths",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        },
        {
            "id": "AUDIT-APP-006",
            "category": "duplication",
            "priority": "P2",
            "module": "App",
            "title": "OpenGL 2D overlay state save/restore duplicated with GeometryEngine",
            "files": [
                "src/App/Platform/WindowManager.cpp",
                "src/Render/Engine/GeometryEngine.cpp"
            ],
            "evidence": "duplicate clusters a47f0843 and dc5b4bbe span WindowManager.cpp:743-759 and GeometryEngine.cpp:433-434, 1584-1586 (glGetBooleanv depth/blend, disable depth, enable blend, restore)",
            "action": "extract small GlScopedBlend2D or reuse existing render util; one implementation for HUD/help/text passes",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        },
        {
            "id": "AUDIT-APP-007",
            "category": "duplication",
            "priority": "P3",
            "module": "Gui",
            "title": "List widget scroll/selection logic duplicated across CheckList and ListView",
            "files": [
                "src/Gui/Widgets/GuiCheckList.cpp",
                "src/Gui/Widgets/GuiListView.cpp"
            ],
            "evidence": "scan_duplicates: 14 clusters between GuiCheckList.cpp and GuiListView.cpp (e.g. hashes 659f072b, 7ea56a8f, ea4abfe6, e6550ace) covering ApplyMinimumBounds, scrollbar drag, row hit-test patterns; Gui module 14373 LOC largest in repo",
            "action": "extract GuiScrollableListMixin or shared GuiListScrollController base used by both widgets",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        },
        {
            "id": "AUDIT-APP-008",
            "category": "duplication",
            "priority": "P3",
            "module": "Gui",
            "title": "Icon cache GL teardown blocks duplicated in CreatureIconCache and PrefabIconCache",
            "files": [
                "src/Gui/Cache/CreatureIconCache.cpp",
                "src/Gui/Cache/PrefabIconCache.cpp"
            ],
            "evidence": "duplicate clusters 0a194e54, 0dc88ee5, dc40cc38: matching FBO/VBO/VAO/texture delete sequences at CreatureIconCache.cpp:241-285 and PrefabIconCache.cpp:95-110",
            "action": "shared GuiOffscreenIconCacheBase or small GlResourceBundle helper for icon render targets",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        },
        {
            "id": "AUDIT-APP-009",
            "category": "duplication",
            "priority": "P3",
            "module": "Gui",
            "title": "Focus/scroll hit-test boilerplate duplicated in GuiFocus and GuiScrollView",
            "files": [
                "src/Gui/Core/GuiFocus.cpp",
                "src/Gui/Widgets/GuiScrollView.cpp"
            ],
            "evidence": "scan_duplicates: 10 clusters (06219a43, 158d0db9, 3880a223, etc.) between GuiFocus.cpp:8-19 and GuiScrollView.cpp:10-20",
            "action": "consolidate pointer-to-widget routing helpers in GuiFocus or GuiHitTest util",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-016"
        }
    ],
    "worldgen": [
        {
            "id": "AUDIT-WG-001",
            "category": "architecture",
            "priority": "P3",
            "module": "WorldGen",
            "title": "Legacy procedural config shims in ProceduralConfigIO",
            "files": [
                "src/WorldGen/Core/ProceduralConfigIO.cpp",
                "src/WorldGen/Core/ProceduralSettings.cpp"
            ],
            "evidence": "ApplyLegacyVerticalMode (compact/extended), ApplyLegacyOverworldProfile (generator overworld_biomes toggles caves/ores/fluids), root terrain string fallback when procedural absent, WARN when procedural.generator overrides legacy terrain; ProceduralGeneratorFromString maps overworld_biomes/overworld_full/beta_retro and warns on indev_retro",
            "action": "document migration matrix in TECH_DEBT_WORLDGEN.md; keep shims until old configs retired; add config version field when breaking",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-WG-002",
            "category": "dead_code",
            "priority": "P3",
            "module": "WorldGen",
            "title": "Dead-code scan false positives: WorldGeneratorRegistry factory symbols",
            "files": [
                "src/WorldGen/Core/WorldGeneratorRegistry.cpp",
                "src/WorldGen/Core/WorldGeneratorDescriptor.h"
            ],
            "evidence": "dead_code.json lists ApplyFlatDefaults, ApplyHeightmapDefaults, ApplyHillsDefaults, ApplyMountainsDefaults, ApplyOverworldDefaults, ApplyBetaRetroDefaults, CreateFlat, CreateHeightmap, CreateHills, CreateMountains, CreateOverworld, CreateBetaRetro (callers=1 each); all are function pointers in constexpr kDescriptors[] and invoked via descriptor->ApplyDefaults / descriptor->Create in UWorldGeneratorRegistry::Create — not dead",
            "action": "whitelist WorldGeneratorRegistry.cpp in scan_dead_code.py or teach scanner function-pointer tables; do not delete",
            "risk": "low",
            "status": "rejected",
            "tech_debt_ref": "TD-AUD-015"
        },
        {
            "id": "AUDIT-WG-003",
            "category": "architecture",
            "priority": "P2",
            "module": "WorldGen",
            "title": "UBiomeSampler translation unit ~1137 LOC — per-column hot path god-module",
            "files": [
                "src/WorldGen/Sampling/BiomeSampler.cpp",
                "src/WorldGen/Sampling/BiomeSampler.h"
            ],
            "evidence": "8177 LOC WorldGen module; BiomeSampler.cpp alone ~1137 lines with 30+ free functions (ClassifyBiome, ComputeBiomeWeights, BlendedBiomeWeights, RefineSurfaceYWithBiomes, river/coast/erosion) all called per column during overworld generation",
            "action": "split into BiomeClassifier, BiomeHeightRefiner, BiomeSurfaceRules; keep UBiomeSampler as facade; profile before micro-opts",
            "risk": "medium",
            "status": "open"
        },
        {
            "id": "AUDIT-WG-004",
            "category": "architecture",
            "priority": "P2",
            "module": "WorldGen",
            "title": "ProceduralConfigIO ~571 LOC mixes parse, legacy migration, and serialize",
            "files": [
                "src/WorldGen/Core/ProceduralConfigIO.cpp",
                "src/WorldGen/Core/ProceduralConfigIO.h"
            ],
            "evidence": "single file handles ParseProceduralSettings, ParseTuning, legacy vertical/generator shims, duplicate keys (caves + enable_caves, trees booleans), WriteProceduralSettings and WriteUiSettings; writes both root terrain and procedural.generator on save",
            "action": "split ProceduralConfigReader / ProceduralConfigWriter; centralize legacy field mapping table",
            "risk": "medium",
            "status": "open"
        },
        {
            "id": "AUDIT-WG-005",
            "category": "architecture",
            "priority": "P3",
            "module": "WorldGen",
            "title": "Dual config keys and deprecated generator ids retained for compatibility",
            "files": [
                "src/WorldGen/Core/ProceduralConfigIO.cpp",
                "src/WorldGen/Core/ProceduralSettings.cpp"
            ],
            "evidence": "WriteProceduralSettings emits both procedural.caves and procedural.enable_caves; Parse accepts trees/enable_trees variants; indev_retro maps to Heightmap with WARN; unknown generator falls back to Overworld",
            "action": "on save emit canonical keys only; keep read-side aliases documented; log once per session for deprecated ids",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-WG-006",
            "category": "architecture",
            "priority": "P3",
            "module": "WorldGen",
            "title": "UWorldGenPack loader ~669 LOC — pack/biome/pipeline parsing monolith",
            "files": [
                "src/WorldGen/Core/WorldGenPack.cpp",
                "src/WorldGen/Core/WorldGenPack.h"
            ],
            "evidence": "WorldGenPack.cpp ~669 LOC; parses pack.json, pipeline.json stage order, biomes, height/climate layers; push_back on StageOrder without reserve (small arrays, load-time only)",
            "action": "optional split PackJsonLoader vs runtime ActivePack cache; low urgency — load-time not hot path",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-WG-007",
            "category": "performance",
            "priority": "P3",
            "module": "WorldGen",
            "title": "Column generation sorts full patch before pipeline.GenerateColumn",
            "files": [
                "src/WorldGen/Core/IWorldGenPipeline.cpp",
                "src/WorldGen/Pipelines/ComposableWorldGenerator.cpp"
            ],
            "evidence": "GenerateAllColumnsInChunkRange builds vector of all columns, sorts by dist2, then calls GenerateColumn per entry; for 16-chunk radius patch this is O(n log n) setup per batch — acceptable but BiomeSampler per-column work dominates; no perf_hints hits in WorldGen module",
            "action": "defer unless profiling shows sort overhead; prefer optimizing BiomeSampler hot path first; see TECH_DEBT_CHUNK_STREAMING for streaming context",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-WG-008",
            "category": "architecture",
            "priority": "P3",
            "module": "WorldGen",
            "title": "Deferred worldgen UX and debug features per TECH_DEBT_WORLDGEN",
            "files": [
                "docs/TECH_DEBT_WORLDGEN.md"
            ],
            "evidence": "open backlog: pack dropdown from pack.json scan, biome/climate debug overlay (worldgen debug on stub), generator presets UX; hot-reload only affects new chunks (documented limitation)",
            "action": "defer to post-audit roadmap; not blocking PR-D/E",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-WG-009",
            "category": "architecture",
            "priority": "P3",
            "module": "WorldGen",
            "title": "integration_test_worldgen fire_blocks threshold smoke failure",
            "files": [
                "docs/TECH_DEBT_AUDIT.md"
            ],
            "evidence": "TD-AUD-018: pre-existing integration_test_worldgen fire_blocks threshold failure tracked separately from module refactor",
            "action": "fix threshold or tune generator in dedicated PR; not part of dead-code cleanup",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-018"
        }
    ],
    "packs": [
        {
            "id": "AUDIT-PACK-001",
            "category": "performance",
            "priority": "P3",
            "module": "ResourcePacks",
            "title": "TD-002: RegisterRuntimeBlock still triggers full Rebuild via FlushRuntimeOverlay",
            "files": [
                "src/ResourcePacks/BlockMergeRegistry.cpp",
                "src/ResourcePacks/BlockMergeRegistry.h",
                "docs/TECH_DEBT_RESOURCE_PACKS.md"
            ],
            "lines": [
                467,
                487
            ],
            "evidence": "RegisterRuntimeBlock batches overlay dirty flag; FlushRuntimeOverlay calls full Rebuild(). Incremental atlas + dirty-chunk path deferred in TD-002.",
            "action": "defer; implement incremental atlas rebuild when overlay batching is insufficient",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-002"
        },
        {
            "id": "AUDIT-PACK-002",
            "category": "performance",
            "priority": "P3",
            "module": "ResourcePacks",
            "title": "TD-005: disk placeholder cache (.placeholder_cache/) unused",
            "files": [
                "src/ResourcePacks/PlaceholderTextureCache.cpp",
                "src/ResourcePacks/PlaceholderTextureCache.h",
                "docs/TECH_DEBT_RESOURCE_PACKS.md"
            ],
            "lines": [
                49,
                76
            ],
            "evidence": "SavePlaceholderFile/LoadPlaceholderFile exist; in-memory LRU (max 256) active; disk cache path still backlog per TD-005.",
            "action": "defer; add disk LRU or remove dead save/load paths",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-005"
        },
        {
            "id": "AUDIT-PACK-003",
            "category": "architecture",
            "priority": "P3",
            "module": "ResourcePacks",
            "title": "TD-006: Android selective asset extraction not implemented",
            "files": [
                "docs/TECH_DEBT_RESOURCE_PACKS.md"
            ],
            "lines": [],
            "evidence": "Open tech debt: whitelist extraction after TD-001; manifest+checksums deferred.",
            "action": "defer to Android packaging milestone",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-006"
        },
        {
            "id": "AUDIT-PACK-004",
            "category": "docs",
            "priority": "P3",
            "module": "ResourcePacks",
            "title": "Manual verify checklist for hotbar and pack UI still open",
            "files": [
                "docs/TECH_DEBT_RESOURCE_PACKS.md"
            ],
            "lines": [],
            "evidence": "Four unchecked manual-verify items (unknown hotbar block, World settings pack swap, Settings defaults, _example_creature_demo overlay).",
            "action": "run manual QA checklist; close or file bugs",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-PACK-005",
            "category": "architecture",
            "priority": "P3",
            "module": "ResourcePacks",
            "title": "smoke_resource_packs tree_bark asset gap (TD-AUD-019)",
            "files": [
                "tools/smoke_resource_packs.py",
                "resource_packs/minetest_default_16/blocks/tree_bark.json",
                "docs/TECH_DEBT_AUDIT.md"
            ],
            "lines": [],
            "evidence": "tree_bark referenced in prefabs/worldgen_refs; smoke may fail on missing texture in primary pack — tracked as TD-AUD-019.",
            "action": "fix primary-pack texture for tree_bark or adjust smoke strictness",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-019"
        },
        {
            "id": "AUDIT-PACK-006",
            "category": "duplication",
            "priority": "P3",
            "module": "ResourcePacks",
            "title": "ParseHexColor helper duplicated with App/Core.cpp",
            "files": [
                "src/ResourcePacks/PlaceholderTextureCache.cpp",
                "src/App/Core.cpp"
            ],
            "lines": [
                16,
                63
            ],
            "evidence": "scan_duplicates.json hash 24c24caec85e6538: 15-line hex color parser in both files.",
            "action": "extract shared color util or accept cross-module duplication",
            "risk": "low",
            "status": "open"
        }
    ],
    "creatures": [
        {
            "id": "AUDIT-CRE-001",
            "category": "architecture",
            "priority": "P3",
            "module": "Creatures",
            "title": "glTF backend stub (TD-CRE-001)",
            "files": [
                "src/Creatures/Visual/CreatureVisualGltf.cpp",
                "src/Creatures/Visual/CreatureVisualFactory.cpp"
            ],
            "lines": [
                25,
                16
            ],
            "evidence": "UCreatureVisualGltf::SubmitDraw logs once and draws debug wireframe only; CreateCreatureVisual routes gltf_skeleton to stub. Open in TECH_DEBT_CREATURES.md.",
            "action": "defer; full cgltf + skinned shader path is phase 5",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CRE-001"
        },
        {
            "id": "AUDIT-CRE-002",
            "category": "duplication",
            "priority": "P2",
            "module": "Creatures",
            "title": "Duplicate catalog sort comparator in CreatureDefinitionStorage and SkinDefinitionStorage",
            "files": [
                "src/Creatures/Definition/CreatureDefinitionStorage.cpp",
                "src/Creatures/Definition/SkinDefinitionStorage.cpp"
            ],
            "lines": [
                363,
                389,
                169
            ],
            "evidence": "scan_duplicates.json: identical 15-line sort-by-sortOrder lambdas in ListIds/ListSpawnable (creature) and ListEquippable (skin).",
            "action": "extract shared SortDefinitionIdsByCatalogOrder helper",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-CRE-003",
            "category": "architecture",
            "priority": "P3",
            "module": "Creatures",
            "title": "visual.rig parsed but does not select pose presenter (TD-CRE-003)",
            "files": [
                "src/Creatures/Definition/CreatureDefinitionStorage.cpp"
            ],
            "lines": [
                282,
                290
            ],
            "evidence": "rig.template/partIds loaded from JSON; pose selection uses locomotion_archetype only per TECH_DEBT_CREATURES.md.",
            "action": "defer or wire rig.template to pose registry",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CRE-003"
        },
        {
            "id": "AUDIT-CRE-004",
            "category": "architecture",
            "priority": "P3",
            "module": "Creatures",
            "title": "AerialPosePresenter: full b3d clip playback for flying birds deferred (TD-CRE-006)",
            "files": [
                "src/Pose/AerialPosePresenter.cpp"
            ],
            "lines": [],
            "evidence": "Ground chicken walk+peck done; fly IK and clip playback backlog per TECH_DEBT_CREATURES.md.",
            "action": "defer",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CRE-006"
        },
        {
            "id": "AUDIT-CRE-005",
            "category": "architecture",
            "priority": "P3",
            "module": "Creatures",
            "title": "FleeActivityAgent / MeleeAttackActivityAgent not implemented (TD-CRE-008)",
            "files": [
                "docs/TECH_DEBT_CREATURES.md"
            ],
            "lines": [],
            "evidence": "Visual scope complete; AI activity agents deferred.",
            "action": "defer to AI phase",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CRE-008"
        },
        {
            "id": "AUDIT-CRE-006",
            "category": "architecture",
            "priority": "P3",
            "module": "Creatures",
            "title": "FP viewmodel arms (fp_parts[]) not implemented (TD-CRE-010)",
            "files": [
                "docs/TECH_DEBT_CREATURES.md"
            ],
            "lines": [],
            "evidence": "First-person arm parts deferred; not a blocker.",
            "action": "defer",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CRE-010"
        },
        {
            "id": "AUDIT-CRE-007",
            "category": "architecture",
            "priority": "P3",
            "module": "Creatures",
            "title": "Wave bake coverage incomplete for ~42 Luanti mobs (TD-CRE-017)",
            "files": [
                "tools/creature_luanti_sources.yaml",
                "tools/bake_rigid_creature_textures.py",
                "docs/TECH_DEBT_CREATURES.md"
            ],
            "lines": [],
            "evidence": "Partial until all research textures present in CubatariumTextureResearch.",
            "action": "import missing PNGs then re-run bake pipeline",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CRE-017"
        },
        {
            "id": "AUDIT-CRE-008",
            "category": "architecture",
            "priority": "P3",
            "module": "Creatures",
            "title": "8 placeholder species missing research textures (TD-CRE-021)",
            "files": [
                "docs/TECH_DEBT_CREATURES.md"
            ],
            "lines": [],
            "evidence": "dolphin, whale, octopus, kitten, warthog, mese_monster, lava_flan, water_dragon — textures missing in CubatariumTextureResearch.",
            "action": "import animalworld/mobs_* PNGs then bake_rigid_creature_textures.py",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-CRE-021"
        },
        {
            "id": "AUDIT-CRE-009",
            "category": "architecture",
            "priority": "P2",
            "module": "Creatures",
            "title": "WorldCreatures.cpp combines spawn, habitat, pack overlay (~776 LOC)",
            "files": [
                "src/Creatures/Core/WorldCreatures.cpp"
            ],
            "lines": [],
            "evidence": "5975 LOC module total; WorldCreatures is largest file and couples spawn probe, habitat snap, pack overlay refresh.",
            "action": "incremental extract spawn vs environment facades when touching this file",
            "risk": "medium",
            "status": "open"
        }
    ],
    "console": [
        {
            "id": "AUDIT-TEST-001",
            "category": "architecture",
            "priority": "P2",
            "module": "Commands",
            "title": "chunk_load_priority_test not in CI",
            "files": [
                "CMakeLists.txt",
                ".github/workflows/windows-release-smoke.yml",
                "src/Test/ChunkLoadPriorityTest.cpp"
            ],
            "lines": [
                76,
                78
            ],
            "evidence": "single C++ test target exists; resolved: chunk_load_priority_test in windows-release-smoke CI (step 'C++ unit test (chunk load priority)')",
            "action": "none — already in CI",
            "risk": "low",
            "status": "done",
            "implemented_in": "fca6e21cdcb71f42f9e880bd02cbf3fbccea753e",
            "tech_debt_ref": "TD-AUD-006"
        },
        {
            "id": "AUDIT-CON-001",
            "category": "architecture",
            "priority": "P2",
            "module": "Commands",
            "title": "RegisterCommands ~340 LOC lives in GameSession, not Commands module",
            "files": [
                "src/Game/GameSession.cpp",
                "src/Commands/CommandRegistry.cpp",
                "src/Commands/CommandRegistry.h"
            ],
            "lines": [
                89,
                430
            ],
            "evidence": "UCommandRegistry is 45 LOC dispatcher; all handlers registered in UGameSession::RegisterCommands with heavy World/Creature coupling.",
            "action": "extract command modules (worldgen, creature, player) or CommandRegistrar helper",
            "risk": "medium",
            "status": "open"
        },
        {
            "id": "AUDIT-CON-002",
            "category": "architecture",
            "priority": "P3",
            "module": "Commands",
            "title": "Console code split across src/Commands, src/Console, Gui/ConsoleScreen",
            "files": [
                "src/Commands/CommandRegistry.cpp",
                "src/Console/ConsoleCommandHistory.cpp",
                "src/Console/ConsoleInputSanitize.cpp",
                "src/Gui/Screens/ConsoleScreen.cpp"
            ],
            "lines": [],
            "evidence": "Module mapped to src/Commands (81 LOC) but UI/history/sanitize in separate trees; baseline shows src/Console 193 LOC.",
            "action": "document module boundaries in ARCHITECTURE.md or consolidate under Commands/",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-CON-003",
            "category": "docs",
            "priority": "P3",
            "module": "Commands",
            "title": "help command uses hardcoded command list",
            "files": [
                "src/Game/GameSession.cpp"
            ],
            "lines": [
                95,
                97
            ],
            "evidence": "Static string may drift from Handlers map; no auto-enumeration of registered commands.",
            "action": "generate help from UCommandRegistry or add compile-time assert",
            "risk": "low",
            "status": "open"
        },
        {
            "id": "AUDIT-CON-004",
            "category": "architecture",
            "priority": "P3",
            "module": "Commands",
            "title": "Stub commands: time, worldgen debug overlay",
            "files": [
                "src/Game/GameSession.cpp"
            ],
            "lines": [
                140,
                135
            ],
            "evidence": "time returns 'not implemented'; worldgen debug stores flag but message says 'visual overlay not wired yet'.",
            "action": "implement or remove from help until ready",
            "risk": "low",
            "status": "open"
        }
    ],
    "tools": [
        {
            "id": "AUDIT-TOOL-DEV-001",
            "category": "architecture",
            "priority": "P3",
            "module": "tools",
            "title": "tools/audit/ pipeline is documented dev tooling (not orphan)",
            "files": [
                "tools/audit/orchestrate.py",
                "tools/audit/merge_findings.py",
                "tools/audit/schema.py",
                "tools/audit/scan_dead_code.py",
                "tools/audit/scan_duplicates.py",
                "tools/audit/scan_includes.py",
                "tools/audit/scan_perf_hints.py",
                "tools/audit/scan_cmake_sources.py",
                "tools/audit/scan_tools_usage.py",
                "tools/audit/scan_docs_drift.py",
                "docs/CODING_STYLE.md",
                "docs/TECH_DEBT_AUDIT.md"
            ],
            "lines": [],
            "evidence": "TD-AUD-001 closed: orchestrator + 7 scanners + merge_findings. Referenced in CODING_STYLE.md and PERFORMANCE_OPTIMIZATION.md. scan_tools_usage excludes audit/ refs — false orphan classification.",
            "action": "reclassified; no archive needed",
            "risk": "low",
            "status": "done",
            "implemented_in": "fca6e21cdcb71f42f9e880bd02cbf3fbccea753e",
            "tech_debt_ref": "TD-AUD-001"
        },
        {
            "id": "AUDIT-TOOL-LIB-001",
            "category": "architecture",
            "priority": "P3",
            "module": "tools",
            "title": "Internal library modules falsely flagged as orphans (filename-only scan)",
            "files": [
                "tools/stem_mapping_common.py",
                "tools/prefab_import_common.py",
                "tools/worldgen_metrics_lib.py"
            ],
            "lines": [],
            "evidence": "scan_tools_usage searches literal 'stem_mapping_common.py' etc.; imported as modules without .py suffix by build_resource_pack, generate_stem_mapping, mts_to_prefab, integration_test_worldgen, analyze_world_chunks.",
            "action": "reclassified as internal libs; improve scan_tools_usage to detect 'from X import'",
            "risk": "low",
            "status": "done",
            "tech_debt_ref": "TD-AUD-017"
        },
        {
            "id": "AUDIT-TOOL-FIX-001",
            "category": "architecture",
            "priority": "P3",
            "module": "tools",
            "title": "One-shot fix_* refactor scripts unreferenced (7)",
            "files": [
                "tools/fix_animated_block_pngs.py",
                "tools/fix_broken_header_includes.py",
                "tools/fix_broken_includes.py",
                "tools/fix_gui_rect_accessors.py",
                "tools/fix_refactor_damage.py",
                "tools/fix_remaining_types.py",
                "tools/fix_worldgen_context.py"
            ],
            "lines": [],
            "evidence": "audit/tools_orphans.json: no references in CI/docs/scripts/README; one-time migration scripts from refactor waves.",
            "action": "move to tools/archive/ with README entry (pattern: import_blocks.ps1 TD-008)",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-017"
        },
        {
            "id": "AUDIT-TOOL-GEN-001",
            "category": "architecture",
            "priority": "P3",
            "module": "tools",
            "title": "Orphan generator/migration scripts (6)",
            "files": [
                "tools/generate_minetest_stem_map.py",
                "tools/generate_worldgen_biome_map.py",
                "tools/inject_display_names.py",
                "tools/restore_tier_a_block.py",
                "tools/restructure_src.py",
                "tools/update_refactor_style.py"
            ],
            "lines": [],
            "evidence": "audit/tools_orphans.json: no CI/docs references; ad-hoc authoring utilities.",
            "action": "document in tools/README or archive if superseded",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-017"
        },
        {
            "id": "AUDIT-TOOL-SMOKE-001",
            "category": "architecture",
            "priority": "P3",
            "module": "tools",
            "title": "Orphan smoke/diagnostic scripts (4)",
            "files": [
                "tools/smoke_control_schemes.py",
                "tools/smoke_creature_fidelity.py",
                "tools/test_chunk_storage.py",
                "tools/test_texture_overrides_yaml.py"
            ],
            "lines": [],
            "evidence": "audit/tools_orphans.json: not wired to CI; may be useful manual QA.",
            "action": "wire to workflow or document as manual-only in docs/CODING_STYLE.md",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-017"
        },
        {
            "id": "AUDIT-TOOL-IMPORT-001",
            "category": "architecture",
            "priority": "P3",
            "module": "tools",
            "title": "Orphan import/prefab utilities (3)",
            "files": [
                "tools/import_luanti_rigid_creature.py",
                "tools/prefab_bounds.py",
                "tools/prefab_import_common.py"
            ],
            "lines": [],
            "evidence": "prefab_import_common imported by mts_to_prefab.py but flagged orphan (filename scan); import_luanti_rigid_creature and prefab_bounds have no external refs.",
            "action": "reclassify prefab_import_common as internal lib; document or archive the other two",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-017"
        },
        {
            "id": "AUDIT-TOOL-019",
            "category": "architecture",
            "priority": "P3",
            "module": "tools",
            "title": "smoke_resource_packs tree_bark missing (TD-AUD-019)",
            "files": [
                "tools/smoke_resource_packs.py",
                ".github/workflows/resource-packs-smoke.yml"
            ],
            "lines": [],
            "evidence": "Pre-existing asset gap; smoke may fail on tree_bark texture in primary pack.",
            "action": "fix asset or relax smoke; see also AUDIT-PACK-005",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-019"
        },
        {
            "id": "AUDIT-TOOL-SCAN-001",
            "category": "architecture",
            "priority": "P3",
            "module": "tools",
            "title": "scan_tools_usage.py uses filename-only rg heuristic",
            "files": [
                "tools/audit/scan_tools_usage.py",
                "audit/tools_orphans.json"
            ],
            "lines": [
                22,
                41
            ],
            "evidence": "Misses 'from module import' references and docs mentioning tools/audit/ only inside audit/ folder; produces false orphans for libs and audit pipeline.",
            "action": "extend scanner: module stem imports, tools/README inventory, exclude tools/audit/",
            "risk": "low",
            "status": "open",
            "tech_debt_ref": "TD-AUD-017"
        }
    ]
}


def main() -> int:
    AUDIT.mkdir(parents=True, exist_ok=True)
    total = 0
    for module_id, findings in MODULES.items():
        doc = {
            "module_id": module_id,
            "commit": COMMIT,
            "generated_at": TS,
            "findings": findings,
        }
        path = AUDIT / f"{module_id}.json"
        path.write_text(
            json.dumps(doc, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        n = len(findings)
        total += n
        print(f"Wrote {path.name}: {n} findings")
    print(f"Total: {total} findings in {len(MODULES)} modules (commit={COMMIT}, ts={TS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
