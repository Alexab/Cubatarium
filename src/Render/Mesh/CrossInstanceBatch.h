#ifndef CROSSINSTANCEBATCH_H
#define CROSSINSTANCEBATCH_H

#include "Blocks/BlockDefinition.h"
#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

struct CrossInstanceGpu
{
  glm::vec3 center{0.0f};
  float skyLight{0.0f};
  float blockLight{0.0f};
};

static_assert(sizeof(CrossInstanceGpu) == 20, "CrossInstanceGpu must match GPU layout");

struct CrossInstanceBatch
{
  BlockId blockId{BLOCK_AIR};
  BlockDecorShape shape{BlockDecorShape::Cross};
  std::vector<CrossInstanceGpu> instances;
};

} // namespace cutum

#endif
