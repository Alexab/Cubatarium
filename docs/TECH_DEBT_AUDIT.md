# Tech debt: Code audit 2026

> Tracker for audit pipeline (`tools/audit/`) and PR-A–F backlog. Review after each fix PR.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-AUD-010 | 2026-06 | UWorld god-class (~3400 LOC) | `UWorldCreatureFacade` spawn slice extracted; further slices backlog | partial |
| TD-AUD-011 | 2026-06 | UApplication god-class (~1800 LOC) | `ScreenNavigator` overlay close helpers | partial |
| TD-AUD-012 | 2026-06 | GeometryEngine coupling | `USkyGradientPass` extracted; `UUnderwaterFogPass` prior | partial |
| TD-AUD-014 | 2026-06 | Remaining perf_hints (push_back without nearby reserve) | ChunkMeshCache reserve(512), diagnostics reserve; GreedyMesher done | partial |
| TD-AUD-015 | 2026-06 | Dead-code candidates (callers=1) | P0 World symbols removed; registry FP whitelisted | partial |
| TD-AUD-016 | 2026-06 | Duplicate code clusters (scan_duplicates) | module review done; fixes in backlog | PR-C/D backlog |
| TD-AUD-017 | 2026-06 | Orphan tools/scripts | fix_*.py archived; tools/README + scan_tools_usage improved | closed |
| TD-AUD-026 | 2026-07 | AUDIT-APP-003 UCore god-class | WorldLifecycleFacade + ResourcePackBootstrap + EnterGameWorld | partial |
| TD-AUD-027 | 2026-07 | AUDIT-WORLD-006 World→Render headers | `URenderMeshSink` invalidation counter; read-model on arch_refactor3 | partial |

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
| TD-AUD-028 | 2026-07 | Touch controls render/input pass above palette preview; `HitTestTouchControls` priority; `RenderTouchControlsOverlay` |
| TD-AUD-029 | 2026-07 | Main menu BACK shows quit confirmation (removed ResumeGame on ESCAPE); inventory/world chain unchanged |
| TD-AUD-030 | 2026-07 | Per-pointer `ReleaseJoystickCaptureForPointer`; multitouch test AND-16 |
| TD-AUD-031 | 2026-07 | Android: deferred `WarmupGreedyGpuFromWorld` (5 frames), load chunk budget 4/frame |

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
- `A-wave0` (2026-07-07): per-pointer joystick release, touch controls above palette, EGL stencil + GLES fluid fallback.
- `A-wave1` (2026-07-07): BACK→quit on main menu, deferred GPU warmup, reduced load chunk budget on Android.
- `A-wave2` (2026-07-07): architecture partial (`2d02a7a`), creatures TD-CRE-034/035 (`3de5d0f`, `f57e383`), fluids TD-FL-034 v2 flag (`f2e8a26`).
- Automated gate (2026-07-07 @ `f2e8a26`): `python tools/audit_style.py` 0 violations; `validate_gltf_creature.py --skinned-only` 33/33; `test_gltf_skinned_bind_pose.py` 33/33. C++ targets `docked_overlay_layout_test`, `touch_input_bridge_lifecycle_test`, `fluid_surface_map_logic_test` — run in CI/desktop build before release.
- **Manual sign-off (2026-07-07):** single Android device — AND-01..16, AND-13..15 PASS; **AND-17 FAIL** (no sea surface film on GLES). Profiles B/C/E N/A. Gate **BLOCKED** until AND-17 fixed — см. [`QA_ANDROID_2026.md`](QA_ANDROID_2026.md).

### TD-AUD-028..031 manual sign-off (2026-07-07)

| Gate | Status |
|------|--------|
| AND-01..04 layout (profiles A, B, C, E) | [X] PASS [ ] FAIL — B/C/E N/A single device |
| AND-05..08 Back flow | [X] PASS [ ] FAIL |
| AND-09..12, AND-16 joystick lifecycle | [X] PASS [ ] FAIL |
| AND-17 sea surface (EGL stencil) | [ ] PASS [X] FAIL |
| AND-13..15 startup/load (profile D) | [X] PASS [ ] FAIL |
| `touch_input_bridge_lifecycle_test` (CI) | [ ] PASS [ ] FAIL |
| `docked_overlay_layout_test` (CI) | [ ] PASS [ ] FAIL |

- Tester: manual (single Android device)
- APK commit: `arch_refactor3` @ `f2e8a26`
- Date: 2026-07-07
- TD-AUD-028..031 release gate: [ ] CLOSED [X] BLOCKED — AND-17 GLES sea surface; reopen TD-FL-034 Android slice

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
