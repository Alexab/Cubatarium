# Era34 CreateBar + SoftDefer FOV Fill — baseline

**Branch:** `perf_opt7`  
**SoT manuals:** ocean `223200` (wall≈263, holes=1.0, SoftDefer empty max12); create `224116` (bar 96–99% minutes, enter_void≈354)

## Contract (inherits Era32/33 KEEP)

| Layer | Era34 change |
|-------|----------------|
| Create settle | near-FOV: underfeet LitDrawable + SoftDefer/mesh debt **r≤2** (not full ring=4) |
| Create progress | debt fraction inside Prepare 4% (`1 - debt/peak`); status `Loading FOV… N left` |
| Soft / hard wall | soft **12s after underfeet lit**; hard **20s or 360 ticks** |
| SoftDefer empty | full-focus age scan + ownership rotate; PreferKick **age 15 only**; residual Cd=2 |
| Cruise emerge | `ShouldBiasFirstMeshOverRemesh` clamps emerge ≤14ms + starve remesh for holes |
| FocusIngress | SoftDefer empty floors `first_mesh_admit≥1` |

## REJECT (additions)

| Attempt | Result |
|---------|--------|
| Create progress = ticks/1800 only | bar stuck 96–99% (`224116`) |
| Create wait without soft wall after underfeet | minutes of empty SoftDefer grind |
| Create settle = SoftDefer ring=4 | same tick grind |
| PreferKick on every RemeshAfterApply | holes regress (Era33) |
| Raise NoteMin/vb_bg without void proof | REJECT KEEP |

## Status

- **P0 Create bar:** debt progress + near-FOV settle + soft/hard wall.
- **P1 SoftDefer fill:** ownership rotation + residual every 2f; PreferKick age-only.
- **P2 Cruise wall:** FM bias + emerge clamp under SoftDefer/holes.
- **P3:** matrix + `OCEAN_MANUAL` + create eye still required for CLOSED.

## Autofly evidence (Era34 binary)

| Sample | holes | wall_fly | SoftDefer stuck | miss_stuck | Notes |
|--------|-------|----------|-----------------|------------|-------|
| `era34_ocean` | **0.57** | **114** | 10s soft OK | 18s | vs SoT `223200` holes=1.0 wall≈263; NO-GO holes/void/VB |
| `era34_land` | **0.14** | wall_med≈74 | — | 10s | ARCH_D3_LAND soft miss/wall; holes near 0.12 |
| `era34_stress` | **0.67** | **125** | — | — | `OCEAN_CRUISE_STRESS` **GO** (void≥400 holes≥0.4 harness) |

**Trending vs `223200`:** wall ≪263, holes 1.0→0.57. **partial** — not CLOSED (holes≤0.30 / void / manual eye).
