#ifndef FOGRANGESMOOTHER_H
#define FOGRANGESMOOTHER_H

#include <algorithm>
#include <cmath>

namespace cutum
{

/// Asymmetric second-order smoothing for a fog distance (blocks).
///
/// A linear rate cap (the previous scheme) starts and stops at full speed, so
/// every change of the target reads as a visible "snap" of the fog wall. This
/// filter chases the target with a time constant (exponential approach) and
/// additionally limits the *acceleration*, so the wall eases in and eases out
/// (the same idea as a critically damped SmoothDamp, but with separate
/// attack/release behaviour like an audio envelope follower):
///   - contracting (hide unfinished geometry): moderately quick, but never
///     instantaneous;
///   - expanding (reveal): deliberately slow, so a boundary that just became
///     ready is not followed by a pumping wall if readiness flickers.
struct FogRangeSmootherParams
{
  /// Time constants (seconds) of the exponential approach per direction.
  float ContractTauSec{1.4f};
  float ExpandTauSec{4.5f};
  /// Speed caps (blocks/s).
  float ContractMaxSpeed{26.0f};
  float ExpandMaxSpeed{9.0f};
  /// Acceleration caps (blocks/s^2) when speeding up in that direction. The
  /// larger of the two is used to brake / reverse so the wall never overshoots.
  float ContractAccel{32.0f};
  float ExpandAccel{7.0f};
};

struct FogRangeSmootherState
{
  float Value{-1.0f}; // negative: uninitialised
  float Velocity{0.0f};
};

inline float StepFogRangeSmoother(FogRangeSmootherState &state, float target,
                                  float dt_sec,
                                  const FogRangeSmootherParams &params)
{
  if (state.Value < 0.0f)
  {
    state.Value = target;
    state.Velocity = 0.0f;
    return state.Value;
  }
  dt_sec = std::clamp(dt_sec, 0.0f, 0.1f);
  if (dt_sec <= 0.0f)
  {
    return state.Value;
  }
  const float delta = target - state.Value;
  const bool contracting = delta < 0.0f;
  const float tau = std::max(
      0.05f, contracting ? params.ContractTauSec : params.ExpandTauSec);
  const float max_speed =
      contracting ? params.ContractMaxSpeed : params.ExpandMaxSpeed;
  const float desired_velocity =
      std::clamp(delta / tau, -params.ContractMaxSpeed, params.ExpandMaxSpeed);
  const float limited_desired =
      contracting ? std::max(desired_velocity, -max_speed)
                  : std::min(desired_velocity, max_speed);
  const float dv = limited_desired - state.Velocity;
  const bool speeding_up =
      (contracting && dv < 0.0f) || (!contracting && dv > 0.0f);
  const float accel =
      speeding_up ? (contracting ? params.ContractAccel : params.ExpandAccel)
                  : std::max(params.ContractAccel, params.ExpandAccel);
  const float max_dv = accel * dt_sec;
  state.Velocity += std::clamp(dv, -max_dv, max_dv);
  const float previous = state.Value;
  state.Value += state.Velocity * dt_sec;
  // Never overshoot the target: landing on it also ends the motion.
  if ((previous - target) * (state.Value - target) < 0.0f)
  {
    state.Value = target;
    state.Velocity = 0.0f;
  }
  return state.Value;
}

} // namespace cutum

#endif
