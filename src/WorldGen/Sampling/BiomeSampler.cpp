#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/BiomeSource.h"
#include "WorldGen/Core/ColumnHash.h"
#include "WorldGen/Sampling/ColumnSample.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
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
  if (const WorldGenSlotSpec *spec = UWorldGenRefs::GetSlot(slot))
  {
    for (const std::string &blockName : spec->BlockNames)
    {
      const BlockId id = ResolvePackScatterBlockId(
          ctx.Registry, ctx.WorldgenOwnerPackId, blockName);
      if (id != BLOCK_AIR)
      {
        return id;
      }
    }
  }
  const BlockId resolved =
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId, slot);
  if (resolved != BLOCK_AIR)
  {
    return resolved;
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

BiomeSurfaceRule ApplyPackPalette(const BiomeKey &biomeKey,
                                  BiomeSurfaceRule rule,
                                  const WorldGenContext &ctx)
{
  const BiomePackDefinition *packDef =
      UWorldGenPack::BiomeDefinitionFor(biomeKey);
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

const BiomePackDefinition *BiomeDefinitionForHandle(BiomeHandle handle)
{
  const WorldGenPack &pack = UWorldGenPack::Get();
  if (!pack.BiomeRegistry)
  {
    return nullptr;
  }
  const BiomeKey *key = pack.BiomeRegistry->Key(handle);
  if (!key)
  {
    return nullptr;
  }
  const auto it = pack.Biomes.find(*key);
  return it == pack.Biomes.end() ? nullptr : &it->second;
}

float ClimateRangeFitness(float value, const BiomeClimateRange &range)
{
  if (!range.Enabled)
  {
    return 1.0f;
  }
  if (value < range.Min || value > range.Max)
  {
    return 0.0f;
  }
  const float middle = (range.Min + range.Max) * 0.5f;
  const float half = std::max(0.001f, (range.Max - range.Min) * 0.5f);
  return std::max(0.0f, 1.0f - std::fabs(value - middle) / half);
}

float CustomBiomeClimateFitness(const BiomeClimateProfile &profile,
                                const ClimateSample &climate)
{
  if (!profile.Enabled)
  {
    return 0.0f;
  }
  return ClimateRangeFitness(climate.temperature, profile.Temperature) *
         ClimateRangeFitness(climate.moisture, profile.Moisture) *
         ClimateRangeFitness(climate.continentalness,
                             profile.Continentalness) *
         ClimateRangeFitness(climate.erosion, profile.Erosion) *
         ClimateRangeFitness(climate.weirdness, profile.Weirdness) *
         ClimateRangeFitness(climate.ridge, profile.Ridge) * profile.Weight;
}

BiomeHeightProfile BiomeHeightProfileForHandle(BiomeHandle handle)
{
  if (const BiomePackDefinition *definition = BiomeDefinitionForHandle(handle))
  {
    return definition->Height;
  }
  const BiomeId builtin = BiomeIdFromString(BiomeKeyForHandle(handle));
  if (builtin != BiomeId::Invalid)
  {
    return BiomeHeightProfileFor(builtin);
  }
  return {};
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
  case BiomeId::Ocean:
    return 9;
  case BiomeId::Beach:
    return 10;
  case BiomeId::Mountains:
    return 11;
  case BiomeId::SnowySlopes:
    return 12;
  case BiomeId::JaggedPeaks:
    return 13;
  case BiomeId::LushCaves:
  case BiomeId::DripstoneCaves:
  case BiomeId::DeepDark:
    return 0;
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
  case 9:
    return BiomeId::Ocean;
  case 10:
    return BiomeId::Beach;
  case 11:
    return BiomeId::Mountains;
  case 12:
    return BiomeId::SnowySlopes;
  case 13:
    return BiomeId::JaggedPeaks;
  case 0:
  default:
    return BiomeId::Plains;
  }
}

BiomeHandle BiomeHandleForId(BiomeId biome)
{
  if (biome == BiomeId::Invalid)
  {
    return {};
  }
  const WorldGenPack &pack = UWorldGenPack::Get();
  return pack.BiomeRegistry
             ? pack.BiomeRegistry->Find(BiomeIdToString(biome))
             : BiomeHandle{};
}

float BiomeWeightFor(const BiomeWeightSet &weights, BiomeId biome)
{
  return weights.Weight(BiomeHandleForId(biome));
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
                          float localHeightNorm, float oceanFactor = 0.0f,
                          float beachFactor = 0.0f,
                          float mountainFactor = 0.0f,
                          float snowySlopeFactor = 0.0f,
                          float jaggedPeakFactor = 0.0f)
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
  case BiomeId::Ocean:
    return oceanFactor;
  case BiomeId::Beach:
    return beachFactor;
  case BiomeId::Mountains:
    return mountainFactor;
  case BiomeId::SnowySlopes:
    return snowySlopeFactor;
  case BiomeId::JaggedPeaks:
    return jaggedPeakFactor;
  case BiomeId::LushCaves:
  case BiomeId::DripstoneCaves:
  case BiomeId::DeepDark:
    return 0.0f;
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

float FlatTerrainJitterScale(int localCoarseRange, float climateErosion)
{
  float scale = 1.0f;
  if (localCoarseRange <= 2)
  {
    scale = climateErosion > 0.6f ? 0.50f : 0.70f;
  }
  else if (localCoarseRange <= 4)
  {
    scale = 0.55f;
  }
  if (climateErosion < 0.4f)
  {
    scale = std::max(scale, 0.80f);
  }
  return scale;
}

int FillMicroDepressions(int x, int z, int y, int localCoarseRange,
                         const CoarseHeightCallback &getCoarseY,
                         float climateErosion)
{
  if (!getCoarseY || localCoarseRange > 3 || climateErosion < 0.70f)
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
  if (localCoarseRange <= 1 && climateErosion > 0.85f && y > maxNeighbor + 2)
  {
    return maxNeighbor + 2;
  }
  return y;
}

int ClampToFlatPlateau(int x, int z, int y, int localCoarseRange,
                       const CoarseHeightCallback &getCoarseY,
                       float climateErosion)
{
  if (!getCoarseY || localCoarseRange > 2 || climateErosion <= 0.90f)
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
  const float forest = BiomeWeightFor(weights, BiomeId::Forest);
  const float plains = BiomeWeightFor(weights, BiomeId::Plains);
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
  if (localCoarseRange <= 2)
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

float ComputeCoastBeachStrength(int x, int z, int surface_y,
                                const ProceduralSettings &settings,
                                const CoarseHeightCallback &getCoarseY)
{
  const int sea = settings.SeaLevel;
  if (surface_y < sea - 2 || surface_y > sea + 6)
  {
    return 0.0f;
  }
  int wet_neighbors = 0;
  constexpr int scan_radius = 4;
  for (int dz = -scan_radius; dz <= scan_radius; ++dz)
  {
    for (int dx = -scan_radius; dx <= scan_radius; ++dx)
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
        ++wet_neighbors;
      }
    }
  }

  constexpr int max_neighbors = (scan_radius * 2 + 1) * (scan_radius * 2 + 1) - 1;
  const float wet_factor =
      Smoothstep(0.0f, static_cast<float>(max_neighbors),
                 static_cast<float>(wet_neighbors));
  const float coast_proximity =
      1.0f - Smoothstep(static_cast<float>(sea + 6), static_cast<float>(sea - 2),
                        static_cast<float>(surface_y));
  return wet_factor * coast_proximity;
}

