#pragma once
// BUDGET_MS: 0.0
// Fog pull-in hole_debt latch predicates (ring mask / unfinished).

namespace cutum
{

/// Near-miss latch (existing): FocusMissingMesh with MissHoriz<=2.
inline bool ShouldLatchFogHoleDebtNearMiss(int focus_missing_mesh, int miss_horiz)
{
  return focus_missing_mesh > 0 && miss_horiz <= 2;
}

/// W1 (170548): also latch while unfinished ring debt or focus FullyDark stalled
/// so fog masks brief rim blacks when near-miss is already 0.
inline bool ShouldLatchFogHoleDebtUnfinishedOrVb(int unfinished_visual,
                                                 int visible_black_fully_dark_stalled)
{
  return unfinished_visual > 0 || visible_black_fully_dark_stalled > 0;
}

inline bool ShouldLatchFogHoleDebtNow(int focus_missing_mesh, int miss_horiz,
                                      int unfinished_visual,
                                      int visible_black_fully_dark_stalled)
{
  return ShouldLatchFogHoleDebtNearMiss(focus_missing_mesh, miss_horiz) ||
         ShouldLatchFogHoleDebtUnfinishedOrVb(unfinished_visual,
                                              visible_black_fully_dark_stalled);
}

/// Clear latch only when miss=0, holes=0, AND no unfinished/VB stalled debt.
inline bool ShouldClearFogHoleDebtLatch(int focus_missing_mesh, int visual_holes,
                                        int unfinished_visual,
                                        int visible_black_fully_dark_stalled)
{
  return focus_missing_mesh == 0 && visual_holes == 0 &&
         unfinished_visual == 0 && visible_black_fully_dark_stalled == 0;
}

} // namespace cutum
