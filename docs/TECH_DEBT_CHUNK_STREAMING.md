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
| TD-ARCH-015 | R0/S2 | Worker-side Capture band | Main `UMeshCaptureStore` (Phase 1 offload): schedule uses store; MarkRelit prefetches; worker Capture still deferred until store-only consumer | done 2026-08-01 (store contract; worker path not re-enabled) |
| TD-ARCH-017 | Phase0 | sim_ms double-counted stream+emerge inside phys_ms | Fixed: sim_ms now uses do_movement_ms (includes stream+emerge); old formula was phys_ms+stream+emerge+view+scene causing negative unacc | done 2026-07-29 |
| TD-ARCH-018 | Phase0 | unacc=240ms in tail frames (emerge=0, scene=8ms) | render_total_ms added; sim_ms uses it; residual now = wall-sim-swap-world_extra; needs runtime verification | in-progress |
| TD-ARCH-019 | Phase3a | GPF1 pipeline reads back rects+verts via glGetBufferSubData | GpuMeshPipeline wired into ChunkMeshCache+GeometryEngine; legacy readback remains fallback when GpuPackedMeshing=false or ProcessSnapshot fails | done 2026-07-29 |
| TD-ARCH-020 | Phase3g | GLES 3.1 lacks glMultiDrawElementsIndirect | Per-chunk glDrawElementsIndirect fallback added; workgroup=64 for GLES compat | done 2026-07-29 |
| TD-ARCH-021 | manual_1645 | Post-load empty mesh ring after EnterGame | Warmup/burst + SoftDefer Capture SLA telemetry (`softdefer_capture_*`, idle pending delta); gate `post_load_ring_idle_max=0` | done 2026-07-29 (measurable; spawn sticky closed via SoT draw) |
| TD-ARCH-022 | manual_1645 | Dark/sticky faces in rendered columns | Hide sticky/stale-dark r>1; nearest-hole r≤1 | done 2026-07-29; **semantics superseded by 026** |
| TD-ARCH-023 | manual_1645 | Horizon fog flicker vs unfinished mesh | Fog hole_debt includes pending_gpu; ahead margin; expand ramp 2.5s | done 2026-07-29 |
| TD-ARCH-024 | manual_1645 | Emerge spikes moving+holes | Cap mesh_schedule≤6 when async<4 (caused underfeed) | done 2026-07-29; **replaced by 027 floor** |
| TD-ARCH-025 | qual_fix3 | Cruise holes_rate regressed vs qual_fix2 | SoT UnfinishedVisual (no pending-proxy); holes≈0.05–0.09 | done 2026-07-29 — evidence `manual_arch_d3_live.json` |
| TD-ARCH-026 | Era13 | Hide⇒RepairTicket via ColumnFlow | draw-when-meshed + near≤2 RelightThenMesh/Remesh; unit SoT+Contains | done 2026-07-29 |
| TD-ARCH-027 | Era13 | Async throughput floor for FOV unfinished | SoftDefer **AllowUnlitFirstMesh** SoT predicate (r≤3\|\|nearest); `mesh_async_med_when_dirty≥4` | done 2026-07-29 — evidence `manual_arch_d3_live.json` |
| TD-ARCH-028 | Era13 | ColumnRenderable single SoT | Draw/telemetry from one state API | done 2026-07-29 (D2a) |
| TD-ARCH-029 | Era13 | FirstMesh vs Remesh dirty classes | FirstMesh must not starve behind remesh thrash | done 2026-07-29 (D2a) |
| TD-ARCH-030 | Era13 | SoftDefer Capture/relight floor | SoftDefer → ColumnFlow FirstMesh/RelightThenMesh (cap 1); no `bg_budget=max(floor)` thrash | done 2026-08-01 (Phase 3 offload) |
| TD-ARCH-031 | manual_1957 | Older mesh apply orphaned Active → remesh thrash | Discard-older keep-Active; GPU pending without Active drops without Dirty | done 2026-07-29 |
| TD-ARCH-032 | Era13 | ARCH_D1/D3 harness GO | Architecture A–E landed. Autofly×2 `--replay-manual`: `manual_arch_era13_01/02.json`. **cold_relight=2≤3 OK**; holes/async OK on 02. **D3 NO-GO:** `wall_ms_med≈44` (need ≤30), `post_stop_black_sticky_max≈9`. Stop SoftDefer zoo after 2 iters. | in-progress |
| TD-ARCH-033 | Era13/rim | Frontier first-mesh latency (manual 225337) | Stage SLA + UnlitFirstMesh + sync promote | partial — confirm on World_164 edge smoke |

