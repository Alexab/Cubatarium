#pragma once

#include <algorithm>
#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

/// Per-frame SoftDefer inputs updated cheaply; callbacks installed once read this.
struct SoftDeferFramePolicy
{
  UWorld *world{nullptr};
  glm::ivec3 focus_ground{0};
  int focus_radius{0};
  bool have_nearest_missing{false};
  glm::ivec3 nearest_missing_hole{0};
  bool missing_visible_mesh{false};
  int pending_focus_count{0};
  int unlit_near_count{0};
};

/// SoftDefer empty rim scan budget: near ring is full; rim is a rotating slice.
inline int SoftDeferEmptyRimCellsPerFrame(int heal_r, int diam)
{
  (void)heal_r;
  // ~two diameter rows per frame — full RD disk rotates across frames.
  return std::max(48, diam * 2);
}

/// True when this horizontal cell should be probed this frame.
inline bool SoftDeferEmptyShouldProbeCell(int horiz, int near_r, int idx,
                                          int cells, int scan_offset,
                                          int rim_budget)
{
  if (horiz <= near_r)
  {
    return true;
  }
  if (cells <= 0 || rim_budget <= 0)
  {
    return false;
  }
  const int rel = (idx - scan_offset) % cells;
  const int norm = rel < 0 ? rel + cells : rel;
  return norm < rim_budget;
}

} // namespace cutum
