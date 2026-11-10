#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Sampling/ColumnSample.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
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

static void TestAltitudeAdjustedMountainSnowLine()
{
  cutum::ProceduralSettings settings = MakeBalancedSettings();
  settings.Seed = 410058U;
  cutum::UDensityFieldSampler density_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      settings.Tuning.terrainRoughness, cutum::DensityFieldParams{},
      settings.Caves, settings.MinHeight);

  constexpr int sampleX = 200;
  constexpr int sampleZ = 136;
  const int surfaceY = density_sampler.SurfaceYAt(sampleX, sampleZ);
  const cutum::ClimateSample climate =
      cutum::SampleClimate(sampleX, sampleZ, settings.Seed);
  const cutum::BiomeWeightSet weights = cutum::ComputeBiomeWeights(
      sampleX, sampleZ, surfaceY, settings.SeaLevel, settings.MaxHeight,
      settings.Seed, settings.Tuning, settings.WorldGenProfileVersion);

  FluidTest::Expect(cutum::UWorldGenPack::ClimateConfig().Loaded &&
                        cutum::UWorldGenPack::ClimateConfig()
                                .TemperatureLapsePerBlock > 0.0f,
                    kTestName,
                    "climate pack configures an altitude temperature lapse");
  FluidTest::Expect(surfaceY >= settings.SeaLevel + 96 &&
                        climate.temperature >= 0.30f,
                    kTestName,
                    "snowline regression uses a high, temperate mountain sample");
  FluidTest::Expect(
      cutum::DominantBiome(weights) == cutum::BiomeId::SnowySlopes &&
          cutum::BiomeWeightFor(weights, cutum::BiomeId::SnowySlopes) >
              cutum::BiomeWeightFor(weights, cutum::BiomeId::Mountains),
      kTestName,
      "altitude cooling allows snowy slopes to win over the mountain palette");
}

static void TestRiverValleysAndRavinesAreIndependent()
{
  cutum::WorldGenPackPipeline packPipeline;
  packPipeline.Loaded = true;
  packPipeline.Valleys = true;
  packPipeline.Ravines = true;

  cutum::ProceduralSettings settings;
  settings.Tuning.useAnalyticValleys = true;
  settings.Ravines.enabled = true;
  cutum::ComposableWorldGenConfig generatorConfig;
  generatorConfig.Valleys = true;
  generatorConfig.Ravines = true;
  const cutum::WorldGenStageMask mask = cutum::BuildWorldGenStageMask(
      generatorConfig, settings, packPipeline);
  FluidTest::Expect(mask.IsEnabled(cutum::WorldGenStageId::Valleys), kTestName,
                    "analytic river valleys are enabled by their setting");
  FluidTest::Expect(mask.IsEnabled(cutum::WorldGenStageId::Ravines), kTestName,
                    "ravines remain enabled alongside analytic valleys");

  settings.Ravines.enabled = false;
  const cutum::WorldGenStageMask noRavines = cutum::BuildWorldGenStageMask(
      generatorConfig, settings, packPipeline);
  FluidTest::Expect(noRavines.IsEnabled(cutum::WorldGenStageId::Valleys) &&
                        !noRavines.IsEnabled(cutum::WorldGenStageId::Ravines),
                    kTestName,
                    "ravines keep their independent enable switch");
}