**2026-08-05 perf_opt3:** `NearFocusHoles` telemetry no longer ORs pending-light debt (kept in `LightDebt`); reduces false `nh_no_miss_rate` when SoftDefer remesh-until-lit with mesh present. Admission still uses pending-light urgency locally.

**perf_opt3 closeout (2026-08-05):** P0–P2/P4 code landed. Residual gaps closed:
`gpu_cull_gpu_ms`; cull-stats SubData once/period + ShowPerformance; frustum
revision on flat-skip; idle FirstMesh admit=3 when missing. P3 ARCH_D3_LAND /
cruise rim still gate-residual (no SoftDefer zoo). DoD wall≤40 deferred.

### FOV/progressive plan (2026-08) — Closed (P4 validate)

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-ARCH-034 | 2026-08 FOV plan | Far-rim nh≥4 dual-backlog (manual 215629 mh=5) | Hotfix `9cfe265a`; smoke PASS; mid residual still needs fresh manual vs 215629 | **done** 2026-08-02 (mid residual open → user manual) |
| TD-ARCH-035 | 2026-08 FOV plan | FOV idle FirstMesh / camera-front priority | `b6b75a5f` + `2ef3711f`; smoke PASS | **done** 2026-08-02 |
| TD-ARCH-036 | 2026-08 FOV plan | Per-cy draw gate (column all-or-nothing) | `bd4e0356`; smoke PASS | **done** 2026-08-02 |
| TD-ARCH-037 | 2026-08 FOV plan | Soft flight speed clamp on underfeet/near ahead miss | `4abd8683` ×0.85 underfeet / HoleDrain nh≤1 ahead; smoke miss_end=0 | **done** 2026-08-02 |
| TD-ARCH-038 | 2026-08 FOV plan | Fog knobs follow-up | P4: autofly holes≈0.27 but mid `215629` rim_ok=false (nh≥4); no Fog knobs without miss≤0.45 **and** rim hold | **open** backlog |
| TD-ARCH-039 | 2026-08 FOV plan | Sub-16 mesh brick | P2 closed visual progressive DoD without sub-16 | **wont-fix** 2026-08-02 |

### TD-ARCH Era14 (V4) — Open

