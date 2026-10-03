# A42 conformance — LightRepair remesh vs SoftDefer

Date: 2026-09-24  
Parent: [ADR](ADR_VISUAL_OBLIGATION_SOT_2026-09.md) (A42 amend), [135414 autopsy](A41_MANUAL_135414_AUTOPSY.md)

## Landed

| Item | Change |
|---|---|
| P0 | ADR SoftDefer must not cancel LightRepair remesh; `SoftDeferAllowsLightRepairRemesh` + units |
| P1 | `IsLightRepairRemeshFn` from Emerge; Cache SoftDefer prune/schedule fallthrough |
| P2 | ClearPending FD/`stale` gates **unchanged**; Accept/unlit-preview SoftDefer **unchanged** |
| P4 | Gate8 notes + `a42/_run_matrix.ps1`; cold1 FAIL (unlit/holes); merge_green OPEN until eye+clean SHA |
| A42b | Capture reserve for LightRepair remesh + no SoftDeferOwned demote — [follow-up](A42_COLD1_BLACKS_FOLLOWUP.md) |
| A42c–e | miss remesh floor; FD/StaleVL SoftDefer; ClearPending unpin FullyDark/StaleVL — dual-lane PASS, eye/holes OPEN |

## P2 stop-line verification (no code greenwash)

- [`World.cpp`](../../src/World/Core/World.cpp) `ClearPendingLightAfterMeshCommitted`: still skips `any_fully_dark && !legal_settled` and `any_stale_dark`.
- SoftDefer MeshLitGate / unlit FullyDark preview: not relaxed.
- Exempt is **schedule only** (`SoftDeferAllowsLightRepairRemesh` → fallthrough Capture).

## Stop-lines

Kick OFF; PreferKick OFF; no SoftDefer blanket; no ClearPending FD ignore; no light+1; Ring OFF until eye+holes.
