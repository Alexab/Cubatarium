# F5 retest procedure — 174657-class manual (flicker / enter audit gap)

## Goal

Repeat the same route/seed/HUD profile as `perf_20260910-174657_26996` after F0–F3, then compare stabilized metrics vs:

| Baseline | Log |
|---|---|
| Pre-remediation | `bin/logs/perf_20260910-141350_27632.jsonl` |
| Broken post-remediation | `bin/logs/perf_20260910-174657_26996.jsonl` |
| Enter | `bin/logs/enter_lit_20260910-174709.jsonl` |

## Manifest (required)

Record in suite report / notes:

- git SHA
- build type (Release)
- GPU / driver
- seed / world / route notes matching 174657

## Commands

```text
# After flight:
python tools/AnalyzeEnterLit.py bin/logs/enter_lit_<new>.jsonl --fail-if-no-settle
python tools/AnalyzePhase57Scorecard.py --perf bin/logs/perf_<new>.jsonl --baseline-manual bin/logs/perf_20260910-141350_27632.jsonl --info <INFO_log>
```

## Hard acceptance (plan §F5)

1. No mass per-block / texture flicker (eye) and no growth of `pool_fence_timeout` / stuck `pool_retired_pending`.
2. Cruise `mesh_apply_stale` med ≤ 2× 141350 (~20).
3. Enter: `enter_lit_gate_active` clears; `gate_elapsed_ms` finite (not ~60s solid active without progress).
4. `unfinished_visual` / empty_backlog not worse than 141350.
5. `wall_ms` may rise vs 174657 — **correctness > FPS**.

## Status

- **Code/tooling:** F0–F4 landed; Release `bin/Cubatarium.exe` rebuilt; `greedy_vertex_pool_lifetime_test: ok`.
- **Historical gate check (174657):** `tools/CompareFlightF5.py` must FAIL on stale storm + enter gate still active — proves F0 gates catch the known-bad flight.
- **New manual flight:** run same route on this build, then:

```text
python tools/CompareFlightF5.py --perf bin/logs/perf_<NEW>.jsonl --enter-lit bin/logs/enter_lit_<NEW>.jsonl
```

Fill the table below after the new flight.

| Metric (cruise med) | 141350 | 174657 | New |
|---|---|---|---|
| wall_ms | 141 | 48 | |
| mesh_apply_stale | 9 | 676 | |
| unfinished_visual | 11 | 24 | |
| column_meshing / render_ready | 81/32 | 117/12 | |
| column_job_* | n/a | n/a | |
| pool_fence_timeout / retired_pending | n/a | n/a | |
| enter gate max continuous ms | — | ~61655 | |
| enter settle_reason | — | missing | |