> Executive: [`streaming/ROOT_CAUSE_2026-08.md`](streaming/ROOT_CAUSE_2026-08.md),
> [`streaming/ERA14_POSTMORTEM.md`](streaming/ERA14_POSTMORTEM.md).
> Evidence baseline: `manual_latest_151212` (wall~200, phys~172 nest, miss sticky~38s).

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-ARCH-040 | Era14 | Frame nest: stream/emerge inside `RunLegacyPhysicsFrame` / `do_movement_ms` | Nest proof fly-clean phys_med≈4.9 | **done 2026-08-07** `0812c77f` |
| TD-ARCH-041 | Era14 | Deadlock calm-wall Imm / stale-wave enqueue (`wall≤40/50`) | Dirty/promote without wall; Imm stays budgeted; sticky remesh budget on hot wall | **done 2026-08-07** (Imm primary DISCARD) |
| TD-ARCH-042 | Era14 | Stand/cruise sticky Imm fork zoo | Imm primary removed; Dirty@≥1–2 + PreferKick | **done 2026-08-07** |
| TD-ARCH-043 | Era14 | Land tops miss sticky / `ARCH_D3_LAND` | Era16 `era16_p2_land` holes≈0.04 miss_stuck=2 sticky=0 no_ticket=0 wall≈52 | **done 2026-08-08** ARCH_D3_LAND GO |
| TD-ARCH-044 | Era14 | Commit-time seed coverage / PendingLight trail | SeedDecision cruise≤32 / idle≤28 cheap seed | **done 2026-08-07** `f9af0c16`+iterate |
| TD-ARCH-045 | Era14 | UnlitFirstMesh → guaranteed remesh-on-lit | MarkRelit → NoteColumnRepairNeeded + RemeshSeam | **done 2026-08-07** `f9af0c16` |
| TD-ARCH-046 | Era14 | Worker Capture residual (TD-ARCH-015 store done) | Store refresh budget tightened; worker path still deferred (hang risk) | **partial** — worker deferred |
| TD-ARCH-047 | Era14 | IdleRecovery/Admission knobs duplicate DesiredStage | Sticky remesh wall-skip removed; IdleRecovery owns sync cost only | **done 2026-08-07** |
| TD-ARCH-048 | Era14 | ARCH_D3 wall_med≤30 / gate DoD | Era17 ocean `era17_p3_ocean` wall≈126 holes≈0.48 (ARCH_D3 soft NO-GO); land ARCH_D3_LAND GO | **partial** — ocean wall/holes open |
| TD-ARCH-049 | Era15 | MeshResidency: FreeChunk-before-replace flicker | CPU Apply/Immediate publish batches before FreeChunk; `mesh_replace_hole_avoided` telem; FLY_CLEAN GO `era15_p1_fly` | **done 2026-08-08** `327dd006`+ |
| TD-ARCH-050 | Era15 | ColumnPublication Unlit→Lit + SoftDeferHeld∥ColumnFlow | LitPending on Unlit FirstMesh; sticky_r≠gate MarkRelit; SoftDeferHeld→FirstMesh ticket; DesiredStage lit_pending/unlit; IDLE_CLEAN/WARM GO | **done 2026-08-08** `327dd006`+ (LAND holes residual via 043/051) |
| TD-ARCH-051 | Era15 | FirstMesh-until-Drawable / PreferKick Kicked stall | PreferKick Queued+Kicked; Era16 land holes≈0.04 ARCH_D3_LAND GO | **done 2026-08-08** via Era16 P2 |
| TD-ARCH-052 | Era16 | VisibleBlack SoT / black_sticky≠user black | `CountVisibleBlackFocusMeshes`; honest StaleDark; Hide⇒Ticket RemeshSeam; IDLE_CLEAN+ARCH_D3_LAND no_ticket=0 | **done 2026-08-08** P0–P3 |
| TD-ARCH-053 | Era17 | Heal-until-predicate / ticket≠progress | Real Contains ticket; Progress/Stalled telem; void RelightThenMesh; FirstMesh class | **done 2026-08-08** P0–P2 |
| TD-ARCH-054 | Era18 | Focus light-debt / VB without PendingLight | Void RecoverUnlit⇒NotePendingLight; drain/capture floors while VB>0; manual 165953 | **partial 2026-08-08** light path OK; FPS/miss fixed via TD-055 autofly; manual eye pending |
| TD-ARCH-055 | Era19 | FrameStreamingBudget / heal-on-hot feedback | Era18 `max` floors force Capture/VB spend on hot wall → wall↑ holes↑ miss↑ (`191229`) | **partial 2026-08-08** FrameStreamingBudget + miss-first; autofly matrix GO; manual 191229-class re-flight pending |

> **Era19 FrameStreamingBudget closeout (2026-08-08):** Unified `FrameStreamingBudget`
> (hot_frame_ms=80 shrink; miss-first Capture≤1 FirstMesh; calm pending mid-floor).
> Kill-switches: `era18_vb_capture_floor`, `era18_vb_bg_budget_floor`,
> `miss_first_frame_budget`. P2: PendingLight vs Remesh exclusivity.
> Autofly: `era19_p3_fly`/`era19_p3_warm` FLY/IDLE_WARM GO; `era19_p3b_idle` /
> `era19_p1c_idle` IDLE_CLEAN GO; `era19_p1_land2` ARCH_D3_LAND GO
> (land matrix flaky opaque/miss — use land2 SoT). Baseline regression `191229`
> (`heal_on_hot_sec` soft_fail). Gap CLOSED for autofly; **manual eye** on
> 191229-class corridor still required before merge claim.
> Keep NotePendingLight / Contains ticket / FirstMesh class. Reject heal-floors-on-hitch.

