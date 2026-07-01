#ifndef CROSSINSTANCEBATCH_H
#define CROSSINSTANCEBATCH_H

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

struct CrossInstanceBatch
{
  BlockId blockId{BLOCK_AIR};
  std::vector<glm::vec3> centers;
};

} // namespace cutum

#endif
