# G1 A10 RelightReplace suite reports

Product gate proxy: `--scenario product-174657` (west yaw 180, no-teleport).

- Diff: [autofly_vs_manual_diff.md](autofly_vs_manual_diff.md)
- Route: `tools/manual_flight_world164_product_174657.json`
- North `--replay-manual` yaw 90 = smoke only (see `g1_progress/`)

Expect proxy baseline FAIL vs 141350 (VB/missing) until RelightReplace sole-owner lands.

Latest west manual evidence after RelightReplace Dirty sole-owner:

- `perf_20260914-100645_45256.jsonl` + `enter_lit_20260914-100713.jsonl`
- no-teleport west `(7,3)→(-3,3)`
- VB all/fly med `51/76`
- `focus_missing` med `1`
- `miss_stuck` max `616`
- `gpu_kick` fly med `0`

Verdict: E3 remains FAIL/freeze, not CLOSED. Proxy hardening must reproduce
miss/stuck class, not only early west VB.

proxy_v2 hardening (2026-09-14):

- defaults tightened to `idle15/fly38/stop20`
- report writes `proxy_adequacy`
- cold `112755` and visible-cold `113011` both fail adequacy:
  - `focus_missing=0`
  - `miss_stuck=0`
  - `fog_rd_fly≈2`
  - VB fly med `20.5` / `13.5`

Result: proxy now fails closed instead of looking green on healed medians.
Post-drain kick code landed, but this proxy still does not enter miss-class, so
there was no justified N3 MarkRelit follow-up from autofly alone.
