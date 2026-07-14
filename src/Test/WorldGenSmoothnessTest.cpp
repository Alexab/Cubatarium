#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestName = "worldgen_smoothness_test";
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

static cutum::ProceduralSettings MakeBalancedSettings()
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
  return settings;
}

static void TestRefineSurfaceYNeighborDelta()
{
  const cutum::ProceduralSettings settings = MakeBalancedSettings();
  const cutum::UOverworldHeightSampler height_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      cutum::HeightPreset::Overworld, settings.Tuning.terrainRoughness);
  const auto coarse = [&height_sampler](int x, int z)
  { return height_sampler.CoarseSurfaceYAt(x, z); };

  int max_delta = 0;
  for (int z = 0; z < kGridSize; ++z)
  {
    for (int x = 0; x < kGridSize; ++x)
    {
      const int y = cutum::RefineSurfaceYWithBiomes(
          x, z, coarse(x, z), settings, settings.Seed, settings.Tuning, coarse);
      const int y_e = cutum::RefineSurfaceYWithBiomes(
          x + 1, z, coarse(x + 1, z), settings, settings.Seed, settings.Tuning,
          coarse);
      const int y_n = cutum::RefineSurfaceYWithBiomes(
          x, z + 1, coarse(x, z + 1), settings, settings.Seed, settings.Tuning,
          coarse);
      max_delta = std::max({max_delta, std::abs(y - y_e), std::abs(y - y_n)});
    }
  }
  FluidTest::Expect(max_delta <= 5, kTestName,
                    "refined surface neighbor delta within threshold");
}

static void TestCoastRampDoesNotSpike()
{
  const cutum::ProceduralSettings settings = MakeBalancedSettings();
  const cutum::UOverworldHeightSampler height_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      cutum::HeightPreset::Overworld, settings.Tuning.terrainRoughness);
  const auto coarse = [&height_sampler](int x, int z)
  { return height_sampler.CoarseSurfaceYAt(x, z); };

  int max_step = 0;
  for (int x = -64; x < 64; ++x)
  {
    const int y0 = cutum::RefineSurfaceYWithBiomes(
        x, 0, coarse(x, 0), settings, settings.Seed, settings.Tuning, coarse);
    const int y1 = cutum::RefineSurfaceYWithBiomes(
        x + 1, 0, coarse(x + 1, 0), settings, settings.Seed, settings.Tuning,
        coarse);
    if (y0 >= settings.SeaLevel - 4 && y0 <= settings.SeaLevel + 8)
    {
      max_step = std::max(max_step, std::abs(y1 - y0));
    }
  }
  FluidTest::Expect(max_step <= 3, kTestName,
                    "coast-adjacent surface steps stay gradual");
}

} // namespace

int main()
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  cutum::UWorldGenPack::LoadPackId("default");

  TestRefineSurfaceYNeighborDelta();
  TestCoastRampDoesNotSpike();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
