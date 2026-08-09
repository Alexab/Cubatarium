#pragma once

#include <algorithm>

namespace cutum
{

/// Era20: SoftDefer empty placeholder → FirstMesh ticket (pure policy).
/// HasGreedy && !Drawable && solid && !in-flight work.
inline bool IsSoftDeferEmptyPlaceholder(bool has_greedy, bool has_drawable,
                                        bool is_dirty, bool pending_gpu,
                                        bool inflight, bool any_solid)
{
  if (has_drawable || pending_gpu || inflight || is_dirty)
  {
    return false;
  }
  if (!has_greedy || !any_solid)
  {
    return false;
  }
  return true;
}

/// Stuck SoftDefer empty → ColumnFlow FirstMesh only while FOV miss
/// (idle PreferKick without miss caused remesh churn; MarkDirty still heals).
inline bool ShouldEnqueueSoftDeferEmptyFirstMesh(bool empty_placeholder,
                                                 int horiz,
                                                 bool missing_visible_mesh)
{
  (void)horiz;
  if (!empty_placeholder || !missing_visible_mesh)
  {
    return false;
  }
  return true;
}

/// Era20 I-M2: Imm/Force Dirty escape when miss and async dead (ignore wall).
inline bool ShouldColdAsyncImmEscape(bool missing_visible_mesh, int mesh_async)
{
  return missing_visible_mesh && mesh_async < 2;
}

/// Era22 I-S1: SoftDefer must not drop/park !Drawable FirstMesh without a
/// schedule under miss-class or in-focus (place-to-reveal / SoftDeferHeld SLA).
inline bool ShouldScheduleFirstMeshUnderSoftDefer(bool has_drawable,
                                                  bool miss_or_in_focus)
{
  return !has_drawable && miss_or_in_focus;
}

/// Era22 I-S2: SoftDeferHeld side-set counts as repair progress for no_ticket
/// honesty (Hide⇒Ticket).
inline bool SoftDeferHeldCountsAsRepairProgress(bool soft_defer_held)
{
  return soft_defer_held;
}

/// Era22 I-V3: VB ticket collect radius. Count uses full focus; under miss
/// collect must not clamp to r≤2 while no_ticket orphans exist.
inline int VisibleBlackTicketCollectRadius(int focus_radius,
                                           bool missing_visible_mesh,
                                           bool visible_black_no_ticket)
{
  const int r = std::max(0, focus_radius);
  if (visible_black_no_ticket || !missing_visible_mesh)
  {
    return r;
  }
  return std::min(2, r);
}

/// Era22 I-V3: enqueue nearest no_ticket VB repair while async has headroom.
inline bool ShouldEnqueueNearestVbNoTicket(bool visible_black_no_ticket,
                                           bool async_ok)
{
  return async_ok && visible_black_no_ticket;
}

/// Era22 I-M8: miss witness age (periods ≈2s) ⇒ PreferKick + admit bump.
inline bool ShouldMissTimeSlaKick(bool missing_visible_mesh,
                                  int miss_age_periods,
                                  int sla_periods = 2)
{
  return missing_visible_mesh && miss_age_periods >= sla_periods;
}

/// Era22 I-A1: post-Finalize async schedule floor under miss|UV (TD-027).
inline int AsyncScheduleFloorUnderMiss(bool miss_or_unfinished_visual)
{
  return miss_or_unfinished_visual ? 12 : 0;
}

} // namespace cutum
