# Tech debt: Chunk streaming & performance

> Review at end of each phase (A–E). Close items when implemented or explicitly wont-fix.

## Profiling bisect

1. Enable HUD: `ui.show_performance: true` in `config.json`.
2. Watch `Gen`, `Mesh`, `Flat`, `Dirty`, `Cache` lines while flying through forest / after load.
3. Toggle flags one at a time in `render`: `async_meshing`, `greedy_meshing`, `frustum_culling`, `distance_fog`, `altitude_adaptive_fog`.
4. Toggle `procedural.async_chunk_generation`, `procedural.async_chunk_io`, `max_chunk_commits_per_frame`, `max_load_ops_per_frame`.
5. Export: `worlds/World_NNN/movement_diagnostics.json` (schema `movement_diagnostics.v2`).
6. CI: `python tools/smoke_worldgen_metrics.py --metrics-json <path>`.

**Async defaults (2026-06):** `ProceduralSettings.AsyncChunkGeneration` and `AsyncChunkIo` default **true** in code and `bin/config.json`. TD-CS-011/012 doc drift corrected; headless worldgen CLI still forces sync generation for determinism.

**Ring gate (TD-CS-017):** `procedural.ring_gate_enabled` in `config.json` (default **false**). Applied on streamer creation in `UWorldStreaming::EnsureStreamer` and when settings refresh via `RefreshStreamerSettings` → `UChunkStreamer::SetRingGateEnabled`.

**MarkBlockChunkDirty contract (TD-AUD-013):** when `BlockRegistry != nullptr` (normal gameplay), block edits call `MeshCache.RebuildChunkImmediate` on the chunk + neighbors; during headless load before registry init, `MarkDirty` defers rebuild to the frame budget / async mesh path.

**In-game validation (async mesh):** with `ui.show_performance: true`, fly through loaded terrain; toggle `render.async_meshing` in `config.json` and compare `mesh_rebuild_ms` / hitch lines in HUD. Export `movement_diagnostics.v2` before/after — each sample includes `async_meshing_enabled` for bisect correlation. HUD labels: `MeshAsync` (mesh in-flight) vs `GenQ` (pending/in-flight chunk generation).

**Threading audit:** see [`docs/THREADING_AUDIT.md`](THREADING_AUDIT.md) for the inventory of worker pools, cross-thread artifacts, and pattern verdicts.

**Crash / logs:** run output lives under `bin/logs/` (glog per-run files + `.dmp` minidumps). See that folder after AV / terminate.

**Cross vegetation (TD-CS-014):** cross blocks store per-chunk centers in `ChunkMeshCache`; flat merge builds `CrossInstanceBatch[]`; `GeometryEngine` draws via `UCrossGpuBackend` (one `glDrawElementsInstanced` per block type).

**Perf regression fix (2026-07):** `RebuildDirtyChunks` no longer marks flat batches dirty every frame; GPU backends use `glBufferSubData` when buffer capacity suffices (no per-frame orphan); render uses single `PrepareGreedyDraw` per frame.

**Load-path freeze fix (2026-07):** cooperative load no longer finishes mesh warmup with empty dirty-set (`MarkAllDirtyFromWorld` after relight); см. TD-CS-022 (closed).

**Load freeze at 80% (2026-07):** бюджетная фаза `RelightColumns` + async column relight при `async_relight`; см. TD-CS-024 (closed).

**Movement hitch mitigation (2026-07):** sync collision column gen budgeted to 32 sub-columns/frame; relight/io drained with per-frame caps. **Phase F partial (2026-07-11):** deferred physics seed queue, reduced seed budgets (8 cols / 128 liquid per commit), adaptive mesh/commit budgets from `LastMovementFrameMs`. Остаются краткие hitch — см. TD-CS-021.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-CS-021 | 2026-07 | Краткие hitch при движении (~каждые 16 блоков) при генерации/выгрузке/save чанков | Phase F partial (2026-07-11): deferred physics seed, adaptive mesh budgets. **Partial terrain column save (2026-07-11):** incomplete sync/async columns could be persisted on unload → holes on reload; fixed by gating `SaveTerrainColumn` on `IsTerrainChunkComplete` + async regen for partial in-memory columns | Phase F remainder: relight budget per commit, full async collision |
| TD-CS-016 | 2026-06 | Persistent GPU VBO / vertex pooling | Nick McDonald-style pool; large refactor | backlog |
| TD-CS-018 | 2026-06 | Incremental frustum-only greedy cull without full flat merge | camera-chunk skip + `LastVisibleChunks` cache; full incremental cull deferred | partial |