int ApplyCoastShelf(int x, int z, int surfaceY, const ProceduralSettings &settings,
                    const CoarseHeightCallback &getCoarseY)
{
  const float beach_strength =
      ComputeCoastBeachStrength(x, z, surfaceY, settings, getCoarseY);
  if (beach_strength <= 0.001f)
  {
    return surfaceY;
  }

  const int sea = settings.SeaLevel;
  const float beach_height = 2.0f + beach_strength * 2.0f;
  const float target_y =
      static_cast<float>(sea) + beach_strength * beach_height;
  const float blend = beach_strength * 0.65f;
  const float blended =
      static_cast<float>(surfaceY) * (1.0f - blend) + target_y * blend;
  return std::clamp(static_cast<int>(std::floor(blended + 0.5f)), 1,
                    settings.MaxHeight);
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
                     const CoarseHeightCallback &getCoarseY,
                     const BiomeWeightSet &weights, float macro_height01)
{
  (void)seed;
  if (BiomeWeightFor(weights, BiomeId::Hills) > 0.6f &&
      macro_height01 > 0.75f)
  {
    return surfaceY;
  }
  const float erosion = std::clamp(settings.Tuning.terrainErosion, 0.0f, 1.0f);
  const float erosion_strength =
      std::clamp(settings.Tuning.erosionStrength, 0.0f, 1.0f);
  const float blend_strength =
      std::clamp(erosion * (0.35f + erosion_strength * 0.65f), 0.0f, 1.0f);
  if (blend_strength <= 0.0f || !getCoarseY)
  {
    return surfaceY;
  }
  const float gradient = SampleCoarseHeightGradient(x, z, getCoarseY);
  if (gradient <= 1.5f + erosion * 2.0f)
  {
    return surfaceY;
  }

  float sum = static_cast<float>(surfaceY);
  float weight_sum = 1.0f;
  for (int dz = -1; dz <= 1; ++dz)
  {
    for (int dx = -1; dx <= 1; ++dx)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      const int neighbor_y = getCoarseY(x + dx, z + dz);
      const int cheb = std::max(std::abs(dx), std::abs(dz));
      const float w = cheb == 1 ? 2.0f : 1.0f;
      sum += w * static_cast<float>(neighbor_y);
      weight_sum += w;
    }
  }
  const int neighbor_avg =
      static_cast<int>(std::floor(sum / std::max(1.0f, weight_sum) + 0.5f));
  const float blended =
      static_cast<float>(surfaceY) * (1.0f - blend_strength) +
      static_cast<float>(neighbor_avg) * blend_strength;
  return std::max(1, static_cast<int>(std::floor(blended + 0.5f)));
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
  case BiomeId::Ocean:
    profile.baseOffsetBlocks = -16.f;
    profile.amplitudeMultiplier = 0.9f;
    profile.volatilityMultiplier = 0.4f;
    break;
  case BiomeId::Beach:
    profile.baseOffsetBlocks = 0.f;
    profile.amplitudeMultiplier = 0.65f;
    profile.volatilityMultiplier = 0.2f;
    break;
  case BiomeId::Mountains:
    profile.baseOffsetBlocks = 9.f;
    profile.amplitudeMultiplier = 1.45f;
    profile.volatilityMultiplier = 0.95f;
    break;
  case BiomeId::SnowySlopes:
    profile.baseOffsetBlocks = 7.f;
    profile.amplitudeMultiplier = 1.35f;
    profile.volatilityMultiplier = 0.85f;
    break;
  case BiomeId::JaggedPeaks:
    profile.baseOffsetBlocks = 15.f;
    profile.amplitudeMultiplier = 1.8f;
    profile.volatilityMultiplier = 1.2f;
    break;
  case BiomeId::LushCaves:
  case BiomeId::DripstoneCaves:
  case BiomeId::DeepDark:
    profile.baseOffsetBlocks = 0.f;
    profile.amplitudeMultiplier = 1.f;
    profile.volatilityMultiplier = 0.f;
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
  case BiomeId::Ocean:
  case BiomeId::Beach:
  case BiomeId::Mountains:
  case BiomeId::SnowySlopes:
  case BiomeId::JaggedPeaks:
    return 1.0f;
  case BiomeId::LushCaves:
  case BiomeId::DripstoneCaves:
  case BiomeId::DeepDark:
    return 0.0f;
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
                                   const WorldGenTuning &tuning,
                                   int worldgenProfileVersion)
{
  const ClimateSample climate = SampleClimate(x, z, seed);
  const float macro_h01 =
      std::clamp(OverworldMacroHeight01(x, z, seed), 0.0f, 1.0f);
  const float height_norm = std::clamp(
      static_cast<float>(coarseY - seaLevel) /
          std::max(1.f, static_cast<float>(maxHeight - seaLevel)),
      0.f, 1.f);
  const float localHeightNorm = macro_h01 * 0.7f + height_norm * 0.3f;
  const float seaDelta = static_cast<float>(coarseY - seaLevel);
  const float coastalBand =
      1.0f - Smoothstep(3.0f, 12.0f, std::fabs(seaDelta));
  const float oceanFactor =
      (1.0f - Smoothstep(0.34f, 0.55f, climate.continentalness)) *
      (seaDelta < 0.0f ? 1.0f : 0.0f);
  const float beachFactor =
      coastalBand * Smoothstep(0.36f, 0.49f, climate.continentalness) *
      (1.0f - Smoothstep(0.58f, 0.70f, climate.continentalness));
  const float mountainFactor =
      Smoothstep(0.48f, 0.78f, localHeightNorm) *
      (1.0f - Smoothstep(0.58f, 0.86f, climate.erosion)) *
      Smoothstep(0.48f, 0.76f, climate.ridge);
  const float snowySlopeFactor =
      mountainFactor * (1.0f - Smoothstep(0.22f, 0.52f, climate.temperature));
  const float jaggedPeakFactor =
      mountainFactor * Smoothstep(0.55f, 0.84f,
                                  PeaksAndValleys(climate.weirdness)) *
      Smoothstep(0.67f, 0.93f, localHeightNorm);

  BiomeWeightSet result;
  float total = 0.0f;
  for (int i = 0; i < kBuiltinSurfaceBiomeCount; ++i)
  {
    const BiomeId biome = BiomeFromIndex(i);
    const float tuningWeight = BiomeTuningWeight(biome, tuning);
    if (tuningWeight <= 0.0f)
    {
      continue;
    }
    float w = BiomeClimateFitness(
                  biome, climate, localHeightNorm, oceanFactor, beachFactor,
                  mountainFactor, snowySlopeFactor, jaggedPeakFactor) *
              tuningWeight;
    w *= SpawnBiomeWeightMul(biome, x, z);
    if (w < 0.01f)
    {
      w = BiomeAffinity(biome, climate.temperature, climate.moisture,
                        localHeightNorm) *
          tuningWeight * 0.25f;
    }
    result.AddWeight(BiomeHandleForId(biome), w);
    total += w;
  }
  const WorldGenPack &pack = UWorldGenPack::Get();
  if (worldgenProfileVersion >= 2 && pack.BiomeRegistry)
  {
    for (const BiomeHandle handle : pack.SurfaceClimateBiomes)
    {
      const BiomeKey *key = pack.BiomeRegistry->Key(handle);
      if (!key)
      {
        continue;
      }
      const auto definition = pack.Biomes.find(*key);
      if (definition == pack.Biomes.end())
      {
        continue;
      }
      const float weight =
          CustomBiomeClimateFitness(definition->second.Climate, climate);
      if (handle.IsValid() && weight > 0.0f)
      {
        result.AddWeight(handle, weight);
        total += weight;
      }
    }
  }
  if (total > 0.0f)
  {
    for (BiomeBlendEntry &entry : result.entries)
    {
      entry.weight /= total;
    }
  }
  else
  {
    result.SetWeight(
        BiomeHandleForId(ClassifyBiome(climate.temperature, climate.moisture,
                                       localHeightNorm)),
        1.0f);
  }
  result.Normalize();
  return result;
}

