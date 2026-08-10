# Era32 LitDrawable FOV SLA — baseline

**Branch:** `perf_opt7`  
**Commits:** `9ac2c68f` (P0–P3) + follow-up draw gate / Relight floors  
**SoT manual eye:** `165423`-class (new world); autofly ≠ CLOSED

## Contract (land + ocean)

| Layer | Behavior |
|-------|----------|
| `kVisualStageLitDrawableHoriz = 4` | Unlit FirstMesh only hinterland (`horiz > 4`) |
| Draw gate | `IsChunkSliceRenderReady` hides fully-dark slices in ring (no black plugs in MDI) |
| Column `draw_ok` | Meshed-ready for holes telem (missing mesh ≠ dark-as-hole flood) |
| VB / void heal | `RelightThenMesh` only (no RemeshSeam-as-heal) |
| SoftDefer empty | Never `FreeChunk` live `GpuResident` |
| RemeshAfterApply | Live drawable damp via VisualStage (not ocean_heal-only) |
| FirstMesh SLA | `first_mesh_admit ≥ 1` on miss/unfinished |

**REJECT:** SoftDefer force-hide live dark (drops Dirty remesh → void spiral); ocean_heal-only publication; Unlit near; CLOSED on autofly smoke alone.

## Autofly snapshot (post fix SoftDefer + slice gate + Relight Note≥4)

| Scenario | VB max | unlit_med | churn | miss_end | holes | void max | notes |
|----------|--------|-----------|-------|----------|-------|----------|-------|
| `era32_fix_ocean` | 60 | 24 | **43** | 1 | 1.0* | 1545 | churn DoD≤80; holes telem before column draw_ok split |
| `era32_fix_land` | 22 | 2 | 57 | **0** | 0.80 | 0 | FLY_CLEAN GO |
| `era32_fix_stress` | 56 | 24 | 44 | 1 | 1.0 | 3182 | OCEAN_CRUISE_STRESS **GO** |

\*holes=1.0 when unfinished counted dark-as-unfinished; v2 separates draw hide from unfinished.

## Gates

- `OCEAN_CRUISE_STRESS` — GO (debt parity KEEP)
- `FLY_CLEAN` — GO on land
- `OCEAN_CRUISE` / `OCEAN_MANUAL` / `ARCH_D3_LAND` — still NO-GO until VB≤20, void≪800, holes≤0.30 + eye

## TD-066

**Partial** until `OCEAN_MANUAL GO` + manual eye land+ocean on LitDrawable build.
