#include "World/Physics/PhysicsChunkDistance.h"
#include "World/Chunks/ChunkManager.h"
#include <algorithm>

namespace cutum
{

int ChebyshevChunkDistance(glm::ivec3 a, glm::ivec3 b)
{
  return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

int ChebyshevBlockDistanceChunks(glm::ivec3 block_pos, glm::ivec3 focus_chunk)
{
  return ChebyshevChunkDistance(UChunkManager::WorldToChunk(block_pos), focus_chunk);
}

} // namespace cutum