BiomeWeightSet BlendedBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning,
                                   const CoarseHeightCallback &getCoarseY,
                                   int worldgenProfileVersion)
{
  const int radius =
      std::clamp(static_cast<int>(std::lround(tuning.biomeBlendRadius)), 0, 16);
  if (radius <= 0)
  {
    return ComputeBiomeWeights(x, z, coarseY, seaLevel, maxHeight, seed,
                               tuning, worldgenProfileVersion);
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
          x + dx, z + dz, sampleY, seaLevel, maxHeight, seed, tuning,
          worldgenProfileVersion);
      for (const BiomeBlendEntry &entry : local.entries)
      {
        blended.AddWeight(entry.biome, entry.weight * kernel);
      }
      totalKernel += kernel;
    }
  }
  if (totalKernel > 0.0f)
  {
    for (BiomeBlendEntry &entry : blended.entries)
    {
      entry.weight /= totalKernel;
    }
  }
  blended.Normalize();
  return blended;
}

BiomeId DominantBiome(const BiomeWeightSet &weights)
{
  const BiomeHandle handle = DominantBiomeHandle(weights);
  return handle.IsValid()
             ? BiomeIdFromString(BiomeKeyForHandle(handle))
             : BiomeId::Invalid;
}

BiomeHandle DominantBiomeHandle(const BiomeWeightSet &weights)
{
  BiomeHandle best;
  float bestWeight = 0.0f;
  for (const BiomeBlendEntry &entry : weights.entries)
  {
    if (entry.weight > bestWeight ||
        (entry.weight == bestWeight && entry.biome.index < best.index))
    {
      best = entry.biome;
      bestWeight = entry.weight;
    }
  }
  return best;
}

