#pragma once

#include "World/Streaming/EnterVisualWarmupPolicy.h"
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

/// Era30 I-O4 / ColdWall S4: throttle fluid_map rebuild during moving cruise
/// heal pressure, or on hot wall (≥50ms) without requiring VB heal.
inline bool FluidMapShouldThrottleCruise(int pending, double wall_ms,
                                           bool moving, int void_n, int vb_n,
                                           int void_T = 200)
{
  if (!moving)
  {
    return false;
  }
  if (wall_ms > 50.0)
  {
    return true;
  }
  if (!IsOceanHealPressure(false, void_n, vb_n, void_T))
  {
    return false;
  }
  return pending > 24 || wall_ms > 30.0;
}

/// FlickerZero T1: throttle fluid_map full rebuild during enter / post-load heal.
inline bool FluidMapShouldThrottleEnter(bool enter_fov_lit, bool enter_lit_gate,
                                        bool post_load_ring_not_ready,
                                        int visible_black_n, int fluid_pending)
{
  if (enter_fov_lit || enter_lit_gate || post_load_ring_not_ready)
  {
    return true;
  }
  if (visible_black_n > 40)
  {
    return true;
  }
  return visible_black_n > 40 && fluid_pending > 32;
}

/// FlickerZero V1: scale VB no_ticket repair collect cap (was hard min(2)).
inline int VisibleBlackNoTicketRepairCap(int no_ticket_n, int repair_cap,
                                          bool moving, bool enter_fov_lit = false)
{
  const int base = std::max(1, repair_cap);
  // FZ2.7-F: enter seed — do not clamp to 4 (manual 141417 peak 108).
  if (enter_fov_lit)
  {
    return std::min(12, std::max(base, std::max(6, no_ticket_n / 8)));
  }
  if (no_ticket_n <= 0)
  {
    return base;
  }
  if (no_ticket_n <= 8)
  {
    if (!moving)
    {
      return base;
    }
    return std::min(base, std::max(2, no_ticket_n / 2));
  }
  if (!moving)
  {
    return std::min(base, std::max(6, no_ticket_n / 4));
  }
  // moving + no_ticket>8: P14 floor 6 (was 4); no_ticket>24 → floor 8.
  return std::min(base, std::max(no_ticket_n > 24 ? 8 : 6, no_ticket_n / 8));
}

/// FlickerZero V1: void Relight collect cap under no_ticket pressure.
inline int VisibleBlackNoTicketVoidCap(int no_ticket_n, int void_cap, bool moving)
{
  if (no_ticket_n <= 8)
  {
    return void_cap;
  }
  const int boosted =
      VisibleBlackNoTicketRepairCap(no_ticket_n, void_cap, moving);
  return std::max(void_cap, std::min(boosted, 8));
}

/// Era30 I-O6 / Era41/Era42: legacy soft enter_app name kept; lit warn wall
/// uses EnterFovLitHardWallMs. Do not abort load at 200ms.
inline int EnterVisualWarmupHardCapMs()
{
  return EnterFovLitHardWallMs();
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

/// Era31 I-T1: min void Relight Note enqueue per frame under ocean heal.
/// Era32 kept at 2 — NoteMin=4 flooded PendingLight and worsened void drain.
inline int OceanHealVoidRelightNoteMinPerFrame()
{
  return 2;
}

/// Era31 I-T1: moving Relight drain floor columns under ocean heal + void debt.
inline int OceanHealMovingRelightDrainFloor(bool ocean_heal, bool moving,
                                            int void_n, int void_T = 200)
{
  if (!ocean_heal || !moving)
  {
    return 0;
  }
  return void_n > void_T ? 2 : 1;
}

/// Era31 I-T2: cap MeshEmerge budget under ocean heal pressure (moving cruise).
inline double OceanHealMeshEmergeBudgetMs()
{
  return 14.0;
}

/// Era34 P2: under SoftDefer empty / holes while moving — prefer FirstMesh over
/// remesh/dirty flood (emerge budget stays ≤ OceanHealMeshEmergeBudgetMs).
inline bool ShouldBiasFirstMeshOverRemesh(int soft_defer_empty_n, bool holes,
                                          bool moving)
{
  if (!moving)
  {
    return false;
  }
  return soft_defer_empty_n > 0 || holes;
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

/// Era31 I-T4 / Era33 P0 / Era41 / Era42: close enter bar when soft-ready.
/// With require_zero (default), hard-wall never force-closes while lit debt
/// remains — elapsed is warn-only. Set require_zero=false for debug abort.
inline bool ShouldForceEnterVisualCap(double elapsed_ms, bool visual_soft_ready,
                                      bool /*cold_create*/ = false,
                                      int hard_wall_ms = -1,
                                      bool require_zero = true)
{
  if (visual_soft_ready)
  {
    return true;
  }
  if (require_zero)
  {
    return false;
  }
  const int wall =
      hard_wall_ms > 0 ? hard_wall_ms : EnterFovLitHardWallMs();
  return elapsed_ms >= static_cast<double>(wall);
}

/// Land cruise frontier: gen ahead of light while moving (void_near debt).
inline bool IsLandFrontierPressure(bool moving, int void_near_n, int void_T = 200)
{
  return moving && void_near_n > void_T;
}

/// Witness pin length under land frontier (longer than default 8).
inline int LandFrontierCaptureWitnessPinFrames(int void_near_n)
{
  if (void_near_n > 2000)
  {
    return 24;
  }
  if (void_near_n > 500)
  {
    return 18;
  }
  return 14;
}

/// Damp nh≥2 witness hops while land frontier heals (schedule thrash).
inline bool ShouldDampLandFrontierWitnessRetarget(bool land_frontier_pressure,
                                                  int horiz,
                                                  bool new_witness_better_horiz)
{
  if (!land_frontier_pressure || horiz < 2)
  {
    return new_witness_better_horiz;
  }
  return false;
}

} // namespace cutum
