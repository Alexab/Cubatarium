# G1 A10 RelightReplace suite reports

Product gate proxy: `--scenario product-174657` (west yaw 180, no-teleport).

- Diff: [autofly_vs_manual_diff.md](autofly_vs_manual_diff.md)
- Route: `tools/manual_flight_world164_product_174657.json`
- North `--replay-manual` yaw 90 = smoke only (see `g1_progress/`)

Expect proxy baseline FAIL vs 141350 (VB/missing) until RelightReplace sole-owner lands.

Latest west manual evidence (E3 FAIL/freeze, not CLOSED):

- `perf_20260914-122212_41064.jsonl` + `enter_lit_20260914-122244.jsonl`
  - corridor `(7,3)→(-3,3)`, VB all/fly med `78/78.5`, `focus_missing` med `1`,
    `miss_stuck` max `311`, StaleVL ~`77`, `gpu_kick` fly med `0`,
    `gpu_kick_post_drain_n` max `1` (does not clear stuck)
- prior: `perf_20260914-100645_45256.jsonl` + `enter_lit_20260914-100713.jsonl`
  - VB all/fly med `51/76`, `miss_stuck` max `616`

proxy_v2 (2026-09-14) fail-closed but **not miss-class**:

- cold `112755` / visible `113011`: `focus_missing=0`, `miss_stuck=0`,
  `fog_rd≈2`, VB fly med `20.5` / `13.5`
- root cause: `hold_space` climb Y `~76→300` (manual stays `~50→58`)

proxy_v3: `hold_space=False`, `--min-alt-above-sea 0`, `--cruise-eye-y 56`
(land-eye floor without Space climb), pin eye Y ~56, altitude + west-travel
adequacy gates.

Cold `125123` (**adequacy PASS**): focus `(7,3)→(2,3)`, VB fly med `80.5`,
`focus_missing` med `1`, `miss_stuck` tail max `158`, fog_rd `4`,
player_y `50→60` (Δ+10), `gpu_kick` fly med `1`, `gpu_kick_post_drain` max `1`.
Stuck spawn without cruise-eye (`124719`) rejected via `focus_not_west`.

Post-drain kick is exercised under miss-class; product G1 still OPEN (VB/stuck
FAIL vs 141350). Next: drain/consume under miss (no floors), then west manual eye.
