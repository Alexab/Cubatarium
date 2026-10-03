# A41 P0 — Visual readiness writers inventory

Date: 2026-09-24  
Tree: A39 `4d1155df` + A40 WIP. Full plan: `.cursor/plans/a41_visual_sot_root_cause.plan.md`.

## Decision log

| Decision | Action |
|---|---|
| A40 LegalDark + ClearPending FD bypass | **KEEP** |
| open_sky equal-rev FD | **AMEND** → LightRepair one Dirty |
| Cooldown equal-rev force_stale | **REPLACE** (do not expand) |
| ClearPending → RenderReady | **REMOVE** (LitReady only) |

## RenderReady writers

| Site | A41 |
|---|---|
| `ChunkEmergeCoordinator.cpp` LitDrawable Accept | **KEEP** sole |
| `World.cpp` ClearPending ~4414, 4522, 4529 | → LitReady only |

## PendingLight Note (selected)

`World.cpp` Note/TryNote/RecoverUnlit/void; Emerge OnLitPendingNeeded; Streaming commit seed; Persistence commit/drain.

## PendingLight clear (selected)

ClearPendingAfterMeshCommitted; MarkRelit Execute/empty/orphan; RecoverUnlit sky; noop Apply; unload.

## legal_dark_settled

MarkRelit terminal `=!open_sky`; NotePending → false.

## RelightOnly without Dirty (D1 sources)

MarkRelit AlreadySatisfied+FD; Admit relight_only; RecoverUnlit pending; void Note+Enqueue.

## SoftDefer hide

MeshLitGate policy; ChunkMeshCache HoldSoftDefer / empty SoftDefer Commit.

## Evidence discipline

Cold/warm AF: `CUBA_FLIGHT_MOVE_SPEED_SCALE=1` only.
