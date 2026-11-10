# Manual 190350 vs autofly diagnostics

## Manual `perf_20260730-190350`

Route (−483,54)→(−485,47) (−Z), ~54s, short stand. **ARCH_D3_LAND NO-GO**.

| Metric | Manual | land-south-short v1 | land-south v3 |
|--------|--------|---------------------|---------------|
| miss_stuck | 10s | **0** | **2** |
| miss_end | 0 | **0** | **0** |
| holes | 0.32 | **0** | 0.03 |
| nh_no_miss | 0.47 | **0** | **0** |
| stale_end | **214** | **0** | **0** |
| void_end | ~436 | **0** | **0** |
| sticky | 0 | **0** | **0** |
| wall | 131 | ~42 | ~45 |

Timeline (manual): idle start dark≈710 / stale≈100 / void≈610 for ~16s with miss=nh=0; end miss=0 nh=0 stale=214 (exit before heal).

## Why autofly used to miss it

1. **Corridor:** east / +Z cruise never flew **yaw 270 (−Z)** from (−483,54).
2. **Stop length:** manual ~8s mid-heal; long stop≥60 over-healed.
3. **Threshold:** stale wave `>200` skipped idle stale≈100.
4. **Void path:** CollectStaleDark only tickets field-lit stale; void (light=0) needs Relight.

## Fixes (this track)

### Harness
- `--land-south` / `land-south`: yaw 270, fly 20, **stop≥60** (heal GO).
- `--land-south-short` / `land-south-short`: same corridor, **idle≈3**, fly≈25, **stop≈10** (mid-heal window).
- Teleport path is cleaner than warm resume (manual dark=710 at start); short still validates stop-end blacks stay 0.

### Code
- Stale wave: `stale>80` or `(dark>500 && stale>0)`, skip while missing.
- **Void Relight wave:** `DarkFaceVoidNearN>200` → `CollectFullyDarkFocusColumns` (horiz≤2) → RelightThenMesh/Promote; **idle/stop only** (`!moving`) so cruise FirstMesh is not starved.
- Fog latch: stale>80.

## Validation

| Run | Result |
|-----|--------|
| land_south_short_baseline (pre-void, stop10) | ARCH_D3_LAND GO (teleport already clean end) |
| land_south_short_v1 (void + idle3) | **GO** stale/void/sticky 0 |
| land_south_v3 (long) | **GO** |
| land_south_void_cruise | miss_end=0, stale/void 0; miss_stuck 14–20 (known mid-run; nh soft) |

## Remaining

- Warm-resume void stress (manual dark=710 at spawn) still not fully mirrored by teleport autofly.
- land-cruise `miss_stuck≤4` / holes≤0.10 — separate schedule track.
- SoftDefer Capture floor knobs — out of scope.
