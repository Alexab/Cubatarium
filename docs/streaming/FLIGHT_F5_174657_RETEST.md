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

## Manifest (required)

Record in suite report / notes:

- git SHA
- build type (Release)
- GPU / driver
- seed / world / route notes matching 174657
- **Next acceptance flight:** Strategy A `product_anchor` vs 090306 (drawable gates); then Q2b/Q4/Q6 cutover

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

- **175610 FAIL (post S1–S4):** worse than 162400 — stale med ~75k (≈100% `mesh_apply_stale_visual`), `discarded_late=116`, `job_rr=0`, `gpu_kick=0`, unfinished 9→41, enter `soft_clean` ~93s. Wall↓ is not acceptance. **S1–S4 SoftDefer/shed path CLOSED-AS-FAILED.**
- **Strategy A landed (code):** P0 docs; P1 revert S1+S2 + geom-only stamp (`InputsStillValid` ignores SoftDefer visual); M-Q2b DrawOracle/SmallOracleWorld unit gate; M-Q4 MeshInputs + catalog-pinned mesh batch flags; M-Q6 no synthetic `gpu_handle=1`, ShadowCompare→FirstMeshOwner cutover flag (default ShadowCompare).
- **P2 product_anchor:** Release `bin/Cubatarium.exe` rebuilt after P1+Q2b/Q4/Q6. **Manual F5 vs 090306 required** before `git tag product_anchor_YYYYMMDD`. Fill **Anchor** column below on PASS.
- **131523 / 162400 / 175610:** known-bad references only.

```text
python tools/AnalyzeEnterLit.py bin/logs/enter_lit_<NEW>.jsonl --fail-if-no-settle
python tools/CompareFlightF5.py --perf bin/logs/perf_<NEW>.jsonl --enter-lit bin/logs/enter_lit_<NEW>.jsonl
```

| Metric (cruise med) | 141350 | 174657 | 090306 | 131523 | 162400 | 175610 | Anchor |
|---|---|---|---|---|---|---|---|
| wall_ms | 141 | 48 | 44.6 | 36 | 52 | **37** | |
| mesh_emerge_med | — | — | 12.9 | ~4 | ~5 | ~3.4 | |
| prep_schedule_policy_ms | — | — | ~7.1 | ~0.01 | ~0.014 | ~0.016 | |
| mesh_apply_stale | 9 | 676 | 46 | ~1.1e5 | ~3.8e4 | **~7.5e4** | |
| mesh_apply_stale_delta | 0 | 0 | ~4 | ~361 | ~202 | **~312** | |
| mesh_apply_stale_visual | — | — | — | — | — | **≈stale** | |
| mesh_discarded_late | — | — | 0 | 0 | 0 | **116** | |
| unfinished_visual | 11 | 24 | 0 | 49 | 35 | **28 (9→41)** | |
| empty_backlog | — | — | 0 | 49 | 35 | **28** | |
| focus_missing_frac | — | — | 0.85 | 1 | 1 | 1 | |
| visual_holes_frac | — | — | 0.70 | 1 | 1 | 1 | |
| visible_black_focus_med | — | — | 63 | 15 | 21 | **36** | |
| column_job mesh / ready | 81/32 | 117/12 | 54/12 | ~49/0 | 44/0 | **44/0** | |
| pool_fence_timeout | — | — | 0 | 0 | 0 | 0 | |
| enter gate max continuous ms | — | ~61655 | ~75 | ~150000 | ~18 | **~93112 soft_clean** | |
| enter settle_reason | — | missing | live_blockers | force_ingame_no_uf | live_blockers | **soft_clean + abort** | |
