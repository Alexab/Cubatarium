#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Features/ValleyCarver.h"
#include "WorldGen/Features/LakeCarver.h"
#include "WorldGen/Features/RiverField.h"
#include "WorldGen/Core/ColumnHash.h"
#include "WorldGen/Sampling/ClimateSampler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <tuple>
#include <utility>

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
  ctx.Blocks.Dirt = 10;
  ctx.Blocks.Water = 9;
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
  const int rimDelta = MaxCarveRimDelta(world, base_x, base_z, kSurfaceY);
  FluidTest::Expect(rimDelta <= 3,
                    kTestName, "chunk valley seam stays smooth across chunk");
}

static void TestRiverFieldContinuity()
{
  const float center = cutum::SampleRiverChannelStrength(
      101, -57, kSeed, 0.008f, 1.0f, 0.35f, 0.4f);
  FluidTest::Expect(center >= 0.0f && center <= 1.0f, kTestName,
                    "river strength stays normalized");
  FluidTest::Expect(
      center == cutum::SampleRiverChannelStrength(
                    101, -57, kSeed, 0.008f, 1.0f, 0.35f, 0.4f),
      kTestName, "river strength is deterministic");
  const float east = cutum::SampleRiverChannelStrength(
      102, -57, kSeed, 0.008f, 1.0f, 0.35f, 0.4f);
  const float south = cutum::SampleRiverChannelStrength(
      101, -56, kSeed, 0.008f, 1.0f, 0.35f, 0.4f);
  FluidTest::Expect(std::abs(center - east) < 0.55f &&
                        std::abs(center - south) < 0.55f,
                    kTestName,
                    "neighboring river samples do not jump abruptly");
}

