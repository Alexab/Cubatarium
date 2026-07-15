#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/ColumnSample.h"
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
constexpr cutum::BlockId kGrass = 10;
constexpr cutum::BlockId kSand = 11;
constexpr cutum::BlockId kDirt = 12;
constexpr cutum::BlockId kGravel = 13;

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
  settings.FillWater = true;
  settings.Tuning.terrainRoughness = 0.62f;
  settings.Tuning.terrainErosion = 0.32f;
  settings.Tuning.erosionStrength = 0.25f;
  settings.Tuning.biomeBlendRadius = 14.0f;
  settings.Tuning.heightSmoothing = true;
  settings.Tuning.heightSmoothingRadius = 2;
  return settings;
}

static cutum::WorldGenContext MakeSurfaceRuleContext(
    cutum::UBlockWorld &world, cutum::UBlockRegistry &registry,
    const cutum::ProceduralSettings &settings)
{
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.Blocks.Grass = kGrass;
  ctx.Blocks.Sand = kSand;
  ctx.Blocks.Dirt = kDirt;
  ctx.Blocks.Gravel = kGravel;
  return ctx;
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

static void TestCoastRampMonotonic()
{
  const cutum::ProceduralSettings settings = MakeBalancedSettings();
  const cutum::UOverworldHeightSampler height_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      cutum::HeightPreset::Overworld, settings.Tuning.terrainRoughness);
  const auto coarse = [&height_sampler](int x, int z)
  { return height_sampler.CoarseSurfaceYAt(x, z); };

  for (int x = -128; x < 127; ++x)
  {
    const int coarse0 = coarse(x, 0);
    const int coarse1 = coarse(x + 1, 0);
    if (coarse1 >= coarse0 || coarse0 < settings.SeaLevel - 4 ||
        coarse0 > settings.SeaLevel + 8)
    {
      continue;
    }
    const int y0 = cutum::RefineSurfaceYWithBiomes(
        x, 0, coarse0, settings, settings.Seed, settings.Tuning, coarse);
    const int y1 = cutum::RefineSurfaceYWithBiomes(
        x + 1, 0, coarse1, settings, settings.Seed, settings.Tuning, coarse);
    FluidTest::Expect(y1 <= y0, kTestName,
                      "coast ramp does not rise when moving toward sea");
  }
}

static void TestCoastSandMatchesRamp()
{
  const cutum::ProceduralSettings settings = MakeBalancedSettings();
  const cutum::UOverworldHeightSampler height_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      cutum::HeightPreset::Overworld, settings.Tuning.terrainRoughness);
  const auto coarse = [&height_sampler](int x, int z)
  { return height_sampler.CoarseSurfaceYAt(x, z); };

  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  const cutum::WorldGenContext ctx =
      MakeSurfaceRuleContext(world, registry, settings);

  bool checked = false;
  for (int z = -96; z < 96; ++z)
  {
    for (int x = -96; x < 96; ++x)
    {
      const int surface_y = cutum::RefineSurfaceYWithBiomes(
          x, z, coarse(x, z), settings, settings.Seed, settings.Tuning, coarse);
      const float beach_strength = cutum::ComputeCoastBeachStrength(
          x, z, surface_y, settings, coarse);
      if (beach_strength <= 0.25f)
      {
        continue;
      }
      if (surface_y < settings.SeaLevel - 1 ||
          surface_y > settings.SeaLevel + 2)
      {
        continue;
      }

      cutum::ColumnSampleContext sample;
      sample.SurfaceY = surface_y;
      sample.Biomes = cutum::BlendedBiomeWeights(
          x, z, coarse(x, z), settings.SeaLevel, settings.MaxHeight,
          settings.Seed, settings.Tuning, coarse);
      sample.DominantBiome = cutum::DominantBiome(sample.Biomes);
      sample.SurfaceBiome =
          cutum::PickSurfaceBiome(x, z, sample.Biomes);
      const cutum::BiomeSurfaceRule rule =
          cutum::EvaluateSurfaceRule(x, z, sample, ctx);
      FluidTest::Expect(rule.surface == kSand || rule.surface == kGravel,
                        kTestName,
                        "strong coast ramp selects beach surface blocks");
      checked = true;
      break;
    }
    if (checked)
    {
      break;
    }
  }
  FluidTest::Expect(checked, kTestName,
                    "coast sand test found a matching coastal column");
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
  TestCoastRampMonotonic();
  TestCoastSandMatchesRamp();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
