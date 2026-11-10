# A21 operator eye matrix — A25 return

Date: 2026-09-22

| Gate | Status |
|---|---|
| Operator eye (manual west / blacks / holes) | **UNTESTED** — required for merge_green |
| AF fog scorecard | proxy only — **AF ≠ eye** (§3.5 return plan) |
| Visible AF mid stall | diagnostic only — not CLOSED |
| End-of-flight black gate | OPEN (after holes-stable) |
| A24 safety (holes / dirty_dropped) | required on every AF |
| PreferKick counter | not sole heal DoD |
| merge_green | **false** until operator_visual=PASS |

CLOSED only after human eye PASS on west cruise with lit focus and no unexplained holes/blacks, plus frame budget A/B vs `dcc02e27` and stop-SLA.
