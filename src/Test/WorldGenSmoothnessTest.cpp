#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Sampling/ColumnSample.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <vector>

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
  settings.SeaLevel = 63;
  settings.MaxHeight = 319;
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
  int surfaceMin = settings.MaxHeight;
  int surfaceMax = 0;
  int coastBandCount = 0;
  int strongBeachCount = 0;
  float maximumBeachStrength = 0.0f;
  for (int z = -96; z < 96; ++z)
  {
    for (int x = -96; x < 96; ++x)
    {
      const int surface_y = cutum::RefineSurfaceYWithBiomes(
          x, z, coarse(x, z), settings, settings.Seed, settings.Tuning, coarse);
      surfaceMin = std::min(surfaceMin, surface_y);
      surfaceMax = std::max(surfaceMax, surface_y);
      const float beach_strength = cutum::ComputeCoastBeachStrength(
          x, z, surface_y, settings, coarse);
      maximumBeachStrength = std::max(maximumBeachStrength, beach_strength);
      if (surface_y >= settings.SeaLevel - 2 &&
          surface_y <= settings.SeaLevel + 6)
      {
        ++coastBandCount;
      }
      if (beach_strength > 0.25f)
      {
        ++strongBeachCount;
      }
      if (beach_strength <= 0.25f)
      {
        continue;
      }
      cutum::ColumnSampleContext sample;
      sample.SurfaceY = surface_y;
      sample.CoastFactor = beach_strength;
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
  if (!checked)
  {
    std::cerr << kTestName << ": coast diagnostics surface=" << surfaceMin
              << ".." << surfaceMax << " sea=" << settings.SeaLevel
              << " band_columns=" << coastBandCount
              << " strong_columns=" << strongBeachCount
              << " max_strength=" << maximumBeachStrength << std::endl;
  }
  FluidTest::Expect(checked, kTestName,
                    "coast sand test found a matching coastal column");
}

static void TestExpandedOceanDepthProfile()
{
  constexpr int oldSeaLevel = 48;
  constexpr int oldMaxHeight = 128;
  constexpr int oldMinHeight = 0;
  constexpr int newSeaLevel = 63;
  constexpr int newMinHeight = -64;
  constexpr int newMaxHeight = 319;
  const int oldDeepOceanFloor = static_cast<int>(std::lround(
      cutum::ContinentalHeightBlocks(0.0f, oldSeaLevel, oldMaxHeight,
                                     oldMinHeight)));
  const int newDeepOceanFloor = static_cast<int>(std::lround(
      cutum::ContinentalHeightBlocks(0.0f, newSeaLevel, newMaxHeight,
                                     newMinHeight)));
  const float oldShelfFloor = cutum::ContinentalHeightBlocks(
      0.3f, oldSeaLevel, oldMaxHeight, oldMinHeight);
  const float newShelfFloor = cutum::ContinentalHeightBlocks(
      0.3f, newSeaLevel, newMaxHeight, newMinHeight);
  const float newCoastFloor = cutum::ContinentalHeightBlocks(
      0.42f, newSeaLevel, newMaxHeight, newMinHeight);
  const float newLandFloor = cutum::ContinentalHeightBlocks(
      0.95f, newSeaLevel, newMaxHeight, newMinHeight);
  const float oldBoundsLandFloor = cutum::ContinentalHeightBlocks(
      0.95f, newSeaLevel, newMaxHeight, oldMinHeight);
  const int oldDepth = oldSeaLevel - oldDeepOceanFloor;
  const int newDepth = newSeaLevel - newDeepOceanFloor;
  const float newShelfDepth = static_cast<float>(newSeaLevel) - newShelfFloor;
  const float newCoastDepth = static_cast<float>(newSeaLevel) - newCoastFloor;
  const cutum::ProceduralSettings defaultSettings;
  if (newShelfDepth < 55.0f)
  {
    const auto &height = cutum::UWorldGenPack::HeightConfig();
    std::cerr << kTestName << ": ocean profile diagnostic schema="
              << height.SchemaVersion << " deep="
              << height.Ocean.DeepOceanDepthRatio << " shelf="
              << height.Ocean.ShelfDepthRatio << " coast="
              << height.Ocean.CoastDepthRatio << " deep_end="
              << height.Ocean.DeepOceanEnd << " shelf_end="
              << height.Ocean.ShelfEnd << " coast_end="
              << height.Ocean.CoastEnd << " deep_floor="
              << newDeepOceanFloor << " shelf_floor=" << newShelfFloor
              << " coast_floor=" << newCoastFloor << std::endl;
  }

  FluidTest::Expect(oldDepth > 16, kTestName,
                    "expanded profile deepens oceans at the old vertical bounds");
  FluidTest::Expect(oldShelfFloor < static_cast<float>(oldSeaLevel), kTestName,
                    "expanded profile places ocean shelves below sea level");
  FluidTest::Expect(newDeepOceanFloor < 0 &&
                        newDeepOceanFloor > newMinHeight,
                    kTestName,
                    "deep ocean uses signed vertical space above the world floor");
  FluidTest::Expect(newDepth >= 90 && newDepth > oldDepth * 2, kTestName,
                    "expanded profile provides substantially deeper oceans");
  FluidTest::Expect(newShelfDepth >= 55.0f, kTestName,
                    "ocean profile keeps broad shelves substantially below sea level");
  FluidTest::Expect(newCoastDepth >= 40.0f, kTestName,
                    "coastal ocean floors are deep enough for a broad shelf");
  FluidTest::Expect(std::fabs(newLandFloor - oldBoundsLandFloor) < 0.01f,
                    kTestName,
                    "ocean depth scaling does not change land height scaling");
  FluidTest::Expect(defaultSettings.SeaLevel == newSeaLevel &&
                        defaultSettings.MinHeight == -64 &&
                        defaultSettings.MaxHeight == newMaxHeight,
                    kTestName,
                    "default world settings use the full modern vertical range");
  FluidTest::Expect(defaultSettings.BedrockTopY == -64, kTestName,
                    "bedrock floor is anchored to the new world minimum");
  const cutum::WorldVerticalProfile vertical =
      defaultSettings.GetVerticalProfile();
  FluidTest::Expect(vertical.IsValid() && vertical.MinY == -64 &&
                        vertical.MaxY == 319 && vertical.SeaLevelY == 63 &&
                        vertical.BedrockTopY == -64 &&
                        vertical.DatumY == cutum::kWorldDatumY &&
                        vertical.MinChunkY(16) == -4 &&
                        vertical.MaxChunkY(16) == 19 &&
                        vertical.ContainsY(-64) && vertical.ContainsY(319) &&
                        !vertical.ContainsY(-65) && vertical.ClampY(-100) == -64,
                    kTestName,
                    "vertical profile centralizes bounds, datum, sea level, and chunk limits");
}

static void TestNegativeHeightSamplingAndCompactBounds()
{
  cutum::ProceduralSettings settings = MakeBalancedSettings();
  FluidTest::Expect(settings.MinHeight == -64, kTestName,
                    "overworld profile retains the -64 lower world bound");

  const cutum::UOverworldHeightSampler height_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      cutum::HeightPreset::Overworld, settings.Tuning.terrainRoughness,
      settings.MinHeight);
  const int surface_y = height_sampler.SurfaceYAt(-19, 37);
  FluidTest::Expect(surface_y > settings.MinHeight &&
                        surface_y <= settings.MaxHeight,
                    kTestName,
                    "height sampler returns a surface inside world bounds");

  cutum::UDensityFieldSampler density_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      settings.Tuning.terrainRoughness, cutum::DensityFieldParams{},
      settings.Caves, settings.MinHeight);
  FluidTest::Expect(density_sampler.SampleTerrainDensity(-19, -64, 37) > 0.0f,
                    kTestName,
                    "stone terrain density extends through the lower world");
  FluidTest::Expect(density_sampler.SurfaceYAt(-19, 37) > settings.MinHeight,
                    kTestName,
                    "density surface scan includes negative Y samples");

  cutum::ProceduralSettings compact;
  compact.Generator = cutum::ProceduralGenerator::Flat;
  compact.MinHeight = 0;
  compact.MaxHeight = 15;
  FluidTest::Expect(compact.MinHeight == 0 && compact.MaxHeight == 15,
                    kTestName,
                    "compact generator keeps its independent 0..15 bounds");
  FluidTest::Expect(compact.GetVerticalProfile().MinChunkY(16) == 0 &&
                        compact.GetVerticalProfile().MaxChunkY(16) == 0,
                    kTestName,
                    "compact profile maps its bounds into the correct chunk band");
}

