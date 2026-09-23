# A31 P5 — attributed black-frame classes

After JobStageTrace expansion and demand/pub/seam/fluid fixes, attribute remaining
visual defects before further recovery heuristics.

## Mutually exclusive classes

| Class | Evidence |
|---|---|
| Geometry not built/published | Trace has demand, no Published outcome; voxel input present |
| Culled / missing from draw | Artifact published; cull_decision excludes; camera-key mismatch |
| Stale / invalid light | published_light_rev ≠ desired; legal zero vs invalid |
| Seam / peer | face_mask debt; peer gen missing |
| Material / pass | catalog / pass descriptor mismatch |
| Coordinate precision | Same local voxel + healthy queues fail only at large \|X\|/\|Z\| |
| Legal dark | Reference light confirms cave / zero light |

## Procedure

1. Reproduce on clean SHA with `product-174657` / `-far` no-teleport.
2. Dump `UJobStageTrace` (FramePerfMonitor emergency / job_trace).
3. Assign **one** primary class per sample; do not mix telemetry `NearFocusHoles`
   with scheduler holes∥pending_light.
4. Fix only the shown branch (see remediation plan PR8). Precision rebase remains
   deferred until far A/B proves it.

## Current status (A33)

JobStageTrace writers cover Admit + Published/Retain/Reject (MeshCache + GPU).
Cull/order miss writers and operator freeze-frame class assignment remain OPEN →
[A33_CONFORMANCE_2026-09-23.md](A33_CONFORMANCE_2026-09-23.md),
[remediation_after_a32_open.plan.md](../../.cursor/plans/remediation_after_a32_open.plan.md).
