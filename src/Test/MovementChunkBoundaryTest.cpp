#include "World/Chunks/ChunkManager.h"
#include "World/Math/GridMath.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "movement_chunk_boundary_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  const glm::ivec3 block_at_boundary(-1, 0, 15);
  const glm::ivec3 chunk = cutum::UChunkManager::WorldToChunk(block_at_boundary);
  Expect(chunk.x == -1, "x=-1 must map to chunk -1");
  Expect(chunk.z == 0, "z=15 must map to chunk 0 for CHUNK_SIZE=16");

  const glm::ivec3 local = cutum::UChunkManager::WorldToLocal(block_at_boundary);
  Expect(local.x == 15, "local x for world -1 should be 15");
  Expect(local.z == 15, "local z for world 15 should be 15");

  glm::ivec3 neighbor_sum(0);
  for (const glm::ivec3 &offset : cutum::NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 neighbor = block_at_boundary + offset;
    const glm::ivec3 neighbor_chunk = cutum::UChunkManager::WorldToChunk(neighbor);
    neighbor_sum += neighbor_chunk;
  }
  Expect(neighbor_sum.x != chunk.x || neighbor_sum.z != chunk.z,
         "neighbor offsets must touch adjacent chunks at boundary");

  Expect(cutum::FloorDiv(-1, 16) == -1, "FloorDiv must handle negative coords");
  Expect(cutum::PositiveMod(-1, 16) == 15, "PositiveMod must handle negatives");

  std::cout << "movement_chunk_boundary_test: OK" << std::endl;
  return 0;
}