static void TestRegionalDensityTerrainDistribution()
{
  constexpr int sampleRadius = 1024;
  constexpr int sampleStep = 64;
  constexpr std::array<uint32_t, 3> seeds = {410056U, 410057U, 410058U};
  std::vector<int> heights;
  heights.reserve(seeds.size() * 33 * 33);
  int high_peaks_with_mountain_biome_weight = 0;
  int high_peaks_with_mountain_surface_biome = 0;
  int high_peak_samples = 0;
  float max_high_peak_mountain_biome_weight = 0.0f;

  for (uint32_t seed : seeds)
  {
    cutum::ProceduralSettings settings = MakeBalancedSettings();
    settings.Seed = seed;
    settings.Tuning.terrainRoughness = 0.58f;
    cutum::UDensityFieldSampler density_sampler(
        settings.Seed, settings.SeaLevel, settings.MaxHeight,
        settings.Tuning.terrainRoughness, cutum::DensityFieldParams{},
        settings.Caves, settings.MinHeight);

    std::vector<int> seed_heights;
    seed_heights.reserve(33 * 33);
    int peak_x = 0;
    int peak_z = 0;
    int peak_y = settings.MinHeight;
    float min_continentalness = 1.0f;
    float max_continentalness = 0.0f;
    float min_ridge = 1.0f;
    float max_ridge = 0.0f;
    float min_erosion = 1.0f;
    float max_erosion = 0.0f;
    float max_mountain_weight = 0.0f;
    for (int z = -sampleRadius; z <= sampleRadius; z += sampleStep)
    {
      for (int x = -sampleRadius; x <= sampleRadius; x += sampleStep)
      {
        const int y = density_sampler.SurfaceYAt(x, z);
        if (y > peak_y)
        {
          peak_x = x;
          peak_z = z;
          peak_y = y;
        }
        const cutum::ClimateSample climate =
            cutum::SampleClimate(x, z, settings.Seed);
        if (y >= 150)
        {
          std::cout << kTestName << ": high_peak_sample seed=" << seed
                    << " x=" << x << " z=" << z << " y=" << y
                    << std::endl;
          ++high_peak_samples;
          const cutum::BiomeWeightSet biome_weights =
              cutum::ComputeBiomeWeights(x, z, y, settings.SeaLevel,
                                         settings.MaxHeight, settings.Seed,
                                         settings.Tuning);
          const float mountain_biome_weight =
              cutum::BiomeWeightFor(biome_weights,
                                    cutum::BiomeId::Mountains) +
              cutum::BiomeWeightFor(biome_weights,
                                    cutum::BiomeId::SnowySlopes) +
              cutum::BiomeWeightFor(biome_weights,
                                    cutum::BiomeId::JaggedPeaks);
          max_high_peak_mountain_biome_weight = std::max(
              max_high_peak_mountain_biome_weight, mountain_biome_weight);
          if (mountain_biome_weight >= 0.20f)
          {
            ++high_peaks_with_mountain_biome_weight;
          }
          const cutum::BiomeId surface_biome =
              cutum::DominantBiome(biome_weights);
          if (surface_biome == cutum::BiomeId::Mountains ||
              surface_biome == cutum::BiomeId::SnowySlopes ||
              surface_biome == cutum::BiomeId::JaggedPeaks)
          {
            ++high_peaks_with_mountain_surface_biome;
          }
        }
        const float mountain_weight =
            cutum::MountainClimateWeight(climate);
        min_continentalness =
            std::min(min_continentalness, climate.continentalness);
        max_continentalness =
            std::max(max_continentalness, climate.continentalness);
        min_ridge = std::min(min_ridge, climate.ridge);
        max_ridge = std::max(max_ridge, climate.ridge);
        min_erosion = std::min(min_erosion, climate.erosion);
        max_erosion = std::max(max_erosion, climate.erosion);
        max_mountain_weight = std::max(max_mountain_weight, mountain_weight);
        seed_heights.push_back(y);
        heights.push_back(y);
      }
    }
    if (seed == 410058U)
    {
      constexpr int sampleX = 200;
      constexpr int sampleZ = 136;
      const int sampleY = density_sampler.SurfaceYAt(sampleX, sampleZ);
      const cutum::ClimateSample climate =
          cutum::SampleClimate(sampleX, sampleZ, settings.Seed);
      const cutum::BiomeWeightSet weights = cutum::ComputeBiomeWeights(
          sampleX, sampleZ, sampleY, settings.SeaLevel, settings.MaxHeight,
          settings.Seed, settings.Tuning, settings.WorldGenProfileVersion);
      std::cout << kTestName << ": mountain_palette_sample seed=" << seed
                << " pos=(" << sampleX << "," << sampleZ << ") y=" << sampleY
                << " temperature=" << climate.temperature
                << " climate_mountain=" << cutum::MountainClimateWeight(climate)
                << " forest_weight="
                << cutum::BiomeWeightFor(weights, cutum::BiomeId::Forest)
                << " mountain_weight="
                << cutum::BiomeWeightFor(weights, cutum::BiomeId::Mountains)
                << " snowy_weight="
                << cutum::BiomeWeightFor(weights, cutum::BiomeId::SnowySlopes)
                << " peak_weight="
                << cutum::BiomeWeightFor(weights, cutum::BiomeId::JaggedPeaks)
                << " dominant="
                << cutum::BiomeIdToString(cutum::DominantBiome(weights))
                << std::endl;
    }
    const auto [minimum, maximum] =
        std::minmax_element(seed_heights.begin(), seed_heights.end());
    const int minimum_y = *minimum;
    const int maximum_y = *maximum;
    std::sort(seed_heights.begin(), seed_heights.end());
    const std::size_t p95_index = (seed_heights.size() - 1) * 95 / 100;
    const int above_mountain_threshold = static_cast<int>(std::count_if(
        seed_heights.begin(), seed_heights.end(), [&](int y)
        { return y >= settings.SeaLevel + 57; }));
    const int high_peak_samples = static_cast<int>(std::count_if(
        seed_heights.begin(), seed_heights.end(),
        [](int y) { return y >= 150; }));

    std::cout << kTestName << ": density terrain seed=" << seed
              << " min=" << minimum_y << " p95=" << seed_heights[p95_index]
              << " max=" << maximum_y << " peak_at=(" << peak_x << ","
              << peak_z << ")"
              << " mountain_samples=" << above_mountain_threshold << "/"
              << seed_heights.size() << " high_peak_samples="
              << high_peak_samples << " continentalness="
              << min_continentalness << ".." << max_continentalness
              << " ridge=" << min_ridge << ".." << max_ridge
              << " erosion=" << min_erosion << ".." << max_erosion
              << " max_mountain_weight=" << max_mountain_weight << std::endl;
    FluidTest::Expect(minimum_y >= settings.MinHeight &&
                          maximum_y <= settings.MaxHeight,
                      kTestName,
                      "regional density terrain stays within world bounds");
  }

  const auto maximum = std::max_element(heights.begin(), heights.end());
  FluidTest::Expect(maximum != heights.end() && *maximum >= 150, kTestName,
                    "regional density terrain includes high mountain peaks");
  std::cout << kTestName << ": high peaks with mountain biome blend >=0.20="
            << high_peaks_with_mountain_biome_weight
            << "/" << high_peak_samples << " dominant mountain biome="
            << high_peaks_with_mountain_surface_biome << "/"
            << high_peak_samples << " max blend="
            << max_high_peak_mountain_biome_weight
            << std::endl;
  FluidTest::Expect(high_peak_samples > 0 &&
                        high_peaks_with_mountain_biome_weight * 10 >=
                            high_peak_samples * 9 &&
                        high_peaks_with_mountain_surface_biome * 4 >=
                            high_peak_samples * 3,
                    kTestName,
                    "high peaks carry mountain weight and mostly select a mountain biome");
}

} // namespace

int main()
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  cutum::UWorldGenPack::LoadPackId("default");
  FluidTest::Expect(cutum::UWorldGenPack::HeightConfig().SchemaVersion == 2,
                    kTestName,
                    "default height pack uses signed ocean-depth semantics");

  TestExpandedOceanDepthProfile();
  TestRefineSurfaceYNeighborDelta();
  TestCoastRampDoesNotSpike();
  TestCoastRampMonotonic();
  TestCoastSandMatchesRamp();
  TestNegativeHeightSamplingAndCompactBounds();
  TestRegionalDensityTerrainDistribution();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
