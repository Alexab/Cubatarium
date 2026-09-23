---
name: remediation after A32 OPEN
overview: "Successor after A32 S2–S5: code improved for demand/pub/seam/fluid; AF baseline, pixel attribution, eye/holes=0, and ArtifactManifest retirement still OPEN. Ring OFF."
todos:
  - id: af-s0-complete
    content: "Complete clean-SHA cold/warm/dive/far AF with a31_progress_snapshot on HEAD after S2–S5"
    status: pending
  - id: attribute-cull
    content: "JobStageTrace cull/order writers + operator freeze-frame class (A31-03 Gate 3)"
    status: pending
  - id: af-close-partials
    content: "AF evidence to promote A31-01/02/05/06/07 PARTIAL→CLOSED"
    status: pending
  - id: residual-p3
    content: "Finish ArtifactManifest retirement (A30 V3 P3)"
    status: pending
  - id: eye-holes
    content: "Operator eye PASS + whole-route holes=0; then Ring/profile"
    status: pending
isProject: false
---

# Remediation after A32 — remaining OPEN

Date: 2026-09-23  
Parent: [`docs/streaming/A33_CONFORMANCE_2026-09-23.md`](../../docs/streaming/A33_CONFORMANCE_2026-09-23.md)  
Prior: A32 successor plan; A31 reaudit Gate 1–9

## Landed in A32 successor

- S-commit A31 impl (`8f845eaf`); S2–S5 (`6d9c72d6`); A33 docs (`43ddf085`)
- S0 AF baseline: `bin/suite_reports/a32_s0/` — all four FAIL with holes + `demand_stop_converged=false` + `fluid_map` spikes (defect reproduced)
- S1 partial: JobStageTrace on publish/retain/reject (cull/freeze still open)

## Non-goals / stop-lines

- No heal-loop / force_stale / pending-FD RemoveChunk / shell-light mirror
- No Ring ON until holes=0 + eye PASS
- No floating-origin rebase until far precision proof

## Ordered remainders

1. ~~S0 AF baseline~~ → **done** (FAIL honest); re-run with `--visible` when operator eye required.
2. **Attribution** cull/order + freeze frames → one class per defect (esp. holes vs not-ready).
3. Drive `demand_stop_converged` + post_stop + fluid hitch vs S0 A/B; CLOSE PARTIAL findings with evidence.
4. ArtifactManifest **retirement** residual.
5. Eye + whole-route holes=0 → only then Ring/profile.

## Final of this successor

Conformance vs ENGINE_REMEDIATION + next plan for any still-OPEN.
