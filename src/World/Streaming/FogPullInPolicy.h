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

/// W1 (170548): also latch while unfinished ring debt or focus FullyDark stalled
/// so fog masks brief rim blacks when near-miss is already 0.
inline bool ShouldLatchFogHoleDebtUnfinishedOrVb(int unfinished_visual,
                                                 int visible_black_fully_dark_stalled)
{
  return unfinished_visual > 0 || visible_black_fully_dark_stalled > 0;
}

/// SoT 115048: high VisibleBlackFocus keeps fog pulled (masks stuck FullyDark
/// plugs while remesh/relight drain; prevents debt thrash with rim miss).
inline bool ShouldLatchFogHoleDebtVbFocus(int visible_black_focus_n,
                                          int vb_thresh = 15)
{
  return visible_black_focus_n >= vb_thresh;
}

inline bool ShouldLatchFogHoleDebtNow(int focus_missing_mesh, int miss_horiz,
                                      int unfinished_visual,
                                      int visible_black_fully_dark_stalled,
                                      int visible_black_focus_n = 0)
{
  return ShouldLatchFogHoleDebtNearMiss(focus_missing_mesh, miss_horiz) ||
         ShouldLatchFogHoleDebtUnfinishedOrVb(unfinished_visual,
                                              visible_black_fully_dark_stalled) ||
         ShouldLatchFogHoleDebtVbFocus(visible_black_focus_n);
}

/// Clear latch only when miss=0, holes=0, unfinished/VB stalled gone, and
/// focus blacks drained below mask thresh.
inline bool ShouldClearFogHoleDebtLatch(int focus_missing_mesh, int visual_holes,
                                        int unfinished_visual,
                                        int visible_black_fully_dark_stalled,
                                        int visible_black_focus_n = 0,
                                        int vb_clear_thresh = 8)
{
  return focus_missing_mesh == 0 && visual_holes == 0 &&
         unfinished_visual == 0 && visible_black_fully_dark_stalled == 0 &&
         visible_black_focus_n < vb_clear_thresh;
}

/// SoT 115048: while FocusMissingMesh persists, do not decay hole hold —
/// decay+passthrough snap caused fog margin thrash with rim miss.
inline bool ShouldDecayFogHoleDebtHold(int focus_missing_mesh,
                                       int hole_hold_frames)
{
  return hole_hold_frames > 0 && focus_missing_mesh <= 0;
}

} // namespace cutum
