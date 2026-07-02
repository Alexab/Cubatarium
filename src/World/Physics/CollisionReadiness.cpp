#include "World/Physics/CollisionReadiness.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

bool IsCollisionRingReady(const UBlockWorld &world, glm::ivec3 feet_block_pos,
                          int radius_chunks)
{
  const glm::ivec3 feet_chunk = UChunkManager::WorldToChunk(feet_block_pos);
  for (int dx = -radius_chunks; dx <= radius_chunks; ++dx)
  {
    for (int dz = -radius_chunks; dz <= radius_chunks; ++dz)
    {
      const glm::ivec3 coord(feet_chunk.x + dx, 0, feet_chunk.z + dz);
      if (!world.GetChunkManager().HasChunk(coord))
      {
        return false;
      }
    }
  }
  return true;
}

} // namespace cutum
