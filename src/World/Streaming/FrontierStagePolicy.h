#pragma once
// BUDGET_MS: 0.0  // perf-root P4: measure via Tracy; kill-switch required for new heuristics

#include "World/Streaming/OceanCruisePolicy.h"

namespace cutum
{

/// Era25 I-F4 + Era30 I-O1 + sky-fix: frontier pressure — gen/async under miss
/// or void; ocean heal without backlog; moving absent columns (no mesh telem).
inline bool IsFrontierPressure(int gen_backlog, int async_queued, bool miss,
                               int void_n, int void_T = 200, int vb_n = 0,
                               int absent_columns = 0)
{
  const bool ocean_heal =
      IsOceanHealPressure(miss, void_n, vb_n, void_T);
  const bool absent_heal = absent_columns > 0;
  if (ShouldFrontierPressureDespiteEmptyGen(gen_backlog <= 0, async_queued <= 0,
                                            ocean_heal || absent_heal))
  {
    return true;
  }
  if (gen_backlog <= 0 && async_queued <= 0)
  {
    return false;
  }
  return miss || void_n > void_T || absent_heal;
}

/// Era25 I-F2: MC-style light ticket residency for near-focus frontier column.
inline bool FrontierColumnNeedsLightTicket(bool near_focus, bool pending_light,
                                           bool drawable, bool fully_dark)
{
  if (!near_focus || !pending_light)
  {
    return false;
  }
  return !drawable || fully_dark;
}

/// Era25 I-F3: after LitReady / lit, near-focus solid needs FirstMesh until
/// Drawable (post-gen ownership, not only SoftDefer empty).
inline bool FrontierColumnNeedsFirstMeshAfterLit(bool near_focus,
                                                 bool lit_ready_or_lit,
                                                 bool drawable, bool solid)
{
  if (!near_focus || !lit_ready_or_lit || !solid || drawable)
  {
    return false;
  }
  return true;
}

/// Era25 I-F5: under frontier pressure while moving, do not clamp NearLoad to
/// underfeet-only (UE load-ahead floor).
inline int FrontierNearLoadOpsFloor(bool frontier_pressure, bool moving,
                                    int base_ops, int floor_ops = 3)
{
  if (!frontier_pressure || !moving)
  {
    return base_ops;
  }
  return base_ops < floor_ops ? floor_ops : base_ops;
}

/// Era25 I-F5: NearLoad radius under frontier moving — at least focus band.
inline int FrontierNearLoadRadius(bool frontier_pressure, bool moving,
                                  int clamped_radius, int focus_radius)
{
  if (!frontier_pressure || !moving)
  {
    return clamped_radius;
  }
  return clamped_radius < focus_radius ? focus_radius : clamped_radius;
}

} // namespace cutum