static int DumpBiomeGrid(uint32_t seed, int radius, int step)
{
  if (radius < 0 || radius > 4096 || step <= 0)
  {
    std::cerr << "radius must be in [0,4096] and step must be positive\n";
    return 2;
  }

  cutum::ProceduralSettings settings = MakeBalancedSettings();
  settings.Seed = seed;
  const cutum::UDensityFieldSampler height_sampler(
      settings.Seed, settings.SeaLevel, settings.MaxHeight,
      settings.Tuning.terrainRoughness, cutum::DensityFieldParams{},
      settings.Caves, settings.MinHeight);
  const cutum::CoarseHeightCallback coarse_height =
      [&height_sampler](int x, int z)
  { return height_sampler.SurfaceYAt(x, z); };

  std::cout << "seed,x,z,surface_y,temperature,moisture,continentalness,"
               "erosion,weirdness,ridge,dominant_biome,biome_weights\n";
  std::cout << std::setprecision(6);
  for (int z = -radius; z <= radius; z += step)
  {
    for (int x = -radius; x <= radius; x += step)
    {
      const int surface_y = height_sampler.SurfaceYAt(x, z);
      const cutum::ClimateSample climate =
          cutum::SampleClimate(x, z, settings.Seed);
      const cutum::BiomeWeightSet weights = cutum::BlendedBiomeWeights(
          x, z, surface_y, settings.SeaLevel, settings.MaxHeight,
          settings.Seed, settings.Tuning, coarse_height,
          settings.WorldGenProfileVersion);
      const cutum::BiomeKey dominant = cutum::BiomeKeyForHandle(
          cutum::DominantBiomeHandle(weights));
      std::cout << seed << ',' << x << ',' << z << ',' << surface_y << ','
                << climate.temperature << ',' << climate.moisture << ','
                << climate.continentalness << ',' << climate.erosion << ','
                << climate.weirdness << ',' << climate.ridge << ',' << dominant
                << ",\"";
      bool first = true;
      for (const cutum::BiomeBlendEntry &entry : weights.entries)
      {
        if (!first)
        {
          std::cout << '|';
        }
        first = false;
        std::cout << cutum::BiomeKeyForHandle(entry.biome) << ':'
                  << entry.weight;
      }
      std::cout << '"' << '\n';
    }
  }
  return 0;
}

static bool LoadSurfaceHeights(
    const fs::path &path, std::map<std::pair<int, int>, int> &surface_heights,
    std::string &error)
{
  std::ifstream input(path);
  if (!input)
  {
    error = "could not open saved-surface CSV: " + path.string();
    return false;
  }

  std::string line;
  if (!std::getline(input, line) || line != "x,z,surface_y")
  {
    error = "saved-surface CSV header must be x,z,surface_y";
    return false;
  }

  int line_number = 1;
  while (std::getline(input, line))
  {
    ++line_number;
    if (line.empty())
    {
      continue;
    }

    std::stringstream row(line);
    int x = 0;
    int z = 0;
    int surface_y = 0;
    char first_separator = '\0';
    char second_separator = '\0';
    if (!(row >> x >> first_separator >> z >> second_separator >> surface_y) ||
        first_separator != ',' || second_separator != ',')
    {
      error = "invalid saved-surface CSV row " + std::to_string(line_number);
      return false;
    }
    row >> std::ws;
    if (!row.eof())
    {
      error = "unexpected data in saved-surface CSV row " +
              std::to_string(line_number);
      return false;
    }
    if (!surface_heights.emplace(std::make_pair(x, z), surface_y).second)
    {
      error = "duplicate coordinate in saved-surface CSV row " +
              std::to_string(line_number);
      return false;
    }
  }

  if (surface_heights.empty())
  {
    error = "saved-surface CSV contains no heights";
    return false;
  }
  return true;
}

