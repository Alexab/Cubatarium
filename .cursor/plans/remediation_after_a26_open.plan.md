---
name: remediation after A26 OPEN
overview: Successor after A26 N-cycle; only remaining OPEN/UNTESTED vs ENGINE_REMEDIATION.
todos:
  - id: s1-eye
    content: Operator eye PASS per A25_R6_EYE_PROTOCOL + frame A/B vs dcc02e27
    status: completed
  - id: s2-flight-stop
    content: Warm AF+manual prove stop-SLA / dirty_dropped≤800 / holes=0
    status: completed
  - id: s3-p3-remaining
    content: Finish residual publish callers beyond greedy CPU+GPU; fence free e2e
    status: completed
  - id: s4-p4-seam-product
    content: Seam extract product path (D3 shell mirror stays FREEZE)
    status: completed
  - id: s5-p5-p6-flight
    content: Wire resumable/unified pools + fluid worker into hot paths; hitch gate in flight
    status: completed
  - id: s6-ring
    content: RingReadiness ON only after s1+s2
    status: completed
  - id: s7-profile
    content: P8 only after s1 + corrected profile
    status: completed
isProject: false
---

# Remediation after A26 — remaining OPEN

Date: 2026-09-22  
Parent conformance: [`A26_CONFORMANCE_AUDIT_2026-09-22.md`](../../docs/streaming/A26_CONFORMANCE_AUDIT_2026-09-22.md)  
Canon: [`ENGINE_REMEDIATION_PLAN_2026-09-22.md`](../../docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md)

## Non-goals

- No heal-loop A22–A24 expansion.
- No Ring ON until eye + stop-SLA.
- No shell-light mirror / force_stale flood / RemoveChunk pending FD.

## Ordered remainders

1. **S1** — Operator eye + frame A/B (merge_green gate).
2. **S2** — Flight stop-SLA and A24 safety on warm AF+manual.
3. **S3** — Residual P3 callers + fence retirement e2e.
4. **S4** — Product seam coverage (manifest FREEZE intact).
5. **S5** — Hot-path P5/P6 wiring beyond unit stubs.
6. **S6** — RingReadiness per A26_N7 criteria.
7. **S7** — P8 profile opts per A21_P8_PROFILE_GATES.

## Final of this successor

Must end with conformance vs ENGINE_REMEDIATION + next plan for any still-OPEN.
