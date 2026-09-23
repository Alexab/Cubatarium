---
name: remediation after A30 OPEN
overview: Successor after A30; dirty_dropped PASS, near_focus_holes still FAIL (best 2 from A29). V1 PreferKick/age/snapshot probes regressed — do not repeat.
todos:
  - id: w1-holes
    content: Drive near_focus_holes periods>0 from ~2 to 0 on warm AF+manual (no heal-loop; no PreferKick thrash)
    status: pending
  - id: w2-eye
    content: Operator eye PASS + frame A/B vs dcc02e27 after w1
    status: pending
  - id: w3-p3-p6
    content: Finish residual P3 retirement + seam permanence + fluid MT residual
    status: pending
  - id: w4-ring
    content: RingReadiness ON only after w1+w2
    status: pending
  - id: w5-profile
    content: P8 only after w2 + corrected profile
    status: pending
isProject: false
---

# Remediation after A30 — remaining OPEN

Date: 2026-09-22  
Parent: [`A30_CONFORMANCE_AUDIT_2026-09-22.md`](../../docs/streaming/A30_CONFORMANCE_AUDIT_2026-09-22.md)  
Canon: [`ENGINE_REMEDIATION_PLAN_2026-09-22.md`](../../docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md)

## Non-goals

- No A22–A24 heal-loop / force_stale / RemoveChunk pending FD / shell-light mirror.
- No Ring ON until holes=0 + eye PASS.
- Keep dirty_dropped/period ≤800 (thrash CLOSED).
- Do **not** retry A30-regressed levers: SoftDefer Owned-without-stuck scan, PreferKick/PromoteRelight blanket on nh≤1, MissWitness age 8–30, CaptureRefresh×8 under starve.

## Ordered remainders

1. **W1** — near_focus_holes → 0 (warm AF+manual). Evidence: sticky nh≤1 + SoftDefer clear + often `schedule_ok=0` + `pending_light_focus`~20–35; pin completion without MarkDirty storm.
2. **W2** — Operator eye + frame A/B.
3. **W3** — Residual P3 retirement + seam permanence + fluid MT residual.
4. **W4–W5** — Ring then P8 only after W1+W2.

## Final of this successor

Must end with conformance vs ENGINE_REMEDIATION + next plan for any still-OPEN.
