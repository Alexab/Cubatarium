# Cubatarium Audit Report 2026

- **Commit:** `fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`
- **Generated:** 2026-06-27T14:36:46+00:00
- **Status:** pending_review

## Executive Summary

Open findings (done excluded):

| Priority | Open |
|----------|------|
| P0 | 0 |
| P1 | 10 |
| P2 | 18 |
| P3 | 13 |

Closed: **3** finding(s).

## Open Findings by Priority

### P1

- **AUDIT-PERF-001** [performance] push_back in loop without nearby reserve()
  - Module: Render; Files: src/Render/Mesh/GreedyMesher.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: push_back in loop without nearby reserve()
- **AUDIT-PERF-002** [performance] push_back in loop without nearby reserve()
  - Module: Render; Files: src/Render/Mesh/GreedyMesher.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: push_back in loop without nearby reserve()
- **AUDIT-PERF-003** [performance] push_back in loop without nearby reserve()
  - Module: Render; Files: src/Render/Mesh/ChunkMeshCache.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: push_back in loop without nearby reserve()
- **AUDIT-PERF-004** [performance] push_back in loop without nearby reserve()
  - Module: Render; Files: src/Render/Mesh/ChunkMeshCache.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: push_back in loop without nearby reserve()
- **AUDIT-PERF-005** [performance] push_back in loop without nearby reserve()
  - Module: Render; Files: src/Render/Mesh/ChunkMeshCache.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: push_back in loop without nearby reserve()
- **AUDIT-PERF-006** [performance] both RebuildChunkImmediate and MarkDirty paths present
  - Module: Render; Files: src/Render/Mesh/ChunkMeshCache.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: both RebuildChunkImmediate and MarkDirty paths present
- **AUDIT-PERF-007** [performance] push_back in loop without nearby reserve()
  - Module: Render; Files: src/World/Core/World.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: push_back in loop without nearby reserve()
- **AUDIT-PERF-008** [performance] push_back in loop without nearby reserve()
  - Module: Render; Files: src/World/Core/World.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: push_back in loop without nearby reserve()
- **AUDIT-PERF-009** [performance] both RebuildChunkImmediate and MarkDirty paths present
  - Module: Render; Files: src/World/Core/World.cpp
  - Action: add reserve() or unify dirty path
  - Evidence: both RebuildChunkImmediate and MarkDirty paths present
- **AUDIT-WORLD-002** [performance] MarkBlockChunkDirty uses RebuildChunkImmediate vs MarkDirty branches
  - Module: World; Files: src/World/Core/World.cpp
  - Action: unify dirty chunk + neighbor propagation
  - Evidence: dual mesh update paths

### P2

- **AUDIT-APP-001** [architecture] UApplication ~1800 LOC combines game loop and all screens
  - Module: App; Files: src/App/Application.cpp
  - Action: extract screen transition helpers
  - Evidence: god-class from baseline
- **AUDIT-APP-002** [architecture] Legacy config shims scattered in Core.cpp
  - Module: App; Files: src/App/Core.cpp
  - Action: group in LegacyConfigAdapter
  - Evidence: legacy_hud, legacy generator warnings
- **AUDIT-ARCH-001** [architecture] UWorld combines persistence, streaming, mesh, creatures
  - Module: World; Files: src/World/Core/World.cpp, src/World/Core/World.h
  - Action: incremental extract UWorldPersistence / UWorldStreaming facades
  - Evidence: ~3500 LOC god class
- **AUDIT-DEAD-001** [dead_code] Possible dead symbol HasChunkJsonFiles
  - Module: unknown; Files: src/World/Core/World.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-002** [dead_code] Possible dead symbol ResolveMovementAxisEye
  - Module: unknown; Files: src/World/Core/World.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-003** [dead_code] Possible dead symbol ApplyFlatDefaults
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-004** [dead_code] Possible dead symbol ApplyHeightmapDefaults
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-005** [dead_code] Possible dead symbol ApplyHillsDefaults
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-006** [dead_code] Possible dead symbol ApplyMountainsDefaults
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-007** [dead_code] Possible dead symbol ApplyOverworldDefaults
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-008** [dead_code] Possible dead symbol ApplyBetaRetroDefaults
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-009** [dead_code] Possible dead symbol CreateFlat
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-010** [dead_code] Possible dead symbol CreateHeightmap
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-011** [dead_code] Possible dead symbol CreateHills
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-012** [dead_code] Possible dead symbol CreateMountains
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-013** [dead_code] Possible dead symbol CreateOverworld
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-DEAD-014** [dead_code] Possible dead symbol CreateBetaRetro
  - Module: unknown; Files: src/WorldGen/Core/WorldGeneratorRegistry.cpp
  - Action: manual verify before removal
  - Evidence: callers=1
