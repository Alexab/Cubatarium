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

} // namespace cutum

#endif
