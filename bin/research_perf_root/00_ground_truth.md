# Perf ground truth — Phase 5

## Captures

| id | file | notes |
|---|---|---|
| baseline manual | `perf_20260904-175907_32988.jsonl` | wall 246 / stream 140 / scene 44 |
| post P0–P4 manual | `perf_20260904-214912_18544.jsonl` | wall 277; Self~60 unexplained |
| Phase5 S0+S1 auto | `perf_20260904-233454_21544.jsonl` | fly-heavy; setup≈64 = NeedsEnterGameMeshWarmup |
| Phase5 pre-setup-cut | `perf_20260905-001749_18324.jsonl` | S1 partial; setup_probe≈58; emerge_prep≈0.65 w/ deadline |
| **Phase5 S2 cut** | `perf_20260905-010702_35336.jsonl` | enter-warmup early-out; Tracy OFF; wall~42 fps~22 |
| Phase5 final | `perf_20260905-013321_27952.jsonl` | schedule_policy idle shed; emerge_prep~1.9; wall~60 (holes still red) |

## S0b / Tracy

- Gate flights: **`CUBATARIUM_ENABLE_TRACY=OFF`**.
- H1 (Tracy as Self source): **rejected** — after S0, Self≈0; wall was `NeedsEnterGameMeshWarmup` ring scans mislabeled as setup_probe.

## Period medians (key deltas)

| metric | 233454 | 001749 | **010702** |
|---|---:|---:|---:|
| wall_ms | 243 | 254 | **42** |
| wall_ms_fly (report) | 306 | 289 | **46** |
| stream_ms | 143 | 143 | **7.4** |
| prep_refresh_pressure_ms | 67 | 74 | **0.26** |
| prep_refresh_setup_probe_ms | — | 58 | **0.0003** |
| mesh_emerge_prep_ms | 21 | 0.65 | 7.5 |
| prep_schedule_policy_ms | 15.8 | 3.1 | 7.0 |
| scene_transparent_ms | 23 | 14 | **4.2** |
| pool_unsync_uploads | 64 | 64 | **0** |
| effective_fps_fly | ~3.3 | ~3.5 | **~22** |

## Root cause fixed (S2)

`NeedsEnterGameMeshWarmup()` called full `SampleEnterGameMeshWarmupBlockers` every Refresh during **cruise**. Early-out when `!EnterLitGateActive && !IsEnterSessionActive()`; cache spawn catch-up.

## Remaining toward ≤16.6

1. scene ~12–23 (opaque dominates after transparent ~4–7) → ≤5
2. holes_rate / unfinished still red on fly-heavy — visual debt (soft `stable_holes`)
3. wall fly ~46–70 vs hard ≤16.6 — Hard FPS gates (S5) FAIL honestly until scene+holes fixed
4. Best overall FPS capture so far: **010702** (wall~42, fps~22); final shed improved emerge_prep≤2 but wall/holes not better

## Attribution

- refresh self/total ≤0.10: **PASS**
- scene self/total ≤0.10: **PASS**