- **AUDIT-RENDER-001** [architecture] GeometryEngine ~2600 LOC with World+Gui+Creatures coupling
  - Module: Render; Files: src/Render/Engine/GeometryEngine.cpp
  - Action: enforce Pipeline include rules; incremental backend extraction
  - Evidence: god-class size from baseline

### P3

- **AUDIT-CRE-001** [architecture] glTF backend stub (TD-CRE-001)
  - Module: Creatures; Files: src/Creatures/Visual/CreatureVisualGltf.cpp
  - Action: defer
  - Evidence: open in TECH_DEBT_CREATURES.md
- **AUDIT-PACK-001** [performance] TD-002 incremental atlas rebuild deferred
  - Module: ResourcePacks; Files: docs/TECH_DEBT_RESOURCE_PACKS.md
  - Action: defer; out of scope for quick wins
  - Evidence: open tech debt item
- **AUDIT-TOOL-001** [architecture] Orphan script tools/_gen_members.py
  - Module: tools; Files: tools/_gen_members.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-002** [architecture] Orphan script tools/_gen_struct_fields.py
  - Module: tools; Files: tools/_gen_struct_fields.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-003** [architecture] Orphan script tools/analyze_world_chunks.py
  - Module: tools; Files: tools/analyze_world_chunks.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-004** [architecture] Orphan script tools/analyze_world_chunks_deep.py
  - Module: tools; Files: tools/analyze_world_chunks_deep.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-005** [architecture] Orphan script tools/animated_texture_utils.py
  - Module: tools; Files: tools/animated_texture_utils.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-006** [architecture] Orphan script tools/audit/merge_findings.py
  - Module: tools; Files: tools/audit/merge_findings.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-007** [architecture] Orphan script tools/audit/orchestrate.py
  - Module: tools; Files: tools/audit/orchestrate.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-008** [architecture] Orphan script tools/build_resource_pack.py
  - Module: tools; Files: tools/build_resource_pack.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-009** [architecture] Orphan script tools/convert_shaders_gles.py
  - Module: tools; Files: tools/convert_shaders_gles.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-TOOL-010** [architecture] Orphan script tools/create_stub_cc0_pack.py
  - Module: tools; Files: tools/create_stub_cc0_pack.py
  - Action: archive or document
  - Evidence: no references in CI/docs/scripts/README
- **AUDIT-WG-001** [architecture] ApplyLegacyOverworldProfile shims in ProceduralConfigIO
  - Module: WorldGen; Files: src/WorldGen/Core/ProceduralConfigIO.cpp
  - Action: document; keep for migration
  - Evidence: legacy generator id migration

## Closed Findings

- **AUDIT-RENDER-002** — StreamingHorizonBlocks deprecated but still used (`fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`)
- **AUDIT-TEST-001** — chunk_load_priority_test not in CI (`fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`)
- **AUDIT-WORLD-001** — JSON voxel parsing duplicated across LoadBlocks/LoadChunks (`fca6e21cdcb71f42f9e880bd02cbf3fbccea753e`)

## Recommended PR Sequence

1. **PR-A:** P0 dead code + duplicate includes + auto-fixable style
2. **PR-B:** World/IO — legacy JSON loader extract, chunk dirty helper
3. **PR-C:** Render — fog API cleanup, include hygiene
4. **PR-D:** App/Gui — incremental Application extractions
5. **PR-E:** Perf micro-optimizations with smoke metrics
6. **PR-F:** Documentation sync + CI style gate

## Human Gate

Set `audit/findings.json` → `"status": "approved"` and optional `approved_ids` before Fix agents run.
