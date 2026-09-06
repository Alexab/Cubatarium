#ifndef EMPTYBACKLOGPOLICY_H
#define EMPTYBACKLOGPOLICY_H

#include "World/Physics/PhysicsTelemetry.h"
#include <algorithm>
#include <climits>
#include <cstdint>

namespace cutum
{

/// Phase 5.3: shared empty-column backlog predicates (SoT for drip / skip / telem).
inline int EmptyBacklogN(const PhysicsTelemetry &pt)
{
  const int unf = std::max(0, pt.UnfinishedVisual);
  const int colnm = std::max(0, pt.ColumnLoadedNoMeshN);
  const int cnr = static_cast<int>(
      std::min<uint64_t>(pt.ChunkNotReady, static_cast<uint64_t>(INT_MAX)));
  return std::max(unf, std::max(colnm, cnr));
}

inline bool HasEmptyBacklog(const PhysicsTelemetry &pt)
{
  return EmptyBacklogN(pt) > 0;
}

inline bool HasVisualHolePressure(const PhysicsTelemetry &pt, bool visual_holes,
                                  int focus_missing)
{
  return visual_holes || focus_missing != 0 || HasEmptyBacklog(pt) ||
         pt.SoftDeferEmptyStuckN > 0 || pt.SoftDeferEmptyStuckHoriz > 0;
}

inline bool AbortNeedsDrip(const PhysicsTelemetry &pt, bool missing_underfeet,
                           int nearest_miss_h, bool visual_holes)
{
  return missing_underfeet || nearest_miss_h <= 2 ||
         HasVisualHolePressure(pt, visual_holes, pt.FocusMissingMesh);
}

/// Phase 5.4.2: escalate AbortDripCap 2→3 on sticky near rim / underfeet.
inline int AbortDripN(const PhysicsTelemetry &pt, int nearest_miss_h,
                      int miss_witness_age, bool missing_underfeet)
{
  const bool sticky_rim =
      pt.FocusMissingMesh > 0 && nearest_miss_h <= 4 && miss_witness_age >= 8;
  const bool near_urgent = nearest_miss_h <= 2 || missing_underfeet;
  if (sticky_rim || near_urgent)
  {
    return 3;
  }
  return 2;
}

} // namespace cutum

#endif
