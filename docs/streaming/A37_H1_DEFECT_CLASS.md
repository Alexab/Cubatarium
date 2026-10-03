# A37 H1 — AF defect class attribution (cold5 / far1)

Date: 2026-09-23  
Evidence: `bin/suite_reports/a36/cold5.json`, `far1.json`, matrix_summary.  
SHA family: `177e0739` (dirty=clean).

## Samples → primary class

| Sample | Telem | Primary `ChunkDefectClass` | Notes |
|---|---|---|---|
| cold5 mid-fly | unfinished≈80 ≈ not_ready; nfh periods_gt0=3 | **GeometryMissing** | Frontier miss_stuck_cx≈−7; Kick/admit lag, not empty-world |
| cold5 dual | unlit_max≈48 ≫ 15 | **LightStale** (secondary) | Separate from mesh holes; dual-lane FAIL |
| cold5 stop | demand_stop_converged=false; post_stop missing/holes | **GeometryMissing + FaceDebt** | StopConverged blocked by open desire/face |
| far1 cruise | unfinished≈82; nfh=11; miss_stuck 100s @ cx−36 | **GeometryMissing** | Distance 688≪8192; not A31-04 precision |
| far1 hitch | fluid_map max≈106; heavy fluid×17 | **FluidHitch** | Main flags/GPU-off sync residual (H5) |
| far1 dual | unlit_max≈51 | **LightStale** | Same light-desire class as cold |

## Not primary on these AF

- **Culled** — OpaqueCmdTotal≫On present but not causal for nfh periods
- **Precision** — far_flight=false; queues unhealthy
- **LegalDark** — mid FullyDark stalled is diagnostic only

## Causal hypothesis (for H2)

Visible unfinished keys had no `NoteDemand` → Kick/`MarkDirty(cy=0)` bypass → `demand_stop_converged` never true. Fix: AdmitUnfinishedVisualDemand (H2), not stronger Kick.
