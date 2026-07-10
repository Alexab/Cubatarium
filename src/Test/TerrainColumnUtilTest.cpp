#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Chunks/Chunk.h"
#include "World/Math/BlockTypes.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "terrain_column_util_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::UBlockWorld world;
  constexpr int kMaxWorldY = 128;
  constexpr cutum::BlockId kStone = 8;
  const glm::ivec3 ground(0, 0, -1);

  cutum::UChunkManager &chunks = world.GetChunkManager();
  chunks.EnsureChunk(glm::ivec3(0, 0, -1));
  cutum::UChunk &slice0 = *chunks.GetChunk(glm::ivec3(0, 0, -1));
  slice0.SetBlockLocal(glm::ivec3(0, 0, 0), kStone);
  chunks.EnsureChunk(glm::ivec3(0, 1, -1));
  cutum::UChunk &slice1 = *chunks.GetChunk(glm::ivec3(0, 1, -1));
  slice1.SetBlockLocal(glm::ivec3(0, 0, 0), kStone);

  Expect(cutum::GetHighestNonAirChunkSlice(world, ground, kMaxWorldY) == 1,
         "highest non-air slice should be cy=1");
  Expect(!cutum::AreTerrainColumnSlicesLoaded(world, ground, kMaxWorldY, 3),
         "column missing required slice cy=3 should not be loaded");
  Expect(!cutum::IsTerrainChunkComplete(world, ground, kMaxWorldY, 3),
         "column missing required slice cy=3 should not be complete");

  chunks.EnsureChunk(glm::ivec3(0, 2, -1));
  chunks.EnsureChunk(glm::ivec3(0, 3, -1));
  Expect(cutum::AreTerrainColumnSlicesLoaded(world, ground, kMaxWorldY, 3),
         "contiguous slices through cy=3 should be loaded");
  Expect(cutum::IsTerrainChunkComplete(world, ground, kMaxWorldY, 3),
         "column with lower terrain and required slices should be complete");

  cutum::MaterializeTerrainColumnAirSlices(world, ground, kMaxWorldY);
  const int max_cy = (kMaxWorldY + cutum::CHUNK_SIZE - 1) / cutum::CHUNK_SIZE;
  for (int cy = 0; cy <= max_cy; ++cy)
  {
    Expect(chunks.HasChunk(glm::ivec3(ground.x, cy, ground.z)),
           "materialize should create every slice through maxCy");
  }

  std::cout << "terrain_column_util_test: OK" << std::endl;
  return 0;
}