## TD-ARCH (V2–V5 completion) — Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-ARCH-011 | R0 | blue_screen / opaque_on_min residual after E1 | Edge still sees opaque_on_min=0 under load; draw blank fixed for meshed+Pending (016) | backlog |
| TD-ARCH-013b | R4/tail | Android GLES compute skylight seed | Desktop F2/C/CB not GO; CPU seed remains | backlog |
| TD-ARCH-015 | R0/S2 | Worker-side Capture band | Worker Capture hung edge_S1 (world races); step A cruise ≤1 Capture kept | backlog |

### Approach FPS plan — S0 baseline (2026-07-29)

Hypothesis: `IsColumnRenderReady` early-outs on `PendingLight` → blank FOV while
`HasMissingGreedyMesh` stays false (mesh already present). MeshAsync≈1–2 under
Dirty 200–300 starves first-mesh. Capture on main drives emerge spikes.

| Report | run_outcome | wall_med | dirty | holes | opaque_on_min | blue_screen | F2 | C | CB |
|--------|-------------|----------|-------|-------|---------------|-------------|----|----|-----|
| `manual_approach_0852.json` (raw play) | n/a | 94 | 196 | 0.73 | 126 | 0 | — | — | — |
| `edge_S0.json` | success | 78 | 389 | 0.71 | 2 | 0 | NO-GO cold=8 | NO-GO spike=562 cold=8 | NO-GO |
| `manual_S0.json` | success | 52 | 647 | 0.81 | 0 | 1 | NO-GO sticky=5 cold=14 | NO-GO | — |

Debt pass S0: closed none; opened TD-ARCH-016; baseline autofly recorded.

### S1–S3 code (2026-07-29)

- **S1:** `IsColumnRenderReady` — PendingLight + any greedy in visual band → draw
  (closes TD-ARCH-016 blank FOV). FirstMesh admit also on `missing_visible_mesh`
  while cruise.
- **S2:** Cruise Capture hard-cap ≤1 + tighter budgets (3–6ms). Worker Capture
  attempted → hang; reverted; TD-ARCH-015 stays backlog.
- **S3:** Stop DropRemesh keep_h=2 when `focus_dirty>280` and holes/pending clear.
- **S4:** TD-ARCH-013b remains backlog (desktop F2/C/CB not GO).

Autofly note: after S0, machine load made World_164 load+flight wall multi-second
(`hang_killed` on control revert too). Re-run edge/manual when host is cool;
gates still expected NO-GO until cold_relight/spike drop.

`edge_S1.json` (post-fix, run_outcome=success, host overloaded): wall_med=1034,
holes=1.0, opaque_on_min=0, blue=1, cold=10, spike_holes=3859, chunks=3;
F2/C NO-GO. Compare to healthy `edge_S0` wall=78 — re-validate on cool host.

