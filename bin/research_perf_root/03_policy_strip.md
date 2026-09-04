# Policy strip A/B (perf-root P3)

## Flag

- Runtime: `URuntimeTuning::StreamSimple` (env `CUBA_STREAM_SIMPLE=1` or `streaming_tune.json` `"stream_simple": true`)
- Effect: disables diet/cadence throttles in `RefreshStreamingPressure`; disables SoftDefer witness retarget blocking; RefreshProbe state is explicit on `UWorldStreaming` (no function-static cadence locals)

## How to A/B

```powershell
python tools/simple_ab_suite.py
```

## Status 2026-09-04

- Infrastructure landed (flag + RefreshProbe + BUDGET_MS annotations).
- Full SimpleAB suite not yet green: land-stand verification hang-killed (exit 124) before period samples — re-run after next manual enter-path check.
- Do not delete policy headers until SimpleAB-on uniquely attributes a hard gate to them.

## Heuristic → gate → cost table

| Heuristic | Gate it holds | Cost (ms, Tracy) | Keep? |
|---|---|---:|---|
| diet_cruise_cadence | wall_ms_fly / stream_ms | TBD | TBD |
| unfinished sample cadence | unfinished_visual | TBD | TBD |
| SoftDefer capture pin | holes / flicker | TBD | TBD |
| facing rim cadence | facing_ms | TBD | TBD |
| rim_witness_idle_diet | witness_latch_diet_share | TBD | TBD |

## Headers annotated with BUDGET_MS

All `*Policy.h` under `src/World/Streaming` carry `// BUDGET_MS: 0.0` stubs. Enforced by `tools/check_policy_budgets.py` and hard gate `policy_headers_have_budget`.

## Deleted headers

None yet — await A/B evidence.
