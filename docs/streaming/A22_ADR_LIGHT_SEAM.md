# ADR A22-S4 — LightValidity and seam separation

Status: Accepted (implementation incremental)  
Date: 2026-09-22

## Context

ENGINE_REMEDIATION §2.3 / P4: permanent mesh must not depend on neighbor
drawable; FullyDark census is observational; LightValidity ≠ zero light.

## Decision

1. **Permanent mesh** is computed from voxel/light halo data only — not from
   peer drawable readiness.
2. **Seam / temporary boundary coverage** is a separate short-lived output with
   its own generation and deletion rules (target extract; during migration,
   seam inputs must be in ArtifactManifest).
3. **LegalDark** requires light revs match + settled + no repair ticket.
   FullyDark debt otherwise drives PendingLight / force_stale remesh (A22 S1).
4. **Reference CPU light** on small worlds remains a follow-up gate before
   claiming P4 CLOSED.

## Consequences

- Do not remesh solely because vertices are dark when revs match and settled.
- Do not clear FaceDebt for an entire column from one Y-slice publish.
- RingReadiness must not greenwash by shrinking the scored ring (S7 deferred
  until end-gate green).
