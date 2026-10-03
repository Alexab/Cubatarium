# Sysreset v3 AF evidence (fog ON)

Date: 2026-09-20  
Commits: `bfb4201d` FaceDebt+hitch; `7f90f8d3` D4 light Accept no FaceDebt  
SoT manual: `perf_20260920-202031_6988.jsonl`  
Gates: [SYSRESET_OPERATOR_AF_GATES.md](SYSRESET_OPERATOR_AF_GATES.md)

## AF suite (record = r2 after D4)

| Run | perf | scorecard |
|---|---|---|
| cold | `bin/logs/perf_20260920-215802_33168.jsonl` | `sysreset_v3_cold_r2_score.json` |
| warm | `bin/logs/perf_20260920-220053_59408.jsonl` | `sysreset_v3_warm_r2_score.json` |
| dive | `bin/logs/perf_20260920-220332_42560.jsonl` | `sysreset_v3_dive_r2_score.json` |

## Gate matrix vs 202031 / v3 pass

| Gate | 202031 | cold r2 | warm r2 | dive r2 | Verdict |
|---|---|---|---|---|---|
| west | COVERED | COVERED | COVERED | COVERED | PASS |
| VB fly med ≤35 | 42 | **34.5** | 51.5 | 45.5 | cold PASS; warm/dive KEEP-ish |
| mid_fd_stalled ≤5 | 19 | **1** | 0 | 1 | PASS |
| unfinished max ≤4 | 12 | med 3 / max 59 | med 65 / max 78 | — | OPEN (enter/warm inflate) |
| prior_lit_hold fly med ≤400 | ~599 | **5** | **9** | — | PASS |
| flip/dual ≡0 | 0 | KEEP | KEEP | KEEP | PASS |
| mesh_emerge max ≤40 | 90–137 | 90 | 136 | — | OPEN (↓ vs SoT residual) |
| gpu_kick max ≤20 | ~114 | **14.7** | **11.4** | — | PASS |
| spikes max ≤2 | 5 | 6 | 5 | — | OPEN |
| transparent reorder mid ≤0.5 | 0–0.5 | 1.0 | 0 | 0 | mixed |
| cold blink ≤0.3 / non-block | 0.14 | **0** | 0.33 | 0 | cold PASS; warm ~boundary |
| eye-proxy | — | PASS | FAIL holes/blink | PASS | mixed |
| dual-lane | — | PASS | PASS | PASS | PASS |
| merge_green | false | false | false | false | OPEN (operator UNTESTED + hitch/unfinished) |

## Wins

- PriorLit hold count collapsed (~599 → ~5–9).
- Mid-Kick gate: kick max ≪114ms SoT class A.
- Cold VB ≤35, mid_fd_stalled ≤5, west COVERED, eye-proxy PASS.
- PreferKick stall clock fixed (note only when waiting without progress).

## Still OPEN (no SoftDefer spoof)

- unfinished max / warm med (FaceDebt overlay/BecameKnown residual + !RenderReady).
- emerge residual class B (max still >40; dirty_n flood ~300+; PreferKick N still 0 on AF).
- spikes max >2; finish ms spikes still hitch class A/B mix.
- merge_green blocked until operator eye + unfinished/hitch gates close.

## Blink honesty (phase E)

Cold blink 0 PASS. Warm blink 0.33 documented non-blocking per plan when unfinished/hitch still OPEN — does not alone claim merge_green.
