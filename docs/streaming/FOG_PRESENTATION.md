# Distance fog presentation (asymmetric smoothing)

The fog *target* (render distance pull-in, end margin, start ratio) changes in
steps whenever streaming debt appears or clears. The value sent to the renderer
is smoothed by `StepFogRangeSmoother` (`src/Render/Engine/FogRangeSmoother.h`).

Why not a linear rate cap: it starts and stops at full speed, so each target
change reads as a snapping fog wall. In a 200 s land flight the old scheme moved
the horizon at up to 168 blocks/s; the smoother peaks at about 8 blocks/s for the
same route.

Model (second order, attack/release like an audio envelope follower):

- desired speed = clamp(delta / tau) with a per-direction time constant;
- speed follows it with a per-direction acceleration cap, so the wall eases in
  and eases out; braking/reversal uses the larger cap so it never overshoots;
- contracting (hide unfinished geometry): tau 1.4 s, <= 26 blocks/s, 32 blocks/s^2;
- expanding (reveal): tau 4.5 s, <= 9 blocks/s, 7 blocks/s^2 (slow, so flickering
  readiness cannot pump the wall).

Parameters live in `FogRangeSmootherParams`. Flight telemetry records
`fog_horizon_min/max/tv/max_rate/max_accel` per window (`CUBA_FLIGHT_TELEMETRY=1`).
Unit test: `fog_range_smoother_test`.
