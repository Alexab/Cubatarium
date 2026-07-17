#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Stages/MudflowErosion.h"

#include <algorithm>
#include <array>
#include <iostream>

namespace
{

constexpr const char *kTestName = "mudflow_erosion_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kDirt = 11;
constexpr int kChunkSize = 16;

static cutum::WorldGenContext MakeContext(cutum::UBlockWorld &world,
                                          cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings;
  settings.SeaLevel = 48;
  settings.MaxHeight = 128;
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.Blocks.Stone = kStone;
  ctx.Blocks.Dirt = kDirt;
  return ctx;
}

static void FillColumn(cutum::UBlockWorld &world, int x, int z, int surface_y,
                       cutum::BlockId surface_id)
{
  for (int y = 0; y < surface_y; ++y)
  {
    world.SetBlock(glm::ivec3(x, y, z), kStone);
  }
  world.SetBlock(glm::ivec3(x, surface_y, z), surface_id);
}

static int TopSolidY(const cutum::UBlockWorld &world, int x, int z, int max_y)
{
  for (int y = max_y; y >= 1; --y)
  {
    if (!world.IsAir(glm::ivec3(x, y, z)))
    {
      return y;
    }
  }
  return 1;
}

static void TestMudflowReducesStep()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeContext(world, registry);

  for (int z = 0; z < kChunkSize; ++z)
  {
    for (int x = 0; x < kChunkSize; ++x)
    {
      const int surface_y = (x == 8 && z == 8) ? 82 : 79;
      FillColumn(world, x, z, surface_y, kDirt);
    }
  }

  cutum::ApplyMudflowToChunk(ctx, 0, 0, 1);

  int max_delta = 0;
  for (int z = 0; z < kChunkSize; ++z)
  {
    for (int x = 0; x < kChunkSize; ++x)
    {
      const int y = TopSolidY(world, x, z, ctx.Settings.MaxHeight);
      if (x + 1 < kChunkSize)
      {
        const int y_e = TopSolidY(world, x + 1, z, ctx.Settings.MaxHeight);
        max_delta = std::max(max_delta, std::abs(y - y_e));
      }
      if (z + 1 < kChunkSize)
      {
        const int y_n = TopSolidY(world, x, z + 1, ctx.Settings.MaxHeight);
        max_delta = std::max(max_delta, std::abs(y - y_n));
      }
    }
  }
  FluidTest::Expect(max_delta <= 2, kTestName,
                    "mudflow reduces artificial step to neighbor delta <= 2");
}

} // namespace

int main()
{
  TestMudflowReducesStep();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
