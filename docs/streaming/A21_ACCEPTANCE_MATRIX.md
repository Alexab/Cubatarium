# A21/A22 acceptance — status after S0–S6 code land

Date: 2026-09-22  
Branch: `cursor_audit7_impl`

| Step | Status | Notes |
|---|---|---|
| S0 evidence + visible baseline | DONE | `A22_MANUAL_133440.md`, flight log; debt 8 |
| S1 PendingLight heal | DONE (partial) | mid stall ↓; end-gate FAIL; 2 iters |
| S2 sole demand writer | DONE | `kChunkDemandShadow=false` |
| S3 publish validator | DONE | expected from live revs |
| S4 LightValidity ADR | DONE | `A22_ADR_LIGHT_SEAM.md`; seam extract follow-up |
| S5 fluid stamp-hit | DONE | cache before GetBlock scan |
| S6 critical unit 4ms | DONE | `MaxCriticalUnitMs` default 4 |
| S7 RingReadiness | **DEFERRED** | end-gate not green — flag stays OFF |
| S8 operator eye | **UNTESTED** | merge_green=false |

`operator_visual=UNTESTED` ⇒ `merge_green=false`. Visible AF Loop continues.
