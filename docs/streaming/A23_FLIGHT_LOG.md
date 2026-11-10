# A23 flight log (visible AF)

Protocol: `CUBA_FLIGHT_FOG_ON=1`, World_164, `product-174657` (+warm), **`--visible` required**.

## Status: REGRESSED (superseded by A24)

Land `a7acc856` closed mid-stall and PL-orphan but **failed no-regress**:
manual [`155529`](../../bin/logs/perf_20260922-155529_8064.jsonl) showed focus holes
(`near_focus_holes` 12/20 periods) and DirtyAdmit thrash (`dirty_dropped/period` ~1287).
See [`A24_REGRESS_AUDIT.md`](A24_REGRESS_AUDIT.md). End debt ≤8 never met.
`operator_visual=UNTESTED`. RingReadiness **OFF**.

No-regress vs A22 warm `142532` / S0 `135347` (original gates — not all met):
- mid FD stalled med ≤ **0** (warm) — met on AF
- end debt last3 ≤ **8** then → **0** — **FAIL**
- holes / dirty_dropped — **FAIL** on manual 155529

---

## No-regress table

| Run | mid FD stalled | end debt last3 | plf@end | vb_w/o plf sec | eye-proxy | notes |
|---|---:|---:|---:|---:|---|---|
| A22 S0 cold `135347` | ~13 | **8** | low | 52 | PASS | baseline |
| A22 warm `142532` | **0** | **45** | ~0 | — | blink FAIL | force_stale flood |
| A23 cold pre-D2 `145944` | 18 | **78** | **0** | 16 | PASS | PL erased while FD |
| A23 warm+D2 `151631` | **0**/1 | **43** | **14** | 0 | blink FAIL | heal owned |
| A23 D3 try `152133` | **0** | **98** | 45 | 0 | blink FAIL | **REGRESS — reverted** |
| A23 final warm `152522` | **0** | **62** | **18** | **0** | blink FAIL | false land |
| Manual `155529` | 0 | 32–40 | 6–17 | — | holes | **REGRESS confirmed** |

**Verdict:** A23 **REGRESSED**. A24 rolls back RemoveChunk×D2 exposure and seamed remesh heal; narrow equal-rev cooldown instead of force_stale flood.

---

## D0–D4 (historical)

D0 instrument, D1 bifurcate, D2 PL keep, D3 deferred, D4 eye UNTESTED — see A24 for remediation.
