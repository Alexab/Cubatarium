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
  Expect(chunks.GetResidentChunkCount() == 0, "empty manager has zero residents");
  chunks.EnsureChunk(glm::ivec3(0, 0, -1));
  Expect(chunks.GetResidentChunkCount() == 1, "one EnsureChunk -> count 1");
  cutum::UChunk &slice0 = *chunks.GetChunk(glm::ivec3(0, 0, -1));
  slice0.SetBlockLocal(glm::ivec3(0, 0, 0), kStone);
  chunks.EnsureChunk(glm::ivec3(0, 1, -1));
  Expect(chunks.GetResidentChunkCount() == 2, "second EnsureChunk -> count 2");
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
  // Full-column complete requires every xz column seeded; only (0,-16) has stone.
  Expect(!cutum::IsTerrainChunkComplete(world, ground, kMaxWorldY, 3),
         "partial stone column should not mark whole chunk complete");

  cutum::MaterializeRequiredTerrainColumnSlices(world, ground, kMaxWorldY, 3);
  for (int cy = 0; cy <= 3; ++cy)
  {
    Expect(chunks.HasChunk(glm::ivec3(ground.x, cy, ground.z)),
           "materialize should create required slices through topCy");
  }
  Expect(!chunks.HasChunk(glm::ivec3(ground.x, 8, ground.z)),
         "materialize should not pad empty sky slices up to maxCy");

  cutum::UBlockWorld tall_world;
  constexpr int kTallMaxWorldY = 319;
  for (int cy = 0; cy <= kTallMaxWorldY / cutum::CHUNK_SIZE; ++cy)
  {
    tall_world.GetChunkManager().EnsureChunk(glm::ivec3(0, cy, 0));
  }
  tall_world.SetBlock(glm::ivec3(0, kTallMaxWorldY, 0), kStone);
  tall_world.GetChunkManager().EnsureChunk(glm::ivec3(0, 20, 0));
  tall_world.SetBlock(glm::ivec3(0, kTallMaxWorldY + 1, 0), kStone);
  Expect(cutum::GetHighestNonAirChunkSlice(tall_world, glm::ivec3(0, 0, 0),
                                           kTallMaxWorldY) == 19,
         "319 is the top valid block in chunk slice cy=19");
  Expect(cutum::AreTerrainColumnSlicesLoaded(tall_world, glm::ivec3(0, 0, 0),
                                             kTallMaxWorldY, 19),
         "tall terrain column readiness includes the top valid slice");
  cutum::ClearTerrainColumnChunks(tall_world, glm::ivec3(0, 0, 0),
                                  kTallMaxWorldY);
  Expect(!tall_world.GetChunkManager().HasChunk(glm::ivec3(0, 19, 0)) &&
             tall_world.GetChunkManager().HasChunk(glm::ivec3(0, 20, 0)),
         "terrain column bounds stop after the slice containing y=319");

  cutum::UBlockWorld full_height_world;
  constexpr int kMinWorldY = -64;
  constexpr int kFullHeightMaxY = 319;
  constexpr int kMinCy = -4;
  constexpr int kMaxCy = 19;
  for (int cy = kMinCy; cy <= kMaxCy; ++cy)
  {
    full_height_world.GetChunkManager().EnsureChunk(glm::ivec3(0, cy, 0));
  }
  full_height_world.SetBlock(glm::ivec3(0, kMinWorldY, 0), kStone);
  full_height_world.SetBlock(glm::ivec3(0, kFullHeightMaxY, 0), kStone);
  Expect(cutum::GetHighestNonAirChunkSlice(
             full_height_world, glm::ivec3(0, 0, 0), kFullHeightMaxY,
             kMinWorldY) == kMaxCy,
         "full-height column scans signed slices through the top bound");
  Expect(cutum::AreTerrainColumnSlicesLoaded(
             full_height_world, glm::ivec3(0, 0, 0), kFullHeightMaxY,
             kMaxCy, kMinWorldY),
         "full-height readiness requires a contiguous range from cy=-4 to 19");
  Expect(!cutum::TerrainColumnNeedsFill(full_height_world, 0, 0,
                                        kFullHeightMaxY, kMinWorldY),
         "a column containing terrain only at negative Y is not treated as empty");
  full_height_world.GetChunkManager().EnsureChunk(glm::ivec3(0, -5, 0));
  full_height_world.GetChunkManager().EnsureChunk(glm::ivec3(0, 20, 0));
  cutum::ClearTerrainColumnChunks(full_height_world, glm::ivec3(0, 0, 0),
                                  kFullHeightMaxY, kMinWorldY);
  Expect(!full_height_world.GetChunkManager().HasChunk(glm::ivec3(0, -4, 0)) &&
             !full_height_world.GetChunkManager().HasChunk(glm::ivec3(0, 19, 0)) &&
             full_height_world.GetChunkManager().HasChunk(glm::ivec3(0, -5, 0)) &&
             full_height_world.GetChunkManager().HasChunk(glm::ivec3(0, 20, 0)),
         "full-height clear respects the exact signed vertical bounds");

  const size_t before_remove = chunks.GetResidentChunkCount();
  chunks.RemoveChunk(glm::ivec3(0, 0, -1));
  Expect(chunks.GetResidentChunkCount() == before_remove - 1,
         "RemoveChunk decrements resident count");
  chunks.Clear();
  Expect(chunks.GetResidentChunkCount() == 0, "Clear zeros resident count");

  std::cout << "terrain_column_util_test: OK" << std::endl;
  return 0;
}
