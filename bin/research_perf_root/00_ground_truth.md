# Perf ground truth — Phase 5 / 5.1 / 5.2 / 5.3 / 5.4

## Captures

| id | file | notes |
|---|---|---|
| baseline manual | `perf_20260904-175907_32988.jsonl` | wall 246 / stream 140 / scene 44 |
| post P0–P4 manual | `perf_20260904-214912_18544.jsonl` | wall 277; Self~60 unexplained |
| Phase5 S0+S1 auto | `perf_20260904-233454_21544.jsonl` | fly-heavy; setup≈64 = NeedsEnterGameMeshWarmup |
| Phase5 pre-setup-cut | `perf_20260905-001749_18324.jsonl` | S1 partial; setup_probe≈58; emerge_prep≈0.65 w/ deadline |
| **Phase5 S2 cut** | `perf_20260905-010702_35336.jsonl` | enter-warmup early-out; Tracy OFF; wall~42 fps~22 |
| Phase5 final | `perf_20260905-013321_27952.jsonl` | schedule_policy idle shed; emerge_prep~1.9; wall~60 (holes still red) |
| manual 081303 | `perf_20260905-081303_18276.jsonl` | wall~73 fly~83; transp~26; unsync**64**; phase~25–38 |
| Phase5.1 v4 fly | `perf_20260905-101909_23740.jsonl` / `phase51_v4_flyheavy.json` | hang=false; unsync**0**; transp~4.9; prep_warmup**0**; wall~40; scene~25; phase~9 |
| **Phase5.1 v6b fly** | `phase51_v6b_flyheavy.json` | leftovers packed; hang=false; wall~39 fly~39; scene~23; phase~7.9; transp~4.3; unsync**0** |
| manual empty SoT | `perf_20260905-192151_13756.jsonl` | unfinished/colnm plateau **64–85**; stuck_horiz=5 locked; holes~0.78 |
| **manual post-5.3** | `perf_20260906-075706_512.jsonl` + INFO `…075704.512` | empty closed; rim miss + Quiesce vis_debt settle |

## Phase 5.4 (miss catch-up — after empty drip)

### Attribution (manual 075706 cruise `movement_speed>2`)

| Bottleneck | Evidence | Not |
|---|---|---|
| Rim miss / holes | `focus_missing≈1`, holes~0.70, `miss_horiz` med~2; AbortDrip fixed **2** while `phase_abort_heavy=100%` | empty-batch (plateau closed) |
| “Lagging stream” feel | phase~36 = stream~8 + emerge~27; `IngressDebtLevel=3` ShedFar; `relight_drain` p90~20 from **WorldStreaming** | disk (`streamer_update≪1`) |
| Underwater enter | INFO `settle_reason=live_blockers` with **`visibility_debt=81`** via Quiesce / `coop_prepared` OR-bypass; post-enter `miss_horiz` 3–4 | empty SoftDefer alone |
| Scene | `scene_opaque_cull_ms` med~22; `pool_unsync_uploads` med~36 / max~72 | opaque refresh |

### Scorecard baseline (075706 vs 192151 vs phase53)

| Metric | Manual 192151 | Manual **075706** | phase53 fly |
|---|---:|---:|---:|
| empty_backlog / unf / colnm med | **~64** | **~5** | **~2** |
| empty_batch_event | (unknown) | **0** | **0** |
| softdefer_stuck_horiz locked | yes | **no** | **no** |
| focus_missing frac | — | **~1.0** | — |
| holes_rate | 0.78 | **~0.70** | 0.60 |
| phase_abort_heavy frac | — | **1.0** | — |
| world_streaming_phase_ms med | ~14 | **~36** | **7.8** |
| wall_ms_med | ~24 | **~86** | **39** |
| settle vis_debt on live/soft | — | **81** | — |
| pool_unsync_uploads med | — | **~36** | **0** |

Phase 5.3 algorithms: **COMPLETE** (empty UX closed). Phase 5.4 targets rim miss escalation, enter FOV presentable settle, stream relight/phase cost, cull/unsync diet. Hard gates **not demoted**.

## Phase 5.5 (presentable ownership — ship)

