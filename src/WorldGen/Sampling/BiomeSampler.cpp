#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Core/ColumnHash.h"
#include "WorldGen/Sampling/ColumnSample.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Features/PrefabFeatureConfig.h"
#include "World/Math/BlockTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace cutum
{

namespace
{

BlockId ResolveSlotBlock(const WorldGenContext &ctx, const std::string &slot,
                         BlockId fallback)
{
  if (slot == "grass")
  {
    return ctx.Blocks.Grass;
  }
  if (slot == "dirt")
  {
    return ctx.Blocks.Dirt;
  }
  if (slot == "sand")
  {
    return ctx.Blocks.Sand != BLOCK_AIR ? ctx.Blocks.Sand : fallback;
  }
  if (slot == "sandstone")
  {
    return ctx.Blocks.Sandstone != BLOCK_AIR ? ctx.Blocks.Sandstone : fallback;
  }
  if (slot == "gravel")
  {
    return ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : fallback;
  }
  if (slot == "snow")
  {
    return ctx.Blocks.Snow != BLOCK_AIR ? ctx.Blocks.Snow : fallback;
  }
  if (slot == "stone")
  {
    return ctx.Blocks.Stone;
  }
  return fallback;
}

BiomeSurfaceRule ApplyPackPalette(BiomeId biome, BiomeSurfaceRule rule,
                                  const WorldGenContext &ctx)
{
  const BiomePackDefinition *packDef =
      UWorldGenPack::BiomeDefinitionFor(BiomeIdToString(biome));
  if (!packDef)
  {
    return rule;
  }
  if (!packDef->SurfaceSlot.empty())
  {
    rule.surface = ResolveSlotBlock(ctx, packDef->SurfaceSlot, rule.surface);
  }
  if (!packDef->SubsurfaceSlot.empty())
  {
    rule.subsurface =
        ResolveSlotBlock(ctx, packDef->SubsurfaceSlot, rule.subsurface);
  }
  return rule;
}

uint32_t BiomePickHash(int x, int z, uint32_t seed)
{
  return ColumnHash(x, z, seed);
}

} // namespace

int BiomeIndex(BiomeId biome)
{
  switch (biome)
  {
  case BiomeId::Forest:
    return 1;
  case BiomeId::Desert:
    return 2;
  case BiomeId::Hills:
    return 3;
  case BiomeId::Tundra:
    return 4;
  case BiomeId::Savanna:
    return 5;
  case BiomeId::Foothills:
    return 6;
  case BiomeId::Scrubland:
    return 7;
  case BiomeId::ColdSteppe:
    return 8;
  case BiomeId::Plains:
  default:
    return 0;
  }
}

BiomeId BiomeFromIndex(int index)
{
  switch (index)
  {
  case 1:
    return BiomeId::Forest;
  case 2:
    return BiomeId::Desert;
  case 3:
    return BiomeId::Hills;
  case 4:
    return BiomeId::Tundra;
  case 5:
    return BiomeId::Savanna;
  case 6:
    return BiomeId::Foothills;
  case 7:
    return BiomeId::Scrubland;
  case 8:
    return BiomeId::ColdSteppe;
  case 0:
  default:
    return BiomeId::Plains;
  }
}

float AxisFitness(float value, float minV, float maxV)
{
  if (value < minV || value > maxV)
  {
    return 0.0f;
  }
  const float mid = (minV + maxV) * 0.5f;
  const float half = std::max(0.05f, (maxV - minV) * 0.5f);
  return 1.0f - std::fabs(value - mid) / half;
}

float BiomeClimateFitness(BiomeId biome, const ClimateSample &climate,
                          float localHeightNorm)
{
  switch (biome)
  {
  case BiomeId::Desert:
    return AxisFitness(climate.temperature, 0.65f, 1.0f) *
           AxisFitness(climate.moisture, 0.0f, 0.35f);
  case BiomeId::Tundra:
    return AxisFitness(climate.temperature, 0.0f, 0.25f) *
           AxisFitness(climate.moisture, 0.0f, 0.6f);
  case BiomeId::Hills:
    return std::max(0.0f, (localHeightNorm - 0.55f) * 2.5f) *
           AxisFitness(climate.erosion, 0.0f, 0.45f);
  case BiomeId::Forest:
    return AxisFitness(climate.moisture, 0.55f, 1.0f) *
           std::max(0.0f, 1.0f - localHeightNorm);
  case BiomeId::Savanna:
    return AxisFitness(climate.temperature, 0.55f, 0.85f) *
           AxisFitness(climate.moisture, 0.25f, 0.5f);
  case BiomeId::Foothills:
    return AxisFitness(climate.erosion, 0.35f, 0.75f) *
           std::max(0.2f, localHeightNorm);
  case BiomeId::Scrubland:
    return AxisFitness(climate.temperature, 0.45f, 0.75f) *
           AxisFitness(climate.moisture, 0.3f, 0.55f);
  case BiomeId::ColdSteppe:
    return AxisFitness(climate.temperature, 0.2f, 0.45f) *
           AxisFitness(climate.moisture, 0.2f, 0.5f);
  case BiomeId::Plains:
  default:
    return AxisFitness(climate.continentalness, 0.25f, 0.8f) *
           AxisFitness(climate.erosion, 0.35f, 0.75f);
  }
}

float BiomeAffinity(BiomeId biome, float temperature, float moisture,
                    float localHeightNorm)
{
  ClimateSample climate;
  climate.temperature = temperature;
  climate.moisture = moisture;
  climate.continentalness = 0.5f;
  climate.erosion = 0.5f;
  return BiomeClimateFitness(biome, climate, localHeightNorm);
}

int LocalCoarseHeightRange(int x, int z, const CoarseHeightCallback &getCoarseY)
{
  if (!getCoarseY)
  {
    return 99;
  }
  const int center = getCoarseY(x, z);
  int minY = center;
  int maxY = center;
  constexpr int kDx[] = {-1, 1, 0, 0};
  constexpr int kDz[] = {0, 0, -1, 1};
  for (int i = 0; i < 4; ++i)
  {
    const int ny = getCoarseY(x + kDx[i], z + kDz[i]);
    minY = std::min(minY, ny);
    maxY = std::max(maxY, ny);
  }
  return maxY - minY;
}

float FlatTerrainJitterScale(int localCoarseRange)
{
  if (localCoarseRange <= 2)
  {
    return 0.0f;
  }
  if (localCoarseRange <= 4)
  {
    return 0.35f;
  }
  return 1.0f;
}

int FillMicroDepressions(int x, int z, int y, int localCoarseRange,
                         const CoarseHeightCallback &getCoarseY)
{
  if (!getCoarseY || localCoarseRange > 3)
  {
    return y;
  }
  int minNeighbor = getCoarseY(x, z);
  int maxNeighbor = minNeighbor;
  for (int dz = -1; dz <= 1; ++dz)
  {
    for (int dx = -1; dx <= 1; ++dx)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      const int ny = getCoarseY(x + dx, z + dz);
      minNeighbor = std::min(minNeighbor, ny);
      maxNeighbor = std::max(maxNeighbor, ny);
    }
  }
  if (y < minNeighbor)
  {
    return minNeighbor;
  }
  if (localCoarseRange <= 2 && y > maxNeighbor + 1)
  {
    return maxNeighbor + 1;
  }
  return y;
}

