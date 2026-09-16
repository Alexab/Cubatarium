# ADR: Neighbor shell boundary as versioned overlay (audit R06 / S4)

Date: 2026-09-16  
Status: Accepted  
Context: `CURRENT_STATE_AUDIT_2026-09-16` R06

## Decision

Keep permanent mesh stamps on voxel occupancy/material + light only
(`ChunkInputStamp` Strategy A). Do **not** fold `neighbor_drawable` into
`InputsStillValid` equality (SoftDefer thrash class).

Temporary closing faces for undelivered neighbors remain a **boundary overlay**
owned separately from permanent mesh identity. When neighbor published coverage
arrives, overlay is removed; permanent mesh is not invalidated solely by
drawable flip.

## Consequences

- `InputsStillValidVisualTest` Strategy A remains KEEP.
- Seam gaps while neighbor missing are overlay/coverage issues, not stamp churn.
- Follow-on: explicit overlay payload + version (not required to close R01–R05).
