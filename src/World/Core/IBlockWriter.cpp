#include "World/Core/IBlockWriter.h"

namespace cutum
{

bool IBlockWriter::IsAir(glm::ivec3 worldPos) const
{
  return GetBlock(worldPos) == BLOCK_AIR;
}

} // namespace cutum
