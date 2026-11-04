#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/CoarseHeightCache.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestName = "density_refine_parity_test";
constexpr uint32_t kSeed = 12345;
constexpr int kGridSize = 32;

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

static cutum::ProceduralSettings MakeSettings()
{
  cutum::ProceduralSettings settings;
  settings.Seed = kSeed;
  settings.SeaLevel = 48;
  settings.MaxHeight = 128;
  settings.Tuning.terrainRoughness = 0.62f;
  settings.Tuning.terrainErosion = 0.32f;
  settings.Tuning.erosionStrength = 0.25f;
  settings.Tuning.biomeBlendRadius = 14.0f;
  settings.Tuning.heightSmoothing = true;
  settings.Tuning.heightSmoothingRadius = 2;
  settings.Tuning.useDensityRefineParity = true;
  return settings;
}

static void TestDensityRefineParityMatchesHeightmap()
{
  const cutum::ProceduralSettings settings = MakeSettings();
  cutum::UOverworldHeightSampler height_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      cutum::HeightPreset::Overworld, settings.Tuning.terrainRoughness);
  cutum::DensityFieldParams density_params;
  density_params.cavesInDensity = false;
  cutum::UDensityFieldSampler density_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      settings.Tuning.terrainRoughness, density_params, settings.Caves);
  cutum::UBiomeSampler biome_sampler(settings.Seed, settings.Tuning);
  biome_sampler.SetCoarseHeightCallback(
      [&height_sampler](int hx, int hz)
      { return height_sampler.CoarseSurfaceYAt(hx, hz); });

  double sum_abs_delta = 0.0;
  int count = 0;
  for (int z = 0; z < kGridSize; ++z)
  {
    for (int x = 0; x < kGridSize; ++x)
    {
      const int coarse_y = height_sampler.CoarseSurfaceYAt(x, z);
      const int height_y =
          biome_sampler.RefineSurfaceY(x, z, coarse_y, settings);
      const int density_raw = density_sampler.SurfaceYAt(x, z);
      const int density_y =
          biome_sampler.RefineSurfaceY(x, z, density_raw, settings);
      sum_abs_delta += static_cast<double>(std::abs(height_y - density_y));
      ++count;
    }
  }
  const double mean_abs_delta = sum_abs_delta / static_cast<double>(count);
  FluidTest::Expect(mean_abs_delta < 3.0, kTestName,
                    "density refine parity tracks heightmap surface within 3 blocks");
}

static void TestChunkCoarseCache()
{
  cutum::UCoarseHeightCache cache(16, 32, 48, 48);
  int computations = 0;
  const auto compute = [&computations](int x, int z)
  {
    ++computations;
    return x * 3 + z;
  };
  FluidTest::Expect(cache.GetOrCompute(16, 32, compute) == 80, kTestName,
                    "coarse cache initial value");
  FluidTest::Expect(cache.GetOrCompute(16, 32, compute) == 80, kTestName,
                    "coarse cache repeated value");
  FluidTest::Expect(computations == 1, kTestName,
                    "coarse cache avoids repeated computation");
  FluidTest::Expect(cache.GetOrCompute(0, 0, compute) == 0, kTestName,
                    "coarse cache out-of-range fallback");
  FluidTest::Expect(computations == 2, kTestName,
                    "out-of-range fallback computes directly");
  FluidTest::Expect(cache.GetStats().Hits == 1 && cache.GetStats().Misses == 1 &&
                        cache.GetStats().OutOfRange == 1,
                    kTestName, "coarse cache request counters");
  cache.Reset(0, 0, 4, 4);
  FluidTest::Expect(cache.GetStats().Hits == 0 && cache.GetStats().Misses == 0 &&
                        cache.GetStats().OutOfRange == 0,
                    kTestName, "coarse cache scope resets counters");
}

} // namespace

int main()
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  cutum::UWorldGenPack::LoadPackId("default");

  TestDensityRefineParityMatchesHeightmap();
  TestChunkCoarseCache();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
