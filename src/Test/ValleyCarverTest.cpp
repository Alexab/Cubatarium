#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Features/ValleyCarver.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestName = "valley_carver_test";
constexpr cutum::BlockId kStone = 8;
constexpr int kSurfaceY = 80;
constexpr uint32_t kSeed = 12345;

static fs::path FindRepoRoot()
{
  fs::path dir = fs::current_path();
  for (int depth = 0; depth < 8; ++depth)
  {
    if (fs::exists(dir / "content" / "worldgen_packs" / "default" / "pack.json"))
    {
      return dir;
    }
    if (!dir.has_parent_path())
    {
      break;
    }
    dir = dir.parent_path();
  }
  FluidTest::Expect(false, kTestName, "could not find repository root");
  return fs::current_path();
}

static cutum::WorldGenContext MakeContext(cutum::UBlockWorld &world,
                                          cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings;
  settings.Seed = kSeed;
  settings.SeaLevel = 48;
  settings.MaxHeight = 128;
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.Blocks.Stone = kStone;
  return ctx;
}

static void FillStoneColumn(cutum::UBlockWorld &world, int x, int z, int top_y)
{
  for (int y = 0; y <= top_y; ++y)
  {
    world.SetBlock(glm::ivec3(x, y, z), kStone);
  }
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

static void TestValleyCarveSmoothWalls()
{
  cutum::ValleyParams params;
  params.enabled = true;
  params.maxDepth = 12;
  params.widthSigma = 2.5f;
  params.aquaticDepthScale = 0.4f;

  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeContext(world, registry);

  int carved_x = 0;
  int carved_z = 0;
  bool carved = false;
  for (int z = 0; z < 256 && !carved; ++z)
  {
    for (int x = 0; x < 256; ++x)
    {
      for (int gz = -4; gz <= 4; ++gz)
      {
        for (int gx = -4; gx <= 4; ++gx)
        {
          FillStoneColumn(world, x + gx, z + gz, kSurfaceY);
        }
      }
      cutum::CarveColumnValleys(ctx, x, z, kSurfaceY, kSeed, params, 48, 1.0f,
                                nullptr);
      if (DeepestAirBelowSurface(world, x, z, kSurfaceY) < kSurfaceY)
      {
        carved = true;
        carved_x = x;
        carved_z = z;
        break;
      }
    }
  }
  FluidTest::Expect(carved, kTestName, "valley carve occurred for some column");

  int max_rim_delta = 0;
  for (int dz = -3; dz <= 3; ++dz)
  {
    for (int dx = -3; dx <= 3; ++dx)
    {
      const int nx = carved_x + dx;
      const int nz = carved_z + dz;
      const int depth =
          kSurfaceY - DeepestAirBelowSurface(world, nx, nz, kSurfaceY);
      if (depth <= 0)
      {
        continue;
      }
      const int depth_e =
          kSurfaceY - DeepestAirBelowSurface(world, nx + 1, nz, kSurfaceY);
      const int depth_n =
          kSurfaceY - DeepestAirBelowSurface(world, nx, nz + 1, kSurfaceY);
      if (depth_e > 0)
      {
        max_rim_delta = std::max(max_rim_delta, std::abs(depth - depth_e));
      }
      if (depth_n > 0)
      {
        max_rim_delta = std::max(max_rim_delta, std::abs(depth - depth_n));
      }
    }
  }
  FluidTest::Expect(max_rim_delta <= 2, kTestName,
                    "valley cross-section rim delta stays smooth");
}

static void FillStoneChunk(cutum::UBlockWorld &world, int base_x, int base_z,
                           int top_y)
{
  for (int lz = 0; lz < 16; ++lz)
  {
    for (int lx = 0; lx < 16; ++lx)
    {
      FillStoneColumn(world, base_x + lx, base_z + lz, top_y);
    }
  }
}

static int MaxCarveRimDelta(const cutum::UBlockWorld &world, int base_x,
                            int base_z, int surface_y)
{
  int max_rim_delta = 0;
  for (int lz = 0; lz < 16; ++lz)
  {
    for (int lx = 0; lx < 16; ++lx)
    {
      const int x = base_x + lx;
      const int z = base_z + lz;
      const int depth = surface_y - DeepestAirBelowSurface(world, x, z, surface_y);
      if (depth <= 0)
      {
        continue;
      }
      if (lx + 1 < 16)
      {
        const int depth_e = surface_y - DeepestAirBelowSurface(
                                            world, x + 1, z, surface_y);
        if (depth_e > 0)
        {
          max_rim_delta = std::max(max_rim_delta, std::abs(depth - depth_e));
        }
      }
      if (lz + 1 < 16)
      {
        const int depth_n = surface_y - DeepestAirBelowSurface(
                                            world, x, z + 1, surface_y);
        if (depth_n > 0)
        {
          max_rim_delta = std::max(max_rim_delta, std::abs(depth - depth_n));
        }
      }
    }
  }
  return max_rim_delta;
}

static void TestChunkValleySeamStable()
{
  cutum::ValleyParams params;
  params.enabled = true;
  params.maxDepth = 12;
  params.widthSigma = 2.5f;
  params.aquaticDepthScale = 0.4f;
  params.riverNoiseScale = 0.008f;

  const auto surface_at = [](int, int) { return kSurfaceY; };

  int base_x = 0;
  int base_z = 0;
  bool carved = false;
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  auto ctx = MakeContext(world, registry);

  for (int z = 0; z < 256 && !carved; ++z)
  {
    for (int x = 0; x < 256 && !carved; ++x)
    {
      world.Clear();
      FillStoneChunk(world, x, z, kSurfaceY);
      cutum::CarveChunkValleys(ctx, x, z, kSeed, params, 48, 1.0f, surface_at);
      for (int lz = 0; lz < 16 && !carved; ++lz)
      {
        for (int lx = 0; lx < 16; ++lx)
        {
          if (DeepestAirBelowSurface(world, x + lx, z + lz, kSurfaceY) <
              kSurfaceY)
          {
            carved = true;
            base_x = x;
            base_z = z;
            break;
          }
        }
      }
    }
  }
  FluidTest::Expect(carved, kTestName, "chunk valley carve occurred");
  FluidTest::Expect(MaxCarveRimDelta(world, base_x, base_z, kSurfaceY) <= 2,
                    kTestName, "chunk valley seam stays smooth across chunk");
}

} // namespace

int main()
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  cutum::UWorldGenPack::LoadPackId("default");

  TestValleyCarveSmoothWalls();
  TestChunkValleySeamStable();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
