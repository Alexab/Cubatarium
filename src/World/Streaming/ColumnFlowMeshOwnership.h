#pragma once

#include "World/Streaming/ColumnJobGraph.h"
#include <cstdint>

namespace cutum
{

/// M4: FirstMesh / RemeshSeam must route through ColumnFlowExecutor only.
inline bool ShouldRouteFirstMeshViaColumnFlow(ColumnJobStage stage)
{
  return stage == ColumnJobStage::Absent || stage == ColumnJobStage::PendingLight;
}

inline bool ShouldRouteRemeshSeamViaColumnFlow(ColumnJobStage stage)
{
  return stage == ColumnJobStage::LitReady || stage == ColumnJobStage::Meshing;
}

/// Returns true when parallel MarkDirty from emerge Admit* must be blocked.
inline bool BlockParallelMarkDirtyForColumnFlow(ColumnJobStage stage, bool is_first_mesh)
{
  if (is_first_mesh)
  {
    return ShouldRouteFirstMeshViaColumnFlow(stage);
  }
  return ShouldRouteRemeshSeamViaColumnFlow(stage);
}

} // namespace cutum
