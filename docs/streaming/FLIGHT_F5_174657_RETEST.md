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
| Post-RecordWants (095318) | `bin/logs/perf_20260913-095318_39488.jsonl` |
| Enter (095318) | `bin/logs/enter_lit_20260913-095332.jsonl` |
| Post-keep-until-replace (100100) | `bin/logs/perf_20260913-100100_38604.jsonl` |
| Enter (100100) | `bin/logs/enter_lit_20260913-100115.jsonl` |
| Post-Q4 catalog pin (111618) | `bin/logs/perf_20260913-111618_40168.jsonl` |
| Enter (111618) | `bin/logs/enter_lit_20260913-111634.jsonl` |
| Post-FirstMeshOwner (124958) | `bin/logs/perf_20260913-124958_42260.jsonl` |
| Enter (124958) | `bin/logs/enter_lit_20260913-125019.jsonl` |
| Post-RelightOwner (130708) | `bin/logs/perf_20260913-130708_41652.jsonl` |
| Enter (130708) | `bin/logs/enter_lit_20260913-130721.jsonl` |
| Post-SeamOwner (131728) | `bin/logs/perf_20260913-131728_42144.jsonl` |
| Enter (131728) | `bin/logs/enter_lit_20260913-131742.jsonl` |
| Post-EvictionOwner (133156) | `bin/logs/perf_20260913-133156_23136.jsonl` |
| Enter (133156) | `bin/logs/enter_lit_20260913-133214.jsonl` |
| Post-Q4 GPU-extract catalog (133707) | `bin/logs/perf_20260913-133707_37796.jsonl` |
| Enter (133707) | `bin/logs/enter_lit_20260913-133721.jsonl` |
| Post-Q4main+Q7 batch (162247) | `bin/logs/perf_20260913-162247_18156.jsonl` |
| Enter (162247) | `bin/logs/enter_lit_20260913-162301.jsonl` |
| Q9 cold#1 / post-Q8 (163717) | `bin/logs/perf_20260913-163717_34068.jsonl` |
| Enter (163717) | `bin/logs/enter_lit_20260913-163733.jsonl` |
| Q9 cold#2 (164153) | `bin/logs/perf_20260913-164153_40080.jsonl` |
| Enter (164153) | `bin/logs/enter_lit_20260913-164205.jsonl` |
| Q9 cold#3 + edits (164735) | `bin/logs/perf_20260913-164735_20040.jsonl` |
| Enter (164735) | `bin/logs/enter_lit_20260913-164751.jsonl` |

## Manifest (required)

Record in suite report / notes:

