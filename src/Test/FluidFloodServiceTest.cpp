#include "Test/FluidTestHelpers.h"

#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidFloodService.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "fluid_flood_service_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;

static cutum::FluidFloodOptions MakeFloodOptions(cutum::BlockId water_id,
                                                 int sea_level = -1)
{
  cutum::FluidFloodOptions options;
  options.water_id = water_id;
  options.source_for_air = false;
  options.sea_level = sea_level;
  options.max_passes = 8;
  return options;
}

static void TestOneHopBreakFloodFillsBrokenCell()
{
  auto definitions = FluidTest::MakeTestFluidDefinitions();
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(1, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(0, 10, 0), cutum::BLOCK_AIR);

  const int filled = cutum::UFluidFloodService::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(0, 10, 0), MakeFloodOptions(kWater));
  FluidTest::Expect(filled > 0, kTestName, "one-hop break flood fills broken cell");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 10, 0)) == kWater, kTestName,
                    "broken cell becomes water");
}

static void TestOneHopBreakFloodStaysLocal()
{
  auto definitions = FluidTest::MakeTestFluidDefinitions();
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(2, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(2, 10, 0), cutum::FluidCellState::Source());
  for (int dx = 4; dx <= 9; ++dx)
  {
    for (int dy = 0; dy <= 4; ++dy)
    {
      for (int dz = -2; dz <= 2; ++dz)
      {
        world.SetBlock(glm::ivec3(dx, 10 + dy, dz), cutum::BLOCK_AIR);
      }
    }
  }
  world.SetBlock(glm::ivec3(3, 10, 0), cutum::BLOCK_AIR);

  const int filled = cutum::UFluidFloodService::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(3, 10, 0), MakeFloodOptions(kWater));
  FluidTest::Expect(filled <= 8, kTestName, "one-hop break flood stays local");
  FluidTest::Expect(world.GetBlock(glm::ivec3(3, 10, 0)) == kWater, kTestName,
                    "broken cell fills");
  FluidTest::Expect(world.GetBlock(glm::ivec3(9, 12, 0)) == cutum::BLOCK_AIR,
                    kTestName, "distant air in pocket stays dry");
}

static void TestEightPassBoxFloodFillsChain()
{
  auto definitions = FluidTest::MakeTestFluidDefinitions();
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(0, 61, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 61, 0), cutum::FluidCellState::Source());
  for (int x = 1; x <= 8; ++x)
  {
    world.SetBlock(glm::ivec3(x, 61, 0), cutum::BLOCK_AIR);
  }

  cutum::FluidFloodOptions options = MakeFloodOptions(kWater, 63);
  const int filled = cutum::UFluidFloodService::FloodWetPocketsInBox(
      world, *definitions, glm::ivec3(0, 61, 0), glm::ivec3(8, 61, 0), options);
  FluidTest::Expect(filled == 8, kTestName, "8-pass box flood fills 8 cells");
  for (int x = 1; x <= 8; ++x)
  {
    FluidTest::Expect(world.GetBlock(glm::ivec3(x, 61, 0)) == kWater, kTestName,
                      "8-cell air chain seals fully");
  }
}

static void TestEightPassBoxFloodStopsAtPassLimit()
{
  auto definitions = FluidTest::MakeTestFluidDefinitions();
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(0, 61, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 61, 0), cutum::FluidCellState::Source());
  for (int x = 1; x <= 9; ++x)
  {
    world.SetBlock(glm::ivec3(x, 61, 0), cutum::BLOCK_AIR);
  }

  cutum::FluidFloodOptions options = MakeFloodOptions(kWater, 63);
  cutum::UFluidFloodService::FloodWetPocketsInBox(
      world, *definitions, glm::ivec3(0, 61, 0), glm::ivec3(9, 61, 0), options);
  for (int x = 1; x <= 8; ++x)
  {
    FluidTest::Expect(world.GetBlock(glm::ivec3(x, 61, 0)) == kWater, kTestName,
                      "8-cell air chain seals within pass limit");
  }
  FluidTest::Expect(world.GetBlock(glm::ivec3(9, 61, 0)) == cutum::BLOCK_AIR,
                    kTestName, "9th air cell remains after 8-pass limit");
}

} // namespace

int main()
{
  TestOneHopBreakFloodFillsBrokenCell();
  TestOneHopBreakFloodStaysLocal();
  TestEightPassBoxFloodFillsChain();
  TestEightPassBoxFloodStopsAtPassLimit();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
