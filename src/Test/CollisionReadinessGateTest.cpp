#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include "World/Physics/CollisionReadiness.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "collision_readiness_gate_test: " << message << std::endl;
    std::exit(1);
  }
}

static void EnsureGroundChunk(cutum::UBlockWorld &world, glm::ivec3 ground_coord)
{
  if (world.GetChunkManager().HasChunk(ground_coord))
  {
    return;
  }
  const glm::ivec3 block(ground_coord.x * cutum::CHUNK_SIZE, 0,
                         ground_coord.z * cutum::CHUNK_SIZE);
  world.SetBlock(block, cutum::BLOCK_AIR);
}

int main()
{
  cutum::UBlockWorld world;
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      EnsureGroundChunk(world, glm::ivec3(dx, 0, dz));
    }
  }

  Expect(cutum::IsCollisionRingReady(world, glm::ivec3(8, 4, 8), 0),
         "radius 0 should be ready when feet chunk exists");
  Expect(cutum::IsCollisionRingReady(world, glm::ivec3(8, 4, 8), 1),
         "radius 1 should be ready when neighbor columns exist");
  Expect(!cutum::IsCollisionRingReady(world, glm::ivec3(8, 4, 8), 2),
         "radius 2 should fail when distant columns are missing");

  const glm::ivec3 negative_block(-1, 0, -1);
  const glm::ivec3 negative_chunk = cutum::UChunkManager::WorldToChunk(negative_block);
  Expect(negative_chunk.x <= -1 && negative_chunk.z <= -1,
         "negative world coords must map to negative chunk indices");

  EnsureGroundChunk(world, glm::ivec3(negative_chunk.x, 0, negative_chunk.z));
  Expect(cutum::IsCollisionRingReady(world, negative_block, 0),
         "negative chunk boundary should be ready after load");

  for (int dx = -2; dx <= 2; ++dx)
  {
    for (int dz = -2; dz <= 2; ++dz)
    {
      EnsureGroundChunk(world, glm::ivec3(dx, 0, dz));
    }
  }
  Expect(cutum::IsCollisionRingReady(world, glm::ivec3(8, 4, 8), 2),
         "radius 2 should be ready when full ring exists");

  std::cout << "collision_readiness_gate_test: OK" << std::endl;
  return 0;
}
