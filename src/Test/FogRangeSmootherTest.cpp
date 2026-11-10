// Fog distance smoothing: eases in/out, never overshoots, contracts faster
// than it expands, and stays continuous in velocity.
#include "Render/Engine/FogRangeSmoother.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "fog_range_smoother_test: " << message << std::endl;
    std::exit(1);
  }
}

using cutum::FogRangeSmootherParams;
using cutum::FogRangeSmootherState;
using cutum::StepFogRangeSmoother;

struct Trace
{
  float time_to_target{-1.0f};
  float max_speed{0.0f};
  float max_accel{0.0f};
  float final_value{0.0f};
  bool overshoot{false};
};

static Trace Run(float from, float to, float seconds)
{
  const FogRangeSmootherParams params{};
  FogRangeSmootherState state{from, 0.0f};
  Trace trace;
  const float dt = 1.0f / 60.0f;
  float previous_velocity = 0.0f;
  for (float t = 0.0f; t < seconds; t += dt)
  {
    StepFogRangeSmoother(state, to, dt, params);
    trace.max_speed = std::max(trace.max_speed, std::fabs(state.Velocity));
    trace.max_accel = std::max(
        trace.max_accel, std::fabs(state.Velocity - previous_velocity) / dt);
    previous_velocity = state.Velocity;
    if ((from < to && state.Value > to + 1e-3f) ||
        (from > to && state.Value < to - 1e-3f))
    {
      trace.overshoot = true;
    }
    if (trace.time_to_target < 0.0f && std::fabs(state.Value - to) < 0.5f)
    {
      trace.time_to_target = t;
    }
  }
  trace.final_value = state.Value;
  return trace;
}

int main()
{
  const FogRangeSmootherParams params{};

  // Uninitialised state adopts the target without motion.
  {
    FogRangeSmootherState state{};
    Expect(StepFogRangeSmoother(state, 48.0f, 0.016f, params) == 48.0f,
           "first step adopts target");
  }

  const Trace contract = Run(64.0f, 36.0f, 20.0f);
  const Trace expand = Run(36.0f, 64.0f, 30.0f);

  Expect(!contract.overshoot && !expand.overshoot, "no overshoot");
  Expect(std::fabs(contract.final_value - 36.0f) < 0.6f, "contract settles");
  Expect(std::fabs(expand.final_value - 64.0f) < 0.6f, "expand settles");

  // Contracts faster than it expands.
  Expect(contract.time_to_target > 0.0f && expand.time_to_target > 0.0f,
         "both reach target");
  Expect(contract.time_to_target < expand.time_to_target,
         "contraction is faster than expansion");
  // Neither direction is instantaneous: a 28-block change takes real time.
  Expect(contract.time_to_target > 1.2f, "contraction is not a snap");
  Expect(expand.time_to_target > 3.0f, "expansion is slow");

  // Speed and acceleration stay within the configured caps.
  Expect(contract.max_speed <= params.ContractMaxSpeed + 0.1f,
         "contract speed cap");
  Expect(expand.max_speed <= params.ExpandMaxSpeed + 0.1f, "expand speed cap");
  Expect(contract.max_accel <= std::max(params.ContractAccel,
                                        params.ExpandAccel) + 0.5f,
         "contract accel cap");
  Expect(expand.max_accel <= std::max(params.ContractAccel,
                                      params.ExpandAccel) + 0.5f,
         "expand accel cap");

  // Eases in: speed after the first frame is tiny (no step onset).
  {
    FogRangeSmootherState state{64.0f, 0.0f};
    StepFogRangeSmoother(state, 20.0f, 1.0f / 60.0f, params);
    Expect(std::fabs(state.Velocity) < 1.0f, "ease-in: no full-speed onset");
  }

  // Reversal mid-motion is smooth (velocity changes continuously).
  {
    FogRangeSmootherState state{64.0f, 0.0f};
    float previous_velocity = 0.0f;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i)
    {
      StepFogRangeSmoother(state, 30.0f, dt, params);
      previous_velocity = state.Velocity;
    }
    for (int i = 0; i < 240; ++i)
    {
      StepFogRangeSmoother(state, 64.0f, dt, params);
      Expect(std::fabs(state.Velocity - previous_velocity) / dt <=
                 std::max(params.ContractAccel, params.ExpandAccel) + 0.5f,
             "reversal keeps acceleration bounded");
      previous_velocity = state.Velocity;
    }
  }

  std::cout << "fog_range_smoother_test: OK" << std::endl;
  return 0;
}
