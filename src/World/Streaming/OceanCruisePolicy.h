#pragma once

#include "World/Streaming/VisualStagePolicy.h"

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

/// Era31/32: min void Relight Note enqueue per frame under ocean heal.
inline int OceanHealVoidRelightNoteMinPerFrame()
{
  return 4;
}

/// Era31/32: moving Relight drain floor columns under ocean heal + void debt.
inline int OceanHealMovingRelightDrainFloor(bool ocean_heal, bool moving,
                                            int void_n, int void_T = 200)
{
  if (!ocean_heal || !moving)
  {
    return 0;
  }
  return void_n > void_T ? 3 : 2;
}

/// Era31 I-T2: cap MeshEmerge budget under ocean heal pressure (moving cruise).
inline double OceanHealMeshEmergeBudgetMs()
{
  return 14.0;
}

/// Era31 I-T2: protected Relight carve-out ms (not zero-sum stolen from emerge).
inline double OceanHealRelightCarveOutMs()
{
  return 6.0;
}

/// Era31 I-T3: VB progress only when dark debt clearing or lit slot pending.
inline bool ShouldCountVisibleBlackProgress(bool has_ticket_or_progress,
                                            bool fully_dark,
                                            bool pending_replace_lit)
{
  if (!has_ticket_or_progress)
  {
    return false;
  }
  if (pending_replace_lit)
  {
    return true;
  }
  return !fully_dark;
}

/// Era31 I-T3 (legacy): near rim hide under ocean heal.
/// Era32: prod uses ShouldHideFullyDarkUntilLitInRing (VisualStage, no ocean_heal).
/// Kept for unit compatibility; ocean_heal ignored when ring predicate applies.
inline bool ShouldHideDrawableUntilLitNearRim(bool ocean_heal_pressure, int horiz,
                                            bool fully_dark,
                                            bool pending_replace_lit,
                                            int near_r = 2)
{
  (void)ocean_heal_pressure;
  (void)near_r;
  return ShouldHideFullyDarkUntilLitInRing(horiz, fully_dark, pending_replace_lit);
}

/// Era31 I-T5: moving cruise + heal pressure → RemeshAfterApply-only.
inline bool ShouldRemeshAfterApplyOnlyOnMovingCruiseHeal(bool moving,
                                                         bool ocean_heal_pressure,
                                                         bool has_drawable)
{
  return moving && ocean_heal_pressure && has_drawable;
}

/// Era31 I-T4: force close enter bar when soft budget or underfeet visual ready.
inline bool ShouldForceEnterVisualCap(double elapsed_ms, bool visual_soft_ready)
{
  return elapsed_ms >= static_cast<double>(EnterVisualWarmupHardCapMs()) ||
         visual_soft_ready;
}

} // namespace cutum
