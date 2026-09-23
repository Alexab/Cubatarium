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

## Landed in A32 successor (code)

- S-commit A31 impl (`8f845eaf`)
- S2: GPU/CommitGpu `NoteInstallResult` + attempt_id; production `StopConverged` → flight cascade
- S3: Immediate / Cross / CommitGpuMeshResult validate-before-commit
- S4: seam per-face peer gens + subscriber satisfy + FaceDebt dual-write
- S5: fluid flags snapshot + background worker + install outcomes
- S1 partial: JobStageTrace on publish/retain/reject

## Non-goals / stop-lines

- No heal-loop / force_stale / pending-FD RemoveChunk / shell-light mirror
- No Ring ON until holes=0 + eye PASS
- No floating-origin rebase until far precision proof

## Ordered remainders

1. Finish **S0 AF baseline** on clean HEAD (cold/warm/dive/far + snapshots).
2. **Attribution** cull/order + freeze frames → one class per defect.
3. AF evidence to CLOSE PARTIAL findings.
4. ArtifactManifest **retirement** residual.
5. Eye + whole-route holes=0 → only then Ring/profile.

## Final of this successor

Conformance vs ENGINE_REMEDIATION + next plan for any still-OPEN.
