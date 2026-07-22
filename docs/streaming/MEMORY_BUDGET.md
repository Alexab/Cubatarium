# Memory Budget Control

Параметризуемый бюджет памяти для streaming / mesh / relight / GPU pool.
Цель: стабильный high-water (~tier Soft), без unbounded Completed/Dirty и без
регресса SoftDefer / greedy / FPS.

Связанные коммиты Era 12 (`perf`, 22.07): `0cb92063`, `b1f8924c`, `02b9868d`,
`8bbc3139`, `152cb5df`.

## Tiers (config)

| Tier | BudgetMb | SoftMb | ExpandKeepMb | Default use |
|------|----------|--------|--------------|-------------|
| low | 1024 | 768 | 512 | weak machines |
| med | 1536 | 1152 | 768 | default |
| high | 3072 | 2304 | 1536 | large keep/RD |

Knobs в `RuntimeTuning` / `bin/streaming_tune.json` (и опционально
`config.json` → `memory.tier`):

| Knob | Default (med) | Role |
|------|---------------|------|
| `MemoryBudgetMb` | 1536 | hard ceiling (telemetry + emergency) |
| `MemorySoftMb` | 1152 | throttle admit / no expand |
| `MemoryExpandKeepMb` | 768 | below → allow Keep/RD / buffer expand |
| `MeshCompletedSlots` | 0 → workers×6 | Completed mesh ring |
| `RelightCompletedSlots` | 0 → workers×8 | Completed relight ring |
| `DirtySoftCap` | 1200 | drop farthest remesh |
| `PendingLightSoftCap` | 80 | drop farthest only if mesh+LitReady |
| `RelightFifoSoftCap` | 96 | drop farthest FIFO |
| `GpuVertexPoolReserveMb` | 64 | pre-Reserve |
| `GpuVertexPoolMaxMb` | 256 | grow cap |
| `MaxKeepPrefetchMargin` | 4 | expand Keep ceiling |
| `MemoryExpandMaxRd` | 6 | expand RD ceiling |
| `CompletedExpandEnabled` | true | stepped Completed slot expand |

## Overflow matrix

Правило: **drop только воспроизводимую работу**; **нельзя drop единственную
копию состояния мира**.

| Buffer | Overflow | Drop oldest/farthest? | Recovery |
|--------|----------|----------------------|----------|
| Mesh Completed | fixed ring, drop-oldest | Yes | `Dirty.MarkDirtyPriority` + clear InFlight |
| Relight Completed | fixed ring, drop-oldest | Yes | `EnqueueTerrainColumnRelight` |
| Gen / Async IO | soft cap + **block admit** | No | drain before admit |
| Dirty set | soft-cap → drop farthest | Yes (not underfeet) | remesh later |
| PendingLightBeforeMesh | soft-cap → drop farthest **iff** HasMesh+LitReady | Conditional | promote later |
| Relight FIFO | max depth → drop farthest | Yes | re-promote |
| GPU VertexPool | grow to MaxMb; then skip orphan grow | N/A | Reserve on RD change |
| ChunkManager | unload beyond Keep; free-list | Unload distant | streamer |
| Capture inflight | admission only | N/A | pace enqueue |

**Иерархия при `private_mb > Soft`:** block admit (gen/Capture/keep prewarm) →
drop farthest remesh/relight results → emergency CancelOutside. Никогда: drop
terrain chunk без persist.

## When to expand buffers

| Buffer | Expand if | Forbidden |
|--------|-----------|-----------|
| Completed slots | `private_mb < ExpandKeepMb` **and** discard_rate high **and** pressure ≤ Yellow | Soft/Hard; wall hitch |
| GPU pool | RD/Keep up; or used>90% with RAM headroom | every frame; Soft+ |
| Keep / effective RD | MemoryBudgetController Green + headroom | holes/pending/wall bad |
| Dirty/Pending soft-cap | config tier only | under load |
| Chunk free-list | Keep expand | above MaxResidentChunks |

Expand **редко и ступенчато** (×1.5 or +fixed); shrink capacity только leave-world
/ explicit trim.

## Anti-realloc

1. `UCompletedJobQueue`: `reserve(Cap)`, ring head/size; drop = move-assign slot.
2. Dirty/FIFO: `reserve` from soft-cap; erase by key.
3. Capture: admission + time budget (object pool snapshots — later phase).
4. `UChunk` free-list: unload → `ResetForReuse` → pop on create.
5. GPU: `EnsureCapacity` only if `needed > cap`; no shrink mid-flight.

## Telemetry (perf jsonl)

- `mesh_completed_n` / `mesh_completed_cap` / `mesh_completed_discarded`
- `relight_completed_n` / `relight_completed_cap` / `relight_completed_discarded`
- `dirty_n` / `pending_light_n` / `relight_fifo_n`
- `dirty_dropped` / `pending_light_dropped` / `relight_fifo_dropped`
- `gpu_pool_used_mb` / `gpu_pool_cap_mb`
- `memory_pressure` (0 under expand / 1 soft / 2 hard)
- `keep_margin_eff` / `buffer_expand_events`
- `rss_mb` / `private_mb` / `chunk_count` (Era 12)

## MemoryBudgetController

`Evaluate(sample, tuning)` → keep_margin, max_effective_rd, allow_keep_prewarm,
emergency_cancel_outside, capture_hard_cap, memory_pressure.

- **Expand Keep/RD:** Green, holes==0, pendf==0, wall≤28, `private_mb < ExpandKeepMb`.
- **Soft:** no expand, keep→baseline, capture_hard_cap≤2.
- **Hard:** capture_hard_cap=1, cancel outside, no keep prewarm.

## Validation gates

- Manual: walk + place light block — `private_mb` bounded; no hang.
- Autofly: `private_mb` p95 ≤ Soft; completed fill% < 0.85 at rest; `max_wall`
  not worse than baseline; holes/sticky not worse.
- Stress: `MeshCompletedSlots=4` → discard↑, Dirty requeue, picture recovers,
  private bounded.

## Anti-patterns

- Drop Completed without Dirty/relight requeue.
- Erase PendingLight on cold hole (no mesh).
- Expand buffers under Soft/Hard pressure.
- `glBufferData` orphan every frame / shrink mid-frame.
- `sync_cap=0` + StarveRemesh; Capture storm; fluid GetOrCreate; unbounded light BFS.