int ClampToFlatPlateau(int x, int z, int y, int localCoarseRange,
                       const CoarseHeightCallback &getCoarseY)
{
  if (!getCoarseY || localCoarseRange > 2)
  {
    return y;
  }
  int minY = getCoarseY(x, z);
  int maxY = minY;
  for (int dz = -1; dz <= 1; ++dz)
  {
    for (int dx = -1; dx <= 1; ++dx)
    {
      const int ny = getCoarseY(x + dx, z + dz);
      minY = std::min(minY, ny);
      maxY = std::max(maxY, ny);
    }
  }
  if (y < minY)
  {
    return minY;
  }
  return y;
}

int ApplyRiverCarve(int x, int z, int surfaceY, const BiomeWeightSet &weights,
                    const ProceduralSettings &settings, int localCoarseRange)
{
  const float forest = weights.weights[BiomeIndex(BiomeId::Forest)];
  const float plains = weights.weights[BiomeIndex(BiomeId::Plains)];
  const float humid = forest + plains;
  const ClimateSample climate = SampleClimate(x, z, settings.Seed);
  const float pv = PeaksAndValleys(climate.weirdness);
  const float valleyBoost = pv < 0.35f ? 1.25f : 1.0f;
  const float riverNoise =
      NormalizedFBM2D(static_cast<float>(x) * 0.008f,
                      static_cast<float>(z) * 0.008f, settings.Seed + 8801, 2,
                      0.5f, 2.0f);
  const float riverWidth =
      settings.Tuning.riverWidth * (1.0f - climate.erosion * 0.5f);
  const float river =
      Smoothstep(0.50f, 0.72f, (riverNoise + 1.0f) * 0.5f) * riverWidth *
      valleyBoost;
  if (river <= 0.0f)
  {
    return surfaceY;
  }
  if (localCoarseRange <= 3)
  {
    return surfaceY;
  }
  const float riverStrength = river;
  if (humid < 0.35f)
  {
    const int dryDepth = static_cast<int>(riverStrength * 6.0f);
    return std::max(1, surfaceY - dryDepth);
  }
  const int targetY = std::max(1, settings.SeaLevel - 2);
  const int depth = static_cast<int>(
      std::floor(riverStrength * static_cast<float>(surfaceY - targetY)));
  return std::max(targetY, surfaceY - depth);
}

