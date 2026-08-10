#pragma once

#include <algorithm>

namespace cutum
{

/// Era30 I-O1: ocean heal pressure without gen/async backlog (void or VB debt).
inline bool IsOceanHealPressure(bool /*miss*/, int void_n, int vb_n,
                                 int void_T = 200)
{
  return void_n > void_T || vb_n > 0;
}

/// Era30 I-O1: frontier/ocean pressure when gen+async queues are empty.
inline bool ShouldFrontierPressureDespiteEmptyGen(bool gen_empty, bool async_empty,
                                                  bool ocean_heal_pressure)
{
  return gen_empty && async_empty && ocean_heal_pressure;
}

/// Era30 I-O2: moving void-pressure Relight drain cap floor.
inline int OceanVoidRelightDrainCapMoving(bool void_pressure, int base_cap)
{
  if (!void_pressure)
  {
    return base_cap;
  }
  return std::max(base_cap, 2);
}

/// Era30 I-O3: PendingLight must not skip stale Remesh for void columns.
inline bool ShouldSkipStaleRemeshForPendingVoid(bool pending_light,
                                                bool void_column)
{
  if (!pending_light)
  {
    return false;
  }
  return !void_column;
}

/// Era30 I-O4: throttle fluid_map rebuild during moving cruise heal pressure.
inline bool FluidMapShouldThrottleCruise(int pending, double wall_ms,
                                           bool moving, int void_n, int vb_n,
                                           int void_T = 200)
{
  if (!moving)
  {
    return false;
  }
  if (!IsOceanHealPressure(false, void_n, vb_n, void_T))
  {
    return false;
  }
  return pending > 32 || wall_ms > 30.0;
}

/// Era30 I-O6: hard enter_app visual gate cap (ms).
inline int EnterVisualWarmupHardCapMs()
{
  return 200;
}

/// Era30 I-O5: ocean cruise Capture witness pin (separate from enter T=16).
inline int OceanCaptureWitnessPinFrames()
{
  return 12;
}

/// Era30 I-O5: damp horiz≥2 retarget thrash on ocean rim while healing.
inline bool ShouldDampOceanCaptureRetarget(bool ocean_heal_pressure, int horiz,
                                         bool new_witness_better_horiz)
{
  if (!ocean_heal_pressure || horiz < 2)
  {
    return new_witness_better_horiz;
  }
  return false;
}

/// Era30 I-O3: drain PendingLight while moving under void/VB without miss.
inline bool ShouldDrainPendingLightUnderOceanVoid(bool moving, int void_n,
                                                  int vb_n, int void_T = 200)
{
  if (!moving)
  {
    return false;
  }
  return void_n > void_T || vb_n > 0;
}

} // namespace cutum
