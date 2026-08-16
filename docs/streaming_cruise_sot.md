# Streaming cruise SoT (perf_opt10 closeout)

Single source of truth for inland cruise throughput after scaffolding
`3640bf46` and closeout phases A–G. EraN / `cruise_sot_refactor_*` plan
files are historical. LOD mesh and ocean Era30 are **follow-ups**, not
claimed done here.

## Pipeline

```
Ticket(level by Chebyshev ring)
  → ColumnRecord.desired (+ emerge dual-write)
  → WorkPoolBudget (gen | light | first_mesh | remesh≥1 | gpu)
  → execute (async, one inflight_job) → emerge bump / RenderReady settle
  → draw via IsChunkSliceRenderReady (NOT ColumnEmergeState::RenderReady)
  → raycast via QueryBlock (Unloaded ≠ AIR)
```

## SOTA invariants

| Invariant | Rule |
|-----------|------|
| Draw ≠ Ready | Opaque gate = `IsChunkSliceRenderReady` / `draw_ok`. Never gate draw on `RenderReady`. |
| Same-frame sample | `FocusRingVisualSample.frame_epoch` must match `StreamingFrameEpoch`; else recount. |
| Remesh reservation | When RemeshQ ≠ ∅, `remesh_schedule ≥ 1` (pool slot, not `*FloorMs`). |
| Lit remesh class | Drawable lit remesh → RemeshQ (`MarkDirty`); missing/FullyDark → FirstMeshQ (`MarkDirtyPriority`). |
| PreferKick | Cancel-stale GPU promote only (`PreferKickPendingGpuQueued`); not a parallel remesh owner. |
| Floors | `MissEmergeFloorMs`, `LandMovingRelightDrainFloor`, `AsyncScheduleFloorUnderMiss` = 0; use pools. |

## ColumnRecord

- Store: `UColumnRecordStore` on `UWorld` (`ColumnRecord.h`).
- Dual-written from `SetColumnEmergeState` / ColumnFlow `AdvanceColumn`.
- Fields: emerge, desired, revs, inflight_job, pending_light, sticky, light_complete_disk, raa_pending.
- `GetColumnEmergeState` reads **map first** (bump SoT); record is mirror.

## Tickets & pools

- `ColumnTicketMap.h`: `TicketLevelForRing`, `DesiredStageFromTicket`.
- Rings: `kVisualStageNearFovHoriz` (2) / `kVisualStageLitDrawableHoriz` (4).
- `HoleDrainPools`: steal remesh→FM but **keep 1 remesh slot**.

## Hot-path prep (closeout A/B)

- `mesh_emerge_prep_unfinished_ms` is a **legacy sum** of SoftDefer setup + pending/dirty/black scans — **not** `CountUnfinishedVisualNear`.
- Split telem: `prep_pending_light_ms`, `prep_black_sticky_ms`, `prep_dirty_count_ms`, `prep_softdefer_setup_ms`.
- Streaming writes `FocusRingVisualSample`; Coordinator reuses when epoch matches.
- UnfinishedVisualCache: ring-buffer dirty; never wipe on overflow.

## Draw / heal telem (do not confuse)

| Metric | Meaning |
|--------|---------|
| `opaque_cmd_on` | MDI batches that passed slice-ready + cull (**draw SoT**) |
| `visible_black_focus_n` | Drawable FullyDark/StaleDark in focus, **even if hide-until-lit hid them** (**heal SoT**) |
| `column_render_ready_n` | FSM `RenderReady` count only — **not** draw proxy |
| `column_lighting_n` / `column_meshing_n` | Other emerge stages |

Hide-until-lit (`ShouldHideUncomputedFullyDarkInRing`) stays for quality.

## Contracts

- `BlockQueryResult`: Unloaded ≠ AIR (`ChunkManager::QueryBlock`).
- Soft XZ border: `WorldBorderPolicy` before hard 1e5 sanitize.

## Forbidden

- New PreferKick exception owners / orphan PreferKick loops.
- New `*FloorMs` knobs (use pool redistribution).
- Wall-gated enqueue for FirstMesh desire.
- Gating opaque on `ColumnEmergeState::RenderReady`.

## SLA inland cruise (−485/50, restore −7752/96/808)

| Metric | Target (vs 203518 / fail 102747 / 145451) |
|--------|------------------------------------------|
| wall_ms med / p90 | ≤130 / ≤220 |
| prep hot (sum prep_*) med | ≤20 |
| schedule_ok under holes | ≥8 (proxy) |
| dirty_remesh when lit dirty | >0 |
| fifo_drop as heal | not primary |
| opaque_cmd_on | not <200 at Δfocus≤6 |
| visible_black_focus med | ≤25 orient |
| sky-only regress | raa_commit>0 on FullyDark enter; opaque not stuck |

A/B logs: `203518`, `102747`, `145451` via `bin/tmp_cruise_wall_summary.py`.
