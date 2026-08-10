# Era32 LitDrawable FOV SLA — baseline

**Branch:** `perf_opt7`  
**Commits:** `9ac2c68f` (P0–P3), `6d54fd0e` (slice draw gate + Relight Note floors)  
**SoT manual eye:** `165423`-class; autofly ≠ CLOSED

## Contract (land + ocean)

| Layer | Behavior |
|-------|----------|
| `kVisualStageLitDrawableHoriz = 4` | Unlit FirstMesh only hinterland |
| Draw gate | `IsChunkSliceRenderReady` hides fully-dark slices in ring |
| Column `draw_ok` | Meshed-ready (holes telem ≠ dark-as-hole flood) |
| VB / void heal | `RelightThenMesh` only |
| SoftDefer empty | Never `FreeChunk` live `GpuResident` |
| RemeshAfterApply | VisualStage damp (not ocean_heal-only) |
| FirstMesh SLA | `first_mesh_admit ≥ 1` on miss/unfinished |
| SoftDefer | Must **not** force-hide live dark (drops Dirty → void spiral) |

## Autofly results

| Scenario | VB | unlit | churn | miss_end | holes | void | Gate |
|----------|-----|-------|-------|----------|-------|------|------|
| `era32_v2_ocean` | 56 | 33.5 | **45** | 1 | **0.44** | 2606 | OCEAN_CRUISE NO-GO |
| `era32_v2_land` | 23 | 1 | 74 | **0** | **0.09** | 0 | **FLY_CLEAN + ARCH_D3_LAND GO** |
| `era32_v2_stress` | 55 | 21 | 39 | 1 | 0.63 | 4217 | **OCEAN_CRUISE_STRESS GO** |
| `era32_fix_ocean` | 60 | 24 | **43** | 1 | 1.0* | **1545** | best void among Era32 ocean |
| baseline `165423` | 68 | 21 | 161 | 1 | 0.80 | 1491 | pre-Era32 manual |

\*pre column/draw split. Churn DoD ≤80: **met** on land+ocean samples.

## Status

- **Land:** Era32 publication + anti-flicker **GO** (ARCH_D3_LAND / FLY_CLEAN on `era32_v2_land`).
- **Ocean:** stress harness GO; smoke/manual still NO-GO (void/VB/miss_end). TD-066 **partial**.
- **Eye:** request manual land+ocean on LitDrawable build (`165423`-class zone).

## REJECT

SoftDefer force-hide live dark; ocean_heal-only publication; Unlit near; CLOSED on autofly smoke; vb_bg floors >2 without void-drain proof.
