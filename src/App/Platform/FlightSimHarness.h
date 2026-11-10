#pragma once

#include <cstdlib>

namespace cutum
{

/// A37 H0: flight-sim-only move speed scale (far distance stress). Default 1.
/// Set via FlightSimOptions / CUBA_FLIGHT_MOVE_SPEED_SCALE before locomotion.
inline float &FlightSimMoveSpeedScale()
{
  static float scale = []() {
    if (const char *env = std::getenv("CUBA_FLIGHT_MOVE_SPEED_SCALE"))
    {
      const float v = static_cast<float>(std::atof(env));
      if (v > 0.05f && v <= 64.0f)
      {
        return v;
      }
    }
    return 1.0f;
  }();
  return scale;
}

/// A37 H0: stamp period rows with warm=1 when set (honest warm protocol).
inline bool FlightSimWarmStampEnabled()
{
  if (const char *env = std::getenv("CUBA_FLIGHT_WARM"))
  {
    return env[0] == '1' || env[0] == 't' || env[0] == 'T';
  }
  return false;
}

/// A37 H6: KickUnfinished heuristic off by default (symptom path).
inline bool KickUnfinishedHeuristicEnabled()
{
  if (const char *env = std::getenv("CUBA_KICK_UNFINISHED"))
  {
    return env[0] == '1' || env[0] == 't' || env[0] == 'T';
  }
  return false;
}

} // namespace cutum
