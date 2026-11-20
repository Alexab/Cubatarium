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

static void TestFloodReportsExactBoundaryMutations()
{
  using namespace cutum;
  for (int mode = 0; mode < 3; ++mode)
  {
    auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
    UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    for (int x = 13; x <= 18; ++x)
      for (int y = -35; y <= -30; ++y)
        for (int z = 13; z <= 18; ++z)
          world.SetBlock({x, y, z}, kStone);
    const glm::ivec3 center(15, -33, 15);
    for (const auto p : {center, glm::ivec3(16, -33, 15),
                         glm::ivec3(15, -33, 16), glm::ivec3(16, -33, 14),
                         glm::ivec3(14, -33, 16)})
      world.SetBlock(p, BLOCK_AIR);
    for (const auto p : {glm::ivec3(15, -32, 15), glm::ivec3(16, -32, 15)})
      world.SetBlock(p, 10); // permeable grass in the upper section
    world.SetBlock({14, -33, 14}, 10); // shared spill target of both sources
    world.SetBlock({14, -33, 15}, kWater);
    world.SetBlock({15, -33, 14}, kWater);
    struct Before { glm::ivec3 position; BlockId block; uint32_t fluid; };
    std::vector<Before> before;
    for (int x = 13; x <= 18; ++x)
      for (int y = -35; y <= -30; ++y)
        for (int z = 13; z <= 18; ++z)
          before.push_back({{x,y,z}, world.GetBlock({x,y,z}),
                            PackFluidCellState(world.GetFluidState({x,y,z}))});
    std::vector<glm::ivec3> reported;
    const auto options = MakeFloodOptions(kWater);
    int filled;
    if (mode == 0)
      filled = UFluidFloodService::FloodBreakSiteFromWetNeighbors(
          world, *definitions, center, options, &reported);
    else if (mode == 1)
      filled = UFluidFloodService::FloodWetPocketsInBox(
          world, *definitions, {14,-33,14}, {16,-32,16}, options, &reported);
    else
      filled = UFluidFloodService::FloodWetPocketsLocal(
          world, *definitions, center, 2, options, &reported);
    std::unordered_set<glm::ivec3, IVec3Hash> actual, emitted(reported.begin(), reported.end());
    for (const auto &cell : before)
      if (world.GetBlock(cell.position) != cell.block ||
          PackFluidCellState(world.GetFluidState(cell.position)) != cell.fluid)
        actual.insert(cell.position);
    FluidTest::Expect(emitted.size() == reported.size(), kTestName,
                      "flood mutation report must not duplicate an already wet decor cell");
    FluidTest::Expect(emitted == actual && static_cast<size_t>(filled) == actual.size(),
                      kTestName, "flood report and count must equal actual block/fluid mutations");
    FluidTest::Expect(actual.count(center) && actual.count({15,-32,15}) &&
                          actual.count({16,-33,14}) && actual.count({14,-33,16}),
                      kTestName, "exact mutations cover X/Z and negative-Y section seams");
    FluidTest::Expect(world.GetBlock({15,-32,15}) == 10, kTestName,
                      "waterlogging preserves the upper permeable decor block");
  }
}

} // namespace

int main()
{
  TestOneHopBreakFloodFillsBrokenCell();
  TestOneHopBreakFloodStaysLocal();
  TestEightPassBoxFloodFillsChain();
  TestEightPassBoxFloodStopsAtPassLimit();
  TestFloodReportsExactBoundaryMutations();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
