# RelightReplace dual-writer inventory (E2.1)

Plan: `g1_a10_continuation` E2.1. Tip before cutover: `37fd2765`.

Contract: published FullyDark / StaleVertexLight remesh Dirty is owned by
`MarkRelitChunksForMesh` → RemeshQ (`MarkDirty`), not Priority/FM secondary pumps.

| Action | Site | Notes |
|---|---|---|
| Keep | `MarkRelitInstall.cpp` + `RelightInstallPlanner` | Primary |
| Keep | `ChunkMeshCache` FullyDark→RemeshQ demote (~2562) | dual-Q |
| Redirect | Orphan RAA FullyDark `MarkDirtyPriority` → `MarkDirty` or skip | ~5069 |
| Gate | Enter commit FullyDark Dirty pump | ~3772–3784 |
| Redirect | RemeshAfterApply commit FullyDark Priority → Dirty | ~3808 |
| Gate | `RemeshColumnSeamTicket` sticky stale_dark MarkDirty | World.cpp ~3220 |
| Out of scope | hole FM SoftDefer empty, RelightFifo, ColumnRecord RelightOwner | already EvictionOwner Decide |

Flag: `IsRelightReplaceDirtyOwnerEnabled()` default **ON** (rollback OFF).
