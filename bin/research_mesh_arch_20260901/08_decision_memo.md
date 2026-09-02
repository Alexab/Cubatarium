# Decision memo — mesh architecture R1.5 → SHIP (2026-09-02)

**Status: R1.5–R3 CODE LANDED — autofly trio PENDING**

## Sprint verdict

| Sprint | Verdict | Key evidence |
| --- | --- | --- |
| R1.5 capture loop | GO (code) | `PendingCaptureReady_`, end-of-tick `Drain`+`Retry`, `mesh_schedule_retry_test` |
| R2 store diet | GO (code) | `RefreshIncrementalShell`, ring-enter prefetch, `IsWorkerCaptureSaturated` degraded path |
| R2.5 completion | GO (code) | ProactiveFmRefill B/C, GPU budget raise, `fm_completion_chain_audit.py`, classifier v2 |
| R3 M4 ownership | GO (code) | ColumnFlow-owned `MarkMissing`, `m4_ownership_lint` CI target, witness diet × capture backlog |
| R4 harness | GO (code) | `movement_speed` cruise filter, parity `holes_cap` formula, `MESH-R15-capture` gate |
| SHIP autofly | PENDING | Run sequential trio after rebuild |

## R1.5 changes

| Item | Status |
| --- | --- |
| PendingCaptureReady_ + age | `ChunkMeshCache.h/.cpp` |
| End-of-tick drain/retry | `RebuildDirtyChunksWithStats` tail |
| Cap + CancelCoord | `kMaxPendingCaptureSet`, `MeshCaptureWorker::CancelCoord` |
| Telem | `mesh_pending_capture_ready_n`, stale/max age |
| Unit test | `mesh_schedule_retry_test` |

## Harness gates

```powershell
cmake --build build/desktop-msvc --target mesh_schedule_retry_test capture_worker_integration_test -j 4
python tools/flight_sim_run.py --scenario replay-manual --report bin/suite_reports/mesh_R15_replay_manual.json
python tools/flight_sim_phase_gate.py --report bin/suite_reports/mesh_R15_replay_manual.json --phase-id MESH-R15-capture
```

**Expected:** `cruise_schedule_ok_med ≥ 2`, `mesh_schedule_retry_max > 0`, `cruise_idle_spike_share` documents autofly idle exclusion.

## Next (R5 post-SHIP)

GPU mesh pipeline optimizations per `07_roadmap.md` §R5 — only after M4 ownership GO on manual flight.
