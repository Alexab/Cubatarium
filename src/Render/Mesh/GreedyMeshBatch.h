#ifndef GREEDYMESHBATCH_H
#define GREEDYMESHBATCH_H

#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Math/BlockTypes.h"
#include <cstdint>
#include <vector>

namespace cutum
{

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
