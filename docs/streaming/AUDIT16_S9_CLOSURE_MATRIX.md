# Audit 2026-09-16 S9 closure matrix

Source audit: commit `7a658525` / `docs/streaming/CURRENT_STATE_AUDIT_2026-09-16.md`.
Tails plan: H0–P5 on `cursor_audit2_impl`.
Full cutover: E0–E9 on `cursor_audit3_impl`.

## Absolute gates (must be green)

| Gate | Owner step | Status |
|---|---|---|
| live/free allocation overlap = 0 | S1 `publication_audit` | code + CTest |
| empty Replace / Remove last material | S2 | code + CTest |
| reorder bumps table identity | S2 | code + CTest |
| packed excluded when MDI resident | S2 GeometryEngine | code |
| RepresentationSwitch clears MDI | S2 `ApplyPublicationDelta` | code + CTest |
| **Single Replace writer** | S2 `PublishPassInputs`→`ApplyPublicationDelta` | code + CTest (`25488bfe`) |
| eye-proxy absolute holes + missing fields | S0 | GateRepro |
| `operator_visual=None` ↛ merge_green | S0 scorecard | code |
| west route `cx≤−3` or UNTESTED | S0 | COVERED (autofly) |
| per-frame GPU kick/finish timers | S3 | code |
| oracle wrong-tex / checkerboard / liquid / temporal | S3 | OracleFixtures |
| source light stamp on publish | S4 | code (fail-closed) |
| **versioned boundary overlay** | S4 `BoundaryOverlayState` | code + CTest |
| per-column stalled remesh bookkeeping | S5 | code |
| draw-oracle age not reset on unrelated lit | S5 | CTest |
| **H2 FullyDark Dirty gated OFF** | S5 | code (`kEnableH2FullyDarkDirty=false`) |
| enter FullyDark Dirty pump OFF | S5 | code |
| snapshot credit lifetime with Store | S6 | hot-path + CaptureAndStore |
| shared work-slot concurrency | S6 | `TryAcquireWorkSlot` + lifetime test |
| spawn-ring cache world epoch + focus.y + unf/nr deps | S7 | code |
| bounded critical overrun (not infinite) | S7 | `FrameDeadline` |
| no independent 4ms/6ms GPU floors | S7 | emerge consume uses ledger |
| cull stats buffer-update barrier | S8 | code |
| frustum near/far AABB fixtures | S8 | `frustum_clip_test` |

## Product acceptance

- Gate of record: `product-174657` **no-teleport** west.
- Autofly adequacy ≠ CLOSED.
- SLA 16.7/33.3 ms — proposed profile **not chosen**; not a gate.

### Full-cutover E0 baseline

Cold `audit16_full_e0_cold`: west COVERED (−10), Y **55.5→59**, eye_proxy PASS,
incomplete=0, adequacy PASS, dual-lane FAIL, `merge_green=false`.

### E1 Replace writer

Commit `25488bfe`. Cold `audit16_full_e1_cold`: west COVERED (−8), Y **55.5→56**,
incomplete=0, eye_proxy PASS. I3t hold-prior **still temporary** (not dropped).

### E3–E8 cutover AF

Cold `audit16_full_e3e8_cold` (`perf_20260916-230258_39472.jsonl`):
west COVERED (−11), Y **55.5→59**, incomplete=0, eye_proxy PASS, adequacy PASS,
dual-lane FAIL (`mid_fully_dark_stalled_med` **47**, improved vs E0 **57**).

| Signal | Result |
|---|---|
| dual-lane / mid stalled | FAIL OK (G1 OPEN) |
| merge_green | **false** |
| operator west eye | **UNTESTED** (manual required; rim blacks may remain) |
| 10–15 min soak | not wall-clocked (cold AF ~95s only) |
| product CLOSED | **OPEN** — needs operator PASS + soak + dual-lane policy |

## KEEP

N01 incomplete=0, LegalDark rollback, frustum N02, cooldown N06,
I3t hold-prior (temporary).

## FREEZE

N04 census FullyDark remesh caps / wrong-tex gates on misnamed `pass_mdi_stale_*`
(compat alias only; stop-lines use `pass_packed_without_mdi_resident_n`).

## Gap-closure B0 baseline (2026-09-17)

Cold `audit16_gap_b0_cold` (`perf_20260917-104913_9248.jsonl`):
west COVERED (−9), Y **55.5→56.5**, incomplete=0, eye_proxy PASS,
adequacy PASS, dual-lane FAIL (`mid_fully_dark_stalled_med` **49**).
CTest `publication_audit|inputs_still_valid|frustum_clip|job_admission` PASS.

West rim (`cx≤−3`, `y∈[54,68]`): moving med clnm/ring/unf **31/34/31**;
stop med clnm/unf **35/35** (max 50). Manual 101527 anchor: wm **15/22**, stop max **32**.
Product CLOSED still **OPEN** — R1 rim FirstMesh next.

## Remaining for true CLOSED

1. Manual `operator_visual=PASS` on west mid **and** sea rim.
2. Wall-clock soak 10–15 min (free-list/dirty/age non-linear).
3. Dual-lane mid stalled ≤5 or documented OPEN-with-cause accepted for merge.
4. Optional: drop I3t after more Replace field soak.
5. Gap-closure R1→S9 (see plan audit_gaps_closure).
