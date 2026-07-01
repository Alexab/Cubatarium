#include "World/Core/IUBlockWriter.h"

namespace cutum
{

bool IUBlockWriter::IsAir(glm::ivec3 worldPos) const
{
  return GetBlock(worldPos) == BLOCK_AIR;
}

} // namespace cutum
