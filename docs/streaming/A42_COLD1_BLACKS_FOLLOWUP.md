# A42 cold blacks — follow-up (A42b → A42e)

Date: 2026-09-24

## Verdict

**Not merge-green / eye still sees blacks** (`vb_fly≈31`, sticky `near_focus_holes=1`).
Contract progress is real: remesh alive, FullyDark stall cleared, **dual-lane PASS**,
unlit drained, PL unpin (A42e).

| Metric | a42b | a42c2 | a42d | **a42e** |
|---|---|---|---|---|
| ok_remesh med / sum | 0 / 4 | 2 / 69 | 2 / 75 | **2 / 89** |
| mid_fully_dark_stalled | 17 | 0.5 | **0** | **0** |
| vb_fly_med | 38 | 36 | 32 | **31** |
| unlit_max | 25 | 28 | 30 | **13** |
| PL med | 24 | 39 | 50 | **13** |
| dual-lane | FAIL | FAIL unlit | FAIL unlit | **PASS** |
| eye-proxy | FAIL | FAIL | FAIL holes | FAIL holes |

## Root cause chain

1. **A42:** SoftDefer RemoveAt blocked LightRepair remesh.
2. **A42b:** FM spent Capture → remesh Deferred; SoftDeferHeld demoted LR.
3. **A42c:** A29 U1 `remesh_cap=0` under sticky miss → Capture reserve dead.
4. **A42c2/d:** FullyDark + StaleVL SoftDefer/miss eligibility; Capture reserve ≤4.
5. **A42e:** `ClearPending` pinned PL on FullyDark/StaleVL census while lit-ready →
   SoftDefer forever → `ShouldRejectDarkMeshCommit` → dark Capture loop.

## Remaining owners (next)

- Sticky `near_focus_holes≈1` (eye-proxy / A24).
- Residual StaleVL / VB after remesh (early `ok_remesh` still weak).
- Gate8 / merge_green OPEN until clean SHA + eye.

## Stop-lines kept

No PreferKick / mass Dirty / light+1 / ClearPending greenwash as sole heal.
Accept FD gates unchanged.
