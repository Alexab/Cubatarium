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

## Manifest (required)

Record in suite report / notes:

- git SHA: tag **`product_anchor_20260912`** (`8c9bf59a` tip at tag); post-anchor tip advances on `cursor_audit_impl2`
- build type (Release); exe sha256 prefix `AC7003B329F8984B` (anchor build)
- route: same as 090306 / 174657-class
- INFO anchor: `...INFO.20260912-192013.9704`; reflight: `...INFO.20260912-194407.31392`

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
- **175610 FAIL:** SoftDefer/shed CLOSED-AS-FAILED.
- **Next:** F5 vs 192015 after SeamOwner/Q5–Q8 land; GreedyMesher catalog-only geometry; EvictionOwner; G1 VB/holes.

| Metric (cruise med) | 090306 | 175610 | **192015 Anchor** | **194409** |
|---|---|---|---|---|
| wall_ms | 44.6 | 37 | 52.6 | **45.2** |
| mesh_emerge_med | 12.9 | ~3.4 | 14.1 | **9.3** |
| prep_schedule_policy_ms | ~7.1 | ~0.016 | ~6.2 | **~0.43** |
| mesh_apply_stale | 46 | ~7.5e4 | 55 | **29** |
| mesh_apply_stale_delta | ~4 | ~312 | 4 | **2** |
| unfinished_visual | 0 | 28 | 0 | **2** |
| visual_holes_frac | 0.70 | 1 | 0.53 | **0** |
| visible_black_focus_med | 63 | 36 | 64 | **64** |
| column_job ready med | — | 0 | ~1 | **~6** |
| enter continuous ms | ~75 | ~93112 | ~78 | **~3216** |
| enter settle_reason | live_blockers | soft_clean | live_blockers | **live_blockers** |