int ApplyCoastShelf(int x, int z, int surfaceY, const ProceduralSettings &settings,
                    const CoarseHeightCallback &getCoarseY)
{
  const int sea = settings.SeaLevel;
  if (surfaceY < sea - 1 || surfaceY > sea + 5)
  {
    return surfaceY;
  }
  int wetNeighbors = 0;
  for (int dz = -4; dz <= 4; ++dz)
  {
    for (int dx = -4; dx <= 4; ++dx)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      int ny = sea;
      if (getCoarseY)
      {
        ny = getCoarseY(x + dx, z + dz);
      }
      else
      {
        const float nh =
            FBM2D(static_cast<float>(x + dx) * 0.01f,
                  static_cast<float>(z + dz) * 0.01f, settings.Seed, 4, 0.5f,
                  2.0f);
        ny = sea + static_cast<int>((nh - 0.5f) * 6.0f);
      }
      if (ny <= sea - 1)
      {
        ++wetNeighbors;
      }
    }
  }
  if (wetNeighbors >= 10 && surfaceY > sea)
  {
    return std::max(sea, surfaceY - (wetNeighbors >= 18 ? 2 : 1));
  }
  if (wetNeighbors <= 2 && surfaceY == sea)
  {
    return sea + 1;
  }
  return surfaceY;
}

float SampleCoarseHeightGradient(int x, int z,
                               const CoarseHeightCallback &getCoarseY)
{
  if (!getCoarseY)
  {
    return 0.0f;
  }
  const int y = getCoarseY(x, z);
  const int yx = getCoarseY(x + 1, z);
  const int yz = getCoarseY(x, z + 1);
  return static_cast<float>(std::abs(y - yx) + std::abs(y - yz));
}

int ApplyErosionLite(int x, int z, int surfaceY, uint32_t seed,
                     const ProceduralSettings &settings,
                     const CoarseHeightCallback &getCoarseY)
{
  const float erosion = std::clamp(settings.Tuning.terrainErosion, 0.0f, 1.0f);
  if (erosion <= 0.0f)
  {
    return surfaceY;
  }
  const float gradient = getCoarseY
                             ? SampleCoarseHeightGradient(x, z, getCoarseY)
                             : 0.0f;
  if (gradient > 3.5f + erosion * 2.0f)
  {
    return std::max(1, surfaceY - 1);
  }
  return surfaceY;
}

BiomeHeightProfile BiomeHeightProfileFor(BiomeId biome)
{
  if (const BiomeHeightProfile *packProfile =
          UWorldGenPack::HeightProfileFor(BiomeIdToString(biome)))
  {
    return *packProfile;
  }
  BiomeHeightProfile profile;
  switch (biome)
  {
  case BiomeId::Desert:
    profile.baseOffsetBlocks = -1.f;
    profile.amplitudeMultiplier = 0.55f;
    profile.volatilityMultiplier = 0.35f;
    break;
  case BiomeId::Hills:
    profile.baseOffsetBlocks = 1.f;
    profile.amplitudeMultiplier = 1.05f;
    profile.volatilityMultiplier = 0.50f;
    break;
  case BiomeId::Savanna:
    profile.baseOffsetBlocks = 0.f;
    profile.amplitudeMultiplier = 0.9f;
    profile.volatilityMultiplier = 0.50f;
    break;
  case BiomeId::Foothills:
    profile.baseOffsetBlocks = 1.f;
    profile.amplitudeMultiplier = 1.0f;
    profile.volatilityMultiplier = 0.50f;
    break;
  case BiomeId::Scrubland:
    profile.baseOffsetBlocks = -1.f;
    profile.amplitudeMultiplier = 0.75f;
    profile.volatilityMultiplier = 0.40f;
    break;
  case BiomeId::ColdSteppe:
    profile.baseOffsetBlocks = 0.f;
    profile.amplitudeMultiplier = 0.8f;
    profile.volatilityMultiplier = 0.45f;
    break;
  case BiomeId::Forest:
    profile.baseOffsetBlocks = 0.f;
    profile.amplitudeMultiplier = 1.0f;
    profile.volatilityMultiplier = 0.42f;
    break;
  case BiomeId::Tundra:
    profile.baseOffsetBlocks = 1.f;
    profile.amplitudeMultiplier = 0.7f;
    profile.volatilityMultiplier = 0.50f;
    break;
  case BiomeId::Plains:
  default:
    profile.baseOffsetBlocks = 0.f;
    profile.amplitudeMultiplier = 0.85f;
    profile.volatilityMultiplier = 0.35f;
    break;
  }
  return profile;
}

int ApplyBiomeHeightProfile(int coarseY, int seaLevel, int maxHeight,
                            const BiomeHeightProfile &profile)
{
  const float delta = static_cast<float>(coarseY - seaLevel);
  const float scaled =
      delta * profile.amplitudeMultiplier + profile.baseOffsetBlocks;
  int y = seaLevel + static_cast<int>(std::lround(scaled));
  return std::clamp(y, 1, maxHeight);
}

