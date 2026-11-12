#pragma once
#include <algorithm>

namespace cutum
{
// Evidence for a missing slice. A hole inside the persisted/resident terrain
// range is unknown, even when the horizontal column is otherwise complete.
struct KnownAirTerrainSliceEvidence
{
  int sliceY{0}, minSliceY{0}, maxSliceY{0};
  bool columnComplete{false}, hasWorldFolder{false}, savePending{false};
  int highestResidentY{0}, highestOnDiskY{0};
};
inline bool IsKnownAirTerrainSlice(const KnownAirTerrainSliceEvidence &e)
{
  if (e.sliceY < e.minSliceY || e.sliceY > e.maxSliceY) return true;
  return e.columnComplete && e.hasWorldFolder && !e.savePending &&
    e.sliceY > std::max(e.highestResidentY, e.highestOnDiskY);
}
}
