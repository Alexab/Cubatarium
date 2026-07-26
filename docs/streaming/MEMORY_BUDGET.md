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
| `DirtyThrashSoftCap` | 400 | SoftCap under Yellow/Red **or** async thrash |
| `DirtyThrashAsyncMin` | 36 | async≥N → thrash SoftCap (also Yellow alone) |
| `PendingLightSoftCap` | 80 | drop farthest only if mesh+LitReady |
| `RelightFifoSoftCap` | 96 | drop farthest FIFO |
| `GpuVertexPoolReserveMb` | 64 | pre-Reserve |
| `GpuVertexPoolMaxMb` | 256 | grow cap |
| `MaxKeepPrefetchMargin` | 4 | expand Keep ceiling |
| `MemoryExpandMaxRd` | 6 | expand RD ceiling |
| `MaxResidentChunks` | 0 (auto) | free-list cap; 0→Keep footprint/4≤512 |
| `RelightCaptureBandCy` | 4 | max cy layers per Capture; 0=full column |
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

- Soft/Hard (`memory_pressure`) is **byte-budget only** (`private_mb` vs SoftMb /
  BudgetMb). Streaming Yellow/Red must not force Soft — that blocked Keep expand
  while RAM was fine (manual `20260723-081832`).
- Completed rings may **step-expand** (×1.5, hard max 128) when
  `CompletedExpandEnabled` and discard delta ≥4 under `ExpandKeepMb`; logged via
  `buffer_expand_events`.
- Green may **raise** `max_effective_rd` (Adaptive uses it as RD ceiling).
- Dirty thrash SoftCap (`DirtyThrashSoftCap` when stream Yellow/Red **or**
  `mesh_async≥DirtyThrashAsyncMin`) — SoftCap 1200 never engaged at Dirty~400–590
  with async≤29 (manual `20260723-091724`).
- Capture hitch gate: `visual_holes>0` or `wall>500ms` → `capture_hard_cap=1`
  even under byte-budget Green; controller re-evals immediately on holes/wall.
  Drain checks wall budget **before** every Capture (was only after first).
  **Y-band Capture** (`RelightCaptureBandCy=4`, top-down): SoftDefer keeps
  `PendingLight` until `finalize_pending_gate` on the last band (manual
  `102936` full-column ~1.6 s).
- PendingLight trim requires **HasMesh + LitReady**; free-list sized from Keep.
- Relight FIFO soft-cap trims **far + priority** deques.

## Validation gates

- Manual: walk + place light block — `private_mb` bounded; no hang.
- Autofly: `private_mb` p95 ≤ Soft; completed fill% < 0.85 at rest; `max_wall`
  not worse than baseline; holes/sticky not worse.
- Stress: `MeshCompletedSlots=4` → discard↑, Dirty requeue, picture recovers,
  private bounded.

### Validation notes (implementation landed)

Code path (Era 12 Memory Budget Control) is on branch with:

- `docs(streaming)` Era 12 + `MEMORY_BUDGET.md`
- knobs + fill%/pressure jsonl
- Completed rings + Dirty/Pending/FIFO soft-caps
- GPU Reserve/Max + `MemoryBudgetController` + UChunk free-list

Manual/autofly gates (checklist — code landed; evidence still open after CB):

1. Walk existing terrain + place lit block: no hang; `private_mb` stays near Soft.
2. Quiet standing: `mesh_completed_n/cap` fill < 0.85; `dirty_dropped` not
   monotonic when idle and Dirty under SoftCap.
3. Autofly golden (`cb_pack` class): sticky=0 / cold≤3; record `private_mb` p95
   vs `MemorySoftMb` from perf jsonl if present.
4. Stress tune `mesh_completed_slots=4`: discard rises, remesh recovers, private
   stays bounded.

Status 2026-07-26: SoftCap/Dirty drop paths exercised on CB cruise (dirty_no_holes
~185–380 with drops). Autofly evidence:
- `cb_pack`: `private_mb` med≈479 / p95≈482 / max≈522 (≪ Soft 1152);
  mesh_completed fill med≈2/42; `memory_pressure=0`.
- `premerge_cb2`: `private_mb` p95≈447; completed fill med≈0.12.
- Stress `mesh_completed_slots=4` (`mem_slots4`): cap held at **4**,
  `mesh_completed_discarded` → **~329**, Dirty requeue/thrash (dirty_no_holes
  **637**), `private_mb` p95 **≈442** ≪ Soft, `memory_pressure=0`; stop ended
  sticky/holes **0** (picture recovers). F2/CB NO-GO under stress is expected —
  not a golden regress. Tune apply: `UpdateStreaming` forces Completed cap when
  `mesh_completed_slots>0` (was constructor-only).
- Place/edit proxy `t1_break_mem` (`--scenario break-stand`): sticky/holes **0**,
  `break_complete_sum=3`, `private_mb` p95 ≪ Soft (see jsonl), no hang. Closes
  MEMORY checklist item 1 as automated stand-in for walk+place-lit.

## Anti-patterns

- Drop Completed without Dirty/relight requeue.
- Erase PendingLight on cold hole (no mesh).
- Expand buffers under Soft/Hard pressure.
- `glBufferData` orphan every frame / shrink mid-frame.
- `sync_cap=0` + StarveRemesh; Capture storm; fluid GetOrCreate; unbounded light BFS.
