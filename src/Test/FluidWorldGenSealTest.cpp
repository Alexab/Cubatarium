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
  FillStoneColumn(world, 0, 0, 58);
  cutum::FillFluidColumn(ctx, 0, 0, 58);

  for (int y = 59; y <= 63; ++y)
  {
    FluidTest::Expect(world.GetBlock(glm::ivec3(0, y, 0)) == kWater, kTestName,
                      "below-sea column fills with water");
  }
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 58, 0)) == kStone, kTestName,
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

static void TestFillColumnAfterRavineCarve()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry, 48);
  FillStoneColumn(world, 0, 0, 48);
  for (int y = 16; y <= 48; ++y)
  {
    world.SetBlock(glm::ivec3(0, y, 0), cutum::BLOCK_AIR);
  }

  cutum::FillFluidColumn(ctx, 0, 0, 48);

  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 15, 0)) == kStone, kTestName,
                    "ravine floor remains stone");
  for (int y = 16; y <= 48; ++y)
  {
    FluidTest::Expect(world.GetBlock(glm::ivec3(0, y, 0)) == kWater, kTestName,
                      "ravine-carved column fills to sea from actual floor");
  }
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

static void TestSealSeabedDecorWithWaterAbove()
{
  cutum::UBlockWorld world;
  constexpr cutum::BlockId kTallGrass = 10;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDecorDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 58);
  world.SetBlock(glm::ivec3(0, 61, 0), kTallGrass);
  world.SetBlock(glm::ivec3(0, 62, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 62, 0), cutum::FluidCellState::Source());

  cutum::SealFluidPermeableDecorInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 61, 0)) == kTallGrass,
                    kTestName, "seabed decor block preserved");
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(0, 61, 0))) != 0,
      kTestName, "seabed decor waterlogged when water is above");
}

static void TestRestoreWaterColumnReplacesPrefabDirt()
{
  cutum::UBlockWorld world;
  constexpr cutum::BlockId kSand = 11;
  constexpr cutum::BlockId kDirt = 12;
  constexpr cutum::BlockId kTallGrass = 10;
  auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  auto ctx = MakeSealContext(world, registry, 63);
  ctx.Blocks.Sand = kSand;
  ctx.Blocks.Dirt = kDirt;
  FillStoneColumn(world, 0, 0, 60);
  world.SetBlock(glm::ivec3(0, 61, 0), kStone);
  world.SetBlock(glm::ivec3(0, 62, 0), kSand);
  world.SetBlock(glm::ivec3(0, 63, 0), kDirt);
  world.SetBlock(glm::ivec3(0, 64, 0), kTallGrass);

  cutum::SealFluidPermeableDecorInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 63, 0)) == kWater, kTestName,
                    "prefab dirt in water column becomes water");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 62, 0)) == kSand, kTestName,
                    "shore sand floor preserved");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 64, 0)) == kTallGrass,
                    kTestName, "reeds above sea preserved");
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(0, 64, 0))) != 0,
      kTestName, "reeds above sea waterlogged");
}

static void TestSealAirAboveWaterloggedDecor()
{
  cutum::UBlockWorld world;
  constexpr cutum::BlockId kTallGrass = 10;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDecorDefinitions());
  auto ctx = MakeSealContext(world, registry);
  FillStoneColumn(world, 0, 0, 58);
  world.SetBlock(glm::ivec3(0, 61, 0), kTallGrass);
  world.SetBlock(glm::ivec3(0, 62, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(0, 63, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 63, 0), cutum::FluidCellState::Source());

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  cutum::SealFluidPermeableDecorInChunk(ctx, 0, 0);
  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 61, 0)) == kTallGrass,
                    kTestName, "decor preserved when sealing air gap above");
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(0, 61, 0))) != 0,
      kTestName, "decor waterlogged before air gap seals");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 62, 0)) == kWater, kTestName,
                    "air above waterlogged decor seals to water");
}

/// V_fluid: IntraChunkSeal helper must match full commit seal with shore_air=false.
static void TestIntraChunkSealMatchesShoreFalse()
{
  auto run = [](bool use_intra_api)
  {
    cutum::UBlockWorld world;
    cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
    cutum::ProceduralSettings settings;
    settings.FillWater = true;
    settings.SeaLevel = 63;
    FillStoneColumn(world, 0, 0, 58);
    world.SetBlock(glm::ivec3(0, 60, 0), cutum::BLOCK_AIR);
    world.SetBlock(glm::ivec3(0, 61, 0), cutum::BLOCK_AIR);
    world.SetBlock(glm::ivec3(0, 62, 0), cutum::BLOCK_AIR);
    world.SetBlock(glm::ivec3(0, 63, 0), kWater);
    world.SetFluidState(glm::ivec3(0, 63, 0), cutum::FluidCellState::Source());
    if (use_intra_api)
    {
      cutum::SealFluidIntraChunkOnCommitted(world, registry, settings, "",
                                            glm::ivec3(0, 0, 0));
    }
    else
    {
      cutum::SealFluidShoreOnChunkCommitted(world, registry, settings, "",
                                            glm::ivec3(0, 0, 0),
                                            /*include_shore_air=*/false);
    }
    return world.GetBlock(glm::ivec3(0, 62, 0));
  };
  FluidTest::Expect(run(true) == run(false), kTestName,
                    "IntraChunkSeal API matches shore_air=false path");
  FluidTest::Expect(run(true) == kWater, kTestName,
                    "IntraChunkSeal fills wet pocket");
}

} // namespace

int main()
{
  TestFillColumnBelowSea();
  TestFillColumnAboveSea();
  TestFillColumnAfterRavineCarve();
  TestSealSinglePocket();
  TestSealChain8();
  TestSealChain9();
  TestSealChunkBoundarySameWorld();
  TestSealChunkBoundaryIsolated();
  TestSealPermeableDecorPocket();
  TestSealSeabedDecorWithWaterAbove();
  TestRestoreWaterColumnReplacesPrefabDirt();
  TestSealAirAboveWaterloggedDecor();
  TestIntraChunkSealMatchesShoreFalse();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
