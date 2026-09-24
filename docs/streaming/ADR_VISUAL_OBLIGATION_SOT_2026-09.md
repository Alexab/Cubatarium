# ADR: Visual Obligation SoT (A41)

Date: 2026-09-24  
Status: Accepted  
Parent: [A41 plan](../../.cursor/plans/a41_visual_sot_root_cause.plan.md), [A40 LegalDark](A40_P0_BASELINE_DEADLOCK_2026-09-24.md)

## Context

Since `dcc02e27`, black chunks were patched via independent hide/ready layers
(draw, Ready×2, SoftDefer, PendingLight, FullyDark/LegalDark, defect class).
Each gate fix reopened another. A40 cleared PL↔FD deadlock for caves but left
`open_sky` equal-rev FullyDark drawing black with no remesh owner (D1).

## Decision

Introduce exclusive **VisualObligation** classes as the sole visual SoT:

| Class | Draw | PendingLight | Remesh |
|---|---|---|---|
| LitDrawable | show | clear | n/a |
| LegalDark | show | clear | forbidden |
| LightRepair | hide until Published | keep until Published | **one** Invalidate+Dirty (content light desire, never +1) |
| GeomRepair | hide if !mesh | n/a | Admit Dirty / coverage desire |
| SoftDeferOwned | hide | n/a | only with Geom\|Light ticket+SLA |

### Invariants

1. Hide ⇒ exactly one class with `attempt_id` + deadline.
2. Draw ⇔ LitDrawable ∨ LegalDark.
3. PendingLight ⊆ LightRepair.
4. `RenderReady` written only by LitDrawableCommit / PromoteReadyFromObligation.
5. Desire light = content_light (never invent +1).

### Equal-rev FullyDark (exactly three)

1. **LegalDark** — `!open_sky` ∧ equal-rev ∧ !stale → stamp, clear PL, draw, no Dirty.
2. **LightRepair** — `open_sky` ∧ equal-rev ∧ FD **or** still_stale → one Dirty attempt; keep PL until Published.
3. **Already Dirty/Inflight** — coalesce, no second invent.

### Ready writers

`ClearPendingLightAfterMeshCommitted` must **not** set `RenderReady` (LitReady only).
Sole promote: Emerge LitDrawable Accept (or PromoteReadyFromObligation).

### Replace

`ShouldCooldownForceEqualRevPendingFullyDark` — do not expand; LightRepair terminal owns remesh.
Keep `ShouldForceMarkRelitForTicketedStale` for true still_stale|ticket only.

## Consequences

- A40 cave LegalDark KEEP.
- open_sky wrong bake gets remesh without desire invent / Kick / mass remesh.
- SoftDefer cannot hide without a repair ticket.
- Gate 8 cold/warm AF requires `CUBA_FLIGHT_MOVE_SPEED_SCALE=1`.

## A42 amend (2026-09-24)

Evidence: manual `135414` — SLA remint live, SoftDefer **RemoveAt** drawable remesh
under LightRepair+PL ⇒ FD/PL deadlock.

**Amend SoftDefer vs LightRepair:** SoftDefer may defer FirstMesh / unlit sole-image
publish while PendingLight. SoftDefer **must not** cancel schedule of drawable remesh
Dirty when `visual_obligation == LightRepair` (that Dirty is the Published owner).

Predicate: `SoftDeferAllowsLightRepairRemesh` — schedule fallthrough only; Accept /
unlit-preview / ClearPending FD gates unchanged.

**A42b (cold1 after SoftDefer fallthrough):** `skip_softdefer→0` but `ok_remesh→0` /
`skip_snapshot` dominate — FirstMesh spent `CaptureRefreshBudgetLeft`. Contract:
reserve Capture refresh credits for LightRepair remesh; `OnSoftDeferHeld` must not
demote `LightRepair` → `SoftDeferOwned`.
`IsLightRepairRemesh` also true when `PendingLight` (ADR: PL ⊆ LightRepair) so
Capture reserve applies without stamp lag.

**A42c (cold_a42b/b2):** Capture reserve still dead — A29 U1 sets `remesh_cap=0`
under sticky `focus_missing`/`StarveRemeshForHoles`, so remesh never calls
`TryAcquireSnapshot`. Contract: keep `LightRepairCaptureReserveLeft` as remesh
schedule floor under miss; remesh snapshot slice LightRepair/FullyDark-only (no
hinterland A29 U1 reopen). SoftDefer fallthrough also for FullyDark stamp-lag.

**A42d:** After A42c `mid_fully_dark_stalled→0` but `vb_fly≈stale_vl` — StaleVL
drawable remesh must share SoftDefer/miss floor eligibility; Capture reserve up
to 4 under VB≥12.

**A42e:** ClearPending pinned PL on FullyDark/StaleVL census while column was
already lit-ready → SoftDefer forever → `ShouldRejectDarkMeshCommit` rejects
remesh → Capture stays dark (`ok_remesh>0`, VB stuck). Clear on LitReady only;
mesh heal is LightRepair remesh after SoftDefer lifts.

## Stop-lines (inherited)

No SoftDefer/PreferKick blanket, Kick ON, FullyDark mass remesh flood, Ring ON
before eye+holes, force-stale every-apply, shell-light mirror, eye_proxy/unfinished
hole_key as merge_green.
ClearPending must not ignore FD without lit/LegalDark terminal.
