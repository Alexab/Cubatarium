#pragma once
#include <cmath>

namespace cutum
{
// Distance, not elapsed time, drives a grounded support gait. The authored
// legacy speed multiplier is already embodied in the baked cycle distance.
inline float GltfDistancePlayback(float horizontalSpeed,float duration,float worldDistance)
{
  if(!std::isfinite(horizontalSpeed) || !std::isfinite(duration) ||
     !std::isfinite(worldDistance) || horizontalSpeed<=0.f || duration<=0.f || worldDistance<=0.f)
    return 0.f;
  const float rate=static_cast<float>(static_cast<double>(horizontalSpeed)*duration/worldDistance);
  return std::isfinite(rate)?rate:0.f;
}
}
