#pragma once

#include <algorithm>
#include <cstdint>

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

/// Stuck SoftDefer empty → ColumnFlow FirstMesh while FOV miss OR in-focus
/// SoftDefer-held undrawn (Era32 P3: black-as-hole after LitDrawable ring).
inline bool ShouldEnqueueSoftDeferEmptyFirstMesh(bool empty_placeholder,
                                                 int horiz,
                                                 bool missing_visible_mesh,
                                                 bool in_focus = false)
{
  (void)horiz;
  if (!empty_placeholder)
  {
    return false;
  }
  return missing_visible_mesh || in_focus;
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

/// Era23 I-V6: SoftDeferHeld must not count as void-heal progress while the
/// column still has fully-dark faces (Collect skip masked void on 172232).
inline bool SoftDeferHeldCountsAsVoidProgress(bool soft_defer_held,
                                              bool has_fully_dark_face)
{
  if (has_fully_dark_face)
  {
    return false;
  }
  return soft_defer_held;
}

/// Era23 I-V4 / Era31 I-T1: reserve Relight slots for void>T, miss+void, or VB.
inline bool ShouldReserveVoidRelightSlots(int dark_face_void_near_n,
                                          int visible_black_n,
                                          bool missing_visible_mesh,
                                          int void_threshold = 200)
{
  if (dark_face_void_near_n > void_threshold)
  {
    return true;
  }
  if (visible_black_n > 0)
  {
    return true;
  }
  return missing_visible_mesh && dark_face_void_near_n > 0;
}

/// Era23 I-V4: void collect cap independent of no_ticket nearest-N=1–2.
inline int VoidRelightCollectCap(int repair_cap, bool void_pressure)
{
  const int base = std::max(1, repair_cap);
  if (void_pressure)
  {
    return std::max(base, std::min(4, std::max(2, repair_cap)));
  }
  return base;
}

/// Era23 I-V5: NotePendingLight when enqueueing void Relight (not only Dispatch).
inline bool ShouldNotePendingLightOnVoidEnqueue(bool fully_dark_or_no_sky)
{
  return fully_dark_or_no_sky;
}

/// Era23 I-M9: PreferKick miss witness every miss-frame in FirstMesh class
/// (do not wait age≥2 periods).
inline bool ShouldPreferKickMissWitnessEarly(bool missing_visible_mesh,
                                             bool miss_first_mesh_class)
{
  return missing_visible_mesh && miss_first_mesh_class;
}

/// Era23 P2: SoftDefer empty PreferKick only when GPU queue is stuck on the
/// same coord (not empty placeholder apply storm).
inline bool ShouldPreferKickSoftDeferEmptyStuck(bool soft_defer_empty,
                                                bool missing_visible_mesh,
                                                bool queued_or_kicked_stuck)
{
  return soft_defer_empty && missing_visible_mesh && queued_or_kicked_stuck;
}

/// Era23 I-P1: SoftDefer empty / !Drawable place column needs FirstMesh SLA.
inline bool ShouldForceFirstMeshOnPlaceHole(bool soft_defer_empty_or_undrawn,
                                            bool near_or_underfeet)
{
  return soft_defer_empty_or_undrawn && near_or_underfeet;
}

/// Era24 I-E2: SoftDefer empty under FOV miss/focus needs FirstMesh ownership
/// until Drawable (Hide⇒Ticket).
inline bool SoftDeferEmptyNeedsFirstMeshOwnership(bool empty_placeholder,
                                                  bool miss_or_in_focus)
{
  return empty_placeholder && miss_or_in_focus;
}

/// Era24 I-E4: SoftDefer empty age (frames) ⇒ escalate PreferKick/Capture.
/// Era32: tighter SLA under FOV miss (miss_stuck 28–38s on hide4) — 30 frames.
inline bool ShouldEscalateSoftDeferEmptyAge(int age_frames,
                                            int sla_frames = 30)
{
  return age_frames >= sla_frames;
}

/// Era24 I-E3: SoftDefer empty heal is FirstMesh-class only (not Remesh/Relight).
enum class SoftDeferEmptyHealKind : uint8_t
{
  FirstMesh = 0
};

inline SoftDeferEmptyHealKind SoftDeferEmptyHealKindOf()
{
  return SoftDeferEmptyHealKind::FirstMesh;
}

} // namespace cutum