Manual SoT: `perf_20260906-170813_25272.jsonl` + INFO `…170811.25272` — soft_force@150s vis_debt=81; cruise abort/miss 100%; SoftDefer exit stuck.

### Landed

| Sprint | Commit intent | Notes |
|---|---|---|
| 5.5.0 | Auto↔manual fidelity + soft_force debt FAIL scorecard | `AnalyzePhase55Scorecard`; timeout 600; no teleport for phase55 |
| 5.5.0b | Enter HB relight vs mesh vs gpu; SoftDefer owned-no-gpu FPM | telem only |
| 5.5.1–5.5.3 | Near-band async + PresentableCatchUp; SoftDefer invalidate; presentable abort carve | fz+fly PRODUCT_OK (latch + miss≤0.3) |

### Auto scorecard (Tracy OFF, no-teleport, World_164, timeout 600)

| report | hang | soft_force+debt | latch | focus_miss | abort_frac | SoftDefer age tail | product |
|---|---|---|---|---:|---:|---:|---|
| **fly-heavy** `phase55_v1_flyheavy` | false | yes (honest FAIL line) | **1** | **0.05** | 0.68 | **0** | **OK** |
| **fz-cold-enter** `phase55_v1_fz` | false | yes | **1** | **0.09** | 0.72 | **0** | **OK** |
| **land replay** `phase55_land_replay` | false | yes | **1** | **0.09** | 0.70 | **0** | **OK** |

vs manual 170813: auto still greener on wall/phase (locus/resume drift); SoftDefer exit stuck closed on auto; soft_force+debt still primary enter path but **PresentableCatchUp latch** armed. Gate of record for UX ship = **manual eye**. Hard contract still red.

Harness: [`tools/AnalyzePhase55Scorecard.py`](../../tools/AnalyzePhase55Scorecard.py) — soft_force+debt = FAIL (honest); `--baseline-manual` delta. Policy: [`04_presentable_ownership.md`](04_presentable_ownership.md). No-teleport + `--process-timeout 600`.

## Phase 5.7 (flicker/VB drain + CPU accel)

Manual SoT: `perf_20260907-093857_26724.jsonl` + INFO `…093855.26724` (+ `enter_lit_20260907-093914.jsonl`).

| Metric (cruise / stand) | 093857 SoT | 192816 | phase56 ship fly-heavy |
|---|---:|---:|---:|
| mesh_discarded_late med/max | **12** / stand **30** | **0** | — |
| pool_unsync max | **76** | — | — |
| visible_black_focus_n med/max | **43** / **108** | — | — |
| vb_no_ticket / stalled max | **91** / **26** | — | — |
| miss_stuck_run_frames (stand) | **→1166** @ kick=0 | →1469 | ≤198 |
| soft_force+debt | yes debt=81; clear never | yes debt=81; never | yes; never |
| visibility_debt med | **59–72** | — | ~20 |
| wall / phase / opaque_cull med | **~98** / **~69** / **~13** | ~101 / ~69 | ~60 / ~23 |

### Roots (Phase 5.7 targets; hard gates not demoted; no AbortDrip / budget / soft_force wall raise)

1. Discard thrash → Dirty requeue (flicker holes); attribute epoch vs job mismatch.
2. VB FullyDark / no_ticket + miss_stuck @ kick=0 → black rim.
3. Latch: soft_force debt never clears (settle honesty).
4. CPU: opaque_cull ~13 + Relight FIFO O(Nd) + VB full scan in stream/emerge.

### Landed

| Sprint | Commit | Notes |
|---|---|---|
| 5.7.0 | (this) | Phase57 scorecard + discard epoch/job attribution vs 093857; fly+fz FIDELITY_OK |

Harness: [`tools/AnalyzePhase57Scorecard.py`](../../tools/AnalyzePhase57Scorecard.py) — baseline default `093857`. Locus pin `(120,57.31,56)` + yaw 90. **Not** `--land-stand`. No teleport for `phase57_*`.

## Phase 5.6 (ring frontier — ship)

Manual SoT: `perf_20260906-192816_24828.jsonl` + INFO `…192814.24828` (+ `enter_lit_20260906-192829.jsonl`).

