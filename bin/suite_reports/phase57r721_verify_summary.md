# Phase 5.7R7.2.1 verify — Cut B revert

## Plan

[`.cursor/plans/phase57r72_1_cutb_revert.plan.md`](../../.cursor/plans/phase57r72_1_cutb_revert.plan.md)

## Done in tree

**Reverted Cut B** (`ChunkMeshCache.cpp` / `.h`):
- DropRemesh remesh_only fingerprint early-return — removed
- SoftDeferHeld / missing-cache cheap continue reorder — restored to `!HasDrawableGreedyMesh` + RemeshAfterApply guard

**Kept Cut A** (`GeometryEngine`):
- Opaque AABB probe period 10 on healthy cruise `spd>1.5`
- Transparent compact cull reuse
- Unit locks period-10 / no light-cruise skip / half-rate false

**Not touched:** Cut C, AbortDrip, PreferKick-empty, SoftDefer RemoveAt

## Verify

| Check | Result |
|---|---|
| `miss_first_mesh_class_test` Release | **OK** |
| `Cubatarium` Release rebuild | **OK** (ChunkMeshCache.cpp rebuilt) |

## Ship gate (mandatory manual)

Locus World_164 `(120, 57.31, 56)` yaw 90; Tracy OFF.

Compare vs **093849** (bad A+B), **172957** (R7.1), **131116** (KEEP):

| Gate | Pass if |
|---|---|
| SoftDefer stuck | =0 |
| schedule_ok | ≥3.5 (hard FAIL ≤1) |
| dirty_fm | ≫2 (near R7.1 ~8) |
| dirty_remesh | not stuck at 0 |
| holes cruise | ≯172957 (~0.54); not ≫ KEEP |
| wall / cull | keep most of Cut A win vs 172957 if possible |

Do **not** ship on auto alone.
