#pragma once

#include <algorithm>
#include <cmath>

namespace cutum
{

/// Era38 A0: lower score = sooner (mirrors AdmitFocusVisibleMissing sort key).
inline float NearFovWorkScore(int horiz, float view_dot)
{
  return static_cast<float>(horiz) - 1.5f * std::max(0.0f, view_dot);
}

/// Era38 A1: reserve ownership slots for near ring (horiz<=2) before rim.
inline int SoftDeferEmptyNearReserveSlots(int cap)
{
  return std::min(6, std::max(1, cap / 2));
}

/// Era38 A1/A3: FirstMesh ColumnFlow priority rises as horiz falls.
inline int ColumnFlowFirstMeshPriority(int base, int horiz, int focus_r)
{
  return base + std::max(0, focus_r - horiz);
}

/// Era38 A2: starve hinterland Unlit / rear slots while near SoftDefer empty
/// or pending light debt.
inline bool StarveHinterlandUnlit(int softdefer_empty_near, int pending_focus)
{
  return softdefer_empty_near > 0 || pending_focus > 15;
}

/// Era38 A3: RelightThenMesh must stay ≤99 under miss so FirstMesh wins.
inline int ColumnFlowRelightPriorityUnderMiss(int base_prio, int horiz,
                                              int focus_r, bool miss)
{
  const int boosted =
      base_prio + std::max(0, focus_r - horiz);
  if (miss)
  {
    return std::min(boosted, 99);
  }
  return boosted;
}

} // namespace cutum
