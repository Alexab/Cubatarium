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

## Autofly evidence (`202125`)

| Metric | Value | Target |
|--------|-------|--------|
| effective_holes_rate | **0.24** | ≤0.30 |
| fly_void_near_max | **0** | ≤800 |
| miss_stuck_max_run_sec | 6 | ≤4 |
| miss_end | 1 | 0 |
| enter_void_near_max | **14** | ≪2371 land cold |
| enter_app_update_max | 1420 (pre sparse fix) | ≤200 soft |
| post_stop_visible_black_max | 21 | ≤20 |
| opaque_idle_churn_max | 3 | ≤80 |

OCEAN_CRUISE: near-GO (VB stop 21>20; miss residual). Holes/void strong vs `202518` holes=1.0.

## Status

- **P0 Cold enter:** ring=4 on bar; no cold force-cap; ground/sea Y-band.
- **P1 cy_order:** land ground-first Immediate order.
- **P2 FOV fill:** lit PreferKick + HoleDrain residual; holes trending ≤0.30.
- **P3:** needs clean autofly matrix + `OCEAN_MANUAL` + eye (ocean+land). Closed only with eye.