BiomeHandle PickSurfaceBiomeHandle(int x, int z,
                                   const BiomeWeightSet &weights)
{
  (void)x;
  (void)z;
  return DominantBiomeHandle(weights);
}

BiomeKey BiomeKeyForHandle(BiomeHandle handle)
{
  const WorldGenPack &pack = UWorldGenPack::Get();
  if (!pack.BiomeRegistry)
  {
    return {};
  }
  const BiomeKey *key = pack.BiomeRegistry->Key(handle);
  return key ? *key : BiomeKey{};
}

BiomeId PickSurfaceBiome(int x, int z, const BiomeWeightSet &weights)
{
  return BiomeIdFromString(
      BiomeKeyForHandle(PickSurfaceBiomeHandle(x, z, weights)));
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

int BiomeBlendedHeightY(int coarseY, int seaLevel, int maxHeight,
                        const BiomeWeightSet &weights)
{
  float sum = 0.0f;
  float weight_sum = 0.0f;
  for (const BiomeBlendEntry &entry : weights.entries)
  {
    const BiomeHeightProfile profile = BiomeHeightProfileForHandle(entry.biome);
    sum += entry.weight * static_cast<float>(ApplyBiomeHeightProfile(
                               coarseY, seaLevel, maxHeight, profile));
    weight_sum += entry.weight;
  }
  if (weight_sum <= 0.0f)
  {
    return std::clamp(coarseY, 1, maxHeight);
  }
  return std::clamp(
      static_cast<int>(std::floor(sum / weight_sum + 0.5f)), 1, maxHeight);
}

int BiomeBlendedBaseY(int x, int z, int coarseY, const BiomeWeightSet &weights,
                      const ProceduralSettings &settings)
{
  (void)x;
  (void)z;
  float offset = 0.0f;
  for (const BiomeBlendEntry &entry : weights.entries)
  {
    offset += entry.weight *
              BiomeHeightProfileForHandle(entry.biome).baseOffsetBlocks;
  }
  const int y =
      coarseY + static_cast<int>(std::floor(offset + 0.5f));
  return std::clamp(y, 1, settings.MaxHeight);
}

int SmoothBlendedSurfaceY(int x, int z, int baseY,
                          const BiomeWeightSet &weights,
                          const ProceduralSettings &settings, uint32_t seed,
                          const WorldGenTuning &tuning,
                          const CoarseHeightCallback &getCoarseY,
                          float climateErosion)
{
  if (!tuning.heightSmoothing || tuning.heightSmoothingRadius <= 0 || !getCoarseY)
  {
    return baseY;
  }
  (void)climateErosion;
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
                             const CoarseHeightCallback &getCoarseY,
                             const BiomeWeightSet *weights_override,
                             int worldgenProfileVersion)
{
  const BiomeWeightSet weights =
      weights_override
          ? *weights_override
          : BlendedBiomeWeights(x, z, coarseY, settings.SeaLevel,
                                settings.MaxHeight, seed, tuning, getCoarseY,
                                worldgenProfileVersion);
  float volatilityJitter = 0.0f;
  for (const BiomeBlendEntry &entry : weights.entries)
  {
    volatilityJitter += entry.weight *
                        BiomeHeightProfileForHandle(entry.biome)
                            .volatilityMultiplier;
  }

  int y = tuning.useUnifiedHeightField
              ? BiomeBlendedHeightY(coarseY, settings.SeaLevel,
                                    settings.MaxHeight, weights)
              : BiomeBlendedBaseY(x, z, coarseY, weights, settings);
  const ClimateSample climate = SampleClimate(x, z, seed);
  y = SmoothBlendedSurfaceY(x, z, y, weights, settings, seed, tuning, getCoarseY,
                            climate.erosion);

  const int localCoarseRange = LocalCoarseHeightRange(x, z, getCoarseY);

  const PackHeightConfig &heightPack = UWorldGenPack::HeightConfig();
  const float jitterScale =
      heightPack.Loaded ? heightPack.JitterScale : 0.03f;
  const float jitterAmp =
      heightPack.Loaded ? heightPack.JitterAmplitude : tuning.jitterAmplitude;
  const float erosionDamp =
      heightPack.Loaded ? heightPack.JitterErosionDamp : 0.85f;
  const float jitter =
      NormalizedFBM2D(static_cast<float>(x) * jitterScale,
                      static_cast<float>(z) * jitterScale, seed + 77, 2, 0.5f,
                      2.0f) *
      volatilityJitter * jitterAmp *
      FlatTerrainJitterScale(localCoarseRange, climate.erosion) *
      (1.0f - climate.erosion * erosionDamp) * 5.0f;
  y = static_cast<int>(std::floor(static_cast<float>(y) + jitter + 0.5f));

  const float micro_rolling =
      NormalizedFBM2D(static_cast<float>(x) * 0.045f,
                      static_cast<float>(z) * 0.045f, seed + 8801, 2, 0.5f,
                      2.0f) *
      volatilityJitter * 4.2f;
  const float rolling_hills =
      NormalizedFBM2D(static_cast<float>(x) * 0.018f,
                      static_cast<float>(z) * 0.018f, seed + 8800, 3, 0.5f,
                      2.0f) *
      (0.6f + volatilityJitter * 0.4f) * 6.5f;
  y += static_cast<int>(std::floor(micro_rolling + rolling_hills + 0.5f));

  if (localCoarseRange <= 3 && getCoarseY)
  {
    float neighbor_sum = static_cast<float>(y);
    float neighbor_wsum = 1.0f;
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        if (dx == 0 && dz == 0)
        {
          continue;
        }
        neighbor_sum += static_cast<float>(getCoarseY(x + dx, z + dz));
        neighbor_wsum += 1.0f;
      }
    }
    const int neighbor_avg = static_cast<int>(
        std::floor(neighbor_sum / neighbor_wsum + 0.5f));
    y = std::clamp(y, neighbor_avg - 2, neighbor_avg + 2);
  }

  y = ApplyRiverCarve(x, z, y, weights, settings, localCoarseRange);
  y = ApplyCoastShelf(x, z, y, settings, getCoarseY);
  y = FillMicroDepressions(x, z, y, localCoarseRange, getCoarseY, climate.erosion);
  y = ClampToFlatPlateau(x, z, y, localCoarseRange, getCoarseY, climate.erosion);
  const float macro_height01 =
      std::clamp(OverworldMacroHeight01(x, z, seed), 0.0f, 1.0f);
  y = ApplyErosionLite(x, z, y, seed, settings, getCoarseY, weights,
                       macro_height01);
  return std::clamp(y, 1, settings.MaxHeight);
}

