#pragma once

namespace cutum
{

/// Calm cruise: few fixed steps. Red streaming (phase over / miss / wall>100)
/// uses a tighter cap so locomotion cannot spend ~56 ms catching up a 200 ms
/// frame (Gaffer spiral / Unity Maximum Allowed Timestep).
constexpr int kPhysicsSubstepCapCalm = 4;
constexpr int kPhysicsSubstepCapRed = 3;
constexpr float kPhysicsFixedDt = 1.0f / 60.0f;

inline bool IsStreamingPhysicsRed(bool phase_budget_over,
                                  bool focus_missing_mesh, double prev_wall_ms)
{
  return phase_budget_over || focus_missing_mesh || prev_wall_ms > 100.0;
}

inline int PhysicsSubstepCap(bool streaming_red)
{
  return streaming_red ? kPhysicsSubstepCapRed : kPhysicsSubstepCapCalm;
}

struct PhysicsAccumulatorDrain
{
  int steps{0};
  float leftover{0.0f};
};

/// Horizontal speed for prefetch / Immediate gates. Divide by simulated
/// substep time, not wall dt — otherwise a 3-step cap on a 250 ms hitch
/// reports 0.2× speed and cruise looks idle (Immediate 50 ms greedy).
inline float MovementSpeedFromDisplacement(float dist_xz, float wall_dt,
                                           int physics_substeps, float fixed_dt)
{
  const float sim_dt = (physics_substeps > 0 && fixed_dt > 0.0f)
                           ? static_cast<float>(physics_substeps) * fixed_dt
                           : wall_dt;
  const float denom = sim_dt > 1.0e-4f ? sim_dt : 1.0e-4f;
  return dist_xz / denom;
}

/// Drain `dt` steps up to `cap`. If the cap is hit with leftover still ≥ dt,
/// drop the debt (slow-mo) so the next frame does not inherit 12 catch-up steps.
inline PhysicsAccumulatorDrain DrainPhysicsAccumulator(float accumulator,
                                                       float dt, int cap)
{
  PhysicsAccumulatorDrain out;
  out.leftover = accumulator;
  if (dt <= 0.0f || cap <= 0)
  {
    if (cap <= 0 && out.leftover >= dt && dt > 0.0f)
    {
      out.leftover = 0.0f;
    }
    return out;
  }
  while (out.leftover >= dt && out.steps < cap)
  {
    out.leftover -= dt;
    ++out.steps;
  }
  if (out.steps >= cap && out.leftover >= dt)
  {
    out.leftover = 0.0f;
  }
  return out;
}

} // namespace cutum
