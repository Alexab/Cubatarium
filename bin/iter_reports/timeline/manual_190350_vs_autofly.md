# Manual 190350 vs autofly diagnostics

## Manual `perf_20260730-190350`

Route (−483,54)→(−485,47) (−Z), ~54s, short stand. **ARCH_D3_LAND NO-GO**.

| Metric | Manual | north_v3 | land-south v2 (new) |
|--------|--------|----------|---------------------|
| miss_stuck | 10s | 0 | **0** |
| miss_end | 0 | 0 | **0** |
| holes | 0.32 | 0 | **0** |
| nh_no_miss | 0.47 | 0 | **0.03** |
| stale_end | **214** | 0 | **0** |
| dark_end | **650** | 0 | **0** |
| churn | 96 | 352 | **85** |
| wall | 131 | 35 | **38** |

Timeline: idle start dark≈710 / stale≈100 / void≈610 for ~16s with miss=nh=0; end miss=0 nh=0 stale=214 (exit before heal).

## Why autofly missed it

1. **Corridor:** east land-stand / +Z cruise / north (−Z opposite of L2 name) never flew **yaw 270 (−Z)** from (−483,54).
2. **Stop length:** manual stop ~8s; heal needs stop≥60 (stale wave cooldown 2s).
3. **Threshold:** stale wave required `stale>200`; idle start stuck at stale≈100 (void-dominated) → no tickets.
4. **nh gate already removed** (182125); residual was threshold + route.

## Fixes landed

- Stale wave: `stale>80` or `(dark>500 && stale>0)`, still skip while missing.
- Fog latch: stale>80.
- Harness: `--land-south` / scenario `land-south` (yaw 270, fly 20, stop 60).
- **land-south v2 → ARCH_D3_LAND GO**.

## Remaining (manual-class)

- Short exits still snapshot mid-heal (`stale_end` high).
- Void-heavy dark (light field 0) needs Relight/neighbor, not remesh alone.
- land-cruise mid-run miss/nh still separate from this stand class.
