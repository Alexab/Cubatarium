---
name: remediation after A29 OPEN
overview: Successor after A29; dirty_dropped PASS, near_focus_holes still FAIL (best 2).
todos:
  - id: v1-holes
    content: Drive near_focus_holes periods>0 from ~2 to 0 on warm AF+manual (no heal-loop)
    status: pending
  - id: v2-eye
    content: Operator eye PASS + frame A/B vs dcc02e27 after v1
    status: pending
  - id: v3-p3-p6
    content: Finish residual P3 retirement + seam extract + fluid MT path
    status: pending
  - id: v4-ring
    content: RingReadiness ON only after v1+v2
    status: pending
  - id: v5-profile
    content: P8 only after v2 + corrected profile
    status: pending
isProject: false
---

# Remediation after A29 — remaining OPEN

Date: 2026-09-22  
Parent: [`A29_CONFORMANCE_AUDIT_2026-09-22.md`](../../docs/streaming/A29_CONFORMANCE_AUDIT_2026-09-22.md)  
Canon: [`ENGINE_REMEDIATION_PLAN_2026-09-22.md`](../../docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md)

## Non-goals

- No A22–A24 heal-loop / force_stale / RemoveChunk pending FD / shell-light mirror.
- No Ring ON until holes=0 + eye PASS.
- Keep dirty_dropped/period ≤800 (A28/A29 thrash CLOSED).

## Ordered remainders

1. **V1** — near_focus_holes → 0 (warm AF+manual). Last gap: miss_h≤1 sticky while schedule_ok>0 / SoftDefer clear — pin completion + snapshot under phase clamp.
2. **V2** — Operator eye + frame A/B.
3. **V3** — Residual P3/P4/P6 product fill.
4. **V4–V5** — Ring then P8 only after V1+V2.

## Final of this successor

Must end with conformance vs ENGINE_REMEDIATION + next plan for any still-OPEN.
