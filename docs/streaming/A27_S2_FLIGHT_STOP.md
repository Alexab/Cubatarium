# A27 S2 — Flight stop-SLA / A24 safety

Date: 2026-09-22  
Log: `bin/_a27_s12_warm_af.log`  
Perf: `bin/logs/perf_20260922-203434_29964.jsonl`

## Warm AF (`product-174657`, fog-ON, warmup-sec 20)

| Gate | Result |
|---|---|
| Adequacy | PASS |
| A24 safety (`near_focus_holes_periods_gt0==0`, `dirty_dropped/period≤800`) | **FAIL** — holes_gt0=10; dirty_dropped/period≈30180 |
| Eye-proxy | **FAIL** — near_focus_holes_mid_med=1; blink_rate |
| Dual-lane | **FAIL** — unlit_max |
| Stop-SLA / holes=0 | **OPEN / FAIL** |

## Manual

Not re-run this cycle; AF already proves A24 safety not CLOSED.

## Implication

S6 Ring stays OFF. `merge_green=false`. Successor must address holes + thrash before eye PASS.
