# Land rim fix loop — closeout

**Verdict: NO-GO** for `ARCH_D3_LAND` on canon L2 (yaw 90). Do not merge to `develop` on this track yet. Ocean `--replay-edge` unchanged / not required.

## What landed

| Phase | Change | Result |
|-------|--------|--------|
| P0 | `nh_no_miss_rate≤0.25` gate; wall soft 55; terrain eye `FindHighestSolidY+12`; land analyze `warmup≥16s` | Harness catches land symptoms |
| P1 | Relight urgent prio ≤55 while missing; FirstMesh before promote; SoftDefer **first-mesh in focus never deferred** (remesh-only SoftDefer); AllowUnlit focus | Best mid: miss_stuck **14s**, holes ~0.27, sticky **0** (no 092627 thrash). Target miss≤4 / holes≤0.10 **not met** |
| P2 | Eye +12 | Mid-run `dark_stale` max **~3k** (stress OK); stop **end** often 0 after recovery |
| P3 | Idle DropRemesh keep_h=1; seam drain bump | Churn 326→~160; still >120; sticky 0; no dark Capture floor |
| P4 | L2 + L3 west | See below |

## P4 runs

| Run | Route | miss_stuck | holes | churn | dark_stale_end | opaque_med | wall_med | Notes |
|-----|-------|------------|-------|-------|----------------|------------|----------|-------|
| L2 | south yaw 90 | 16s | 0.29 | 159 | 0 | 899 | 58 | Canon; NO-GO |
| L3 | west yaw 180 | 2s | 0 | 106 | **240** | 199 | 111 | Not blue_screen; **chunks_traveled=0** (stuck spawn / hitch) |

## Remaining

1. **miss_stuck ≤4 / holes ≤0.10** — SoftDefer remesh-only + Relight prio insufficient under terrain eye load; needs deeper ColumnFlow / schedule work (out of narrow P1 scope).
2. **opaque_idle_churn ≤120** — keep_h=1 helps; still ~160 on L2.
3. **wall_ms_med ≤40** — leave ARCH_D3_LAND soft **55** until miss path cools emerge.
4. **West L3** — dark_stale stress works (240 end) but travel=0; investigate hold-space / hitch after terrain eye.
5. Manual land symptoms (holes / black faces / flicker) still expected until (1)–(2) close.

## Reports

- `bin/iter_reports/land_fix_P1g.json` — best P1 mid
- `bin/iter_reports/land_fix_P2.json` — dark mid-run stress
- `bin/iter_reports/land_fix_P3.json`
- `bin/iter_reports/land_fix_P4_L2.json` / `land_fix_P4_L3.json`
