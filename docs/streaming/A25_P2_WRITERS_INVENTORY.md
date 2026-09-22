# A25 R1 — Demand writers inventory (cutover ON)

Date: 2026-09-22  
Cutover: `ChunkDemandCutoverEnabled()` default true; rollback `CUBA_DEMAND_CUTOVER=0`.

## Allowed writers (sole path)

| Writer | Location | Notes |
|---|---|---|
| `UChunkRenderDemandStore::NoteDemand` | MarkRelit / mesh admit | coalesce / AlreadySatisfied |
| `NoteStageProgress` | MarkRelitInstall (always, A25) | Admit→…; not PreferKick DoD |
| `NoteInstallResult` | ChunkMeshCache apply / publish path | Published / Retain / Cancel |
| `NotePublishedRevs` | MarkRelitInstall shadow sync (A26) | sole published_* refresh without lifecycle |
| `NoteFaceDebt` / `NoteFaceDebtSatisfied` | EmergeCoordinator | peer_gen aware |
| multi-Y RenderReady | ChunkEmergeCoordinator | sibling FullyDark blocks Ready |

## Forbidden while cutover ON

| Path | Status |
|---|---|
| `ColumnRecords.ClearFaceDebt` | gated by `ChunkDemandAllowsColumnFaceDebtClear()` |
| Dual shadow Dirty admits | `kChunkDemandShadow=false` |
| Direct `rec.published_* =` outside store APIs | **removed** (A26 N1) |

## Heal-owner honesty

Product liveness DoD = Dirty / Invalidate / Relight bifurcate / PendingLight progressing to Published (job_trace desired/published revs).  
`prefer_kick_n≡0` ≠ zero heal.

## Orphan recovery

`ReconcileMaintenance` cancels `has_active_attempt && Created && last_progress_ms<=0`.
