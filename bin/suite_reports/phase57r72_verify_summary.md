# Phase 5.7R7.2 verify (spike / frame-pacing diet)

## Shipped in tree

**Cut A** (cull pacing):
- Opaque AABB probe period `6 → 10` on healthy cruise (`GeometryEngine.cpp`)
- Transparent compact cull reuse parity + period 10 (`GeometryEngine.h/.cpp`)

**Cut B** (DropRemesh diet):
- Cheap SoftDeferHeld / missing-cache continue before `HasDrawableGreedyMesh`
- remesh_only fingerprint skip-walk (`ChunkMeshCache.cpp/.h`)

**Cut C aborted** (anti-regress):
- ABC fly-heavy `perf_20260909-185029_35216.jsonl` → **PRODUCT_FAIL**
  `miss_stuck_run_frames_tail_max=564` with `gpu_kick~0`
- Drain_cap 4→6 + Normal finish-bias **not** in tree

## Auto verify

| Run | SoftDefer | holes_frac | schedule_ok | wall | cull | Product |
|---|---:|---:|---:|---:|---:|---|
| R7.1 baseline 155214 | 0 | 0.24 | 3 | 68.7 | 17.6 | OK |
| ABC 185029 | 0 | 0.10 | 5 | 59.5 | 18.4 | **FAIL** miss_stuck |
| **A+B final 200007** | **0** | **0.10** | **1** | **46** | **7.0** | **OK** |
| Unit | miss_first_mesh_class_test **OK** (period-10 + KEEP locks) | | | | | |

Note: `schedule_ok_med=1` sits on Gate-B hard edge (≤1 fail). Prior A+B rerun had schedule_ok=2. Treat as **WARN for manual** — SoftDefer/holes clean; do not ship on auto alone.

Scorecards:
- [`phase57r72_AB_scorecard.txt`](phase57r72_AB_scorecard.txt)
- [`phase57r72_ABC_scorecard.txt`](phase57r72_ABC_scorecard.txt)
- Report: [`phase57r72_flyheavy_AB.json`](phase57r72_flyheavy_AB.json)

`flight_sim pass=false` is expected (strict wall/scene gates); ship gate is **AnalyzePhase57 PRODUCT_*** + mandatory manual.

## Ship gate (mandatory manual)

Locus World_164 `(120, 57.312599182128906, 56)` yaw 90; Tracy OFF; compare vs **172957 / 131116 / 153045**:

Hard FAIL if SoftDefer stuck≥1, holes≥0.63 / ≫172957, schedule_ok≤1, cruise light-cull skip at spd>1.5.

Primary DoD: cruise `|Δfps| med` ≤1.6, wall>160 ≤20%, SoftDefer=0, schedule_ok≥3.5.
