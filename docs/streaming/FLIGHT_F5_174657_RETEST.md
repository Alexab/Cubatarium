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
| Post-S1–S4 FAIL (175610) | `bin/logs/perf_20260912-175610_19976.jsonl` |
| Enter (175610 soft_clean) | `bin/logs/enter_lit_20260912-175738.jsonl` |
| **product_anchor (192015)** | `bin/logs/perf_20260912-192015_9704.jsonl` |
| Enter (192015) | `bin/logs/enter_lit_20260912-192029.jsonl` |

## Manifest (required)

Record in suite report / notes:

- git SHA: `8c9bf59a` (+ Strategy A stack); tag **`product_anchor_20260912`**
- build type (Release); exe sha256 prefix `AC7003B329F8984B`
- route: same as 090306 / 174657-class
- INFO: `Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260912-192013.9704`

## Commands

```text
# After flight:
python tools/AnalyzeEnterLit.py bin/logs/enter_lit_<new>.jsonl --fail-if-no-settle
python tools/CompareFlightF5.py --perf bin/logs/perf_<new>.jsonl --enter-lit bin/logs/enter_lit_<new>.jsonl
python tools/AnalyzePhase57Scorecard.py --report <report.json> --perf bin/logs/perf_<new>.jsonl --baseline-manual bin/logs/perf_20260910-141350_27632.jsonl --info <INFO_log>
```

## Hard acceptance (plan §F5)

1. No mass per-block / texture flicker (eye) and no growth of `pool_fence_timeout` / stuck `pool_retired_pending`.
2. Cruise `mesh_apply_stale` med ≤ 2× 141350 (~20); `mesh_apply_stale_delta` ≈ 090306 (единицы), не 131523/162400/175610-класс.
3. Enter: `enter_lit_gate_active` clears; settle ≠ `force_ingame_no_uf`; continuous ≪ 60s.
4. `unfinished_visual` / empty_backlog not worse than 141350; `column_job_render_ready_n` med > 0.
5. `wall_ms` may rise — **correctness > FPS**. Wall↓ with stale↑ is a false positive (175610).
6. After Q2: `visible_black_focus_med` / `focus_missing_frac` must not be accepted as PASS while red vs gates.
7. Strategy A product_anchor: stale ≪ 162400; job_rr>0; unfinished not growing early→late like 175610; eye: world draws.

## Status

- **product_anchor PASS (192015):** drawable gates match 090306 class — stale med **55** (vs 090306=46; 175610≈75k), stale_delta med **4**, unfinished **0**, prep_schedule ≈**6–7ms**, `column_job_render_ready_n` med **1** (gt0≈52%), enter `live_blockers` ~**78ms**. CompareFlightF5 still FAIL vs full 141350 product gates (stale>2×9, VB/holes) — **expected; G1 open**. Tag: `product_anchor_20260912`.
- **175610 FAIL:** SoftDefer/shed path CLOSED-AS-FAILED.
- **Next:** deepen M-Q4 catalog-only Compute (GreedyMesher registry reads) + M-Q6 Relight/Seam/Eviction cutover; F5 regressions vs **192015** anchor.

```text
python tools/AnalyzeEnterLit.py bin/logs/enter_lit_20260912-192029.jsonl --fail-if-no-settle
python tools/CompareFlightF5.py --perf bin/logs/perf_20260912-192015_9704.jsonl --enter-lit bin/logs/enter_lit_20260912-192029.jsonl
```

| Metric (cruise med) | 141350 | 174657 | 090306 | 131523 | 162400 | 175610 | **192015 Anchor** |
|---|---|---|---|---|---|---|---|
| wall_ms | 141 | 48 | 44.6 | 36 | 52 | 37 | **52.6** |
| mesh_emerge_med | — | — | 12.9 | ~4 | ~5 | ~3.4 | **14.1** |
| prep_schedule_policy_ms | — | — | ~7.1 | ~0.01 | ~0.014 | ~0.016 | **~6.2** |
| mesh_apply_stale | 9 | 676 | 46 | ~1.1e5 | ~3.8e4 | ~7.5e4 | **55** |
| mesh_apply_stale_delta | 0 | 0 | ~4 | ~361 | ~202 | ~312 | **4** |
| mesh_apply_stale_visual | — | — | — | — | — | ≈stale | **~43** |
| mesh_discarded_late | — | — | 0 | 0 | 0 | 116 | **0** |
| unfinished_visual | 11 | 24 | 0 | 49 | 35 | 28 (9→41) | **0** |
| empty_backlog | — | — | 0 | 49 | 35 | 28 | **0** |
| focus_missing_frac | — | — | 0.85 | 1 | 1 | 1 | **0.88** |
| visual_holes_frac | — | — | 0.70 | 1 | 1 | 1 | **0.53** |
| visible_black_focus_med | — | — | 63 | 15 | 21 | 36 | **64** |
| column_job mesh / ready | 81/32 | 117/12 | 54/12 | ~49/0 | 44/0 | 44/0 | **meshing~35 / ready~1** |
| pool_fence_timeout | — | — | 0 | 0 | 0 | 0 | **0** |
| enter gate max continuous ms | — | ~61655 | ~75 | ~150000 | ~18 | ~93112 soft_clean | **~78** |
| enter settle_reason | — | missing | live_blockers | force_ingame_no_uf | live_blockers | soft_clean + abort | **live_blockers** |