void ComputeBiomeClimate(int x, int z, uint32_t seed, float &temperature,
                         float &moisture)
{
  const ClimateSample climate = SampleClimate(x, z, seed);
  temperature = climate.temperature;
  moisture = climate.moisture;
}

BiomeId ClassifyBiome(float temperature, float moisture, float localHeightNorm)
{
  if (temperature > 0.65f && moisture < 0.35f)
  {
    return BiomeId::Desert;
  }
  if (temperature < 0.25f && moisture < 0.6f)
  {
    return BiomeId::Tundra;
  }
  if (temperature >= 0.55f && temperature <= 0.85f && moisture >= 0.25f &&
      moisture <= 0.5f)
  {
    return BiomeId::Savanna;
  }
  if (temperature >= 0.2f && temperature <= 0.45f && moisture >= 0.2f &&
      moisture <= 0.5f)
  {
    return BiomeId::ColdSteppe;
  }
  if (temperature >= 0.45f && temperature <= 0.75f && moisture >= 0.3f &&
      moisture <= 0.55f)
  {
    return BiomeId::Scrubland;
  }
  if (localHeightNorm > 0.85f)
  {
    return BiomeId::Hills;
  }
  if (localHeightNorm > 0.68f && localHeightNorm <= 0.85f)
  {
    return BiomeId::Foothills;
  }
  if (moisture > 0.55f)
  {
    return BiomeId::Forest;
  }
  return BiomeId::Plains;
}

float BiomeTuningWeight(BiomeId biome, const WorldGenTuning &tuning)
{
  switch (biome)
  {
  case BiomeId::Forest:
    return tuning.biomeForestWeight;
  case BiomeId::Desert:
    return tuning.biomeDesertWeight;
  case BiomeId::Hills:
    return tuning.biomeHillsWeight;
  case BiomeId::Tundra:
    return tuning.biomeTundraWeight;
  case BiomeId::Savanna:
    return tuning.biomeSavannaWeight;
  case BiomeId::Foothills:
    return tuning.biomeFoothillsWeight;
  case BiomeId::Scrubland:
    return tuning.biomeScrublandWeight;
  case BiomeId::ColdSteppe:
    return tuning.biomeColdSteppeWeight;
  case BiomeId::Plains:
  default:
    return tuning.biomePlainsWeight;
  }
}

float SpawnBiomeWeightMul(BiomeId biome, int x, int z)
{
  const float dist =
      std::hypot(static_cast<float>(x), static_cast<float>(z));
  if (dist > 64.f)
  {
    return 1.f;
  }
  if (biome == BiomeId::Plains || biome == BiomeId::Foothills)
  {
    return 1.5f;
  }
  if (biome == BiomeId::Hills)
  {
    return 0.65f;
  }
  return 1.f;
}

BiomeWeightSet ComputeBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning)
{
  const ClimateSample climate = SampleClimate(x, z, seed);
  const float macro_h01 =
      std::clamp(OverworldMacroHeight01(x, z, seed), 0.0f, 1.0f);
  const float height_norm = std::clamp(
      static_cast<float>(coarseY - seaLevel) /
          std::max(1.f, static_cast<float>(maxHeight - seaLevel)),
      0.f, 1.f);
  const float localHeightNorm = macro_h01 * 0.7f + height_norm * 0.3f;

  BiomeWeightSet result;
  float total = 0.0f;
  for (int i = 0; i < kBiomeCount; ++i)
  {
    const BiomeId biome = BiomeFromIndex(i);
    const float tuningWeight = BiomeTuningWeight(biome, tuning);
    if (tuningWeight <= 0.0f)
    {
      result.weights[i] = 0.0f;
      continue;
    }
    float w = BiomeClimateFitness(biome, climate, localHeightNorm) * tuningWeight;
    w *= SpawnBiomeWeightMul(biome, x, z);
    if (w < 0.01f)
    {
      w = BiomeAffinity(biome, climate.temperature, climate.moisture,
                        localHeightNorm) *
          tuningWeight * 0.25f;
    }
    result.weights[i] = w;
    total += w;
  }
  if (total > 0.0f)
  {
    for (float &w : result.weights)
    {
      w /= total;
    }
  }
  else
  {
    result.weights[BiomeIndex(ClassifyBiome(climate.temperature, climate.moisture,
                                            localHeightNorm))] = 1.0f;
  }
  return result;
}

