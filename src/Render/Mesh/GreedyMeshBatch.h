#ifndef GREEDYMESHBATCH_H
#define GREEDYMESHBATCH_H

#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Math/BlockTypes.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

struct GreedyBatchRef
{
  glm::ivec3 chunkCoord{0};
  uint16_t batchIndex{0};
  /// Cached at ref build time so opaque sort avoids GreedyCache.find per compare.
  BlockId blockId{BLOCK_AIR};
};

struct GreedyMeshBatch
{
  BlockId blockId{BLOCK_AIR};
  bool Transparent{false};
  bool AlphaCutout{false};
  std::vector<GreedyMeshVertex> vertices;
  std::vector<uint32_t> indices;
};

} // namespace cutum

#endif
