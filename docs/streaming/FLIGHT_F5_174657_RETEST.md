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
| Post-anchor reflight (194409) | `bin/logs/perf_20260912-194409_31392.jsonl` |
| Enter (194409) | `bin/logs/enter_lit_20260912-194425.jsonl` |
| Post-Eviction/catalog (201118) | `bin/logs/perf_20260912-201118_28700.jsonl` |
| Enter (201118) | `bin/logs/enter_lit_20260912-201135.jsonl` |
| Post-Q8 soft-defer (203306) | `bin/logs/perf_20260912-203306_7532.jsonl` |
| Enter (203306) | `bin/logs/enter_lit_20260912-203321.jsonl` |
| Post-shadow-telem (211857) | `bin/logs/perf_20260912-211857_30432.jsonl` |
| INFO (211857) | `bin/logs/...INFO.20260912-211853.30432` (no enter_lit JSONL; settle in INFO) |
| Post-Decide split (091748) | `bin/logs/perf_20260913-091748_40588.jsonl` |
| Enter (091748) | `bin/logs/enter_lit_20260913-091804.jsonl` |

## Manifest (required)

Record in suite report / notes:

- git SHA: tag **`product_anchor_20260912`** (`8c9bf59a` tip at tag); post-anchor tip advances on `cursor_audit_impl2`
- build type (Release); exe sha256 prefix `AC7003B329F8984B` (anchor build)
- route: same as 090306 / 174657-class
- INFO anchor: `...INFO.20260912-192013.9704`; reflight: `...INFO.20260912-194407.31392`; post-cutover: `...INFO.20260912-201115.28700`; post-Q8: `...INFO.20260912-203304.7532`; post-shadow-field: `...INFO.20260912-211853.30432`; post-Decide-split: `...INFO.20260913-091746.40588`

## Commands

```text
python tools/AnalyzeEnterLit.py bin/logs/enter_lit_<new>.jsonl --fail-if-no-settle
python tools/CompareFlightF5.py --perf bin/logs/perf_<new>.jsonl --enter-lit bin/logs/enter_lit_<new>.jsonl
```

## Hard acceptance (plan §F5)

1. No mass per-block / texture flicker (eye) and no growth of `pool_fence_timeout` / stuck `pool_retired_pending`.
2. Cruise `mesh_apply_stale` med ≤ 2× 141350 (~20); `mesh_apply_stale_delta` ≈ 090306 (единицы).
3. Enter: settle ≠ `force_ingame_no_uf`; continuous ≪ 60s.
4. `unfinished_visual` / empty_backlog not worse than 141350; `column_job_render_ready_n` med > 0.
5. Correctness > FPS. Wall↓ with stale↑ is a false positive (175610).
6. VB/missing not PASS while red vs 141350 gates (G1 open).
7. product_anchor: stale ≪ 162400; job_rr>0; unfinished not 175610-class growth.

## Status

- **product_anchor PASS (192015):** tag `product_anchor_20260912`. Drawable gates = 090306 class.
- **194409 post-anchor reflight:** **no drawable regress** vs 192015 — stale **29** (better), stale_delta **2**, job_rr med **6**, visual_holes_frac **0**, enter `live_blockers` ~**3.2s** (≪60s). unfinished scorecard med **2** (anchor 0). Still FAIL full 141350 product gates (stale>2×9, VB, miss_stuck+gpu_kick~0). Keep 192015 as SoT anchor.
- **201118 post-EvictionOwner/catalog mesher:** **drawable PASS vs 192015** — stale **23** (better), stale_delta **4**, unfinished **0**, job_rr med **~24**, enter `live_blockers` ~**76ms**, discarded_late **0**, pool_fence_timeout **0**. holes_frac **0.65** (≈anchor 0.53; worse than 194409’s 0 — note, not mass-missing). Still FAIL full 141350 product gates (stale>2×9, VB, holes, miss_stuck+gpu_kick~0). Default cutover remains **ShadowCompare**.
- **203306 post-Q8 soft-defer:** **drawable PASS vs 192015** — stale **29**, unfinished **0**, job_rr med **~22**, holes_frac **0.24** (better than anchor 0.53 and 201118 0.65), VB med **76** (slightly up), enter `live_blockers` ~**2.9s** (≪60s), wall **34.5**. Still FAIL full 141350 (stale>2×9, VB, miss_stuck+gpu_kick~0). holes gate not in FAIL list this run.
- **211857 post-shadow-field reflight:** **drawable PASS vs 192015** — stale **31**, unfinished **0**, job_rr med **~40**, holes_frac **0.37**, enter INFO ~**135ms**. VB med **92** (G1). `shadow_mismatch_n` **~10k** — Sync spam (fixed in `26e8475b`).
- **091748 post-Decide telem split (pre parity fix):** **drawable PASS vs 192015** — stale **36**, unfinished **0**, job_rr **~31**, holes **0.21**, VB **57**, wall **42**, enter ~**3.1s**. `stage_disagree` med **~11** (gauge OK). `shadow_mismatch_n` still **~5k** — Decide used legacy job map + refresh spam (fixed post-091748). Still FAIL full 141350 (missing/VB).
- **175610 FAIL:** SoftDefer/shed CLOSED-AS-FAILED.
- **Next:** remeasure Decide mismatch after RecordWants + new-ticket-only telem; G1 VB; Q4 catalog-only worker; Q9 paired acceptance. Cutover stays **ShadowCompare** until Decide parity honest.

| Metric (cruise med) | 090306 | 175610 | **192015 Anchor** | **194409** | **203306** | **211857** | **091748** |
|---|---|---|---|---|---|---|---|
| wall_ms | 44.6 | 37 | 52.6 | **45.2** | **34.5** | **74.0** | **42.0** |
| mesh_emerge_med | 12.9 | ~3.4 | 14.1 | **9.3** | **8.3** | **28.4** | **6.9** |
| mesh_apply_stale | 46 | ~7.5e4 | 55 | **29** | **29** | **31** | **36** |
| unfinished_visual | 0 | 28 | 0 | **2** | **0** | **0** | **0** |
| visual_holes_frac | 0.70 | 1 | 0.53 | **0** | **0.24** | **0.37** | **0.21** |
| visible_black_focus_med | 63 | 36 | 64 | **64** | **76** | **92** | **57** |
| column_job ready med | — | 0 | ~1 | **~6** | **~22** | **~40** | **~31** |
| enter continuous ms | ~75 | ~93112 | ~78 | **~3216** | **~2905** | **~135** | **~3130** |
| shadow_mismatch_n | — | — | — | — | — | **~10k Sync** | **~5k Decide** |
| stage_disagree_n | — | — | — | — | — | — | **~11** |