BiomeWeightSet BlendedBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning,
                                   const CoarseHeightCallback &getCoarseY)
{
  const int radius =
      std::clamp(static_cast<int>(std::lround(tuning.biomeBlendRadius)), 0, 16);
  if (radius <= 0)
  {
    return ComputeBiomeWeights(x, z, coarseY, seaLevel, maxHeight, seed,
                               tuning);
  }

  BiomeWeightSet blended;
  float totalKernel = 0.0f;
  const int step = std::max(1, radius / 3);
  for (int dx = -radius; dx <= radius; dx += step)
  {
    for (int dz = -radius; dz <= radius; dz += step)
    {
      const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
      if (dist > static_cast<float>(radius))
      {
        continue;
      }
      const float kernel = 1.0f - dist / static_cast<float>(radius + 1);
      const int sampleY =
          getCoarseY ? getCoarseY(x + dx, z + dz) : coarseY;
      const BiomeWeightSet local = ComputeBiomeWeights(
          x + dx, z + dz, sampleY, seaLevel, maxHeight, seed, tuning);
      for (int i = 0; i < kBiomeCount; ++i)
      {
        blended.weights[i] += local.weights[i] * kernel;
      }
      totalKernel += kernel;
    }
  }
  if (totalKernel > 0.0f)
  {
    for (float &w : blended.weights)
    {
      w /= totalKernel;
    }
  }
  return blended;
}

BiomeId DominantBiome(const BiomeWeightSet &weights)
{
  int best = 0;
  float bestW = -1.0f;
  for (int i = 0; i < kBiomeCount; ++i)
  {
    if (weights.weights[i] > bestW)
    {
      bestW = weights.weights[i];
      best = i;
    }
  }
  return BiomeFromIndex(best);
}

BiomeId PickSurfaceBiome(int x, int z, const BiomeWeightSet &weights)
{
  const BiomeId dominant = DominantBiome(weights);
  const float dominantWeight = weights.weights[BiomeIndex(dominant)];
  if (dominantWeight >= 0.62f)
  {
    return dominant;
  }

  int first = -1;
  int second = -1;
  float firstW = -1.0f;
  float secondW = -1.0f;
  for (int i = 0; i < kBiomeCount; ++i)
  {
    if (weights.weights[i] > firstW)
    {
      second = first;
      secondW = firstW;
      first = i;
      firstW = weights.weights[i];
    }
    else if (weights.weights[i] > secondW)
    {
      second = i;
      secondW = weights.weights[i];
    }
  }
  if (second < 0 || secondW < 0.15f || std::fabs(firstW - secondW) < 0.12f)
  {
    return dominant;
  }

  const uint32_t pick = BiomePickHash(x, z, 7711) % 1000;
  const float threshold =
      firstW / std::max(0.001f, firstW + secondW);
  return pick < static_cast<uint32_t>(threshold * 1000.0f)
             ? BiomeFromIndex(first)
             : BiomeFromIndex(second);
}

SubBiomeId SubBiomeFor(int x, int z, BiomeId biome, uint32_t seed)
{
  const float n =
      FBM2D(static_cast<float>(x) * 0.01f, static_cast<float>(z) * 0.01f,
            seed + 5500 + BiomeIndex(biome) * 17, 2, 0.5f, 2.0f);
  const float t = (n + 1.0f) * 0.5f;
  const BiomePackDefinition *pack_def =
      UWorldGenPack::BiomeDefinitionFor(BiomeIdToString(biome));
  if (!pack_def || pack_def->SubBiomes.empty())
  {
    return SubBiomeId::Default;
  }

  struct ThresholdEntry
  {
    float Threshold;
    SubBiomeId Id;
  };
  std::vector<ThresholdEntry> entries;
  entries.reserve(pack_def->SubBiomes.size());
  for (const auto &entry : pack_def->SubBiomes)
  {
    if (entry.second.NoiseThreshold < 0.0f)
    {
      continue;
    }
    entries.push_back(
        {entry.second.NoiseThreshold, SubBiomeIdFromString(entry.first)});
  }
  if (entries.empty())
  {
    return SubBiomeId::Default;
  }
  std::sort(entries.begin(), entries.end(),
            [](const ThresholdEntry &a, const ThresholdEntry &b)
            { return a.Threshold < b.Threshold; });

  if (entries.size() == 1)
  {
    return t < entries.front().Threshold ? entries.front().Id
                                         : SubBiomeId::Default;
  }
  if (entries.size() == 2)
  {
    return t < entries.front().Threshold ? entries.front().Id : entries.back().Id;
  }

  const float low = entries.front().Threshold;
  const float high = entries.back().Threshold;
  if (t < low)
  {
    return entries.front().Id;
  }
  if (t < high)
  {
    return entries[entries.size() - 2].Id;
  }
  return entries.back().Id;
}

int BiomeBlendedBaseY(int x, int z, int coarseY, const BiomeWeightSet &weights,
                      const ProceduralSettings &settings)
{
  (void)x;
  (void)z;
  float offset = 0.0f;
  for (int i = 0; i < kBiomeCount; ++i)
  {
    const BiomeHeightProfile profile = BiomeHeightProfileFor(BiomeFromIndex(i));
    offset += weights.weights[i] * profile.baseOffsetBlocks;
  }
  const int y =
      coarseY + static_cast<int>(std::floor(offset + 0.5f));
  return std::clamp(y, 1, settings.MaxHeight);
}

