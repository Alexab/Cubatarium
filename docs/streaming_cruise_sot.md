# Streaming cruise SoT (perf_opt10)

Single source of truth for the cruise throughput architecture after the
`102747` inland flight diagnosis. EraN plan files are historical; this doc
owns the live contracts.

## Pipeline

```
Ticket(level by Chebyshev ring)
  → ColumnRecord.desired
  → WorkPoolBudget (gen | light | first_mesh | remesh | gpu)
  → execute (async) → ColumnRecord.achieved / emerge bump
  → draw / raycast
```

## ColumnRecord

- Store: `UColumnRecordStore` on `UWorld` (`ColumnRecord.h`).
- Dual-written from `SetColumnEmergeState` / ColumnFlow tickets.
- Fields: emerge, desired, revs, inflight_job, pending_light, sticky, light_complete_disk, raa_pending.

## Tickets & pools

- `ColumnTicketMap.h`: `TicketLevelForRing`, `DesiredStageFromTicket`.
- Rings reuse `kVisualStageNearFovHoriz` (2) / `kVisualStageLitDrawableHoriz` (4).
- `WorkPoolBudget` / `HoleDrainPools`: redistribute remesh→FirstMesh under holes.
  **No new `*FloorMs` knobs.**

## Hot-path (Phase 1)

- `UnfinishedVisualCache`: ring-buffer dirty; never wipe on overflow; cheap predicate.
- One `CountUnfinishedVisualNear` SoT per frame (Streaming); Coordinator reuses sample.
- SoftDefer: ring≤2 FirstMesh non-stealable; Pass1 early-stop on FirstMeshQ.
- Relight TrimFar counts `relight_trim_far_n` and boosts drain (not silent heal).

## Contracts (Phase 4)

- `BlockQueryResult`: Unloaded ≠ AIR (`ChunkManager::QueryBlock`).
- Raycast targets use QueryBlock (unloaded never place/break).
- Soft XZ border: `WorldBorderPolicy` clamp + speed scale before hard 1e5 sanitize.

## Forbidden

- New PreferKick exception paths / orphan PreferKick loops.
- New schedule/drain floor ms knobs (use pool redistribution).
- Wall-gated enqueue for FirstMesh desire.

## SLA inland cruise (−485/50)

| Metric | Target (vs 203518) |
|--------|-------------------|
| wall_ms med | ≤130 |
| prep_unfinished med | ≤20 |
| prep_unfinished_full_n med | ≤1 |
| schedule_ok under holes | ≥8 |
| relight_trim as heal | not primary |
