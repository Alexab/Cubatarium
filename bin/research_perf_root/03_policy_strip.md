# Policy strip A/B (perf-root P3 / Phase5 S4)

## Flag

- Runtime: `URuntimeTuning::StreamSimple` (env `CUBA_STREAM_SIMPLE=1` or `streaming_tune.json` `"stream_simple": true`)
- Effect: disables diet/cadence throttles in `RefreshStreamingPressure`; disables SoftDefer witness retarget blocking; RefreshProbe state is explicit on `UWorldStreaming`

## How to A/B

```powershell
python tools/simple_ab_suite.py
```

## Status 2026-09-05 (Phase5 S4 complete)

### land-stand hang

- **Root cause:** enter-warmup stuck `mesh_missing` without underfeet after coop abort-drain.
- **Fix:** `ShouldForceEnterLoadSoftExit` (150s + fov_debt==0).
- **Verify:** `hang_killed=false`, periods>0 (`phase5_landstand_verify.json`, SimpleAB suites).

### SimpleAB 2026-09-05

| Suite | report | land-cruise wall | land-stand wall | fz-cold-enter wall | hang |
|---|---|---:|---:|---:|---|
| SimpleAB-off | `20260905-011150_suite_summary.json` | 37.8 | 37.0 | 62.4 | false |
| SimpleAB-on | `20260905-012222_suite_summary.json` | **126** | 58 | 68 | false |

**Verdict:** StreamSimple=ON uniquely worsens land-cruise wall (~3×). Diet/cadence / SoftDefer heuristics **KEEP** — do **not** delete Policy headers. Full suite still `pass=false` due to promoted FPS hard gates (expected until wall≤16.6).

## Heuristic → gate → cost table

| Heuristic | Gate it holds | Cost (ms, FPM 010702) | Keep? |
|---|---|---:|---|
| diet_cruise_cadence / AntiFlicker | wall_ms_fly / stream | ~0.01 | **KEEP** (AB: ON worse) |
| unfinished sample cadence | unfinished_visual | TBD | KEEP |
| SoftDefer capture pin | holes / flicker | ~0.05 | KEEP |
| facing rim cadence | facing_ms | ~0.08 | KEEP |
| rim_witness_idle_diet | witness_latch_diet_share | TBD | KEEP |
| schedule policy (FIFO/ocean/idle) | stream / emerge | ~1.3 after shed (was 15.8) | KEEP envelope; shed under idle |

## Headers annotated with BUDGET_MS

| Header | BUDGET_MS | Source |
|---|---:|---|
| SoftDeferEmptyPolicy.h | 0.05 | softdefer_empty + prep_softdefer_pre |
| SoftDeferFramePolicy.h | 0.01 | prep_softdefer_policy_ms |
| RelightFifoPolicy.h | 15.8 | historical schedule envelope (post-shed ~1.3) |
| OceanCruisePolicy.h | 15.8 | shares schedule envelope |
| IdleRecoveryPolicy.h | 15.8 | shares schedule envelope |
| AntiFlickerPolicy.h | 0.01 | diet flags |

Remaining `*Policy.h` keep `0.0` stubs. Gate `policy_headers_have_budget` green.

## Deleted headers

**None** — SimpleAB-on does not uniquely attribute a hard gate fail that justifies deletion; OFF is better on wall.
