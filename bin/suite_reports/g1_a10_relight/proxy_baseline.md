# product-174657 proxy baseline (E0d)

Tip: `feb43173` (+ locus pin + fog_pull_in off during scenario).
Scenario: `--scenario product-174657` (yaw 180, pin focus~(7,3), idle15/fly45/stop30, fog_pull_in=false for run).

## Adequacy vs manual 080455

| Check | Result |
|---|---|
| West corridor | PASS — cold `094214` focus `(7,3)→(−5,3)` |
| Early west VB class | PASS — early fly VB **115** with fog **4** (manual class) |
| Full-run VB med | **20** (heal after fog/RD catch-up) — not sole gate |
| focus_missing | still 0 (manual had miss=1) — residual adequacy gap |
| flight_sim `pass` | false (holes/stream pressure) |

**Verdict:** proxy is **adequate for west stress + early VB FAIL class**. Do **not** claim G1 product CLOSED on full-run VB med alone. Gate for A10 work: early_west_vb (first ~12 fly periods) ≥40 and/or holes FAIL, while progress telem may still move.

North `--replay-manual` yaw90 remains smoke only.

## Cold

- Report: `proxy_baseline_cold.json`
- Perf: `bin/logs/perf_20260914-094214_17884.jsonl`
- Metrics (analyze): VB med 20, fly_vb_max 115, holes_rate ~0.18, focus `(7,3)→(−5,3)`, chunks_traveled 12
- Early fly VB sample: `[115×6, 108×3, 104…]`, fog stays 4

## Warm

- Report: `proxy_baseline_warm.json`
- Perf: `bin/logs/perf_20260914-094516_38780.jsonl`
- Metrics: VB med 14.5, fly_vb_max **116**, holes_rate ~0.31, focus `(7,3)→(−7,3)`, chunks_traveled 14
- `pass`: false

## Compact score

See `proxy_baseline_summary.json`.

## E1 branch

**→ E2 RelightReplace sole-owner** (expected): early west VB FAIL class + dual Dirty writers remain; full-run med PASS is heal artifact, not product close.
