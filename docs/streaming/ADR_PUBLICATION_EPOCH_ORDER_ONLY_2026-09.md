# ADR: publication epoch vs order-only table updates (A21 P0.5 / P3)

Date: 2026-09-22  
Status: Accepted  
Context: `publication_audit` order-only epoch contract

## Decision

Order-only membership reorder of resident batches **must not** bump artifact
`publicationVersion`. MDI / compact consumers use a separate **resident table
revision** (and transparent order key) that is invalidated on reorder.

`publication_audit` verifies: same artifact epoch + bumped
`resident_table_revision` on reorder (stale-draw rejection via table epoch).

## Related

- `relight_install_planner_test` FullyDark: P4 LightValidity
- `ArtifactManifest.h` / `ValidatePublicationCandidate` in MeshPublishContract
