# Tech debt: Code audit 2026

> Tracker for audit pipeline (`tools/audit/`) and PR-A–F backlog. Review after each fix PR.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-AUD-010 | 2026-06 | UWorld god-class (~3400 LOC) | incremental facade extract | PR-D backlog |
| TD-AUD-011 | 2026-06 | UApplication god-class (~1800 LOC) | screen helpers extract | PR-D backlog |
| TD-AUD-012 | 2026-06 | GeometryEngine coupling | Pipeline include rules first | PR-C backlog |
| TD-AUD-014 | 2026-06 | Remaining perf_hints (push_back without nearby reserve) | ChunkMeshCache reserve(512), diagnostics reserve; GreedyMesher done | partial |
| TD-AUD-015 | 2026-06 | Dead-code candidates (callers=1) | P0 World symbols removed; registry FP whitelisted | partial |
| TD-AUD-016 | 2026-06 | Duplicate code clusters (scan_duplicates) | module review done; fixes in backlog | PR-C/D backlog |
| TD-AUD-017 | 2026-06 | Orphan tools/scripts | fix_*.py archived; tools/README + scan_tools_usage improved | closed |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-AUD-001 | 2026-06 | `tools/audit/` orchestrator + 7 scanners + merge_findings |
| TD-AUD-002 | 2026-06 | P0 dead code: SaveBlocks/SaveChunks/GetStreamingHorizonBlocks removed |
| TD-AUD-003 | 2026-06 | Duplicate `#include ChunkStorageService.h` removed from World.cpp |
| TD-AUD-004 | 2026-06 | `ULegacyChunkJsonLoader` extracted from LoadBlocks/LoadChunks |
| TD-AUD-005 | 2026-06 | U-prefix class renames + GuiTouchControls PascalCase (audit_style 0) |
| TD-AUD-006 | 2026-06 | `audit_style.py` + `chunk_load_priority_test` in Windows CI |
| TD-AUD-007 | 2026-06 | Docs: ARCHITECTURE, CODING_STYLE, PERFORMANCE_OPTIMIZATION, AUDIT_* |
| TD-AUD-008 | 2026-06 | GreedyMesher `quads.reserve(512)` in BuildChunkMesh hot paths |
| TD-AUD-009 | 2026-06 | StreamingHorizonBlocks deprecated API removed |
| TD-AUD-020 | 2026-06 | `audit_clang_format.py` (changed-files check) in CI |
| TD-AUD-022 | 2026-06 | P0: HasChunkJsonFiles, ResolveMovementAxisEye, Cube_GLM.h removed |
| TD-AUD-023 | 2026-06 | IsGameDataRoot deduped into GameDataRoot.cpp |
| TD-AUD-024 | 2026-06 | ChunkMeshCache GreedyBatches.reserve in flat rebuild |
| TD-AUD-013 | 2026-06 | MarkBlockChunkDirty dual path documented (immediate vs deferred) |
| TD-AUD-018 | 2026-06 | spawn_fire_blocks_max=8; seed 42 decorative fire documented |
| TD-AUD-019 | 2026-06 | tree_bark in cubatarium_cc0_base for merge smoke |

## Phase tracker

| Phase | Status | Notes |
|-------|--------|-------|
| Infra + scan | done | orchestrate.py --phase all |
| PR-A dead code | done | committed fca6e21 |
| PR-B legacy loader | done | committed 0815e34 |
| PR-E perf reserve | partial | GreedyMesher only |
| PR-F docs + CI | done | style gate; clang-format on diff |
| Module agents ×8 | done | 71 findings in audit/modules/ (manual pass 2026-06-27) |
| Human gate | approved | P0 fixes applied; backlog PR-D/E open |