static void TestLakeCarveAndChunkOrder()
{
  cutum::LakeParams params;
  params.cellSize = 48;
  params.chancePerCell = 1;
  params.radiusMin = 4;
  params.radiusMax = 6;
  params.minElevationAboveSea = 2;
  params.maxElevationAboveSea = 4;
  params.maxExcavationDepth = 20;
  params.basinDepthMin = 2;
  params.basinDepthMax = 4;
  params.minMoisture = 0.0f;

  int anchorX = 0;
  int anchorZ = 0;
  int centerX = 0;
  int centerZ = 0;
  bool suitableCell = false;
  const int margin = params.radiusMax + 2;
  const int span = params.cellSize - margin * 2;
  const int candidateStep = std::max(params.radiusMax + 4, params.cellSize / 4);
  static constexpr std::array<std::pair<int, int>, 8> kCandidateOffsets = {{
      {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
  for (int cellZ = -8; cellZ <= 8 && !suitableCell; ++cellZ)
  {
    for (int cellX = -8; cellX <= 8 && !suitableCell; ++cellX)
    {
      const uint32_t placementHash =
          cutum::ColumnHash(cellX, cellZ, kSeed + 9602U);
      const int x = cellX * params.cellSize + margin +
                    static_cast<int>(placementHash %
                                     static_cast<uint32_t>(span));
      const int z = cellZ * params.cellSize + margin +
                    static_cast<int>((placementHash / 31U) %
                                     static_cast<uint32_t>(span));
      for (int attempt = 1; attempt <= 8 && !suitableCell; ++attempt)
      {
        const std::size_t offsetIndex = static_cast<std::size_t>(
            (placementHash + static_cast<uint32_t>(attempt - 1)) %
            static_cast<uint32_t>(kCandidateOffsets.size()));
        const int candidateX = std::clamp(
            x + kCandidateOffsets[offsetIndex].first * candidateStep,
            cellX * params.cellSize + margin,
            cellX * params.cellSize + params.cellSize - margin - 1);
        const int candidateZ = std::clamp(
            z + kCandidateOffsets[offsetIndex].second * candidateStep,
            cellZ * params.cellSize + margin,
            cellZ * params.cellSize + params.cellSize - margin - 1);
        const auto climate = cutum::SampleClimate(candidateX, candidateZ, kSeed);
        if (climate.moisture >= params.minMoisture &&
            climate.continentalness >= 0.40f &&
            climate.continentalness <= 0.82f && climate.erosion <= 0.92f &&
            (candidateX != x || candidateZ != z))
        {
          suitableCell = true;
          anchorX = x;
          anchorZ = z;
          centerX = candidateX;
          centerZ = candidateZ;
        }
      }
    }
  }
  FluidTest::Expect(suitableCell, kTestName,
                    "test seed has a climate-compatible fallback lake site");

  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  cutum::UBlockWorld forwardWorld;
  auto forwardCtx = MakeContext(forwardWorld, registry);
  cutum::UBlockWorld reverseWorld;
  auto reverseCtx = MakeContext(reverseWorld, registry);
  cutum::UBlockWorld submergedWorld;
  auto submergedCtx = MakeContext(submergedWorld, registry);
  for (int z = centerZ - params.radiusMax; z <= centerZ + params.radiusMax; ++z)
  {
    for (int x = centerX - params.radiusMax; x <= centerX + params.radiusMax; ++x)
    {
      const int surfaceY = (x == centerX && z == centerZ) ? 51 : 64;
      cutum::CarveLakeColumn(forwardCtx, x, z, surfaceY, kSeed, params, 48,
                             [=](int sampleX, int sampleZ)
                             {
                               return sampleX == anchorX && sampleZ == anchorZ
                                          ? 48
                                          : 64;
                             });
      cutum::CarveLakeColumn(submergedCtx, x, z, 48, kSeed, params, 48,
                             [](int, int) { return 48; });
    }
  }
  for (int z = centerZ + params.radiusMax; z >= centerZ - params.radiusMax; --z)
  {
    for (int x = centerX + params.radiusMax; x >= centerX - params.radiusMax; --x)
    {
      const int surfaceY = (x == centerX && z == centerZ) ? 51 : 64;
      cutum::CarveLakeColumn(reverseCtx, x, z, surfaceY, kSeed, params, 48,
                             [=](int sampleX, int sampleZ)
                             {
                               return sampleX == anchorX && sampleZ == anchorZ
                                          ? 48
                                          : 64;
                             });
    }
  }

  using BlockMap = std::map<std::tuple<int, int, int>, cutum::BlockId>;
  BlockMap forward;
  BlockMap reverse;
  forwardWorld.ForEachBlock([&](glm::ivec3 pos, cutum::BlockId id)
                            { forward.emplace(std::make_tuple(pos.x, pos.y, pos.z), id); });
  reverseWorld.ForEachBlock([&](glm::ivec3 pos, cutum::BlockId id)
                            { reverse.emplace(std::make_tuple(pos.x, pos.y, pos.z), id); });
  FluidTest::Expect(forward == reverse, kTestName,
                    "lake output is independent of column visitation order");
  int waterBlocks = 0;
  int highestWaterY = -1000;
  bool filledLowCenterToSurface = false;
  for (const auto &[position, id] : forward)
  {
    if (id != 9)
    {
      continue;
    }
    ++waterBlocks;
    const int y = std::get<1>(position);
    highestWaterY = std::max(highestWaterY, y);
    filledLowCenterToSurface =
        filledLowCenterToSurface ||
        (std::get<0>(position) == centerX && std::get<2>(position) == centerZ &&
         y == 52);
    FluidTest::Expect(y >= 44 && y <= 52, kTestName,
                      "lake water stays within its basin profile");
  }
  FluidTest::Expect(waterBlocks >= 5, kTestName,
                    "climate-compatible site creates a water basin");
  FluidTest::Expect(highestWaterY > 48, kTestName,
                    "lake water surface is above sea level");
  FluidTest::Expect(filledLowCenterToSurface, kTestName,
                    "basin fills low terrain up to a continuous waterline");
  int submergedWrites = 0;
  submergedWorld.ForEachBlock([&](glm::ivec3, cutum::BlockId)
                              { ++submergedWrites; });
  FluidTest::Expect(submergedWrites == 0, kTestName,
                    "ocean-floor candidates do not create inland lakes");
}

} // namespace

int main()
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  cutum::UWorldGenPack::LoadPackId("default");
  FluidTest::Expect(cutum::UWorldGenPack::Get().Pipeline.Lakes,
                    kTestName, "default pipeline enables lakes");
  FluidTest::Expect(cutum::UWorldGenPack::LakesConfig().Loaded,
                    kTestName, "lakes parameters load from the pack");

  TestValleyCarveSmoothWalls();
  TestChunkValleySeamStable();
  TestRiverFieldContinuity();
  TestLakeCarveAndChunkOrder();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
