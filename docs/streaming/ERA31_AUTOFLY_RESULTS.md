# Era31 Autofly Matrix Results (2026-08-10)

Build: post-Era31 + ForceCap hang fix + VB Relight slot + single WarmupGreedy.  
World: `World_164`. SoT manual still `122032` (pre-Era31 code).

## Runs

| Scenario | Report | Perf log |
|----------|--------|----------|
| smoke | `bin/iter_reports/era31_ocean_smoke.json` | `perf_20260810-151531_9900` |
| stress | `bin/iter_reports/era31_ocean_stress.json` | `perf_20260810-151730_26932` |
| enter | `bin/iter_reports/era31_ocean_enter.json` | (enter scenario) |
| smoke2 (post Relight/enter tweak) | `bin/iter_reports/era31_ocean_smoke2.json` | `perf_20260810-152716_26876` |
| stress2 | `bin/iter_reports/era31_ocean_stress2.json` | `perf_20260810-152918_504` |

## Metrics vs Era31 targets (vs manual 122032)

| Metric | 122032 manual | smoke2 | stress2 | Target | Autofly? |
|--------|---------------|--------|---------|--------|----------|
| `fly_void_near_max` | 1823 | **0** | **0** | ≪800 | no debt (H4) |
| `effective_holes_rate` | 70% | 17% | 34% | ≤30% | stress 34% soft |
| `fly_visible_black_max` | 44 | — | 21 | ≤20 | stress ~GO |
| `vb_progress_without_dark_clear_sec` | 22s | **0** | **0** | ≈0 | **GO** |
| `wall_ms_fly_med` | 123 | 40 | 47 | ≤80 | **GO** |
| `enter_app_update_max` | 1196 | 1026 | 890 | ≤200 | **NO** |
| `opaque_idle_churn_max` | 251 | 24 | 124 | ≤120 | smoke GO / stress soft |
| `fly_frontier_pressure_frac` | 1.0 | 0.86 | 0.90 | KEEP | **GO** |
| `emerge_spike_frac` | 0.67 | 0.52 | **0.26** | ≪0.8 | improved |
| `void_drain_rate` | −54 | 0 | 0 | >0 under debt | N/A (void=0) |

## Gates

| Gate | Result | Note |
|------|--------|------|
| `OCEAN_CRUISE` | **GO** | soft WARN: enter_app, relight_drain_while_vb |
| `OCEAN_CRUISE_STRESS` | **NO-GO** | void≥400 / holes≥40% not reproduced (autofly still cleaner than manual) |
| `OCEAN_MANUAL` | **NO-GO** | needs new post-fix manual flight + eye; 122032 is pre-Era31 |

## Plan checklist (Q0–Q6)

| Phase | Implemented in code? | Target met on autofly? | Notes |
|-------|----------------------|------------------------|-------|
| Q0 harness / SoT 122032 | yes | yes | metrics + baseline doc |
| Q1 heal throughput | yes | **partial** | pressure ON; void debt not reproduced by autofly; Relight-while-VB still soft-warn |
| Q2 emerge/heal split | yes | **improved** | emerge_spike_frac 0.67→0.26 on stress2; wall≪123 |
| Q3 VB dark-clear | yes | **yes** | vb_progress_without_dark_clear=0 |
| Q4 full enter cap | yes (then fixed hang) | **no** | ForceCap mid-load removed; enter_app still ~0.9–1.1s |
| Q5 opaque churn | yes | **mostly** | smoke 24; stress2 124 (soft over 120) |
| Q6 OCEAN_MANUAL GO | docs/gates | **no** | TD-066 remains **partial** until manual eye |

## Honest verdict

- Era31 **landed** pressure→throughput wiring, VB honesty, emerge cap, opaque damp, enter hang fix.
- Autofly **cannot CLOSE** Era31: smoke is still too clean on void (H4), stress does not reproduce void≥400, enter hitch remains ≫200ms.
- **DoD** stays: new manual flight on 122032-class zone + `OCEAN_MANUAL GO` + eye.

## Follow-ups (Era32 candidates)

1. Enter hitch: profile first InGame `app_update` (WarmupGreedy / PrepareEnter / streamer init).
2. Autofly void parity: colder teleport / denser FillWater path so stress can hit void≥400.
3. Relight telem honesty: `relight_drain_ms` vs async FIFO (soft fail may be async-false).
