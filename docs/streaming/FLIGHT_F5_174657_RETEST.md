# F5 retest procedure — 174657-class manual (flicker / enter audit gap)

## Goal

Repeat the same route/seed/HUD profile as `perf_20260910-174657_26996` after F0–F3, then compare stabilized metrics vs:

| Baseline | Log |
|---|---|
| Pre-remediation | `bin/logs/perf_20260910-141350_27632.jsonl` |
| Broken post-remediation | `bin/logs/perf_20260910-174657_26996.jsonl` |
| Q0/Q2 retest reference (090306) | `bin/logs/perf_20260912-090306_32784.jsonl` |
| Q4 apply-asymmetry regress (131523) | `bin/logs/perf_20260912-131523_29204.jsonl` |
| Enter (131523) | `bin/logs/enter_lit_20260912-132402.jsonl` |
| Post-R1 partial (162400) | `bin/logs/perf_20260912-162400_17536.jsonl` |
| Enter (162400) | `bin/logs/enter_lit_20260912-162609.jsonl` |

## Manifest (required)

Record in suite report / notes:

- git SHA
- build type (Release)
- GPU / driver
- seed / world / route notes matching 174657
- **Next acceptance flight:** Q0 schema-valid perf/INFO (no missing-field→0), plus Q2 attribution fields (`visible_black_*`, pending token, published allocation, pass/bounds version) in period samples

## Commands

```text
# After flight:
python tools/AnalyzeEnterLit.py bin/logs/enter_lit_<new>.jsonl --fail-if-no-settle
python tools/CompareFlightF5.py --perf bin/logs/perf_<new>.jsonl --enter-lit bin/logs/enter_lit_<new>.jsonl
python tools/AnalyzePhase57Scorecard.py --report <report.json> --perf bin/logs/perf_<new>.jsonl --baseline-manual bin/logs/perf_20260910-141350_27632.jsonl --info <INFO_log>
```

## Hard acceptance (plan §F5)

1. No mass per-block / texture flicker (eye) and no growth of `pool_fence_timeout` / stuck `pool_retired_pending`.
2. Cruise `mesh_apply_stale` med ≤ 2× 141350 (~20); `mesh_apply_stale_delta` ≈ 090306 (единицы), не 131523-класс (~361).
3. Enter: `enter_lit_gate_active` clears; settle ≠ `force_ingame_no_uf`; continuous ≪ 60s. Cross-check INFO `settle_reason=` with enter_lit `gate_end` (162400: INFO `live_blockers` but JSONL missing `gate_end` — telem gap).
4. `unfinished_visual` / empty_backlog not worse than 141350; `column_job_render_ready_n` med > 0.
5. `wall_ms` may rise vs 174657/131523 — **correctness > FPS**.
6. After Q2: `visible_black_focus_med` / `focus_missing_frac` must not be accepted as PASS while red vs gates — attribution fields required; unfinished_visual=0 is **not** proof of no black.
7. After 162400 class: `prep_schedule_policy_ms` must not stay ≈0 while `visual_holes=1`; period0 `mesh_apply_stale_delta` must not be 10^4-class SoftDefer/shed storm.

## Status

- **Code/tooling:** F0–F4 landed; Q0–Q10 follow-on; **R1–R3 landed** (apply NeighborDrawableFn, CaptureAndStore nullopt on admission fail, shed refuses stale-storm delta≥32).
- **131523:** known-bad reference (Q4 apply asymmetry). Do **not** treat as post-fix acceptance.
- **162400:** post-R1 partial — enter INFO=`live_blockers` (~13–18ms), but cruise still red: stale med ~38k / delta ~202, `prep_schedule_policy_ms`≈0.014, `column_job_render_ready_n`≈0, empty_backlog/UV med 35. Root class: Phase5.1 idle shed under visual_holes + SoftDefer seam MarkDirty thrash; enter_lit JSONL false FAIL (`gate_end` missing after `EndSession`). Next: S1–S4 code fixes then reflight (fill **New**).

```text
python tools/CompareFlightF5.py --perf bin/logs/perf_<NEW>.jsonl --enter-lit bin/logs/enter_lit_<NEW>.jsonl
```

| Metric (cruise med) | 141350 | 174657 | 090306 | 131523 | 162400 | New |
|---|---|---|---|---|---|---|
| wall_ms | 141 | 48 | 44.6 | 36 | 52 | |
| mesh_emerge_med | — | — | 12.9 | ~4 | ~5 | |
| prep_schedule_policy_ms | — | — | ~7.1 | ~0.01 | **~0.014** | |
| mesh_apply_stale | 9 | 676 | 46 | ~1.1e5 | **~3.8e4** | |
| mesh_apply_stale_delta | 0 | 0 | ~4 | ~361 | **~202** | |
| unfinished_visual | 11 | 24 | 0 | 49 | **35** | |
| empty_backlog | — | — | 0 | 49 | **35** | |
| focus_missing_frac | — | — | 0.85 | 1 | 1 | |
| visual_holes_frac | — | — | 0.70 | 1 | 1 | |
| visible_black_focus_med | — | — | 63 | 15 | 21 | |
| column_job mesh / ready | 81/32 | 117/12 | 54/12 | ~49 / **0** | 44 / **0** | |
| pool_fence_timeout | — | — | 0 | 0 | 0 | |
| enter gate max continuous ms | — | ~61655 | ~75 | ~150000 | ~18 (INFO) | |
| enter settle_reason | — | missing | live_blockers | force_ingame_no_uf | INFO live_blockers; JSONL gap | |
