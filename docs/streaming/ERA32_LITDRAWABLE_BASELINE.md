# Era32 LitDrawable FOV SLA — baseline

**Branch:** `perf_opt7`  
**SoT manual eye:** `183525` (ocean cruise); prior `165423`  
**Autofly ≠ CLOSED** until OCEAN_MANUAL + eye

## Contract (land + ocean)

| Layer | Behavior |
|-------|----------|
| `kVisualStageLitDrawableHoriz = 4` | Unlit FirstMesh only hinterland |
| Draw gate | `IsChunkSliceRenderReady` hides **fully-dark** in ring **even during PendingLight/PendingGpu** (hole > black plug) |
| Column `draw_ok` | Meshed-ready (holes telem ≠ dark-as-hole flood) |
| VB / void heal | `RelightThenMesh` only; CountVisibleBlack counts drawable dark (not drawn-only) |
| SoftDefer empty | Never `FreeChunk` live `GpuResident` |
| RemeshAfterApply | VisualStage damp (not ocean_heal-only) |
| FirstMesh SLA | `first_mesh_admit ≥ 1` on miss/unfinished |
| SoftDefer | Must **not** force-hide live dark (Dirty remesh) |
| Relight floors | KEEP Era31 NoteMin=2 / vb_bg≤2 (higher flooded void) |

## REJECT (proven)

| Attempt | Result |
|---------|--------|
| SoftDefer force-hide live dark | void death spiral |
| SoftDefer-reject dark FirstMesh (`defer` without `had_mesh`) | holes_rate≈1.0 / SoftDefer empty stuck |
| Hide dark out to full focus_radius | void 3–7k spiral |
| Hide StaleDark (not only FullyDark) | discarded_late≈50 remesh thrash |
| CountVisibleBlack drawn-only | starved VB Relight tickets |
| NoteMin/vb_bg knobs without void-drain proof | void flood |

## Evidence

| Sample | VB | churn | miss_end | holes | void | Gate |
|--------|-----|-------|----------|-------|------|------|
| manual `183525` | 62 | 85 | 1 | 0.87 | 1290 | OCEAN_MANUAL NO-GO |
| `era32_hide4_ocean` | 51 | **13** | 1 | 0.77 | **887** | OCEAN_CRUISE NO-GO (void≈800, VB stop, holes) |
| `era32_hide3_stress` | 55 | 46 | 1 | 0.86 | 1735 | **OCEAN_CRUISE_STRESS GO** |
| `era32_hide2_land` | 0* | 5 | **0** | **0.03** | 0 | near ARCH_D3 (miss_stuck=6) |
| `era32_v2_land` (prior) | 23 | 74 | 0 | 0.09 | 0 | FLY_CLEAN + ARCH_D3 GO |

\* drawn-only VB telem experiment; reverted for heal.

## Status

- **P0 LitDrawable FOV:** draw gate no longer keep-priors Unlit blacks in ring (eye: black→hole until lit bind).
- **Land:** holes/churn strong; miss_stuck/miss_end residual.
- **Ocean:** void trending down (887 vs 1.3–1.8k); VB/holes/miss still NO-GO.
- **Next:** lit remesh after Relight throughput; FirstMesh miss_stuck; manual eye re-flight.

## Eye checklist (manual)

1. No constant black water/land surface in near FOV (ring≤4).
2. Transient holes OK until lit remesh; fewer than prior black plugs.
3. Flicker (opaque churn) ≤80.
