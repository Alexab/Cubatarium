---
name: remediation after A31 OPEN
overview: "Successor after A31 impl: code-path PARTIAL for demand/pub/seam/fluid; AF/pixel/stop gates and residual retirement still OPEN. Ring OFF."
todos:
  - id: af-baseline
    content: "Clean-SHA no-teleport cold/warm/dive/far AF + a31_progress_snapshot baseline"
    status: pending
  - id: af-attribute
    content: "Attribute black/hole frames via JobStageTrace + independent pixel class (A31-03)"
    status: pending
  - id: close-partials
    content: "Drive A31-01/02/05/06/07 from PARTIAL→CLOSED with AF evidence; finish cross/shell/Immediate pub gate if needed"
    status: pending
  - id: stop-sla
    content: "Wire production StopConverged / stop-to-publish SLA into flight gates"
    status: pending
  - id: residual-p3
    content: "Finish ArtifactManifest retirement residual (A30 V3 P3)"
    status: pending
  - id: eye-holes
    content: "Operator eye PASS + whole-route near_focus_holes=0; then reconsider Ring/profile"
    status: pending
isProject: false
---

# Remediation after A31 — remaining OPEN

Date: 2026-09-23  
Parent conformance: [`docs/streaming/A32_CONFORMANCE_2026-09-23.md`](../../docs/streaming/A32_CONFORMANCE_2026-09-23.md)  
Impl plan (history): `a31_remediation_impl_9cdcbd77.plan.md`  
Canon: [`ENGINE_REMEDIATION_PLAN_2026-09-22.md`](../../docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md)

## Closed / landed in A31 impl (code)

- Demand predicates + writers default ON (`kChunkDemandShadow()`), world-switch Clear
- CPU/GPU Replace validate-before-commit
- Seam: no fabricated peer_pub=1; GetMeshPublishRevs; SeamCoverageFullySatisfied; NoteFaceDebt dual-write
- Fluid: Y-range content stamp; defer without sync fallthrough; pack Reset on dirty/world switch
- Harness: whole-route holes, post_stop cascade, far fields, progress snapshot, `product-174657-far`
- JobStageTrace expanded fields

## Non-goals / stop-lines

- No A22–A24 heal-loop / force_stale / RemoveChunk pending FD / shell-light mirror
- No Ring ON / P8 profile until holes=0 + eye PASS
- Keep dirty_dropped/period ≤800 (thrash); thrash alone ≠ correctness
- No floating-origin rebase until far A/B proves precision

## Ordered remainders

1. **AF baseline** — clean SHA cold/warm/dive/far no-teleport; publish all runs with `a31_progress_snapshot`.
2. **Attribute** — one primary class per black/hole sample (geometry / cull / light / seam / material / precision / legal dark).
3. **Close PARTIALs** — AF evidence for seam/fluid/demand/pub; finish any remaining Cross/Shell/Immediate validator gaps.
4. **Stop SLA** — production caller for StopConverged + latency gate.
5. **Residual P3 retirement** — ArtifactManifest full retirement.
6. **Eye + holes=0** whole route → only then Ring/profile.

## Final of this successor

Must end with conformance vs ENGINE_REMEDIATION + next plan for any still-OPEN.
