# A37 conformance — post AF-matrix remediation (H0–H6)

Date: 2026-09-23  
Parents: [A36_CONFORMANCE](A36_CONFORMANCE_2026-09-23.md), [A31_REAUDIT](A31_REAUDIT_2026-09-23.md)

## Code landed (this workstream)

| Item | Change |
|---|---|
| H0 harness | Far: `CUBA_FLIGHT_MOVE_SPEED_SCALE` default 12; process_timeout≥900. Warm: `CUBA_FLIGHT_WARM` stamps period `warm:1` |
| H1 | [A37_H1_DEFECT_CLASS](A37_H1_DEFECT_CLASS.md) |
| H2 | `AdmitUnfinishedVisualDemand`; Kick OFF unless `CUBA_KICK_UNFINISHED=1`; cutover⇒authority; stop reconcile+orphan cancel |
| H3 | No coverage auto-advance; face debt peer_gen≥1; no clear_mask without record; CrossGpu stale refresh reject |
| H4 | FullyDark → raise light desire + invalidate capture |
| H5 | Defer: no main flags under frame pressure; PreferGpu cold path intact; hitch rejects sync fallthrough; drain completed-only |
| H6 | Kick OFF (`CUBA_KICK_UNFINISHED`); matrix runner `bin/suite_reports/a37/_run_matrix.ps1`; Gate 8 needs clean SHA + eye |

## Unit gates (landed)

- `chunk_render_demand_test` — cutover⇒authority, no coverage auto-advance, face peer_gen
- `mesh_publish_contract_test` — publish contract
- `fluid_surface_pack_reuse_test` — PreferGpu pack reuse

## Gate matrix

| Gate | Status | Notes |
|---|---|---|
| 1 / P0 | CODE | far speed×12 + timeout≥900; warm period `warm:1`; AF re-flight for far_flight=true |
| 2–3 / P1 | DOC | [A37_H1_DEFECT_CLASS](A37_H1_DEFECT_CLASS.md) |
| 4 / P3 | CODE | peer_gen≥1; coverage desire on face debt; no clear without record |
| 5 / P4 | CODE | defer no main 16×16; no inline pending drain; sync hitch skip |
| 6 / P2.2 | CODE | Cross/Greedy RefreshPass backwards reject; ArtifactManifest retirement still separate |
| 7 / P2.1 | CODE | AdmitUnfinished + stop reconcile/orphan; AF demand_stop re-measure |
| 8 / P6 | OPEN | run `_run_matrix.ps1` on clean SHA; merge_green=false until operator eye |
| 9 / P7 | OFF | Ring stays OFF |

## Stop-lines

Ring OFF; Kick heuristic OFF by default; no SoftDefer/PreferKick blanket; dirty AF rejected unless `CUBA_ALLOW_DIRTY_AF=1`.
**AF exe = Release only** (`bin/Cubatarium.exe`). Debug AF forbidden — `ExeDir/worlds` under Debug has no `World_164` → Creating world (`post_create`).
