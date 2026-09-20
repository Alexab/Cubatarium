#pragma once
// BUDGET_MS: 0.0
// Fog pull-in hole_debt latch predicates (ring mask / unfinished).

namespace cutum
{

/// Near-miss latch: FocusMissingMesh. SoT 115048: rim miss_horiz=4..5 left
/// only unfinished/stall latching → hole_debt decayed → fog_rd 3↔4 thrash
/// (margin 28↔132). Any focus miss must keep fog pulled.
inline bool ShouldLatchFogHoleDebtNearMiss(int focus_missing_mesh,
                                           int /*miss_horiz*/)
{
  return focus_missing_mesh > 0;
}

/// W1 unfinished/stalled latch (enter). VB census is NOT a heal latch.
inline bool ShouldLatchFogHoleDebtUnfinishedOrVb(int unfinished_visual,
                                                 int visible_black_fully_dark_stalled)
{
  return unfinished_visual > 0 || visible_black_fully_dark_stalled > 0;
}

/// Sysreset: remove VB≥15 as fog heal. Hysteresis uses unfinished/miss only.
inline bool ShouldLatchFogHoleDebtVbFocus(int /*visible_black_focus_n*/,
                                          int /*vb_thresh*/ = 15)
{
  return false;
}

inline bool ShouldLatchFogHoleDebtNow(int focus_missing_mesh, int miss_horiz,
                                      int unfinished_visual,
                                      int visible_black_fully_dark_stalled,
                                      int /*visible_black_focus_n*/ = 0)
{
  return ShouldLatchFogHoleDebtNearMiss(focus_missing_mesh, miss_horiz) ||
         ShouldLatchFogHoleDebtUnfinishedOrVb(unfinished_visual,
                                              visible_black_fully_dark_stalled);
}

/// Clear latch only when miss=0, holes=0, unfinished/VB stalled gone.
/// Exit thresh documented: unfinished=0 and stalled=0 (enter was >0).
inline bool ShouldClearFogHoleDebtLatch(int focus_missing_mesh, int visual_holes,
                                        int unfinished_visual,
                                        int visible_black_fully_dark_stalled,
                                        int /*visible_black_focus_n*/ = 0,
                                        int /*vb_clear_thresh*/ = 8)
{
  return focus_missing_mesh == 0 && visual_holes == 0 &&
         unfinished_visual == 0 && visible_black_fully_dark_stalled == 0;
}

/// SoT 115048: while FocusMissingMesh persists, do not decay hole hold —
/// decay+passthrough snap caused fog margin thrash with rim miss.
inline bool ShouldDecayFogHoleDebtHold(int focus_missing_mesh,
                                       int hole_hold_frames)
{
  return hole_hold_frames > 0 && focus_missing_mesh <= 0;
}

} // namespace cutum