| Metric (cruise / stand) | 192816 SoT | 170813 | phase56 ship fly-heavy |
|---|---:|---:|---:|
| soft_force+debt | yes @150s debt=81 | yes @150s debt=81 | yes + latch=1 |
| latch clear | **never** (all periods=1) | n/a (pre-latch) | **never** (debt med~20 max=81) |
| visual_holes_frac | **~0.60** (15/25) | lower class | **0.18** |
| empty_backlog_max | **19** | — | **6** |
| focus_missing_frac | **1.0** | 1.0 | **0.20** |
| phase_abort_heavy_frac | **1.0** | 1.0 | ~0.62 |
| wall / phase med | ~101 / ~69 | ~116 / ~70 | ~60 / ~23 |
| focus_unfinished_ahead (stand) | **10** | — | **0–1** |
| miss_stuck_run_frames (stand) | **→1469** gpu_kick=0 | — | **≤198** |

### Roots (addressed in code; hard gates not demoted; no AbortDrip / budget / soft_force wall raise)

1. Latch clear waits debt≤0; remesh mesh-but-!VisualReady in R=4 makes debt drainable — clear still needs real debt=0 (ocean/unready keeps latch).
2. Keep-ring FM reserve ≥2 outside abort carve (no AbortDrip++).
3. Miss PreferKick only with PendingGpu; empty → Dirty/FM; stand miss dense facing (no stale ahead).
4. Auto locus: Phase56 pin = land save resume + `--yaw 90` (no teleport).

### Landed

| Sprint | Commit | Notes |
|---|---|---|
| 5.6.0 | `b9285c7e` | Land-corridor auto pin + Phase56 scorecard + 192816 GT |
| 5.6.1 | `4da188bb` | Drainable PresentableCatchUp / lit debt remesh; clear = SoftDefer + debt≤0 |
| 5.6.2 | `4f6a88ae` | Keep-ring frontier FM reserve outside abort |
| 5.6.3 | `59902a7e` | Miss witness remesh when pending_gpu empty + dense stand ahead |
| 5.6.4 | docs | Ship trio Analyze vs 192816; **manual deferred** (latch/product not honest-green) |

### Auto ship trio (Tracy OFF, no-teleport, World_164, timeout 600)

| report | hang | soft_force+debt | latch clear | holes_frac | empty_max | focus_miss | ahead_tail | miss_stuck | SoftDefer age | fidelity |
|---|---|---|---|---:|---:|---:|---:|---:|---:|---|
| **fly-heavy** `phase56_v3_flyheavy` | false | yes debt=81 | never | 0.18 | 6 | 0.20 | 0 | 167 | 0 | OK |
| **land** `phase56_v3_land` | false | yes debt=81 | never | 0.51 | 10 | 0.53 | 1 | 167 | 0 | OK |
| **fz-cold-enter** `phase56_ship_fz` | false | yes debt=81 | never | 0.45 | 8 | 0.45 | 0 | 198 | 0 | OK |

vs 192816: cruise holes/empty/ahead/miss_stuck improved on fly; latch still never clears (debt≠0); soft_force+debt remains honest FAIL. Hard contract still red. **Manual eye gate deferred** until latch clears or debt↓ post-InGame on auto product-green.

SoT locus: 192816 focus **[7,3]**; save pin chunk mid `(120,57.31,56)` + yaw 90 after drift.

Harness: [`tools/AnalyzePhase56Scorecard.py`](../../tools/AnalyzePhase56Scorecard.py) — baseline default `192816`. **Not** `--land-stand`.

## Phase 5.4 ship (code + no-teleport verify)

### Landed commits

| Sprint | Commit intent | Notes |
|---|---|---|
| 5.4.0 | GT attribution 075706 | docs |
| 5.4.1 | FOV presentable settle — no Quiesce/`coop_prepared` bypass with `visibility_debt>0`; FillWater SoftDefer pin; soft_exit last-resort ignores sticky fov | fz: live_blockers debt0; soft_force@150s may remain with debt |
| 5.4.2 | `AbortDripN` 2→3 sticky rim + `MissWitnessScheduleFloor` | focus_missing frac ≪0.5 |
| 5.4.3 | `prep_shed_skip` protect FocusMissing; SoftDefer cy cruise; stream RelightMissTops | phase med≤12; abort_frac≤0.5 class |
| 5.4.4 | EnterUnsyncDiet lit-gate cap **32** (8 starved soft_force→holes); OpaqueCullDistanceBand **reverted** (holes) | cruise unsync med≤8 |

