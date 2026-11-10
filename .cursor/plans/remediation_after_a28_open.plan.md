---
name: remediation after A28 OPEN
overview: Successor after A28; dirty_dropped PASS, near_focus_holes still FAIL.
todos:
  - id: u1-holes
    content: "Drive near_focus_holes periods>0 to 0 on warm AF+manual (no heal-loop)"
  - id: u2-eye
    content: "Operator eye PASS + frame A/B vs dcc02e27 after u1"
  - id: u3-p3-p6
    content: "Finish residual P3 cross/shell + seam commit + fluid worker fill"
  - id: u4-ring
    content: "RingReadiness ON only after u1+u2"
  - id: u5-profile
    content: "P8 only after u2 + corrected profile"
isProject: false
---

# Remediation after A28 — remaining OPEN

Date: 2026-09-22  
Parent: [`A28_CONFORMANCE_AUDIT_2026-09-22.md`](../../docs/streaming/A28_CONFORMANCE_AUDIT_2026-09-22.md)  
Canon: [`ENGINE_REMEDIATION_PLAN_2026-09-22.md`](../../docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md)

## Non-goals

- No A22–A24 heal-loop / force_stale / RemoveChunk pending FD / shell-light mirror.
- No Ring ON until holes=0 + eye PASS.
- Keep dirty_dropped/period ≤800 (A28 T1).

## Ordered remainders

1. **U1** — near_focus_holes → 0 (warm AF+manual).  
2. **U2** — Operator eye + frame A/B.  
3. **U3** — Residual P3/P4/P6 product fill.  
4. **U4–U5** — Ring then P8 only after U1+U2.

## Final of this successor

Must end with conformance vs ENGINE_REMEDIATION + next plan for any still-OPEN.
