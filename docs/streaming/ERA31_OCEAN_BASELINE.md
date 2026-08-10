# Era31 Ocean Heal Throughput Baseline (Q0)

SoT manual: `bin/logs/perf_20260810-122032_27372.jsonl` (post-Era30, zone −550,102→−555,110)  
SoT autofly smoke: `bin/logs/perf_20260810-113512_16520.jsonl`  
Pre-Era30 manual: `bin/logs/perf_20260810-104841_30332.jsonl`

## Root-cause matrix (122032 post-Era30)

| # | Factor | Evidence (122032) | Era31 mitigation |
|---|--------|-------------------|------------------|
| T1 | Pressure without throughput | `fly_frontier_pressure_frac=1.0`, void max≈1920 | Q1 drain floor + void Relight slots |
| T2 | Emerge eats heal budget | emerge spikes 115/172, wall_med 123 | Q2 MeshEmerge cap + Relight carve-out |
| T3 | VB flicker honesty | `vb_progress_without_dark_clear_sec=22s` | Q3 dark-clear progress gate + near hide-until-lit |
| T4 | Enter hitch | `enter_app_update_max=1196ms` | Q4 full Loading enter cap ≤200ms |
| T5 | Opaque churn | `opaque_idle_churn_max=251` | Q5 moving RemeshAfterApply-only |

## Baseline metrics (122032 vs autofly 113512)

| Metric | 122032 manual | 113512 autofly | Era31 target |
|--------|---------------|----------------|--------------|
| `fly_void_near_max` | 1823 | 0 | ≪800 |
| `effective_holes_rate` | 70% | 8% | ≤30% |
| `fly_visible_black_max` | 44 | — | ≤20 |
| `vb_progress_without_dark_clear_sec` | 22s | 0s | ≈0 |
| `wall_ms_fly_med` | 123 | 41 | ≤80 |
| `enter_app_update_max` | 1196ms | 689 | ≤200 |
| `opaque_idle_churn_max` | 251 | ~52 | ≤120 |
| `fly_frontier_pressure_frac` | 1.0 | 0 | KEEP |

## Harness scenarios

- `ocean-cruise` — smoke (teleport, idle 8)
- `ocean-cruise-enter` — full enter path
- `ocean-cruise-stress` — sprint + fly 90 (parity regression pre-fix)
- `ocean-cruise-short` — idle 3 stop-debt snapshot

## Analyze extensions (Q0)

- `void_peak_period_idx` — fly segment index of void peak (bisect)
- `void_drain_rate` — void/sec decline after peak (throughput signal)
- `emerge_spike_frac` — fraction of spikes dominated by `mesh_emerge_ms`

## Gates

- **OCEAN_CRUISE** — smoke soft targets
- **OCEAN_CRUISE_STRESS** — must reproduce void≥400 + holes≥40% pre-fix
- **OCEAN_MANUAL** — DoD on 122032-class manual analyze (CLOSED only when GO + eye)

## TD-066

**Partial** until post-Era31 build passes `OCEAN_MANUAL` on 122032-class zone + eye sign-off. Absorbs Era30 note: admission (`frontier_pressure`) without throughput was the gap Era31 closes.

## Code (Era31 Q1–Q6)

- `OceanCruisePolicy.h` — Era31 throughput predicates (I-T1…T5)
- `FrameStreamingBudget.h` — ocean heal vb_bg floor ≥2 under void>T
- `ColumnFlowExecutor.cpp` — void Note min 2/frame; VB progress honesty
- `ChunkEmergeCoordinator.cpp` — emerge cap 14ms; moving void drain; hide-until-lit
- `WorldOperationRunner.cpp` — full enter cap `EnterLoadElapsedMs`
- `WorldCooperativeOps.cpp` — `ForceCapEnterGameVisual`
