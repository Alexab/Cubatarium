# Dual-lane S4 — west manual eye checklist

Autofly gates (S1/S3) are green vs `125123`/`133817`/`125331`. Product G1 remains OPEN.

## Operator flight

1. Build tip with dual-lane (`b84c735e` or later on `cursor_audit_impl4`).
2. World_164, eye-level west `(7,3)→(−3,3)` — same corridor as `122212` / `134914`.
3. No Space climb; stay Y ~50–60.
4. Record `perf_*.jsonl` + enter_lit; note subjective black FullyDark/StaleVL chunks.

## Pass vs fail

| Compare | Pass |
|---|---|
| vs `122212` | StaleVL/VB/unlit max ≤ that class; no new black-chunk feel |
| vs `134914` | Must be clearly better (not P3 regress class) |
| G1 CLOSED | **Do not claim** without separate PASS vs 141350 |

Paste perf path + medians into `FLIGHT_F5_174657_RETEST.md` when done.