### Auto scorecard (Tracy OFF, no-teleport, World_164)

| report | hang | wall | phase | holes | focus_miss frac | abort_frac | unsync | empty_backlog |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **fly-heavy** (`phase54_flyheavy` / best `v4e`) | false | ~41–51 | ~7–9 | ~0.59–0.68 | ≤0.10 | ≤0.45 | **0** | ≤5 |
| **fz-cold-enter** (`phase54_fz_cold_enter` / best `v4e`) | false | ~43–57 | ~7–9 | ~0.37–0.81 | ≤0.09 | ≤0.53 | **6–8** | ≤5 |
| **land replay** (`phase54_land_replay`, `--process-timeout 600`) | **false** | **41.3** | **7.2** | **0.45** | **0.02** | **0.36** | **0** | **4** |

vs 075706: empty closed kept; focus_missing frac **~1→≤0.10**; Quiesce live settle with vis_debt=81 **closed**; phase_abort_heavy **1.0→~0.3–0.5**; cruise unsync **~36→0**. Hard contract still red. Manual **deferred** (holes≰0.40 on fly/ship; land needs extended process-timeout because soft_force@150s).

## Phase 5.3 (empty-chunk drip + SoftDefer escape)

### Landed code

| Sprint | Cut | Notes |
|---|---|---|
| 5.3.0 | `EmptyBacklogPolicy` + `empty_backlog_n` / abort/skip latches in FPM; `empty_batch_event` INFO | Steady-clock rise+20 / drop−20 detector |
| 5.3.1 | `AbortDripCap` (≥1–2 under backlog/holes) + `CarveUpTo2` | No schedule starve when `empty_backlog_n>0` |
| 5.3.2 | Forbid `skip_empty_emerge` on hole/backlog pressure; SoftDefer stuck escape + PreferKick under abort | Clear stuck telem each scan; rate-limit ≤2 escapes/frame |
| 5.3.3 | HoleDrain/Deep `FirstMeshDripCap` ≤4 when backlog>8 | Anti late EMPTY pack |
| 5.3.4 | `OpaqueCullSkipStable` (draw+cull rev + camera eps + IndirectCullReady + GpuCompactActive) | Stand/idle skip only — camera motion always culls |

### Empty scorecard (vs manual 192151 / phase52)

| Metric | Manual 192151 | phase52 fly | **phase53 fly** (`phase53_flyheavy` / `210204`) |
|---|---:|---:|---:|
| hang_killed | false | false | **false** |
| settle_reason elapsed | ~64ms | — | **~35–60ms** |
| wall_ms_med | ~24 | ~39 | **39.0** (≤43 auto) |
| scene_ms | ~10 | ~25 | **23.4** |
| world_streaming_phase_ms | ~14 | ~7.6 | **7.8** |
| holes_rate | **0.78** | 0.58 | **0.60** |
| empty_backlog / unf / colnm med | **~64** | — | **~2** (p90~6) |
| softdefer_empty_stuck_horiz==5 frac | ~locked | — | **0** |
| soft_defer_empty_stuck_sec | — | — | **0** |
| empty_batch_event | (unknown) | — | **0** |
| abort_schedule_final \| backlog>8 | ~2 stuck | — | **≥2** |
| pool_unsync_uploads_med | — | 0 | **0** |
| scene_opaque_cull_ms med | ~6 | — | ~17.6 (still cull-dominant) |

### Verify harness (no-teleport, Tracy OFF, World_164)

