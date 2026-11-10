# Column stage SLA — closeout

**Verdict: forever-hole / black-neighbor class GO on land-stand; full `ARCH_D3_LAND` phase gate NO-GO** (`nh_no_miss_rate` residual). Do not merge to `develop` until land-stand clears the full phase gate. Ocean `--replay-edge` out of scope.

**SoTA alignment:** Admit now follows derived `should_mesh` each tick (provisional UnlitFirstMesh while PendingLight) instead of waiting on a Capture barrier — same class of fix as voxel_enginevk “lost chunk” and Sodium provisional→remesh. Capture stays paced under miss (priority + budget); SoftDefer knobs were not retuned.

## What landed

| Step | Change | Result |
|------|--------|--------|
| S1 | `AdmitFocusVisibleMissing`: always MarkDirty + Meshing when missing solid (PendingLight only enqueues relight) | land-stand `miss_end=0`, `miss_stuck=4s` |
| S2 | Stale-wave near-ring even while another focus column missing; radius `horiz≤1` under miss | `stale_end=0`, sticky=0 |
| S3 | While miss: Capture floor paced 1–2; promote `r=1` only; no dark_debt Capture floor | no 092627 softdefer/sticky plateau on stand |
| S4 | `--land-stand` / scenario `land-stand` (east yaw 0, stop≥60, warmup≥16, `ARCH_D3_LAND`) | harness repro for manual 170154 |

## Runs

| Run | Route | miss_stuck | miss_end | holes | nh_no_miss | stale_end | sticky | churn | wall_med |
|-----|-------|------------|----------|-------|------------|-----------|--------|-------|----------|
| land-stand S1 | east yaw 0 | **4s** | **0** | **0.08** | **0.32** FAIL | **0** | **0** | 116 | **51.6** |
| land-cruise S1 | south yaw 90 | 14s | 0 | 0.24 | 0.24 | 0 | 0 | 159 | 64.7 |

vs P4 L2 cruise: miss_stuck same (14), holes better (0.24 vs 0.29), churn same (~160), wall slightly worse (65 vs 58). No sticky thrash.

## Plan success vs phase gate

| Metric (plan) | Target | land-stand |
|---------------|--------|------------|
| `miss_end` | 0 | **OK** |
| `miss_stuck_max_run_sec` | ≤4 | **OK** (4.0) |
| `stop_dark_face_stale_near_end` | <100 | **OK** (0) |
| `post_stop_black_sticky_max` | 0 | **OK** |
| 092627 sticky/softdefer plateau | none | **OK** |
| land-cruise vs P4 | not worse miss/churn/sticky | miss/churn/sticky OK; wall soft |

`ARCH_D3_LAND` gate still fails land-stand on `nh_no_miss_rate` (0.32 > 0.25) — light-debt / pending proxy with mesh present; SoftDefer remesh-until-lit intentionally keeps that class. Cruise still fails miss_stuck/holes/churn/wall (known mid-run stress; not the forever-hole stand repro).

`wall_ms_med` soft **55** kept until emerge cools further.

## Reports

- `bin/iter_reports/land_stage_S1.json` — land-stand
- `bin/iter_reports/land_stage_S1_cruise.json` — L2 sanity
- Manual baseline: `perf_20260730-170154_*` / forever-hole at (−484,47)

## Remaining (next track)

1. Clear `nh_no_miss≤0.25` without SoftDefer Capture floor knobs (faster MarkRelit clear / seam remesh after provisional commit).
2. Cruise mid-run `miss_stuck≤4` / holes≤0.10 under terrain eye (deeper schedule; out of this SLA patch).
3. Merge to `develop` only after land-stand full `ARCH_D3_LAND` GO.