int SmoothBlendedSurfaceY(int x, int z, int baseY,
                          const BiomeWeightSet &weights,
                          const ProceduralSettings &settings, uint32_t seed,
                          const WorldGenTuning &tuning,
                          const CoarseHeightCallback &getCoarseY)
{
  if (!tuning.heightSmoothing || tuning.heightSmoothingRadius <= 0 || !getCoarseY)
  {
    return baseY;
  }
  const int radius = std::clamp(tuning.heightSmoothingRadius, 1, 2);
  float sum = 0.0f;
  float wsum = 0.0f;
  for (int dz = -radius; dz <= radius; ++dz)
  {
    for (int dx = -radius; dx <= radius; ++dx)
    {
      const int cheb = std::max(std::abs(dx), std::abs(dz));
      const float w = cheb == 0 ? 4.0f : cheb == 1 ? 2.0f : 1.0f;
      sum += w * static_cast<float>(getCoarseY(x + dx, z + dz));
      wsum += w;
    }
  }
  const int smoothedCoarse =
      static_cast<int>(std::floor(sum / std::max(1.0f, wsum) + 0.5f));
  return BiomeBlendedBaseY(x, z, smoothedCoarse, weights, settings);
}

int RefineSurfaceYWithBiomes(int x, int z, int coarseY,
                             const ProceduralSettings &settings, uint32_t seed,
                             const WorldGenTuning &tuning,
                             const CoarseHeightCallback &getCoarseY)
{
  const BiomeWeightSet weights = BlendedBiomeWeights(
      x, z, coarseY, settings.SeaLevel, settings.MaxHeight, seed, tuning,
      getCoarseY);
  float volatilityJitter = 0.0f;
  for (int i = 0; i < kBiomeCount; ++i)
  {
    const BiomeHeightProfile profile = BiomeHeightProfileFor(BiomeFromIndex(i));
    volatilityJitter += weights.weights[i] * profile.volatilityMultiplier;
  }

  int y = BiomeBlendedBaseY(x, z, coarseY, weights, settings);
  y = SmoothBlendedSurfaceY(x, z, y, weights, settings, seed, tuning, getCoarseY);

  const int localCoarseRange = LocalCoarseHeightRange(x, z, getCoarseY);

  const PackHeightConfig &heightPack = UWorldGenPack::HeightConfig();
  const float jitterScale =
      heightPack.Loaded ? heightPack.JitterScale : 0.03f;
  const float jitterAmp =
      heightPack.Loaded ? heightPack.JitterAmplitude : tuning.jitterAmplitude;
  const float erosionDamp =
      heightPack.Loaded ? heightPack.JitterErosionDamp : 0.85f;
  const ClimateSample climate = SampleClimate(x, z, seed);
  const float jitter =
      NormalizedFBM2D(static_cast<float>(x) * jitterScale,
                      static_cast<float>(z) * jitterScale, seed + 77, 2, 0.5f,
                      2.0f) *
      volatilityJitter * jitterAmp * FlatTerrainJitterScale(localCoarseRange) *
      (1.0f - climate.erosion * erosionDamp);
  y = static_cast<int>(std::floor(static_cast<float>(y) + jitter + 0.5f));

  y = ApplyRiverCarve(x, z, y, weights, settings, localCoarseRange);
  y = ApplyCoastShelf(x, z, y, settings, getCoarseY);
  y = FillMicroDepressions(x, z, y, localCoarseRange, getCoarseY);
  y = ClampToFlatPlateau(x, z, y, localCoarseRange, getCoarseY);
  return std::clamp(y, 1, settings.MaxHeight);
}

BiomeId PickWeightedBiome(int x, int z, uint32_t seed, float temperature,
                          float moisture, float localHeightNorm,
                          const WorldGenTuning &tuning)
{
  const BiomeId primary = ClassifyBiome(temperature, moisture, localHeightNorm);
  constexpr std::array<BiomeId, kBiomeCount> kBiomes = {
      BiomeId::Plains,    BiomeId::Forest,   BiomeId::Desert, BiomeId::Hills,
      BiomeId::Tundra,    BiomeId::Savanna,  BiomeId::Foothills,
      BiomeId::Scrubland, BiomeId::ColdSteppe};

  float total = 0.0f;
  std::array<float, kBiomeCount> weights{};
  for (size_t i = 0; i < kBiomes.size(); ++i)
  {
    const float tuningWeight = BiomeTuningWeight(kBiomes[i], tuning);
    if (tuningWeight <= 0.0f)
    {
      weights[i] = 0.0f;
      continue;
    }
    float w = BiomeAffinity(kBiomes[i], temperature, moisture, localHeightNorm) *
              tuningWeight;
    if (kBiomes[i] == primary)
    {
      w *= 2.0f;
    }
    weights[i] = w;
    total += w;
  }

  if (total <= 0.0f)
  {
    return primary;
  }

  const uint32_t pick =
      BiomePickHash(x, z, seed + 3301) %
      static_cast<uint32_t>(std::max(1.0f, total * 1000.0f));
  float cursor = static_cast<float>(pick) / 1000.0f;
  for (size_t i = 0; i < kBiomes.size(); ++i)
  {
    if (weights[i] <= 0.0f)
    {
      continue;
    }
    cursor -= weights[i];
    if (cursor <= 0.0f)
    {
      return kBiomes[i];
    }
  }
  return primary;
}