| report | hang | wall | phase | scene | holes | stuck_sec | unsync | pass |
|---|---|---:|---:|---:|---:|---:|---:|---|
| **fly-heavy** (`phase53_flyheavy`) | false | **39.0** | **7.8** | **23.4** | **0.60** | **0** | **0** | false |
| fz-cold-enter (`phase53_fz_cold_enter`) | false | 61.4 | 10.1 | 36.8 | 0.42 | 0 | 10 | false |
| land replay (`phase53_land_replay`) | false | 43.6 | 9.2 | 25.6 | 0.39 | 0 | 0 | false |

Ship status: **empty UX green vs 192151** (no 64–85 plateau / no locked stuck_horiz / no empty_batch_event); hard contract **still red** (wall/scene/phase/holes gates). Manual deferred (`pass≠true`).

## Phase 5.2 (enter unblock + hard contract)

### Landed code

| Sprint | Cut | Notes |
|---|---|---|
| 5.2.0 | Drop `NeedsEnterGameMeshWarmup` epoch memo on Loading; `ShouldForceEnterLoadSoftCleanDebt` @12s; `settle_reason=` telem | Fix PrepareView@100% stick (epoch frozen outside InGame) |
| 5.2.1 | Opaque split timers (refresh/cull/gpu_draw/packed/cross) + FPM JSONL; `need_rebuild`+empty dirty → order-only; packed near r≤4; opaque-only BeginUploadFrame | Attribution before cut; never skip-all packed |
| 5.2.2 | Stream early-return / far_exhausted on `stream_budget` 0.6×phase; skip `TickMeshEmerge` when emerge_cap=0; abort schedule/drain 0 (mh≤2 → 1) | Enter floor 24 kept |
| 5.2.3 | mh∈[2,4] carve + same-epoch thrash rate-limit | Under budget |

### Verify harness (no-teleport, Tracy OFF, World_164)

| report | hang | wall | fly | phase | scene | holes | unsync | pass |
|---|---|---:|---:|---:|---:|---:|---:|---|
| enter fz (`phase52_enter_fz_cold_enter`) | false | 45.5 | 44.5 | — | — | 0.94 | — | false |
| enter land (`phase52_enter_landstand`) | false | 75.5 | 38.8 | 28.4 | 33.2 | 0.20 | 0 | false |
| **fly-heavy** (`phase52_flyheavy`) | false | **39.2** | **40.4** | **7.6** | **24.8** | **0.58** | **0** | false |
| land-stand (`phase52_landstand`) | false | 58.8 | 47.0 | 10.7 | 32.8 | 0.41 | 10 | false |
| fz-cold-enter (`phase52_fz_cold_enter`) | false | 47.6 | 41.2 | 7.7 | 29.3 | 0.48 | 8 | false |

Enter unlock: INFO `settle_reason=live_blockers elapsed_ms≈617` (≪30s) — PrepareView@100% stick fixed.

Opaque split (spike sample fly): refresh≪1ms, **cull~9ms** dominant vs refresh; packed near r≤4 kept. Hard gates still red (same as 5.1 v6b class). Manual skipped (`pass≠true`).

Ship status: **code + verify landed**; hard contract **not green** — next cut likely opaque cull / scene GPU (not refresh).

## Phase 5.1 ship closeout (code `54612d52` + follow-ups)

### Landed

| Sprint | Cut | Result |
|---|---|---|
| T0 | Attribution on 081303 | opaque+transparent ≈ scene (gap≪10%) — no opaque micro-timers |
| T1 | Transparent `consume_dirty=false` + MeshRevAbsorb + KeyMiss append + NotAttempted | `transparent_upload_full_n≈0`, `pool_unsync_uploads` med **0**, transp med **≤5** on v4 |
| T2 | schedule_policy deadline inside else + UV≤1 shed (`ScheduleShedUv1`) | `prep_schedule_policy` med≪5 when warmup latched |
| T3 | `StreamingPhaseBudgetMs=5` + StreamMs latch + phase abort + stream share 0.6 | phase med ~9 (not ≤5 yet); enter auto floor 24 |
| T4 | opaque sort fingerprint + packed leftovers-only (not full dual, not skip-all) | skip-all packed regressed holes/scene (v5); leftovers kept |
| T5 | mh∈[2,4] FirstMesh carve under remain | holes still red (~0.3–0.6) |
| Hang fix | soft-exit w/o AbortDrain; `IsCreateSpawnWarmupSettled` cruise latch | fly/land/fz **hang_killed=false** |

