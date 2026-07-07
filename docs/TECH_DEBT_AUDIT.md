# Tech debt: Code audit 2026

> Tracker for audit pipeline (`tools/audit/`) and PR-A–F backlog. Review after each fix PR.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-AUD-010 | 2026-06 | UWorld god-class (~3400 LOC) | `UWorldFluidFacade` extracted (TryAddFluidObject, break-site flood); further slices backlog | partial |
| TD-AUD-011 | 2026-06 | UApplication god-class (~1800 LOC) | screen helpers extract | PR-D backlog |
| TD-AUD-012 | 2026-06 | GeometryEngine coupling | UUnderwaterFogPass extract; LOC 2012 (−139 vs baseline 2151); `#include App/Core.h` removed from GeometryEngine | partial |
| TD-AUD-014 | 2026-06 | Remaining perf_hints (push_back without nearby reserve) | ChunkMeshCache reserve(512), diagnostics reserve; GreedyMesher done | partial |
| TD-AUD-015 | 2026-06 | Dead-code candidates (callers=1) | P0 World symbols removed; registry FP whitelisted | partial |
| TD-AUD-016 | 2026-06 | Duplicate code clusters (scan_duplicates) | module review done; fixes in backlog | PR-C/D backlog |
| TD-AUD-017 | 2026-06 | Orphan tools/scripts | fix_*.py archived; tools/README + scan_tools_usage improved | closed |
| TD-AUD-026 | 2026-07 | AUDIT-APP-003 UCore god-class | WorldLifecycleFacade + ResourcePackBootstrap + EnterGameWorld | partial |
| TD-AUD-027 | 2026-07 | AUDIT-WORLD-006 World→Render headers | RenderMeshSink wiring; mesh cache still in render path; remediation DoD metrics in REMEDIATION_BASELINE_METRICS | partial |
| TD-AUD-028 | 2026-07 | Android inventory preview overlaps control buttons | On small/varied resolutions right-side preview occludes close/menu controls; user cannot close inventory/menu reliably | partial |
| TD-AUD-029 | 2026-07 | Android Back button navigation flow is inconsistent | Back should close inventory; in-world should open main menu; in main menu should request app exit | partial |
| TD-AUD-030 | 2026-07 | Android left joystick can stick in move state after finger release | Likely pointer-up outside joystick hit area; movement remains active and joystick does not recenter | partial |
| TD-AUD-031 | 2026-07 | Android may briefly show ANR-like freeze dialog on startup/world load | Transient watchdog stall near end of load/generation; dialog disappears but UX regression remains | partial |

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
| TD-AUD-025 | 2026-07 | Legacy `I*` interfaces → `IU*` (PR-IU-1..6): platform, world IO, worldgen, creatures, GUI, render/progress |

## Android UX acceptance notes

### TD-AUD-028 inventory controls vs preview
- Repro: open inventory on Android (multiple aspect ratios); preview panel hides close/menu controls.
- Close when: controls are always visible and operable (either guaranteed layout fit or controls rendered/clickable above preview) across supported resolutions.

### TD-AUD-029 Back button flow
- Repro: press system Back in inventory/in-world/main-menu states.
- Close when:
  - in inventory: closes inventory,
  - in world: opens main menu,
  - in main menu: shows exit confirmation and exits only after confirmation.

### TD-AUD-030 left joystick stuck movement
- Repro: drag left joystick and release finger near/outside joystick zone; movement sometimes continues.
- Close when: pointer release/cancel always resets joystick state to neutral and movement stops immediately.

### TD-AUD-031 startup freeze dialog
- Repro: cold app start and/or world generation; transient system "app not responding/frozen" dialog may appear.
- Close when: dialog no longer appears in startup/load path under normal load scenarios, confirmed on target Android devices.

## Execution progress (2026-07-07)

- `P0.1/P0.2` docs normalization + baseline refresh: commit `15bbb00`.
- `A1..A4` Android blockers first pass: commit `4836d01`.
  - Back key routing wired from Android key events to `GLFW_KEY_ESCAPE`.
  - Touch routing migrated to stable `pointer id` in motion path.
  - Overlay dock switches to single-pane on narrow/short viewports.
  - Android enter-game warmup de-blocked (non-blocking path).
- `S1` world-render read-model groundwork: commit `fdcf6d8`.
- `R1` icon cache manifest/diagnostics hardening: commit `57ef029`.
- `A-test` Android automated gate (2026-07-07): `docked_overlay_layout_test`, `touch_input_bridge_lifecycle_test`, `docs/QA_ANDROID_2026.md`; joystick fail-safe on Hud pointer release + motion cancel.
- Manual Android matrix (`AND-01..AND-15`) still required before closing `TD-AUD-028..031`.

## Phase tracker

| Phase | Status | Notes |
|-------|--------|-------|
| Infra + scan | done | orchestrate.py --phase all |
| PR-A dead code | done | committed fca6e21 |
| PR-B legacy loader | done | committed 0815e34 |
| PR-E perf reserve | partial | GreedyMesher only |
| PR-F docs + CI | done | style gate; clang-format on diff |
| Module agents ×8 | done | 71 findings in audit/modules/ (manual pass 2026-06-27) |
| Human gate | approved | P0 fixes applied; arch refactor phases 0–5 landed 2026-07 |
| Remediation DoD | done | 2026-07-05: FluidSpread coordinator 183 LOC, audit_style 0, block migration script applied, QA runbook + CI surface-map test |
