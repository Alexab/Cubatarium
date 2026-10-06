# Voxel streaming and rendering practices — research notes (2026-10-06)

This note records design patterns checked against primary sources while
investigating long-distance chunk visibility. They are reference patterns,
not proof that another engine's implementation can be copied unchanged.

## Practices supported by the sources

1. **Separate data production, CPU meshing, and render-thread publication.**
   Luanti's client mesh worker consumes queued mesh input, builds a mesh on a
   worker, and publishes a result through a manager for later consumption.
   Its world-emerge path separately handles memory lookup, disk load, and
   procedural generation on emerge workers. This gives each transition a
   visible queue and completion point. See the upstream
   [mesh worker](https://github.com/luanti-org/luanti/blob/master/src/client/mesh_generator_thread.cpp)
   and [emerge manager](https://github.com/luanti-org/luanti/blob/master/src/emerge.cpp).
2. **Do not wait for jobs early on the frame thread.** Unity's job-system
   guidance recommends completing a dependency as late as possible; its
   performance notes explain that frequent wakeups/signalling and very small
   jobs create overhead, so work should be batched at a suitable granularity.
   Apply that here as bounded submit/consume batches, not as a synchronous wait
   for all chunk work. See
   [creating jobs](https://docs.unity.com/en-us/engine/6000.7/manual/scripting/programming-distribute-work-threads/job-system/creating-jobs)
   and [job-system overhead](https://unity.com/blog/engine-platform/improving-job-system-performance-2022-2-part-2).
3. **Treat mesh size and response latency as separate goals.** The original
   greedy-meshing analysis shows that merging faces can reduce geometry
   substantially while retaining linear-time work over a voxel volume. Its
   follow-up explicitly treats update latency as a separate constraint and
   discusses a fast provisional mesh followed by later quality improvement.
   That suggests measuring both time-to-first-drawable and final mesh quality;
   it does not imply greedy meshing is always the fastest first-result path.
   See [Meshing in a Minecraft Game](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/)
   and [Part 2](https://0fps.net/2012/07/07/meshing-minecraft-part-2/).
4. **Make streaming demand explicit and spatial.** Unreal's World Partition
   documentation describes cells loading/unloading around streaming sources.
   For this engine, camera/player demand and ahead-of-motion demand should be
   explicit inputs to cell/column priorities, with a retained representation
   until replacement data is ready. See
   [World Partition](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine)
   and [streaming cells](https://dev.epicgames.com/documentation/fortnite/cell?lang=en-US).

## Application to Cubatarium

- Runtime work is already parallel: `ChunkLoadScheduler` obtains its worker
  count from `ComputeWorkerThreadCount(ChunkGeneration)`, and
  `UAsyncMeshBuilder` uses a separate `MeshBuild` pool. On this Ryzen 7
  4700U (8 logical CPUs), current caps resolve to 4 chunk-generation workers
  and 7 mesh-build workers when no override is provided. Release compilation
  used MSBuild parallelism 8. More workers are not the next fix: route evidence
  must show worker saturation or queue wait before increasing those limits.
- The M420 observer now distinguishes live/probed/deferred/dispatched
  FirstMesh, Relight, Seam, and Promote tickets, plus the synchronous dispatch
  cost by class. A partial first 221-period sample showed an increasing
  Relight queue (median 7 overall; median 13.5 in window 1) and no Relight
  dispatch in that window, despite measured Relight dispatch cost below
  0.155 ms in the early sample. This is consistent with a frame-deadline and
  class-starvation risk; the unchanged long route must complete before
  changing policy or calling it a cause of visible holes.
- M335's measured streaming phase exceeds the nominal 5 ms streaming budget.
  `UFrameDeadline` is intentionally soft and begins before stream work; the
  later ColumnFlow stage sees that deadline already exhausted. A fixed
  per-frame item cap therefore interacts with both the queue's numeric
  priorities and producer order. The refactor should introduce measured,
  class-aware service with a strict elapsed-time/operation cap, while keeping
  non-visible background promotion below near-camera first-draw work.
- `mesh_async` and `mesh_worker_inflight_n` indicate active asynchronous work,
  but neither proves CPU workers are saturated, results are stale, or GPU
  publication is the bottleneck. Measure queue age, worker wait/build time,
  completion wait, validation/rejection, GPU apply, and first drawable
  separately. Keep world-data existence, mesh ownership, and pixel-visible
  coverage as separate facts.

## Refactoring sequence

1. Finish the unchanged M335 repeatable route with the expanded per-class
   telemetry and compare first, middle, and late periods.
2. Decide whether near-camera Relight starvation is reproduced under actual
   readiness debt. If so, add an aged/near-camera service quantum with both a
   small maximum unit count and an elapsed-time cap. Preserve FirstMesh's
   first-result guarantee; do not let unbounded relight or Promote scans use
   the carve-out.
3. Trace any confirmed missing visible surface end-to-end: stream demand,
   resident voxel data, mesh owner, immutable snapshot/revision, worker build,
   result validation, GPU upload/slot, and pixel depth. Repair the first broken
   ownership transition, not the last symptom.
4. If main-thread scanning is still responsible for the streaming overrun,
   move snapshot selection/admission to a resumable, bounded cursor and keep
   world mutation and GPU publication on their owning thread. Do not schedule
   one tiny task per voxel; batch at column/slice granularity and measure the
   full wake/queue/copy cost.
5. Retain a drawable predecessor or coarse mesh while a replacement is
   generated, and replace it only after revision validation and GPU
   publication succeed. Measure first-drawable latency independently from
   quality remesh completion.
6. Keep M335/World_164 as the primary regression route, and periodically run
   the same profile on a cold/new world so disk-load and generation stages are
   actually exercised. Require pixel/depth evidence for visual-hole claims;
   readiness debt alone is not a visible defect.

## Scope and evidence limits

The references describe general approaches, not Cubatarium measurements.
M335 on `World_164` so far has zero disk-load completions and zero generation
commits, so it does not validate cold-world startup. The visible M420 run is
still in progress at the time of this note; its early queue data does not yet
establish late-route degradation or an exposed screen-space hole.