static int DumpBiomeGridFromSurface(
    uint32_t seed, int radius, int center_x, int center_z,
    const std::map<std::pair<int, int>, int> &saved_surface)
{
  if (radius < 0 || radius > 4096)
  {
    std::cerr << "radius must be in [0,4096]\n";
    return 2;
  }

  cutum::ProceduralSettings settings = MakeBalancedSettings();
  settings.Seed = seed;
  const int blend_radius = std::clamp(
      static_cast<int>(std::lround(settings.Tuning.biomeBlendRadius)), 0, 16);
  for (int z = center_z - radius - blend_radius;
       z <= center_z + radius + blend_radius; ++z)
  {
    for (int x = center_x - radius - blend_radius;
         x <= center_x + radius + blend_radius; ++x)
    {
      if (saved_surface.find(std::make_pair(x, z)) == saved_surface.end())
      {
        std::cerr << "saved-surface CSV is missing blend input coordinate ("
                  << x << ',' << z << ")\n";
        return 2;
      }
    }
  }
  const cutum::CoarseHeightCallback coarse_height =
      [&saved_surface](int x, int z)
  { return saved_surface.at(std::make_pair(x, z)); };

  std::cout << "seed,x,z,surface_y,temperature,moisture,continentalness,"
               "erosion,weirdness,ridge,dominant_biome,biome_weights\n";
  std::cout << std::setprecision(6);
  for (int z = center_z - radius; z <= center_z + radius; ++z)
  {
    for (int x = center_x - radius; x <= center_x + radius; ++x)
    {
      const auto found = saved_surface.find(std::make_pair(x, z));
      if (found == saved_surface.end())
      {
        std::cerr << "saved-surface CSV is missing output coordinate (" << x
                  << ',' << z << ")\n";
        return 2;
      }
      const int surface_y = found->second;
      const cutum::ClimateSample climate =
          cutum::SampleClimate(x, z, settings.Seed);
      const cutum::BiomeWeightSet weights = cutum::BlendedBiomeWeights(
          x, z, surface_y, settings.SeaLevel, settings.MaxHeight,
          settings.Seed, settings.Tuning, coarse_height,
          settings.WorldGenProfileVersion);
      const cutum::BiomeKey dominant = cutum::BiomeKeyForHandle(
          cutum::DominantBiomeHandle(weights));
      std::cout << seed << ',' << x << ',' << z << ',' << surface_y << ','
                << climate.temperature << ',' << climate.moisture << ','
                << climate.continentalness << ',' << climate.erosion << ','
                << climate.weirdness << ',' << climate.ridge << ',' << dominant
                << ",\"";
      bool first = true;
      for (const cutum::BiomeBlendEntry &entry : weights.entries)
      {
        if (!first)
        {
          std::cout << '|';
        }
        first = false;
        std::cout << cutum::BiomeKeyForHandle(entry.biome) << ':'
                  << entry.weight;
      }
      std::cout << '\"' << '\n';
    }
  }
  return 0;
}

} // namespace

int main(int argc, char **argv)
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);

  if (argc == 5 && std::string(argv[1]) == "--dump-biome-grid")
  {
    // Keep pack-loader status text out of the machine-readable CSV stream.
    std::ostringstream pack_status;
    std::streambuf *original_buffer = std::cout.rdbuf(pack_status.rdbuf());
    const bool pack_loaded = cutum::UWorldGenPack::LoadPackId("default");
    std::cout.rdbuf(original_buffer);
    if (!pack_loaded)
    {
      std::cerr << "could not load default worldgen pack\n";
      return 2;
    }
    try
    {
      const uint32_t seed = static_cast<uint32_t>(std::stoul(argv[2]));
      const int radius = std::stoi(argv[3]);
      const int step = std::stoi(argv[4]);
      return DumpBiomeGrid(seed, radius, step);
    }
    catch (const std::exception &error)
    {
      std::cerr << "invalid biome-grid arguments: " << error.what() << '\n';
      return 2;
    }
  }

  if (argc == 7 &&
      std::string(argv[1]) == "--dump-biome-grid-from-surface")
  {
    // Keep pack-loader status text out of the machine-readable CSV stream.
    std::ostringstream pack_status;
    std::streambuf *original_buffer = std::cout.rdbuf(pack_status.rdbuf());
    const bool pack_loaded = cutum::UWorldGenPack::LoadPackId("default");
    std::cout.rdbuf(original_buffer);
    if (!pack_loaded)
    {
      std::cerr << "could not load default worldgen pack\n";
      return 2;
    }
    try
    {
      const uint32_t seed = static_cast<uint32_t>(std::stoul(argv[2]));
      const int radius = std::stoi(argv[3]);
      const int center_x = std::stoi(argv[4]);
      const int center_z = std::stoi(argv[5]);
      std::map<std::pair<int, int>, int> saved_surface;
      std::string error;
      if (!LoadSurfaceHeights(argv[6], saved_surface, error))
      {
        std::cerr << error << '\n';
        return 2;
      }
      return DumpBiomeGridFromSurface(seed, radius, center_x, center_z,
                                      saved_surface);
    }
    catch (const std::exception &error)
    {
      std::cerr << "invalid saved-surface biome-grid arguments: "
                << error.what() << '\n';
      return 2;
    }
  }

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
  TestAltitudeAdjustedMountainSnowLine();
  TestRiverValleysAndRavinesAreIndependent();
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
