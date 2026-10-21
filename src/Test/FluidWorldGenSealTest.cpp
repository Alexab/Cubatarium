#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/AquiferSampler.h"
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
                                              int sea_level = 63,
                                              bool density_caves = false)
{
  cutum::ProceduralSettings settings;
  settings.FillWater = true;
  settings.SeaLevel = sea_level;
  if (density_caves)
  {
    settings.TerrainBackendMode = cutum::TerrainBackend::Density3D;
    settings.EnableCaves = true;
  }
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

static glm::ivec3 FindAquiferPosition(
    const cutum::UAquiferSampler &sampler, cutum::AquiferFluid fluid,
    int sea_level, int max_height)
{
  for (int y = 4; y < sea_level - 1; ++y)
  {
    for (int x = 1; x < cutum::CHUNK_SIZE - 1; ++x)
    {
      for (int z = 1; z < cutum::CHUNK_SIZE - 1; ++z)
      {
        if (sampler.SampleAt(x, y, z).Fluid == fluid)
        {
          return glm::ivec3(x, y, z);
        }
      }
    }
  }
  return glm::ivec3(-1, max_height, -1);
}

static void TestAquiferAwarePocketSeal()
{
  constexpr int sea_level = 63;
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry, sea_level, true);
  const cutum::UAquiferSampler sampler(
      ctx.Settings.Seed, ctx.Settings.SeaLevel, ctx.Settings.MaxHeight,
      cutum::UWorldGenPack::AquifersConfig());

  const glm::ivec3 dry = FindAquiferPosition(
      sampler, cutum::AquiferFluid::Dry, sea_level, ctx.Settings.MaxHeight);
  FluidTest::Expect(dry.x >= 0, kTestName,
                    "find dry underground aquifer cell for seal test");
  const glm::ivec3 dry_source = dry + glm::ivec3(1, 0, 0);
  world.SetBlock(dry_source, kWater);
  world.SetFluidState(dry_source,
                      cutum::FluidCellState::Source().WithKind(
                          cutum::FluidKind::Water));
  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(dry) == cutum::BLOCK_AIR, kTestName,
                    "pocket sealing leaves dry aquifer cavities empty");

  cutum::UBlockWorld wet_world;
  cutum::UBlockRegistry wet_registry(
      nullptr, FluidTest::MakeTestFluidDefinitions());
  auto wet_ctx = MakeSealContext(wet_world, wet_registry, sea_level, true);
  const cutum::UAquiferSampler wet_sampler(
      wet_ctx.Settings.Seed, wet_ctx.Settings.SeaLevel,
      wet_ctx.Settings.MaxHeight, cutum::UWorldGenPack::AquifersConfig());
  const glm::ivec3 wet = FindAquiferPosition(
      wet_sampler, cutum::AquiferFluid::Water, sea_level,
      wet_ctx.Settings.MaxHeight);
  FluidTest::Expect(wet.x >= 0, kTestName,
                    "find wet underground aquifer cell for seal test");
  const glm::ivec3 wet_source = wet - glm::ivec3(1, 0, 0);
  wet_world.SetBlock(wet_source, kWater);
  wet_world.SetFluidState(wet_source,
                          cutum::FluidCellState::Source().WithKind(
                              cutum::FluidKind::Water));
  cutum::SealFluidPocketsInChunk(wet_ctx, 0, 0);
  FluidTest::Expect(wet_world.GetBlock(wet) == kWater, kTestName,
                    "pocket sealing still fills water-classified aquifers");
}

static void TestAquiferColumnRespectsLocalFluid()
{
  constexpr int sea_level = 63;
  constexpr cutum::BlockId kLava = 10;
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeSealContext(world, registry, sea_level);
  ctx.Settings.FillLava = true;
  ctx.Blocks.Lava = kLava;
  cutum::AquiferParams aquifer_params =
      cutum::UWorldGenPack::AquifersConfig();
  aquifer_params.DryCellChance = 0.30f;
  aquifer_params.BarrierBand = 8.0f;
  aquifer_params.BarrierNoiseThreshold = -1.0f;
  aquifer_params.PressureDifferenceThreshold = 0;
  aquifer_params.LavaMaxY = 16;
  aquifer_params.LavaCellChance = 0.75f;
  const cutum::UAquiferSampler sampler(
      ctx.Settings.Seed, ctx.Settings.SeaLevel, ctx.Settings.MaxHeight,
      aquifer_params);

  const glm::ivec3 dry = FindAquiferPosition(
      sampler, cutum::AquiferFluid::Dry, sea_level, ctx.Settings.MaxHeight);
  const glm::ivec3 wet = FindAquiferPosition(
      sampler, cutum::AquiferFluid::Water, sea_level, ctx.Settings.MaxHeight);
  const glm::ivec3 lava = FindAquiferPosition(
      sampler, cutum::AquiferFluid::Lava, sea_level, ctx.Settings.MaxHeight);
  const glm::ivec3 barrier = FindAquiferPosition(
      sampler, cutum::AquiferFluid::Barrier, sea_level, ctx.Settings.MaxHeight);
  FluidTest::Expect(dry.x >= 0 && wet.x >= 0 && lava.x >= 0 && barrier.x >= 0,
                    kTestName, "find each aquifer class for column fill test");

  cutum::FillAquiferColumn(ctx, dry.x, dry.z, sea_level + 1, sampler);
  cutum::FillAquiferColumn(ctx, wet.x, wet.z, sea_level + 1, sampler);
  cutum::FillAquiferColumn(ctx, lava.x, lava.z, sea_level + 1, sampler);
  cutum::FillAquiferColumn(ctx, barrier.x, barrier.z, sea_level + 1, sampler);

  FluidTest::Expect(world.GetBlock(dry) == cutum::BLOCK_AIR, kTestName,
                    "dry aquifer cell remains air instead of receiving water");
  FluidTest::Expect(world.GetBlock(wet) == kWater, kTestName,
                    "water aquifer cell receives water");
  FluidTest::Expect(world.GetBlock(lava) == kLava, kTestName,
                    "deep lava aquifer cell receives lava");
  FluidTest::Expect(world.GetBlock(barrier) == kStone, kTestName,
                    "aquifer barrier cell is closed with stone");
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
  // Decor above the sea surface must not be waterlogged: the flooded cell
  // would render as a water cube standing on the surface with the plant inside.
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(0, 64, 0))) == 0,
      kTestName, "reeds above sea not waterlogged");
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
  TestAquiferAwarePocketSeal();
  TestAquiferColumnRespectsLocalFluid();
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
