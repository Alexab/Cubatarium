#pragma once

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

class IBlockWriter
{
public:
  virtual ~IBlockWriter() = default;
  virtual void SetBlock(glm::ivec3 worldPos, BlockId id) = 0;
  virtual BlockId GetBlock(glm::ivec3 worldPos) const = 0;
  virtual bool IsAir(glm::ivec3 worldPos) const;
};

} // namespace cutum
