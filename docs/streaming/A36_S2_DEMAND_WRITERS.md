# A36 S2 — Demand writer inventory (A31-05)

Authority default ON via `ChunkDemandAuthorityEnabled()` (`CUBA_DEMAND_SHADOW=1` → observe-only).

| Event | Owner (authority ON) | Legacy / rollback |
|---|---|---|
| Voxel/light desire | `NoteDemand` (MarkRelitInstall / mesh apply) | — |
| Stage progress | `NoteStageProgress` (attempt ID gated) | foreign attempt ignored |
| Install Published/Reject/Retain | `NoteInstallResult` (attempt ID + coverage) | stale completion ignored |
| Face debt | `NoteFaceDebt` / Satisfied (monotonic waiting_peer_gen) | column ClearFaceDebt blocked when cutover ON |
| Stop plateau | `World.cpp` samples `StopConverged(now_ms)` → telemetry | — |
| Reconcile | remints orphan Created after grace | — |

Cutover: `ChunkDemandCutoverEnabled` (env `CUBA_DEMAND_CUTOVER=0` rollback).