> **Era19 FrameStreamingBudget (2026-08-08):** Manual `perf_20260808-191229` —
> heal-on-hot SoT (`wall_med` 279, holes 0.57, miss_stuck 40s). Fixed by
> FrameStreamingBudget; do not claim visual merge without new manual log.

> **Era18 focus light-debt closeout (2026-08-08):** P0–P3 landed. Autofly
> `era18_p3_fly`/`idle`/`warm`/`land` GO; ocean ARCH_D3 soft (TD-048).
> Evidence: `era18_p1_idle`, `era18_p2_idle`, `era18_p3_*`.
> Manual `191229` shows light-debt soft OK but **FPS/holes/miss regression** —
> TD-054 stays **partial**; Gap Era18 honesty → regressed; fix via TD-055.

> **Era18 focus light-debt (2026-08-08):** Manual `165953` — VB=53 plateau with
> `pending_light_focus=0`, `relight_drain≈0`, `softdefer_capture_budget=0`, fifo frozen.
> Ticket≠light debt. Autofly land GO ≠ visual merge.

> **Era17 heal-until closeout (2026-08-08):** Contains-only ticket + Progress/Stalled;
> continuous VB heal (void→RelightThenMesh, stale→MarkDirty); FirstMesh class cy≤1.
> Evidence: `era17_p1_idle`/`land`, `era17_p2_land`. Manual `144227` class **partial** —
> superseded residual `165953` (TD-054). Autofly ≠ sole visual merge SoT.

> **Era16 VisibleBlack closeout (2026-08-08):** `black_sticky` ⊆ StickyRemeshAfterLight;
> DoD = `VisibleBlackNoTicketN` / `post_stop_visible_black_no_ticket_max=0`.
> P3 matrix: `era16_p3_fly` FLY_CLEAN GO; `era16_p3_idle`/`warm` IDLE GO;
> `era16_p3_land` ARCH_D3_LAND GO (holes=0 no_ticket=0 wall≈66 soft≤70);
> `era16_p3_ocean` ARCH_D3 soft NO-GO (TD-048 residual; no_ticket=0).

> **Era16 VisibleBlack (2026-08-08 plan):** `black_sticky` only counts `StickyRemeshAfterLight` ∩ stale.
> User-visible black = drawable stale/fully-dark columns (`VisibleBlackFocusN`); orphans =
> `VisibleBlackNoTicketN`. Manual `perf_20260808-113932`: sticky=0 while stale≈4740.

> **Era15 architecture-first (2026-08-08):** close visual residual (flicker / black / holes) via SoT
> (MeshResidency, ColumnPublication Unlit→LitPending→LitReady, SoftDeferHeld→ColumnFlow,
> FirstMesh-until-Drawable). Knobs ≠ DoD. DISCARD: Imm primary, wall-gate Dirty, SoftDefer zoo,
> worker Capture TD-046. Baseline Era14.1 `f7d25446` + manual `perf_20260808-093701`.
> Evidence: timeline `era15_p1_fly` / `era15_p3b_land` / `era15_p4_idle` / `era15_p4_warm`.
> Commits: `195b22c4` docs; `327dd006` P1–P3 SoT; `d692d49b` LitPending FirstMesh-only.

**Era14 execution log**