### Auto trio (Tracy OFF, no-teleport) — best = v6b fly / v4 trio

| scenario | hang | wall | fly wall | phase | scene | holes | pass |
|---|---|---:|---:|---:|---:|---:|---|
| **fly-heavy v6b** | false | **39.2** | **39.0** | 7.9 | 23.2 | 0.62 | **false** |
| fly-heavy v4 | false | 40.4 | 41.4 | 8.7 | 24.9 | 0.58 | **false** |
| land-stand v4 | false | 43.5 | 58.0 | 18.5 | 30.8 | 0.27 | **false** |
| fz-cold-enter v4 | false | 40.6 | 46.2 | 9.7 | 32.2 | 0.39 | **false** |
| fly-heavy v5 (skip packed+emerge) | false | 54.7 | 51.1 | 7.0 | 36.8 | 0.56 | **false** (regress) |

Hard gates still red: `wall_ms_fly_le_16_6`, `scene_ms_le_5`, `stream_phase_ms_le_5`, `visual_holes_rate_le_0_10`. Manual skipped (auto `pass≠true`).

Ship status: **code closeout landed**; hard contract **not green** — blocker is opaque scene (~18–23ms) + phase (~8) + holes.

### Remaining toward hard contract

1. **scene opaque ~19–25ms** — dominant after T1; cull still primary after 5.3.4 camera-safe skip
2. **phase ~7–9** — stream~4–5 + emerge; empty drip keeps schedule alive under abort
3. **holes** — soft-exit enter leaves visual debt; empty plateau fixed, rate still ~0.6

## S0b / Tracy

- Gate flights: **`CUBATARIUM_ENABLE_TRACY=OFF`**.


### Landed code

| Sprint | Cut | Notes |
|---|---|---|
| 5.2.0 | Drop `NeedsEnterGameMeshWarmup` epoch memo on Loading; `ShouldForceEnterLoadSoftCleanDebt` @12s; `settle_reason=` telem | Fix PrepareView@100% stick (epoch frozen outside InGame) |
| 5.2.1 | Opaque split timers (refresh/cull/gpu_draw/packed/cross) + FPM JSONL; `need_rebuild`+empty dirty → order-only; packed near r≤4; opaque-only BeginUploadFrame | Attribution before cut; never skip-all packed |
| 5.2.2 | Stream early-return / far_exhausted on `stream_budget` 0.6×phase; skip `TickMeshEmerge` when emerge_cap=0; abort schedule/drain 0 (mh≤2 → 1) | Enter floor 24 kept |
| 5.2.3 | mh∈[2,4] carve + same-epoch thrash rate-limit | Under budget |

### Verify harness (no-teleport, Tracy OFF, World_164)

| report | hang | wall | fly | phase | scene | holes | unsync | pass |
|---|---|---:|---:|---:|---:|---:|---:|---|
| enter fz (`phase52_enter_fz_cold_enter`) | false | 45.5 | 44.5 | — | — | 0.94 | — | false |
| enter land (`phase52_enter_landstand`) | false | 75.5 | 38.8 | 28.4 | 33.2 | 0.20 | 0 | false |
| **fly-heavy** (`phase52_flyheavy`) | false | **39.2** | **40.4** | **7.6** | **24.8** | **0.58** | **0** | false |
| land-stand (`phase52_landstand`) | false | 58.8 | 47.0 | 10.7 | 32.8 | 0.41 | 10 | false |
| fz-cold-enter (`phase52_fz_cold_enter`) | false | 47.6 | 41.2 | 7.7 | 29.3 | 0.48 | 8 | false |

Enter unlock: INFO `settle_reason=live_blockers elapsed_ms≈617` (≪30s) — PrepareView@100% stick fixed.

Opaque split (spike sample fly): refresh≪1ms, **cull~9ms** dominant vs refresh; packed near r≤4 kept. Hard gates still red (same as 5.1 v6b class). Manual skipped (`pass≠true`).

Ship status: **code + verify landed**; hard contract **not green** — next cut likely opaque cull / scene GPU (not refresh).

