# Tech debt: Code audit 2026

> Tracker for audit pipeline (`tools/audit/`) and PR-A–F backlog. Review after each fix PR.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-AUD-010 | 2026-06 | UWorld god-class (~3400 LOC) | incremental facade extract | PR-D backlog |
| TD-AUD-011 | 2026-06 | UApplication god-class (~1800 LOC) | screen helpers extract | PR-D backlog |
| TD-AUD-012 | 2026-06 | GeometryEngine coupling | Pipeline include rules first | PR-C backlog |
| TD-AUD-013 | 2026-06 | MarkBlockChunkDirty dual RebuildChunkImmediate/MarkDirty | intentional when BlockRegistry null | review |
| TD-AUD-014 | 2026-06 | Remaining perf_hints (push_back without nearby reserve) | micro-opt; GreedyMesher partial reserve | PR-E backlog |
| TD-AUD-015 | 2026-06 | Dead-code candidates (callers=1) | manual verify before removal | backlog |
| TD-AUD-016 | 2026-06 | Duplicate code clusters (scan_duplicates) | needs module agent review | backlog |
| TD-AUD-017 | 2026-06 | Orphan tools/scripts | archive or document | backlog |
| TD-AUD-018 | 2026-06 | integration_test_worldgen fire_blocks threshold | pre-existing smoke fail | separate PR |
| TD-AUD-019 | 2026-06 | smoke_resource_packs tree_bark missing | pre-existing asset gap | separate PR |

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

## Phase tracker

| Phase | Status | Notes |
|-------|--------|-------|
| Infra + scan | done | orchestrate.py --phase all |
| PR-A dead code | done | committed fca6e21 |
| PR-B legacy loader | done | local, uncommitted |
| PR-E perf reserve | partial | GreedyMesher only |
| PR-F docs + CI | done | style gate; clang-format on diff |
| Human gate | pending | set audit/findings.json status approved |
| Module agents ×8 | seed only | full module JSON review deferred |
