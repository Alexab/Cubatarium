#include "World/Chunks/TerrainColumnUtil.h"
#include <algorithm>
#include "World/Core/BlockWorld.h"

namespace cutum
{

bool TerrainColumnNeedsFill(const UBlockWorld &world, int worldX, int worldZ,
                            int maxWorldY)
{
  if (world.GetBlock(glm::ivec3(worldX, 0, worldZ)) == BLOCK_AIR)
  {
    return true;
  }
  const int topY = std::max(1, maxWorldY);
  for (int y = 1; y <= topY; ++y)
  {
    if (world.GetBlock(glm::ivec3(worldX, y, worldZ)) != BLOCK_AIR)
    {
      return false;
    }
  }
  return true;
}

bool IsTerrainChunkComplete(const UBlockWorld &world, glm::ivec3 groundCoord,
                            int maxWorldY)
{
  if (groundCoord.y != 0)
  {
    return false;
  }
  for (int lx = 0; lx < CHUNK_SIZE; ++lx)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      const int worldX = groundCoord.x * CHUNK_SIZE + lx;
      const int worldZ = groundCoord.z * CHUNK_SIZE + lz;
      if (TerrainColumnNeedsFill(world, worldX, worldZ, maxWorldY))
      {
        return false;
      }
    }
  }
  return true;
}

} // namespace cutum