UBiomeSampler::UBiomeSampler(uint32_t Seed, const WorldGenTuning &tuning)
    : Seed(Seed), Tuning(tuning)
{
}

void UBiomeSampler::SetCoarseHeightCallback(CoarseHeightCallback callback)
{
  CoarseHeightFn = std::move(callback);
}

BiomeId UBiomeSampler::At(int x, int z, int surfaceY, int SeaLevel,
                          int MaxHeight) const
{
  return DominantBiome(WeightsAt(x, z, surfaceY, SeaLevel, MaxHeight));
}

BiomeWeightSet UBiomeSampler::WeightsAt(int x, int z, int surfaceY,
                                        int SeaLevel, int MaxHeight) const
{
  return BlendedBiomeWeights(x, z, surfaceY, SeaLevel, MaxHeight, Seed, Tuning,
                             CoarseHeightFn);
}

int UBiomeSampler::RefineSurfaceY(int x, int z, int coarseY,
                                  const ProceduralSettings &settings) const
{
  return RefineSurfaceYWithBiomes(x, z, coarseY, settings, Seed, Tuning,
                                  CoarseHeightFn);
}

BiomeSurfaceRule UBiomeSampler::SurfaceRule(BiomeId biome,
                                            const WorldGenContext &ctx) const
{
  BiomeSurfaceRule rule;
  switch (biome)
  {
  case BiomeId::Desert:
    rule.surface = ctx.Blocks.Sand;
    rule.subsurface = ctx.Blocks.Sandstone != BLOCK_AIR ? ctx.Blocks.Sandstone : ctx.Blocks.Sand;
    break;
  case BiomeId::Tundra:
    rule.surface = ctx.Blocks.Snow != BLOCK_AIR ? ctx.Blocks.Snow : ctx.Blocks.Stone;
    rule.subsurface = ctx.Blocks.Dirt;
    break;
  case BiomeId::Hills:
    rule.surface = ctx.Blocks.Grass;
    rule.subsurface = ctx.Blocks.Dirt;
    break;
  case BiomeId::Savanna:
    rule.surface = ctx.Blocks.Grass;
    rule.subsurface = ctx.Blocks.Dirt;
    break;
  case BiomeId::Foothills:
    rule.surface = ctx.Blocks.Grass;
    rule.subsurface = ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : ctx.Blocks.Dirt;
    break;
  case BiomeId::Scrubland:
    rule.surface = ctx.Blocks.Sand != BLOCK_AIR ? ctx.Blocks.Sand : ctx.Blocks.Grass;
    rule.subsurface = ctx.Blocks.Dirt;
    break;
  case BiomeId::ColdSteppe:
    rule.surface = ctx.Blocks.Dirt;
    rule.subsurface = ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : ctx.Blocks.Dirt;
    break;
  case BiomeId::Forest:
  case BiomeId::Plains:
  default:
    rule.surface = ctx.Blocks.Grass;
    rule.subsurface = ctx.Blocks.Dirt;
    break;
  }
  return ApplyPackPalette(biome, rule, ctx);
}

