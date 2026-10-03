# A38 conformance — systemic remediation R0–R7

Date: 2026-09-23  
Parent: [A37_CONFORMANCE](A37_CONFORMANCE_2026-09-23.md), [A31_REAUDIT](A31_REAUDIT_2026-09-23.md)

## Code landed

| Item | Change |
|---|---|
| R0 | `bin/suite_reports/a38/` matrix + DISCARD.md; Release-only harness |
| R1 | Coverage publish on face clear + install `DemandCoverageGenToPublish`; Admit multi-Y + backlog budget; attempt≠0 on Published; unsat breakdown telem; no fake geom bump above content |
| R2 | Admit raises light desire + `InvalidateMeshCapture` for meshed_unlit / FullyDark |
| R3 | Face debt required gen from peer coverage/geom; clear/subscribe use `waiting_peer_gen[f]` |
| R4 | CrossGpu `RefreshPass` calls `ValidateCrossOrShellPublication` before mutate |
| R5 | No empty-flags fluid enqueue; incomplete Pending + PreferGpu cold rebuild |
| R6 | Period `defect_class_primary` + `demand_unsat_*`; [A38_H1](A38_H1_DEFECT_CLASS.md) |
| R7 | Matrix + true-far evidence; Gate 8 still OPEN (holes/dual/demand_stop/eye) |

## AF evidence (`bin/suite_reports/a38/`)

| Run | Key |
|---|---|
| cold×5 / warm×2 | empty+enter PASS; unfinished≈16–19 (was ≈81); fluid≈0; demand_stop=false; holes OPEN; dual OPEN |
| far1 (scale 28, 300s) | **`far_flight=true`**, distance **8512** (≥8192); unfinished≈4; fluid≈0.9; Kick OFF |

Ring stays OFF. Operator eye = UNTESTED → `merge_green` not claimed.

## Unit gates

- `chunk_render_demand_test` OK (coverage publish unlocks stop; attempt≠0)
- `mesh_publish_contract_test` OK
- `fluid_surface_pack_reuse_test` OK

## Stop-lines

Ring OFF; Kick OFF default; no SoftDefer/PreferKick; AF = Release `bin/Cubatarium.exe` only.
