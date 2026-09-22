# A25 P3 — Publication epochs ADR (audit7 delta)

Date: 2026-09-22  
Status: Accepted for R2 implementation  
Related: `MeshPublishContract.h`, `publication_audit`, ENGINE_REMEDIATION §2.4 / P3

## Decision

Order-only table reorder **must not** bump geometry `artifact_generation`.  
Consumers that depend on membership/order use:

| Epoch | Changes when | Stale draw if consumer behind |
|---|---|---|
| Artifact generation | geometry/material/light payload | always reject |
| Resident table revision | range/index table membership | MDI/table refs |
| Cull key | camera/frustum/candidates | compact visibility |
| Transparent order key | camera order affecting alpha | transparent cmds |

`publication_audit` FAIL (`reordered_table_keeps_publication_epoch=1` vs expected bump) is resolved by **stale draw rejection** on table/order epochs, not by forcing artifact generation++.

## R2 work

1. `ValidatePublicationCandidate` already returns `TableEpochStale` / `OrderEpochStale`.
2. Production callers must pass live vs draw epochs (not only hot-path GreedyGpu).
3. Tests: reject draw when `draw.resident_table_revision < live.resident_table_revision`.
4. Header `ArtifactManifest` / `SeamCoverageManifest` ≠ P3 CLOSED until all callers validated.

## Explicit non-goals

- Do not global-disable pooled path to green a test.
- Do not treat mid-stall or PreferKick as publication progress.