BiomeId PickWeightedBiome(int x, int z, uint32_t seed, float temperature,
                          float moisture, float localHeightNorm,
                          const WorldGenTuning &tuning)
{
  const BiomeId primary = ClassifyBiome(temperature, moisture, localHeightNorm);
  constexpr std::array<BiomeId, kBuiltinSurfaceBiomeCount> kBiomes = {
      BiomeId::Plains,    BiomeId::Forest,   BiomeId::Desert, BiomeId::Hills,
      BiomeId::Tundra,    BiomeId::Savanna,  BiomeId::Foothills,
      BiomeId::Scrubland, BiomeId::ColdSteppe, BiomeId::Ocean,
      BiomeId::Beach,     BiomeId::Mountains, BiomeId::SnowySlopes,
      BiomeId::JaggedPeaks};

  float total = 0.0f;
  std::array<float, kBuiltinSurfaceBiomeCount> weights{};
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

UBiomeSampler::UBiomeSampler(uint32_t Seed, const WorldGenTuning &tuning,
                             int worldgenProfileVersion)
    : Seed(Seed), Tuning(tuning), ProfileVersion(worldgenProfileVersion)
{
}

void UBiomeSampler::SetCoarseHeightCallback(CoarseHeightCallback callback)
{
  CoarseHeightFn = std::move(callback);
}

int UBiomeSampler::CoarseYAt(int x, int z, int fallback_y) const
{
  if (CoarseHeightFn)
  {
    return CoarseHeightFn(x, z);
  }
  return fallback_y;
}

BiomeId UBiomeSampler::At(int x, int z, int surfaceY, int SeaLevel,
                          int MaxHeight) const
{
  return DominantBiome(WeightsAt(x, z, surfaceY, SeaLevel, MaxHeight));
}

BiomeId UBiomeSampler::At3D(int x, int y, int z, int surfaceY, int SeaLevel,
                            int MaxHeight) const
{
  if (y >= surfaceY - 2)
  {
    return At(x, z, surfaceY, SeaLevel, MaxHeight);
  }

  const int depth = surfaceY - y;
  const ClimateSample climate = SampleClimate(x, z, Seed);
  const float cavePatch =
      FBM3D(static_cast<float>(x) * 0.035f,
            static_cast<float>(y) * 0.042f,
            static_cast<float>(z) * 0.035f, Seed + 7721, 3, 0.5f, 2.0f);
  if (y <= 16 && depth >= 24 && cavePatch > -0.05f)
  {
    return BiomeId::DeepDark;
  }

  if (depth < 5)
  {
    return At(x, z, surfaceY, SeaLevel, MaxHeight);
  }
  if (climate.moisture > 0.56f && climate.temperature > 0.34f &&
      cavePatch > -0.28f)
  {
    return BiomeId::LushCaves;
  }
  return BiomeId::DripstoneCaves;
}

BiomeKey UBiomeSampler::At3DKey(int x, int y, int z, int surfaceY, int SeaLevel,
                                int MaxHeight) const
{
  const BiomeId legacy = At3D(x, y, z, surfaceY, SeaLevel, MaxHeight);
  const BiomeKey legacyKey = BiomeIdToString(legacy);
  if (ProfileVersion < 2 || y >= surfaceY - 2 || surfaceY - y < 5)
  {
    return legacyKey;
  }

  const WorldGenPack &pack = UWorldGenPack::Get();
  if (!pack.BiomeRegistry || pack.CaveClimateBiomes.empty())
  {
    return legacyKey;
  }

  const ClimateSample climate = SampleClimate(x, z, Seed);
  std::vector<BiomeBlendEntry> candidates;
  float total = 0.0f;
  for (const BiomeHandle handle : pack.CaveClimateBiomes)
  {
    const BiomeKey *key = pack.BiomeRegistry->Key(handle);
    if (!key)
    {
      continue;
    }
    const auto definition = pack.Biomes.find(*key);
    if (definition == pack.Biomes.end())
    {
      continue;
    }
    const float weight =
        CustomBiomeClimateFitness(definition->second.Climate, climate);
    if (weight > 0.0f)
    {
      candidates.push_back({handle, weight});
      total += weight;
    }
  }
  if (total <= 0.0f)
  {
    return legacyKey;
  }

  const uint32_t hash = ColumnHash(x, z, Seed + 0x6ca7e5d1U) ^
                        (static_cast<uint32_t>(y) * 0x9e3779b9U);
  float pick = static_cast<float>(hash % 100000U) / 100000.0f;
  if (pick >= std::min(1.0f, total))
  {
    return legacyKey;
  }
  for (const BiomeBlendEntry &entry : candidates)
  {
    pick -= entry.weight / std::max(1.0f, total);
    if (pick < 0.0f)
    {
      const BiomeKey *key = pack.BiomeRegistry->Key(entry.biome);
      return key ? *key : legacyKey;
    }
  }
  const BiomeKey *key = pack.BiomeRegistry->Key(candidates.back().biome);
  return key ? *key : legacyKey;
}

BiomeWeightSet UBiomeSampler::WeightsAt(int x, int z, int surfaceY,
                                        int SeaLevel, int MaxHeight) const
{
  return BlendedBiomeWeights(x, z, surfaceY, SeaLevel, MaxHeight, Seed, Tuning,
                             CoarseHeightFn, ProfileVersion);
}

int UBiomeSampler::RefineSurfaceY(int x, int z, int coarseY,
                                  const ProceduralSettings &settings) const
{
  return RefineSurfaceYWithBiomes(x, z, coarseY, settings, Seed, Tuning,
                                  CoarseHeightFn, nullptr, ProfileVersion);
}

int UBiomeSampler::RefineSurfaceY(int x, int z, int coarseY,
                                  const ProceduralSettings &settings,
                                  const BiomeWeightSet &weights) const
{
  return RefineSurfaceYWithBiomes(x, z, coarseY, settings, Seed, Tuning,
                                  CoarseHeightFn, &weights, ProfileVersion);
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
  case BiomeId::Ocean:
    rule.surface = ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : ctx.Blocks.Sand;
    rule.subsurface = ctx.Blocks.Stone;
    break;
  case BiomeId::Beach:
    rule.surface = ctx.Blocks.Sand;
    rule.subsurface = ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : ctx.Blocks.Sand;
    break;
  case BiomeId::Mountains:
    rule.surface = ctx.Blocks.Stone;
    rule.subsurface = ctx.Blocks.Dirt;
    break;
  case BiomeId::SnowySlopes:
    rule.surface = ctx.Blocks.Snow != BLOCK_AIR ? ctx.Blocks.Snow : ctx.Blocks.Stone;
    rule.subsurface = ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : ctx.Blocks.Dirt;
    break;
  case BiomeId::JaggedPeaks:
    rule.surface = ctx.Blocks.Stone;
    rule.subsurface = ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : ctx.Blocks.Stone;
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
  const bool customBiome = sample.SurfaceBiomeHandle.IsValid() &&
                           BiomeIdFromString(sample.SurfaceBiomeKey) ==
                               BiomeId::Invalid;
  BiomeSurfaceRule rule;
  if (pick == BiomeId::Invalid)
  {
    rule.surface = ctx.Blocks.Grass;
    rule.subsurface = ctx.Blocks.Dirt;
  }
  else
  {
    rule = biome_lookup.SurfaceRule(pick, ctx);
  }

  if (customBiome)
  {
    rule.surface = ctx.Blocks.Grass;
    rule.subsurface = ctx.Blocks.Dirt;
    rule = ApplyPackPalette(sample.SurfaceBiomeKey, rule, ctx);
  }

  if (!customBiome &&
      (pick == BiomeId::Hills || dominant == BiomeId::Hills))
  {
    if (sample.MacroHeight01 < 0.88f)
    {
      rule.surface = ctx.Blocks.Grass;
      rule.subsurface = ctx.Blocks.Dirt;
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
    if (sample.CoastFactor > 0.25f)
    {
      rule.surface = ctx.Blocks.Sand != BLOCK_AIR ? ctx.Blocks.Sand : rule.surface;
      rule.subsurface =
          ctx.Blocks.Gravel != BLOCK_AIR ? ctx.Blocks.Gravel : rule.subsurface;
    }
  }

  const SubBiomeId sub = SubBiomeFor(x, z, pick, ctx.Settings.Seed);
  const BiomePackDefinition *pack_def =
      UWorldGenPack::BiomeDefinitionFor(BiomeIdToString(pick));
  if (sample.SurfaceBiomeHandle.IsValid())
  {
    // Dynamic biome sub-biomes are deliberately not cast to legacy enum IDs.
  }
  else if (pack_def)
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
  sample.SurfaceBiomeHandle = PickSurfaceBiomeHandle(x, z, weights);
  sample.SurfaceBiomeKey = BiomeKeyForHandle(sample.SurfaceBiomeHandle);
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

BiomeWeightSet ProceduralClimateBiomeSource::SampleSurfaceWeights(
    int x, int z, int coarseY, int seaLevel, int maxHeight) const
{
  return Sampler.WeightsAt(x, z, coarseY, seaLevel, maxHeight);
}

BiomeId ProceduralClimateBiomeSource::Sample3D(int x, int y, int z,
                                                int surfaceY, int seaLevel,
                                                int maxHeight) const
{
  return Sampler.At3D(x, y, z, surfaceY, seaLevel, maxHeight);
}

BiomeKey ProceduralClimateBiomeSource::Sample3DKey(
    int x, int y, int z, int surfaceY, int seaLevel, int maxHeight) const
{
  return Sampler.At3DKey(x, y, z, surfaceY, seaLevel, maxHeight);
}

BiomeWeightSet ImageMapBiomeSource::SampleSurfaceWeights(
    int x, int z, int coarseY, int seaLevel, int maxHeight) const
{
  (void)coarseY;
  (void)seaLevel;
  (void)maxHeight;
  BiomeWeightSet weights;
  const BiomeId biome = UWorldGenPack::BiomeAtImage(x, z);
  weights.SetWeight(BiomeHandleForId(biome), 1.0f);
  return weights;
}

BiomeId ImageMapBiomeSource::Sample3D(int x, int y, int z, int surfaceY,
                                      int seaLevel, int maxHeight) const
{
  if (y >= surfaceY - 2)
  {
    return UWorldGenPack::BiomeAtImage(x, z);
  }
  return CaveSampler.At3D(x, y, z, surfaceY, seaLevel, maxHeight);
}

BiomeKey ImageMapBiomeSource::Sample3DKey(int x, int y, int z, int surfaceY,
                                          int seaLevel, int maxHeight) const
{
  if (y >= surfaceY - 2)
  {
    return BiomeIdToString(UWorldGenPack::BiomeAtImage(x, z));
  }
  return CaveSampler.At3DKey(x, y, z, surfaceY, seaLevel, maxHeight);
}

} // namespace cutum
