# Decision memo — mesh architecture R1 ship fix (2026-09-01)

**Status: R1 IMPLEMENTED — autofly gate PENDING rebuild**

## R1 changes landed

| Item | Status |
| --- | --- |
| R1-A Admit marked count | `AdmitFocusVisibleMissing` returns `marked_total`; telem `admit_candidates_n` / `admit_marked_n` |
| R1-B Pending capture + retry | `TryAcquireSnapshotForSchedule`, `PendingCaptureSet`, `RetryPendingCaptures` same-tick |
| R1-C M2a band API | `ReadChunkBandForCapture`, worker by-value `Enqueue`, `BumpWorldEpoch` on `MarkAllDirty` |
| R1-D admission carve | `ShouldSuppressFmAdmissionCarveOut` FM-empty exception; I12-C2 schedule-starved extension |
| R1-E FM refill | post-rebuild `ColumnFlow` FirstMesh enqueue when `dirty_fm==0` && holes |
| R2-A prefetch | `PrefetchMeshCapture` async worker path (no sync `CaptureAndStore` when worker on) |
| R4-H1 | `movement_speed` in perf jsonl |

## Unit tests

| Test | Result |
| --- | --- |
| `capture_worker_integration_test` | PASS |
| `capture_incremental_test` | PASS |

## Autofly gate (R1)

**BLOCKED:** `Cubatarium.exe` rebuild blocked by `vc143.pdb` lock (parallel MSBuild / running sim). Re-run sequential trio after clean single-thread build:

```powershell
cmake --build build/desktop-msvc --target Cubatarium -j 1
python tools/flight_sim_run.py ...  # sequential, not parallel
```

**Expected post-R1:** `dirty_fm_med > 0`, `mesh_schedule_ok_n > 0`, `mesh_pending_capture_n` > 0 on miss ticks, `mesh_schedule_retry_after_capture_n` > 0.

## Phase verdict (updated)

| Phase | Verdict |
| --- | --- |
| R0 | GO — `04_hotspot_matrix.md` |
| R1 code | GO — §A.3–A.7 implemented |
| R1 gate | PENDING — autofly |
| R2 M1-3 incremental shell | NOT WIRED — `RefreshIncrementalShell` uncalled in schedule |
| R3 M4 lint | PARTIAL — `scripts/m4_ownership_lint.py` |
| R5 M3 GPU | DEFERRED post-SHIP |
