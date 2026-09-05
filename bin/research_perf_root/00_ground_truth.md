# Perf ground truth — Phase 5 / 5.1

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