## TD-ARCH — Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-ARCH-016 | S1 | PendingLight no longer blanks meshed columns in IsColumnRenderReady |
| TD-ARCH-001 | R1 (+tails) | Cruise+CanSeed+healthy → cheap sync seed (budget≤2ms / ApplyGpuSkylightSeed); hot cruise → FIFO; SeedDecisionPolicy owns policy |
| TD-ARCH-002 | R1 | Removed `(void)seed`; `seed.applied` → LitReady else PendingLight; backends return applied only when Relight ran (budget≤0 early-out) |
| TD-ARCH-004 | R2 | `ColumnFlowExecutor` DrainBudget uses `item.column` filter on Admit/Recover |
| TD-ARCH-005 | R2 | Emerge Admit/Recover/Promote/DrainIdle routed through executor only |
| TD-ARCH-006 | R2 | Deleted `RecoverStickyBlackFocusSync`; added `IsColumnStickyRemesh` |
| TD-ARCH-007 | R3 (+tails) | Visual SLA: FocusIngress unfinished + PrefetchKeepShell skips !VisualReady; RingPrerequisitesMet voxels-only |
| TD-ARCH-008 | R3 | Cruise unfinished sampled every 8f + dirty/pending proxy |
| TD-ARCH-009 | R3 (+tails) | MemoryBudget soft-cap dirty/pending; WorldStreaming TrimPendingLight under dirty>400+pending>8 |
| TD-ARCH-003 | R4 | Gpu seed uses ApplyGpuSkylightSeedToChunk; factory SelectLightingSeedBackend |
| TD-ARCH-010 | R5 | Idle pending Capture progress when holes=0 + inflight==0 + wall<160 |
| TD-ARCH-013 | R4 | Android PreferGpuLightingSeed=false → Cpu same contracts |
| TD-ARCH-014 | R6 | Deleted CollectAllOpaqueCutoutRefs dead API |
| TD-ARCH-012 | R7 | Docs frozen honest: V2–V5 architecture complete; F2/C/CB gate DoD remains backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-CS-001 | 2026-06 | `MarkDirty` bumps revision only when chunk newly enters dirty set |
| TD-CS-002 | 2026-06 | Deferred `AccumulateDirtyColumn` + chunk-slice `MarkDirtyColumn` iteration |
| TD-CS-003 | 2026-06 | `MovementDiagnostics` exposes gen/mesh/io/dirty breakdown |
| TD-CS-004 | 2026-06 | `UAsyncMeshBuilder` + `RenderSettings.AsyncMeshing` |
| TD-CS-005 | 2026-06 | `UChunkLoadScheduler` + sync collision ring (`forceSync`) |
| TD-CS-006 | 2026-06 | `UAsyncChunkIO` background save/load with main-thread commit |
| TD-CS-007 | 2026-06 | Cave carve early-continue + chunk-level cave density gate in populator |
| TD-CS-008 | 2026-06 | `MeshCache` removed from `WorldGenContext`; dirty on commit/streamer |
| TD-CS-009 | 2026-06 | `UChunkGenerationRegistry` + token validation on async results |
| TD-CS-013 | 2026-06 | Smoke reads `movement_diagnostics.v2` with flat/count/backlog budgets |
| TD-CS-015 | 2026-06 | Cooperative cancel in `PipelineChunkPopulator` via `shouldCancel` token check |
| TD-CS-014 | 2026-07 | `UCrossGpuBackend` retained instance VBO + `glDrawElementsInstanced` per block type |
| TD-CS-010 | 2026-07 | Async meshing default on; bisect documented; diagnostics export `async_meshing_enabled` |
| TD-CS-020 | 2026-06 | Dedupe mesh dirty on async chunk commit (`ColumnMeshDirty` only) |
| TD-CS-022 | 2026-07 | «Только небо» / мгновенный mesh warmup при загрузке (пустой dirty-set после `MarkAllDirty`) | `MarkAllDirtyFromWorld` после relight; diag `[WorldLoad]`; CLI `--load-world` / `--enter-game-smoke` |
| TD-CS-023 | 2026-07 | Multi-second freeze при sync collision gen (256 sub-columns + relight/column) | Incremental `AdvanceTerrainColumnGeneration`, deferred relight batch, budgeted async IO relight queue |
| TD-CS-024 | 2026-07 | Freeze на ~80% cooperative load (sync `RelightTerrainColumn` для всех колонок в одном tick) | Бюджетная фаза `RelightColumns`, async path через `UAsyncRelightBuilder`, mesh warmup progress creep |
| TD-CS-019 | 2026-07 | Sky horizon fog uses fixed screen band; ignores view direction and celestial tint | Radial `fshader_sky` fog + `HorizonFogColor` + `horizon_fog_radial` / `horizon_fog_celestial_tint` |

## Phase tracker

| Phase | Status | Last commit |
|-------|--------|-------------|
| Setup | done | docs: add chunk streaming tech debt tracker |
| A | done | feat(diag): add gen/mesh/io timing breakdown |
| B | done | feat(mesh): integrate async mesh builder |
| C | done | feat(streaming): wire scheduler into chunk streamer |
| D | done | feat(io): async chunk save on unload |
| E | done | perf(mesh): skip empty sky layers in greedy mesher |
| Metrics | done | test: extend smoke_worldgen_metrics with latency budgets |
| Perf2 | done | perf/streaming: mesh warmup, priority, altitude fog, cross cutout |
