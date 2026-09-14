# Dual-lane S4 — west manual eye

Autofly gates (S1/S3) green. Product G1 remains OPEN.

## Result — PASS (2026-09-14)

| Field | Value |
|---|---|
| Perf | `bin/logs/perf_20260914-154921_33492.jsonl` |
| Enter | `bin/logs/enter_lit_20260914-154948.jsonl` |
| Corridor | `(7,3)→(−3,3)`, Y ~48–70 |
| Operator | visually correct; edits not tried |
| vs `122212` | VB 77 / StaleVL 75 / unlit cruise max 13 — class OK |
| vs `134914` | clearly better (closes black-chunk regress) |
| Wall | med ~71 ms (~14 FPS); stream~31 + emerge~28 |
| G1 CLOSED | **not claimed** (enter `live_blockers`, vs 141350 still open) |

Scorecard: [dual_lane_s4_manual_154921.json](dual_lane_s4_manual_154921.json)

## Do not

- Revert dual-lane / reintroduce FM-first order toggle for FPS
- SoftDefer/floors «от дыр»
- Weaken `visible_black≥40` / focus_missing / holes / enter gates