- git SHA: tag **`product_anchor_20260912`** (`8c9bf59a` tip at tag); post-anchor tip advances on `cursor_audit_impl2`
- build type (Release); exe sha256 prefix `AC7003B329F8984B` (anchor build)
- route: same as 090306 / 174657-class
- INFO … post-Q4main+Q7 batch: `...INFO.20260913-162245.18156`; Q9 cold#1 / post-Q8: `...INFO.20260913-163715.34068`

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
- **091748 post-Decide telem split (pre parity fix):** **drawable PASS vs 192015** — stale **36**, unfinished **0**, job_rr **~31**, holes **0.21**, VB **57**, wall **42**, enter ~**3.1s**. `stage_disagree` med **~11**. `shadow_mismatch_n` still **~5k**.
- **095318 post-RecordWants (`73d3403b`):** **drawable PASS** — stale **24**, holes **0.06**, enter ~**99ms**, mismatch **~2.6k** climbing.
- **100100 post-keep-until-replace (`b533fea1`):** **drawable PASS vs 192015** — stale **36.5**, unfinished **0**, holes **0**, job_rr **~10**, enter ~**2.8s**, wall **38.5**, VB **70**. `stage_disagree` med **~2** (late cruise **0**). `shadow_mismatch_n` cruise med **~251**, **plateau at 252** (deltas≈0 in late cruise) — ~10× better than 095318. Remaining ~250 from enter/warmup (RenderReady vs FirstMesh policy). Steady cruise Decide parity OK for cutover *candidate*; still FAIL full 141350 (missing/VB).
- **111618 post-Q4 catalog pin (`f1c65bcd`):** **drawable PASS vs 192015** — stale **29.5**, unfinished **0**, holes **0**, job_rr **~12**, enter ~**95ms**, wall **39.8**, VB **80**. `stage_disagree` med **~7**. `shadow_mismatch_n` cruise med **~434**, late plateau **~444** (delta≈0). Worse mismatch than 100100 (RenderReady vs FirstMesh want) but still flat cruise. Product vs 141350 still FAIL (missing/VB; `miss_stuck`+gpu_kick~0).
- **124958 post-FirstMeshOwner (`eba245d9`):** **drawable PASS vs 192015** — stale **37.5**, unfinished **0**, holes **0**, job_rr **~10**, enter ~**2.6s**, wall **32.7**, VB **~71**. `stage_disagree` med **~3–6**. `shadow_mismatch_n` cruise med **~16** (max **34**) — **~25× better than 111618**; residual = Relight/Seam/Evict still ShadowCompare. Product vs 141350 still FAIL (missing/VB); `miss_stuck`+gpu_kick not in FAIL list this run.
- **130708 post-RelightOwner (`9bc41087`):** **drawable PASS vs 192015** — stale **24**, unfinished **0**, holes **0**, job_rr **~18**, enter ~**67ms**, wall **36.4**, VB **~75.5**. `stage_disagree` med **~11**. `shadow_mismatch_n` **0** (full cruise) — Decide parity clean under RelightOwner; Seam/Evict ShadowCompare not producing Decide gaps. Product vs 141350 still FAIL (missing/VB; `miss_stuck`+gpu_kick~0).
- **131728 post-SeamOwner (`d7373dab`):** **drawable PASS vs 192015** — stale **25**, unfinished **0**, holes **0**, job_rr **~7.5**, enter ~**2.15s**, wall **34.8**, VB **~74**. `stage_disagree` med **~1**. `shadow_mismatch_n` **0**. SeamOwner holds drawable; Eviction still ShadowCompare. Product vs 141350 still FAIL (missing/VB).
- **133156 post-EvictionOwner (`9c841c72`):** **drawable PASS vs 192015** — stale **16**, unfinished **0**, holes **0**, job_rr **~8**, enter ~**68ms**, wall **49.1**, VB **~70**. `stage_disagree` med **~2**. `shadow_mismatch_n` **0**. Q6 cutover ladder F5-proven (FirstMesh→Relight→Seam→Eviction). Product vs 141350 still FAIL (missing/VB).
- **133707 post-Q4 worker GPU-extract catalog (`911dec6e`):** **drawable PASS vs 192015** — stale **29**, unfinished **0**, holes **0**, job_rr **~7**, enter ~**2.65s**, wall **44.4**, VB **~80**. `stage_disagree` med **0**. `shadow_mismatch_n` **0**. Product vs 141350 still FAIL (missing/VB; miss_stuck).
- **162247 post-Q4main+Q7 batch (`7a1f8c70`):** **drawable PASS vs 192015** — stale **29**, unfinished **0**, holes **0**, job_rr **~14.5**, enter ~**71ms**, wall **34.8**, VB **~63**. `stage_disagree` med **~4**. `shadow_mismatch_n` **0**. Batch (main-thread catalog Kick + worker capture reserve) holds drawable; VB best recent vs 141350 still red on missing/VB gates.
- **163717 Q9 cold#1 / post-Q8 (`9ab070e0`):** **drawable PASS vs 192015** — stale **55.5** (≈anchor), unfinished **0**, holes **0**, job_rr **~8**, enter ~**2.64s**, wall **50.4**, VB **~67**. `shadow_mismatch_n` **0**. `frame_deadline_remaining_ms` cruise med **0** (Q8 soft deadline active). Product vs 141350 still FAIL (missing/VB). **Q9 freeze build = `9ab070e0`** — do not rebuild mid-batch.
- **164153 Q9 cold#2:** **drawable PASS** — stale **46.5**, unfinished **0**, holes **0**, enter ~**75ms**, wall **42.9**, VB **~71**, mismatch **0**.
- **164735 Q9 cold#3 (+edits at end):** **drawable PASS** — unfinished **0**, holes **0**, mismatch **0**, enter ~**3.0s**, wall **37.5**, VB **~70**. stale **115** and `drop_no_active` **17** elevated vs cruise-only (edit stress / `phase_abort_heavy`); not 175610-class. New process PID → counts as **cold**, not warm.
- **175610 FAIL:** SoftDefer/shed CLOSED-AS-FAILED.
- **Next:** **warm ×3** — same exe, **do not close** the game between the three; repeat route (edits optional once in the series).

| Metric (cruise med) | **163717** | **164153** | **164735** |
|---|---|---|---|
| wall_ms | 50.4 | **42.9** | **37.5** |
| mesh_apply_stale | 55.5 | **46.5** | **115** (edits) |
| unfinished_visual | 0 | **0** | **0** |
| visual_holes | 0 | **0** | **0** |
| enter continuous ms | ~2644 | **~75** | **~3028** |
| shadow_mismatch_n | 0 | **0** | **0** |

## Q9 batch procedure (G0)

**Frozen tip:** `9ab070e0`. Same `bin/Cubatarium.exe` for all six. Do **not** rebuild or change thresholds.

| Slot | Status | Perf |
|---|---|---|
| cold 1 | **done** | `perf_20260913-163717_34068.jsonl` |
| cold 2 | **done** | `perf_20260913-164153_40080.jsonl` |
| cold 3 | **done** (+edits) | `perf_20260913-164735_20040.jsonl` |
| warm 1 | pending | |
| warm 2 | pending | |
| warm 3 | pending | |

1. Cold ×3: full restart → same route as 163717/174657-class → stop after cruise.
2. Warm ×3: reload/continue without cold restart if possible, same route.
3. Prefer covering once across the six: reverse, small edits, unload/reload, normal shutdown.
4. Aggregate:

```text
python tools/q9_acceptance_suite.py --cold 3 --warm 3 --seed <seed> --route 174657-class \
  --out bin/suite_reports/q9_<stamp>.json \
  --flight bin/logs/perf_20260913-163717_34068.jsonl:bin/logs/enter_lit_20260913-163733.jsonl \
  --flight ... (5 more pairs)
```