| Date | Phase | Note | Commit |
|------|-------|------|--------|
| 2026-08-07 | 0 docs | Postmortem + TD-040..048 skeleton | `56391cdf` |
| 2026-08-07 | baseline | FLY_CLEAN GO; IDLE_CLEAN/WARM GO; ARCH_D3_LAND NO-GO (miss_stuck=48); F2/ARCH_D3 NO-GO wall | timeline `era14_*` |
| 2026-08-07 | Phase 1 | `TickWorldStreamingPhase`; nest proof fly-clean | `0812c77f` |
| 2026-08-07 | Phase 2+3 | DesiredStage; kill calm Imm; seed+remesh-on-lit | `f9af0c16` |
| 2026-08-07 | P2 iterate | Dirty-without-wall; sticky remesh hot; Capture refresh trim; best LAND `era14_p2c_land` (miss=4 wall≈55 sticky=0 holes=0.2 residual) | `7f268eb1` |
| 2026-08-07 | Phase 5 matrix | FLY_CLEAN GO (`era14_p5_fly`); IDLE_WARM GO; IDLE_CLEAN NO-GO emerge/sticky; ARCH_D3_LAND residual holes TD-043 | `7f268eb1` |
| 2026-08-07 | Era14.1 analyze | manual `202855` vs `151212`: nest fixed; root = SoftDeferHeld+pending light (54% miss frames held≥8∧pend≥12; p2c=0%); PreferKick late; period `world_streaming_phase_ms`=last-frame artifact | — |
| 2026-08-07 | Era14.1 code | PreferKick tops HP; SoftDefer requeue floor under miss; phase budget 24ms miss carve-out; period phase avg; IDLE sticky/emerge; Android TickWorldStreamingPhase | working tree |
| 2026-08-07 | Era14.1 matrix | FLY_CLEAN+IDLE_CLEAN+IDLE_WARM GO; ARCH_D3_LAND best `era14_1_land` miss=6 holes=0.2 wall≈53 sticky=0; ocean ARCH_D3 wall≈49 NO-GO; Capture TD-046 deferred | timeline `era14_1_*` |

**Era14.1 root (manual `perf_20260807-202855_2064`):** pending_light~12–24 + SoftDeferHeld~18 under miss → DeepBacklog/HoleDrain + J2 softdefer_requeue clamp → nearest tops not PreferKicked every frame → miss sticky. DISCARD unchanged (no Imm primary / wall-gated Dirty). Period `world_streaming_phase_ms` now averaged (was last-frame artifact).

P4 validate (`4abd8683` tip): unit `streaming_render_ready_invariants_test` PASS;
`phase_P4_land_south_short.json` DoD miss_end=0 + post_stop_missing_zero.
Anti-circle held: no pending_gpu drain cut, no kick_cut 0.55 under HoleDrain,
no Imm expand, no Fog knobs, no SoftDefer predicate widen.
TD-ARCH-032/033 unchanged (Era13 harness / rim latency — not plan blockers).

Evidence (stale-apply + Era13 tails, 2026-07-29):
- `manual_stale_apply_A.json` — `mesh_apply_stale`=0 (was ~392).
- Remaining open outside FOV plan: TD-032 (D3 wall+sticky), TD-033 (rim confirm), 011, 013b, 018; Android GLES.

**Do not merge `arch/streaming-v2-v4` → develop until ARCH_D3 PASS + explicit request.**

Evidence (prior): `bin/iter_reports/timeline/arch_d2_manual.json`, `arch_d2b_manual.json`.

### qual_fix3 verification — visible flight fixes (2026-07-29)

Autofly after TD-ARCH-021..024 vs baselines:

| Report | holes | wall_med | spikes | post_stop_holes | opaque_on_min | blue | chunks |
|--------|-------|----------|--------|-----------------|---------------|------|--------|
| `manual_latest_1645` (user) | 0.29 | 43 | 168 | 1.0 | — | — | — |
| `manual_qual_fix2` | 0.24 | 27 | 22 | 0.0 | — | — | — |
| `manual_qual_fix3` | 0.41 | 26 | 54 | 0.0 | 0 | 1 | 18 |
| `edge_qual_fix3` | 1.0 | 48 | 65 | 1.0 | 1 | 0 | 19 |

**Improved:** wall_ms (−40%), spike_count (−68% vs 1645), post_stop_effective_holes 0%, stop_not_ready_end 0, black_sticky 0.

**Regressed / open:** cruise holes_rate (dark/sticky draw gate trade-off, TD-ARCH-025); edge scenario Red pressure + dirty≈693; cold_relight_holes 10–16 s; manual post-load ring needs user confirm @10 s idle.

Phase 0 telemetry live: `pending_gpu_applies_n`, `mesh_apply_stale_delta`, `post_load_ring_not_ready`, `enter_game_warmup_missing_greedy`.

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
  attempted → hang; reverted. **2026-08-01:** main `UMeshCaptureStore` landed
  (TD-ARCH-015 store contract); worker Capture remains off.
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
