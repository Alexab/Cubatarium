# Threading audit

Inventory of multithreading sites, cross-thread artifacts, concurrency patterns,
and residual risks. Companion to streaming crash/perf work.

Legend: **Risk** L / M / H.

## A. Job infrastructure

| Site | Files | Shared data | Sync | Risk |
|------|-------|-------------|------|------|
| `UJobThreadPool` / `UCompletedJobQueue` | `src/Core/Jobs/JobThreadPool.*` | job queue, completed mailbox | mutex + CV; completed mutex | L |
| `JobPoolKind` budget (5 pools × ≤4) | `src/Core/Jobs/JobThreadBudget.*` | sizing only | none | M (up to ~20 threads, no global cap) |
| TLS `job_kind` | `CubatariumSetWorkerJobKind` via pool ctor | thread-local string | TLS | L |

## B. Worker pools

| Pool | Files | Shared / artifacts | Sync | Risk |
|------|-------|--------------------|------|------|
| MeshBuild | `AsyncMeshBuilder`, `ChunkMeshCache` | owned `ChunkMeshSnapshot`; catalog `shared_ptr` keep + registry maps | snapshot + epoch/jobId + InFlightMutex + catalog pin | L–M |
| Relight | `AsyncRelightBuilder` | owned relight snapshot; catalog keep; capture mutex | capture mutex + epoch + catalog pin | L |
| ChunkIo | `AsyncChunkIO`, `WorldPersistence` | bytes/path; generation token | serialize-on-main; disk on worker; registry sequence check | L |
| ChunkGeneration | `ChunkLoadScheduler` | populator; `WorldGenContentSnapshot` pin; ObjectLibrary | token + shouldCancel + content pin; apply-on-main | L |
| CoopGeneration | `WorldCooperativeOps` | same populator + content snapshot | real gen tokens + `WaitIdleFor(2s)`; completed queue | L |

## C. Tokens / epoch / quiesce

| Site | Files | Sync | Risk |
|------|-------|------|------|
| `UChunkGenerationRegistry` | `ChunkGenerationToken.*` | mutex | L |
| Mesh/Relight epoch | Async builders | atomics | L |
| Quiesce pipeline | `World`, `WorldStreaming`, `WorldOperationRunner` | cancel + WaitIdle timeout | L–M |
| `PauseChunkGeneration` | `WorldStreaming` | cancel pending + wait workers idle | L |
| `RefreshBlockRegistry` | `World.cpp` | PauseChunkGeneration + mesh/relight drain before mutate | L |

## D. Snapshots / TLS (sound patterns)

| Site | Files | Risk |
|------|-------|------|
| Mesh/Relight value snapshots | ChunkMesh/RelightSnapshot | L |
| TLS `ThreadLocalPipelineState` | `PipelineChunkPopulator.cpp` | L |
| ObjectLibrary `GetShared` / TLS keep in `Get` | `ObjectLibrary.*` | L |
| `WorldGenContentPinScope` | `WorldGenContentPin.*` | L (pins pack/features/refs for Populate) |

## E. Catalog / prefab / pack

| Site | Files | Artifact | Sync | Risk |
|------|-------|----------|------|------|
| `BlockDefinitionStorage::Active` | `BlockDefinitionStorage.*` | RCU `shared_ptr` | `atomic_load` / `atomic_store` | L |
| Removed dangling `GetAll()` | — | use `GetCatalogSnapshot()` | — | fixed |
| `UBlockRegistry` maps | `BlockRegistry.*` | `shared_mutex` on NameToId/IdToName | MapsMutex + PauseChunkGeneration on reload | L |
| `UBlockMergeRegistry` | `BlockMergeRegistry.*` | lookups | shared_mutex | L |
| `UObjectLibrary` | `ObjectLibrary.*` | `shared_ptr` defs | shared_mutex + owned ptr | L |
| `UWorldGenPack` / features / refs | Pack, ObjectFeatureConfig, WorldGenRefs | RCU publish + job pin | atomic snapshot + `WorldGenContentPinScope` | L |
| Creature/Skin definition maps | `*DefinitionStorage.*` | maps | shared_mutex | L |
| `CreatureMeshGpuCache` | GPU VAO cache | maps | mutex | L |

## F. Other

| Site | Note | Risk |
|------|------|------|
| glog | any thread after init | L |
| Progressive queues / physics | main-only | L |

## Pattern verdict

**Sound for voxel data:** schedule on main → snapshot/TLS on worker → completed mailbox → apply on main; generation tokens; mesh/relight epoch discard.

```mermaid
flowchart LR
  mainSched[Main schedule]
  worker[Worker pool]
  mailbox[CompletedJobQueue]
  mainApply[Main apply]
  mainSched -->|"snapshot or TLS"| worker
  worker --> mailbox
  mailbox --> mainApply
```

**Fixed:**

1. Async IO token validation compares against `ChunkGenTokens.Current(ground).sequence`.
2. Catalog publish is atomic; `GetAll()` removed in favor of snapshots.
3. `ObjectLibrary` stores `shared_ptr` definitions; `GetShared` for safe lifetime; map insert captures name before move.
4. `RefreshBlockRegistry` uses `PauseChunkGeneration` (not permanent Quiesce) before mutate; registry maps are mutexed.
5. Worker pools stamp TLS `job_kind` for crash logs.
6. Worldgen pack/feature/refs are RCU-published; Populate pins a content snapshot for the job.
7. Mesh/Relight jobs hold `GetDefinitionsCatalogSnapshot()` for the job lifetime.
8. Coop generation uses real chunk gen tokens + bounded `WaitIdleFor`.

**Still watch (non-blocking):**

- No global thread throttle across five pools (perf contention only).
- Phase 4 streaming hitch budgets — adaptive emerge already present; revisit only if hitch remains after gen cache.
