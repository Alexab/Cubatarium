#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Features/RavineCarver.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "ravine_carver_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;
constexpr int kSurfaceY = 80;

static cutum::WorldGenContext MakeContext(cutum::UBlockWorld &world,
                                          cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings;
  settings.SeaLevel = 48;
  settings.MaxHeight = 128;
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

static bool ColumnHasAirBelowSurface(const cutum::UBlockWorld &world, int x,
                                     int z, int surface_y)
{
  for (int y = surface_y - 1; y >= std::max(1, surface_y - 40); --y)
  {
    if (world.IsAir(glm::ivec3(x, y, z)))
    {
      return true;
    }
  }
  return false;
}

static int DeepestAirBelowSurface(const cutum::UBlockWorld &world, int x, int z,
                                  int surface_y)
{
  int deepest = surface_y;
  for (int y = surface_y; y >= 1; --y)
  {
    if (world.IsAir(glm::ivec3(x, y, z)))
    {
      deepest = y;
    }
    else if (deepest < surface_y)
    {
      break;
    }
  }
  return deepest;
}

static void TestRavineCarveAndDepthLimit()
{
  cutum::RavineParams params;
  params.enabled = true;
  params.maxDepth = 8;
  params.aquaticMaxDepth = 0;
  const auto surface_at = [](int, int) { return kSurfaceY; };

  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeContext(world, registry);
  FillStoneColumn(world, 0, 0, kSurfaceY);
  cutum::CarveColumnRavinesDeterministic(ctx, 0, 0, kSurfaceY, params, 48,
                                         surface_at);
  FluidTest::Expect(ColumnHasAirBelowSurface(world, 0, 0, kSurfaceY), kTestName,
                    "ravine carve removed blocks below surface");
  const int deepest = DeepestAirBelowSurface(world, 0, 0, kSurfaceY);
  FluidTest::Expect(kSurfaceY - deepest <= params.maxDepth, kTestName,
                    "ravine carve depth stays within configured max");
}

static void TestPerNeighborSurfaceY()
{
  cutum::RavineParams params;
  params.enabled = true;
  params.maxDepth = 10;
  params.aquaticMaxDepth = 0;
  const auto surface_at = [](int x, int) { return 80 + (x > 0 ? 6 : 0); };

  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeContext(world, registry);
  FillStoneColumn(world, 0, 0, 86);
  FillStoneColumn(world, 1, 0, 80);
  cutum::CarveColumnRavinesDeterministic(ctx, 0, 0, 86, params, 48, surface_at);

  FluidTest::Expect(ColumnHasAirBelowSurface(world, 1, 0, 80), kTestName,
                    "neighbor carve uses local surface height");
  const int deepest = DeepestAirBelowSurface(world, 1, 0, 80);
  FluidTest::Expect(80 - deepest <= params.maxDepth, kTestName,
                    "neighbor carve depth respects local surface");
}

static void TestAquaticMaxDepthCap()
{
  cutum::RavineParams params;
  params.enabled = true;
  params.maxDepth = 20;
  params.aquaticMaxDepth = 5;

  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeContext(world, registry);
  FillStoneColumn(world, 0, 0, 50);
  const auto surface_at = [](int, int) { return 50; };
  cutum::CarveColumnRavinesDeterministic(ctx, 0, 0, 50, params, 48, surface_at);

  FluidTest::Expect(ColumnHasAirBelowSurface(world, 0, 0, 50), kTestName,
                    "aquatic ravine carve occurred");
  const int deepest = DeepestAirBelowSurface(world, 0, 0, 50);
  FluidTest::Expect(50 - deepest <= params.aquaticMaxDepth, kTestName,
                    "aquatic ravine depth capped");
}

static void TestAquaticMaxDepthDefault()
{
  FluidTest::Expect(cutum::RavineParams{}.aquaticMaxDepth == 5, kTestName,
                    "default aquatic max depth is configured");
}

static void TestRavineFillWater()
{
  cutum::RavineParams params;
  params.enabled = true;
  params.maxDepth = 12;
  params.aquaticMaxDepth = 8;
  params.fillWater = true;
  const int sea_level = 48;
  const int surface_y = 50;
  const auto surface_at = [surface_y](int, int) { return surface_y; };

  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeContext(world, registry);
  FillStoneColumn(world, 0, 0, surface_y);
  cutum::CarveColumnRavinesDeterministic(ctx, 0, 0, surface_y, params, sea_level,
                                         surface_at);

  bool has_water = false;
  for (int y = 1; y <= sea_level; ++y)
  {
    if (world.GetBlock(glm::ivec3(0, y, 0)) == kWater)
    {
      has_water = true;
      break;
    }
  }
  FluidTest::Expect(has_water, kTestName,
                    "fillWater places water in carved ravine up to sea level");
}

static void TestRavineFeatherReducesRimDelta()
{
  cutum::RavineParams params;
  params.enabled = true;
  params.maxDepth = 12;
  params.aquaticMaxDepth = 0;
  const auto surface_at = [](int, int) { return kSurfaceY; };

  cutum::UBlockWorld center_world;
  cutum::UBlockWorld rim_world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto center_ctx = MakeContext(center_world, registry);
  auto rim_ctx = MakeContext(rim_world, registry);
  FillStoneColumn(center_world, 0, 0, kSurfaceY);
  FillStoneColumn(rim_world, 3, 0, kSurfaceY);
  cutum::CarveColumnRavinesDeterministic(center_ctx, 0, 0, kSurfaceY, params, 48,
                                         surface_at);
  cutum::CarveColumnRavinesDeterministic(rim_ctx, 0, 0, kSurfaceY, params, 48,
                                         surface_at);

  const int center_depth =
      kSurfaceY - DeepestAirBelowSurface(center_world, 0, 0, kSurfaceY);
  const int rim_depth =
      kSurfaceY - DeepestAirBelowSurface(rim_world, 3, 0, kSurfaceY);
  FluidTest::Expect(center_depth > rim_depth, kTestName,
                    "feather profile carves deeper at center than rim");
  FluidTest::Expect(rim_depth <= 2, kTestName,
                    "feather profile keeps rim carve shallow");
}

} // namespace

int main()
{
  TestAquaticMaxDepthDefault();
  TestRavineCarveAndDepthLimit();
  TestPerNeighborSurfaceY();
  TestAquaticMaxDepthCap();
  TestRavineFillWater();
  TestRavineFeatherReducesRimDelta();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
