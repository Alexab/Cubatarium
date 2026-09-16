# Audit 2026-09-16 S9 closure matrix

Source audit: commit `7a658525` / `docs/streaming/CURRENT_STATE_AUDIT_2026-09-16.md`.
Tails plan: H0–P5 on `cursor_audit2_impl` (land-eye, PublicationDelta, credits/work-slots,
rim NearLoad, oracle/frustum, acceptance evidence).
Full cutover: E0–E9 on `cursor_audit3_impl` (single Replace writer, overlay, sole-owner,
S6–S8 completeness, rim field, S9 CLOSED).

### Full-cutover E0 baseline (`audit16_full_e0_cold`)

Cold `product-174657` no-teleport: west **COVERED** (`focus_cx_min=-10`), Y early→late
**55.5→59**, eye_proxy PASS, incomplete=0, adequacy PASS, dual-lane FAIL (G1 OPEN),
`merge_green=false` (`operator_visual` UNTESTED). CTest publication_audit / frustum /
oracle / GateRepro green. Perf: `bin/logs/perf_20260916-220411_28252.jsonl`.

## Absolute gates (must be green)

| Gate | Owner step | Status |
|---|---|---|
| live/free allocation overlap = 0 | S1 `publication_audit` | code + CTest |
| empty Replace / Remove last material | S2 | code + CTest |
| reorder bumps table identity | S2 | code + CTest |
| packed excluded when MDI resident | S2 GeometryEngine | code |
| RepresentationSwitch clears MDI | S2 `ApplyPublicationDelta` | code + CTest |
| eye-proxy absolute holes + missing fields | S0 | GateRepro |
| `operator_visual=None` ↛ merge_green | S0 scorecard | code |
| west route `cx≤−3` or UNTESTED | S0 | COVERED (autofly) |
| per-frame GPU kick/finish timers | S3 | code |
| source light stamp on publish | S4 | code |
| per-column stalled remesh bookkeeping | S5 | code |
| draw-oracle age not reset on unrelated lit | S5 | CTest |
| snapshot credit lifetime with Store | S6 | hot-path + CaptureAndStore |
| shared work-slot concurrency | S6 | `TryAcquireWorkSlot` |
| spawn-ring cache world epoch + focus.y + unf/nr deps | S7 | code |
| bounded critical overrun (not infinite) | S7 | `FrameDeadline` |
| no independent 4ms GPU floors | S7 | code |
| cull stats buffer-update barrier | S8 | code |
| frustum near/far AABB fixtures | S8 | `frustum_clip_test` |

## Product acceptance

- Gate of record: `product-174657` **no-teleport** west.
- Autofly adequacy ≠ CLOSED.
- Manual operator west: mid improved (`200932`); rim blacks at sea still OPEN.
- SLA 16.7/33.3 ms — proposed, not a gate until profile chosen.

### Harness H0 (`6c071537`)

Land-eye set every frame with floor=`CruiseEyeY` and ceil=`CruiseEyeY+16`.
Cold evidence `audit16_h0_land_eye_cold`: `player_y` early/late med **56**, west COVERED.

### Tails autofly (P5)

Reports under `bin/suite_reports/g1_a10_relight/audit16_tails_*`.

| Run | west | Y early→late | eye_proxy | incomplete | adequacy |
|---|---|---|---|---|---|
| c1 cold | COVERED (−8) | ~55.5→56 | FAIL (holes blink) | 0 | PASS |
| c2 cold | COVERED (−10) | ~55.5→58 | PASS | 0 | PASS |
| c3 cold | COVERED (−9) | ~55.5→57 | PASS | 0 | PASS |
| w1–w3 warm | COVERED (−9/−10) | SoT corridor | PASS/FAIL mix | 0 | PASS |

| Signal | Result |
|---|---|
| dual-lane / mid stalled | FAIL OK (G1 OPEN) |
| merge_green | **false** (`operator_visual` UNTESTED) |
| operator west eye | **UNTESTED** (manual required; rim blacks remain) |
| 10–15 min soak | not wall-clocked this pass (cold×3/warm×3 covers noise) |

## KEEP

N01 incomplete=0, LegalDark rollback, frustum N02, cooldown N06,
I3t hold-prior (temporary).

## FREEZE

N04 census FullyDark remesh caps / wrong-tex gates on misnamed `pass_mdi_stale_*`
(compat alias only; stop-lines use `pass_packed_without_mdi_resident_n`).

## Deferred beyond tails (honest)

- Full single registry writer for all Replace (PublishPassInputs still owns Replace payload)
- Full immutable neighbor overlay (S4) / Flow duplicate writer deletion (S5)
- Product CLOSED without operator eye + rim FirstMesh convergence in field
