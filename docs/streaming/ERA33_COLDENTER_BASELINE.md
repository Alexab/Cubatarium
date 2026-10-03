# Era33 ColdEnter + FOV Fill — baseline

**Branch:** `perf_opt7`  
**SoT prior manuals:** ocean `202518` / `195432`; land cold `202858`  
**Autofly sample:** `perf_20260810-202125` (Era33 P0–P2 binary; hang_killed on exit)

## Contract (inherits Era32 KEEP)

| Layer | Era33 change |
|-------|----------------|
| Enter visual radius | `EnterVisualWarmupRadiusChunks() = kVisualStageLitDrawableHoriz` (4) |
| Cold create force-cap | `ShouldForceEnterVisualCap(..., cold_create)` never forces while debt |
| Create settle | `IsCreateSpawnWarmupSettled = !NeedsEnterGameMeshWarmup()` (visual ring) |
| cy_order | Land: ground → ±1 → canopy; Ocean: sea/prefer then ± (`CyOrderPolicy.h`) |
| SoftDefer empty | FirstMesh ownership KEEP; PreferKick after age SLA 30f KEEP |
| Lit remesh | RemeshAfterApply + PreferKick PendingGpu in lit ring after Relight |
| HoleDrain residual | full-focus every 4 skipped frames; skip suppressed while SoftDefer-empty |

## REJECT (Era32 + Era33 notes)

Keep Era32 REJECT list. Additionally:

| Attempt | Result |
|---------|--------|
| Enter Dirty/greedy radius = full LitDrawable ring (4) | enter Dirty flood / hitch risk — KEEP Dirty r≤2, visual gate r=4 |
| Full `GetData()` solid scan in `NeedsEnterGameVisualWarmup` | enter_app≈1.4s — use sparse sample |

## Autofly evidence

| Sample | holes | void | miss_stuck | enter_void | VB stop | Notes |
|--------|-------|------|------------|------------|---------|-------|
| hang-killed `202125` (partial) | 0.09–0.24 | 0 | 6 | 14 | 21 | incomplete periods; misleading GO trend |
| full Debug `215510` | **0.97** | 725 | 36 | 7 | 40 | PreferKick-on-Remesh flood + denser HoleDrain |
| full Release `era33_rel` | **0.94** | 1758 | 30 | 19 | 36 | worse wall/void |

P2 follow-up: PreferKick only SoftDefer age SLA (15f); empty ownership cap 12; no RemeshAfterApply PreferKick flood.

## Status

- **P0 Cold enter:** ring=4 on bar; no cold force-cap; ground/sea Y-band; sparse solid sample.
- **P1 cy_order:** land ground-first Immediate order.
- **P2 FOV fill:** SoftDefer FirstMesh ownership + age PreferKick; holes still NO-GO on full cruise.
- **P3:** docs/TD-066 updated; full matrix + `OCEAN_MANUAL` + eye still required for CLOSED.
