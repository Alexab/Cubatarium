#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Stages/WorldGenStages.h"

#include <iostream>
#include <memory>

namespace
{

constexpr const char *kTestName = "fluid_worldgen_seal_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;

static cutum::WorldGenContext MakeSealContext(cutum::UBlockWorld &world,
                                              cutum::UBlockRegistry &registry,
                                              int sea_level = 63)
{
  cutum::ProceduralSettings settings;
  settings.FillWater = true;
  settings.SeaLevel = sea_level;
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.Blocks.Stone = kStone;
  ctx.Blocks.Water = kWater;
  return ctx;
}

static void FillStoneColumn(cutum::UBlockWorld &world, int x, int z, int top_y)
{
  for (int y = 0; y <= top_y; ++y)
  {
    world.SetBlock(glm::ivec3(x, y, z), kStone);
  }
}

static void TestFillColumnBelowSea()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 60);
  cutum::FillFluidColumn(ctx, 0, 0, 60);

  for (int y = 61; y <= 63; ++y)
  {
    FluidTest::Expect(world.GetBlock(glm::ivec3(0, y, 0)) == kWater, kTestName,
                      "below-sea column fills with water");
  }
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 60, 0)) == kStone, kTestName,
                    "surface block remains stone");
}

static void TestFillColumnAboveSea()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 65);
  cutum::FillFluidColumn(ctx, 0, 0, 65);

  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 63, 0)) != kWater, kTestName,
                    "above-sea column has no water fill");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 65, 0)) == kStone, kTestName,
                    "surface block remains stone above sea");
}

static void TestSealSinglePocket()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 58);
  world.SetBlock(glm::ivec3(0, 61, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 61, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 61, 0), cutum::BLOCK_AIR);

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 61, 0)) == kWater, kTestName,
                    "single air pocket seals to water");
}

static void TestSealChain8()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 58);
  world.SetBlock(glm::ivec3(0, 61, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 61, 0), cutum::FluidCellState::Source());
  for (int x = 1; x <= 8; ++x)
  {
    world.SetBlock(glm::ivec3(x, 61, 0), cutum::BLOCK_AIR);
  }

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  for (int x = 1; x <= 8; ++x)
  {
    FluidTest::Expect(world.GetBlock(glm::ivec3(x, 61, 0)) == kWater,
                      kTestName, "8-cell air chain seals fully");
  }
}

static void TestSealChain9()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 58);
  world.SetBlock(glm::ivec3(0, 61, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 61, 0), cutum::FluidCellState::Source());
  for (int x = 1; x <= 9; ++x)
  {
    world.SetBlock(glm::ivec3(x, 61, 0), cutum::BLOCK_AIR);
  }

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  for (int x = 1; x <= 8; ++x)
  {
    FluidTest::Expect(world.GetBlock(glm::ivec3(x, 61, 0)) == kWater,
                      kTestName, "8-cell air chain seals within pass limit");
  }
  FluidTest::Expect(world.GetBlock(glm::ivec3(9, 61, 0)) == cutum::BLOCK_AIR,
                    kTestName, "9th air cell remains after 8-pass limit");
}

static void TestSealChunkBoundarySameWorld()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, -1, 0, 58);
  world.SetBlock(glm::ivec3(-1, 61, 0), kWater);
  world.SetFluidState(glm::ivec3(-1, 61, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(0, 61, 0), cutum::BLOCK_AIR);

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 61, 0)) == kWater, kTestName,
                    "chunk boundary pocket seals when neighbor water is in world");
}

static void TestSealChunkBoundaryIsolated()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 58);
  world.SetBlock(glm::ivec3(0, 61, 0), cutum::BLOCK_AIR);

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 61, 0)) == cutum::BLOCK_AIR,
                    kTestName,
                    "isolated air at boundary stays air without neighbor water");
}

static void TestSealPermeableDecorPocket()
{
  cutum::UBlockWorld world;
  constexpr cutum::BlockId kTallGrass = 10;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDecorDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 58);
  world.SetBlock(glm::ivec3(0, 61, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 61, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 61, 0), kTallGrass);

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 61, 0)) == kTallGrass,
                    kTestName, "permeable decor block preserved during seal");
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(1, 61, 0))) != 0,
      kTestName, "permeable decor receives fluid_data during seal");
}

} // namespace

int main()
{
  TestFillColumnBelowSea();
  TestFillColumnAboveSea();
  TestSealSinglePocket();
  TestSealChain8();
  TestSealChain9();
  TestSealChunkBoundarySameWorld();
  TestSealChunkBoundaryIsolated();
  TestSealPermeableDecorPocket();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
