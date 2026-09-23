# A34 / A35 — conformance (realign after A31 drift)

Date: 2026-09-23  
Parents: [A33_CONFORMANCE](A33_CONFORMANCE_2026-09-23.md), [A31_REAUDIT](A31_REAUDIT_2026-09-23.md)  
Plan: realign after drift (R0–R3) — **not** a claim that A32 S0 / A34 cold AF = A31 Gate 8 PASS.

## Honest Gate 1–9 matrix

| A31 Gate | Plan | Requirement | Status | Evidence |
|---|---|---|---|---|
| 1 / P0 | Clean SHA + far/manifest + cold/warm visible | **PARTIAL** | Clean SHA required for AF close; far harness exists; pixel/far UNTESTED until visible AF on this SHA |
| 2–3 / P1 | Pixel→artifact + one defect class | **PARTIAL** | `ClassifyChunkDefect` + JobStageTrace cull field; freeze/eye OPEN |
| 4 / P3 | Peer gen blocks seam | **PARTIAL** | Negatives in `mesh_publish_contract_test`; AF/pixel OPEN |
| 5 / P4 | Fluid install, no sync hitch, hang-safe | **PARTIAL** | Worker join via `ShutdownFluidSummaryWorker`; Installed path + hitch AF OPEN |
| 6 / P2.2 | Pre-pub holds reject | **PARTIAL** | Paths gated; A34 dark carve-out; A35 SourceMismatch provenance fix |
| 7 / P2.1 | Stop convergence SLA | **PARTIAL** | `StopConverged` face/coverage/Retain; AF `demand_stop_converged` often false |
| 8 / P6 | holes=0 whole-route + operator eye | **OPEN / FAIL** | Manual: holes≥1, partial white; AF must not close |
| 9 / P7 | Ring after above | **OFF** | Correct — do not enable |

**Explicit:** A32 S0 AF FAIL and A34 cold `opaque_cmd_on_med>0` ≠ Gate 8 PASS. `merge_green=false` until Gate 8.

## R0 landed (A35)

| Item | Status |
|---|---|
| SourceMismatch storm (`sourceRevision`≠`InputStamps.content`) | **FIXED** — bake self-consistent stamps; Cross no longer MeshedLight vs Publish light lag |
| Enter false-clear soft settle with MeshService dirty residual | **FIXED** — soft settle blocked when dirty>32; MeshWarmup timeout stamps residual |
| Fluid worker detach hang | **FIXED** — joinable + `ShutdownFluidSummaryWorker` on coop cancel / process shutdown |
| Binding AF | `empty_world_stop_line` + `enter_dirty_residual_stop_line` |

## Manual hang class sample (P0/P1)

Sessions `perf_20260923-171410` / `170746`: primary class = **GeometryMissing / publish starve** under SourceMismatch remesh thrash (not LightInvalid; A34 already 0). Secondary: enter false-clear + MeshWarmup wall timeout with dirty residual.

## Successor

Replace A32 “AF done” successor with realign execution of [remediation_after_a30_open.plan.md](../../.cursor/plans/remediation_after_a30_open.plan.md). See updated [.cursor/plans/remediation_after_a32_open.plan.md](../../.cursor/plans/remediation_after_a32_open.plan.md).

## A30 V3 ArtifactManifest retirement

Still **PARTIAL** — Cross/shell share validate path; full dual-system retirement deferred until P2.2 stable post-A35 (R3). Ring stays OFF.