## Phase 5.1 ship closeout (code `54612d52` + follow-ups)

### Landed

| Sprint | Cut | Result |
|---|---|---|
| T0 | Attribution on 081303 | opaque+transparent ≈ scene (gap≪10%) — no opaque micro-timers |
| T1 | Transparent `consume_dirty=false` + MeshRevAbsorb + KeyMiss append + NotAttempted | `transparent_upload_full_n≈0`, `pool_unsync_uploads` med **0**, transp med **≤5** on v4 |
| T2 | schedule_policy deadline inside else + UV≤1 shed (`ScheduleShedUv1`) | `prep_schedule_policy` med≪5 when warmup latched |
| T3 | `StreamingPhaseBudgetMs=5` + StreamMs latch + phase abort + stream share 0.6 | phase med ~9 (not ≤5 yet); enter auto floor 24 |
| T4 | opaque sort fingerprint + packed leftovers-only (not full dual, not skip-all) | skip-all packed regressed holes/scene (v5); leftovers kept |
| T5 | mh∈[2,4] FirstMesh carve under remain | holes still red (~0.3–0.6) |
| Hang fix | soft-exit w/o AbortDrain; `IsCreateSpawnWarmupSettled` cruise latch | fly/land/fz **hang_killed=false** |

### Auto trio (Tracy OFF, no-teleport) — best = v6b fly / v4 trio

| scenario | hang | wall | fly wall | phase | scene | holes | pass |
|---|---|---:|---:|---:|---:|---:|---|
| **fly-heavy v6b** | false | **39.2** | **39.0** | 7.9 | 23.2 | 0.62 | **false** |
| fly-heavy v4 | false | 40.4 | 41.4 | 8.7 | 24.9 | 0.58 | **false** |
| land-stand v4 | false | 43.5 | 58.0 | 18.5 | 30.8 | 0.27 | **false** |
| fz-cold-enter v4 | false | 40.6 | 46.2 | 9.7 | 32.2 | 0.39 | **false** |
| fly-heavy v5 (skip packed+emerge) | false | 54.7 | 51.1 | 7.0 | 36.8 | 0.56 | **false** (regress) |

Hard gates still red: `wall_ms_fly_le_16_6`, `scene_ms_le_5`, `stream_phase_ms_le_5`, `visual_holes_rate_le_0_10`. Manual skipped (auto `pass≠true`).

Ship status: **code closeout landed**; hard contract **not green** — blocker is opaque scene (~18–23ms) + phase (~8) + holes.

### Remaining toward hard contract

1. **scene opaque ~19–25ms** — dominant after T1; fill/Refresh, not packed dual alone
2. **phase ~7–9** — stream~4–5 + emerge; Refresh early-exit landed, emerge skip hurt
3. **holes** — soft-exit enter leaves visual debt; mh carve not enough under budget

## S0b / Tracy

- Gate flights: **`CUBATARIUM_ENABLE_TRACY=OFF`**.
- H1 (Tracy as Self source): **rejected** — after S0, Self≈0; wall was `NeedsEnterGameMeshWarmup` ring scans mislabeled as setup_probe.

## Period medians (key deltas)

| metric | 233454 | 001749 | **010702** | **5.1 v4 fly** |
|---|---:|---:|---:|---:|
| wall_ms | 243 | 254 | **42** | **40** |
| wall_ms_fly (report) | 306 | 289 | **46** | **41** |
| stream_ms | 143 | 143 | **7.4** | ~5.2 |
| prep_refresh_pressure_ms | 67 | 74 | **0.26** | ~0.10 |
| prep_warmup_ms | — | — | — | **0** |
| mesh_emerge_prep_ms | 21 | 0.65 | 7.5 | **0.4** |
| scene_ms | — | — | **12.6** | **25** |
| scene_transparent_ms | 23 | 14 | **4.2** | **4.9** |
| pool_unsync_uploads | 64 | 64 | **0** | **0** |
| world_streaming_phase_ms | — | — | ~12.5 | ~9 |

## Attribution

- refresh self/total ≤0.10: **PASS**
- scene self/total ≤0.10: **PASS**
