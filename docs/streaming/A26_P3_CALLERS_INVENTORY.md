# A26/A27 P3 — Publication callers inventory

Date: 2026-09-22 (A27 S3 update)  
Parent: remediation_after_a26_open S3  
ADR: [`A25_P3_PUBLICATION_ADR.md`](A25_P3_PUBLICATION_ADR.md)

## Callers wired to `ValidatePublicationCandidate`

| Caller | Path | Epochs | Notes |
|---|---|---|---|
| GPU publish | `GreedyGpuPublication.cpp` | live artifact / table / order / cull | reject → `NotePubVerChangedWithoutFresh` |
| CPU apply | `ChunkMeshCache::ApplyMeshResult` | stamp vs PublishRevs | `NoteInstallResult(Published)` under shadow |

## Deferred retirement / fence (A27 S3)

| API | Location | Status |
|---|---|---|
| `ShouldDeferMeshRetirement` | `MeshPublishContract.h` | contract |
| `TryFreeSlotByIndex` / slot `generation` | `GpuMeshSlotAllocator` | wired on `BindCommittedSlot` |
| `NoteFenceCompletedGeneration` | allocator + GPU commit | watermark after BindCommitted |

## Remaining residual sites

| Site | Notes |
|---|---|
| Cross-instance / shell batches | no ArtifactManifest yet — OPEN |
| Greedy remove/representation switch | publicationVersion bump only |
| Staging reject FreeSlotByIndex | intentional immediate free (not live) |

## Non-goals

- Do not bump artifact_generation on order-only reorder.
- D3 shell-light mirror stays FREEZE.
