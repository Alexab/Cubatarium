---
name: remediation after A27 OPEN
overview: Successor after A27 S-cycle; holes/thrash FAIL and eye UNTESTED remain.
todos:
  - id: t1-holes-thrash
    content: "Fix near_focus_holes=0 and dirty_dropped/period≤800 on warm AF+manual"
  - id: t2-eye
    content: "Operator eye PASS + frame A/B vs dcc02e27 after t1"
  - id: t3-p3-cross
    content: "Wire ArtifactManifest for cross/shell residual publish sites"
  - id: t4-seam-commit
    content: "Product seam coverage commit path (shell mirror FREEZE)"
  - id: t5-fluid-worker
    content: "Real fluid summary worker (not enqueue stub) + hitch gate in flight"
  - id: t6-ring
    content: "RingReadiness ON only after t1+t2"
  - id: t7-profile
    content: "P8 only after t2 + corrected profile"
isProject: false
---

# Remediation after A27 — remaining OPEN

Date: 2026-09-22  
Parent: [`A27_CONFORMANCE_AUDIT_2026-09-22.md`](../../docs/streaming/A27_CONFORMANCE_AUDIT_2026-09-22.md)  
Canon: [`ENGINE_REMEDIATION_PLAN_2026-09-22.md`](../../docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md)

## Non-goals

- No A22–A24 heal-loop expansion / force_stale / RemoveChunk pending FD / shell-light mirror.
- No Ring ON until eye + A24 stop-SLA PASS.

## Ordered remainders

1. **T1** — Holes + thrash (A24 safety) on warm AF+manual.  
2. **T2** — Operator eye + frame A/B (merge_green).  
3. **T3** — Residual P3 cross/shell manifests.  
4. **T4** — Seam coverage commit product path.  
5. **T5** — Real fluid worker continuation.  
6. **T6–T7** — Ring then P8 only after T1+T2.

## Final of this successor

Must end with conformance vs ENGINE_REMEDIATION + next plan for any still-OPEN.