BiomeSurfaceRule EvaluateSurfaceRule(int x, int z,
                                     const ColumnSampleContext &sample,
                                     const WorldGenContext &ctx)
{
  if (sample.SpawnSurfaceOverride)
  {
    BiomeSurfaceRule rule;
    rule.surface = ctx.Blocks.Grass;
    rule.subsurface = ctx.Blocks.Dirt;
    return rule;
  }

  const BiomeId pick = sample.SurfaceBiome;
  const BiomeId dominant = sample.DominantBiome;
  const BiomeWeightSet &weights = sample.Biomes;
  const int surface_y = sample.SurfaceY;

  UBiomeSampler biome_lookup(ctx.Settings.Seed, ctx.Settings.Tuning);
  BiomeSurfaceRule rule = biome_lookup.SurfaceRule(pick, ctx);

  if (pick == BiomeId::Hills || dominant == BiomeId::Hills)
  {
    if (sample.MacroHeight01 < 0.88f)
    {
      rule.surface = ctx.Blocks.Grass;
      rule.subsurface = ctx.Blocks.Dirt;
    }
  }

  const float second_weight = [&]() {
    float best = 0.0f;
    for (int i = 0; i < kBiomeCount; ++i)
    {
      if (BiomeFromIndex(i) == dominant)
      {
        continue;
      }
      best = std::max(best, weights.weights[i]);
    }
    return best;
  }();
  if (second_weight > 0.15f && weights.weights[BiomeIndex(dominant)] < 0.85f)
  {
    const uint32_t mix = BiomePickHash(x, z, 8803) % 100;
    if (mix < 35)
    {
      rule.subsurface =
          ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : rule.subsurface;
    }
    if ((dominant == BiomeId::Desert && pick == BiomeId::Forest) ||
        (dominant == BiomeId::Forest && pick == BiomeId::Desert))
    {
      if (mix < 30)
      {
        rule.surface = ctx.Blocks.Sand != BLOCK_AIR ? ctx.Blocks.Sand : rule.surface;
      }
    }
  }

  if (ctx.Settings.FillWater && surface_y < ctx.Settings.SeaLevel)
  {
    if (rule.surface == ctx.Blocks.Grass)
    {
      rule.surface = ctx.Blocks.Sand != BLOCK_AIR ? ctx.Blocks.Sand : ctx.Blocks.Stone;
    }
    if (rule.subsurface == ctx.Blocks.Grass || rule.subsurface == ctx.Blocks.Dirt)
    {
      rule.subsurface =
          ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel
                                  : (ctx.Blocks.Sand != BLOCK_AIR ? ctx.Blocks.Sand
                                                           : ctx.Blocks.Stone);
    }
  }

  if (ctx.Settings.FillWater && surface_y <= ctx.Settings.SeaLevel + 2 &&
      surface_y >= ctx.Settings.SeaLevel - 1)
  {
    const float land =
        weights.weights[BiomeIndex(BiomeId::Plains)] +
        weights.weights[BiomeIndex(BiomeId::Forest)] +
        weights.weights[BiomeIndex(BiomeId::Hills)];
    if (land > 0.2f)
    {
      rule.surface = ctx.Blocks.Sand != BLOCK_AIR ? ctx.Blocks.Sand : rule.surface;
      rule.subsurface =
          ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : rule.subsurface;
    }
  }

  const SubBiomeId sub =
      SubBiomeFor(x, z, pick, ctx.Settings.Seed);
  const BiomePackDefinition *pack_def =
      UWorldGenPack::BiomeDefinitionFor(BiomeIdToString(pick));
  if (pack_def)
  {
    const auto sub_it = pack_def->SubBiomes.find(SubBiomeIdToString(sub));
    if (sub_it != pack_def->SubBiomes.end() &&
        !sub_it->second.SubsurfaceSlot.empty())
    {
      rule.subsurface = ResolveSlotBlock(ctx, sub_it->second.SubsurfaceSlot,
                                         rule.subsurface);
    }
  }
  else if (pick == BiomeId::Desert && sub == SubBiomeId::Dunes)
  {
    rule.subsurface = ctx.Blocks.Sandstone != BLOCK_AIR ? ctx.Blocks.Sandstone : ctx.Blocks.Sand;
  }
  else if (pick == BiomeId::Forest && sub == SubBiomeId::DenseForest)
  {
    rule.subsurface = ctx.Blocks.Dirt;
  }

  const float erosion =
      std::clamp(ctx.Settings.Tuning.terrainErosion, 0.0f, 1.0f);
  const float spawn_dist =
      std::hypot(static_cast<float>(x), static_cast<float>(z));
  if (erosion > 0.2f && sample.MacroHeight01 >= 0.75f && spawn_dist > 48.f &&
      sample.SurfaceGradient > 8.0f + erosion * 4.0f)
  {
    rule.surface = ctx.Blocks.Stone;
    rule.subsurface = ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : ctx.Blocks.Stone;
  }

  return rule;
}

BiomeSurfaceRule UBiomeSampler::BlendedSurfaceRule(
    int x, int z, const BiomeWeightSet &weights, const WorldGenContext &ctx,
    int surfaceY) const
{
  ColumnSampleContext sample;
  sample.SurfaceY = surfaceY;
  sample.Biomes = weights;
  sample.DominantBiome = DominantBiome(weights);
  sample.SurfaceBiome = PickSurfaceBiome(x, z, weights);
  sample.MacroHeight01 =
      std::clamp(OverworldMacroHeight01(x, z, ctx.Settings.Seed), 0.0f, 1.0f);
  sample.SurfaceGradient =
      CoarseHeightFn ? SampleCoarseHeightGradient(x, z, CoarseHeightFn) : 0.0f;
  sample.SpawnSurfaceOverride =
      ShouldSpawnSurfaceOverride(x, z, ctx.Settings);
  return EvaluateSurfaceRule(x, z, sample, ctx);
}

} // namespace cutum
