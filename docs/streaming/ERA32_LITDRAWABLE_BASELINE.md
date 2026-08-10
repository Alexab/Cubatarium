# Era32 LitDrawable FOV SLA — baseline

**Branch:** `perf_opt7`  
**Commits:** `9ac2c68f` → `93d471e6`  
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
| SoftDefer | Must **not** force-hide live dark (Dirty remesh) |
| Relight floors | KEEP Era31 NoteMin=2 / vb_bg≤2 (higher flooded void) |

## Best autofly evidence

| Scenario | VB | churn | miss_end | holes | void | Gate |
|----------|-----|-------|----------|-------|------|------|
| `era32_v2_land` | 23 | 74 | 0 | **0.09** | 0 | **FLY_CLEAN + ARCH_D3_LAND GO** |
| `era32_v5_land` | 24 | 245 | **0** | **0.00** | 0 | FLY_CLEAN GO; ARCH soft churn |
| `era32_v2_stress` | 55 | 39 | 1 | 0.63 | 4217 | **OCEAN_CRUISE_STRESS GO** |
| `era32_v5_ocean` | 61 | **51** | 1 | 0.46 | 1793 | OCEAN_CRUISE NO-GO |
| `era32_fix_ocean` | 60 | **43** | 1 | 1.0* | **1545** | best ocean void |
| baseline `165423` | 68 | 161 | 1 | 0.80 | 1491 | pre-Era32 manual |

Churn DoD ≤80: met on ocean + best land sample. Ocean void/VB/miss_end still above OCEAN_MANUAL.

## Status

- **Land:** publication + anti-flicker largely **GO** (best `era32_v2_land` ARCH_D3_LAND).
- **Ocean:** stress harness GO; smoke/manual **partial** (void≈1.5–1.8k, VB≈60, miss_end=1).
- **TD-066:** partial until `OCEAN_MANUAL GO` + eye.
- **Next:** manual eye land+ocean on this build; ocean void/VB throughput without Note/vb_bg flood.

## REJECT

SoftDefer force-hide live dark; ocean_heal-only publication; Unlit near; CLOSED on smoke; NoteMin/vb_bg knobs without void-drain proof.
