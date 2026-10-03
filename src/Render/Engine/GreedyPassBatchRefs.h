#pragma once

#include "Render/Mesh/GreedyMeshBatch.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

/// Shared pass-material membership used by ChunkMeshCache::AppendGreedyPassBatchRefs
/// and publication_audit live-Append integration (no stub algorithm).
inline void AppendGreedyPassBatchRefsFromBatches(
    glm::ivec3 coord, bool transparent_pass,
    const std::vector<GreedyMeshBatch> &batches,
    std::vector<GreedyBatchRef> &out)
{
  for (size_t i = 0; i < batches.size(); ++i)
  {
    const GreedyMeshBatch &batch = batches[i];
    if (batch.vertices.empty() || batch.indices.empty())
      continue;
    if (batch.Transparent != transparent_pass)
      continue;
    out.push_back(
        GreedyBatchRef{coord, static_cast<uint16_t>(i), batch.blockId});
  }
}

} // namespace cutum
