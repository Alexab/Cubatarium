#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Features/PrefabFeatureConfig.h"
#include "World/Math/BlockTypes.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace cutum
{

namespace
{

BlockId ResolveSlotBlock(const WorldGenContext &ctx, const std::string &slot,
                         BlockId fallback)
{
  if (slot == "grass")
  {
    return ctx.Grass;
  }
  if (slot == "dirt")
  {
    return ctx.Dirt;
  }
  if (slot == "sand")
  {
    return ctx.Sand != BLOCK_AIR ? ctx.Sand : fallback;
  }
  if (slot == "sandstone")
  {
    return ctx.Sandstone != BLOCK_AIR ? ctx.Sandstone : fallback;
  }
  if (slot == "gravel")
  {
    return ctx.Gravel != BLOCK_AIR ? ctx.Gravel : fallback;
  }
  if (slot == "snow")
  {
    return ctx.Snow != BLOCK_AIR ? ctx.Snow : fallback;
  }
  if (slot == "stone")
  {
    return ctx.Stone;
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
  return static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ seed;
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
  case 0:
  default:
    return BiomeId::Plains;
  }
}

float BiomeAffinity(BiomeId biome, float temperature, float moisture,
                    float localHeightNorm)
{
  switch (biome)
  {
  case BiomeId::Desert:
    return std::max(0.0f, (temperature - 0.55f) * 2.0f) *
           std::max(0.0f, (0.45f - moisture) * 2.0f);
  case BiomeId::Tundra:
    return std::max(0.0f, (0.35f - temperature) * 2.0f) *
           std::max(0.0f, (0.65f - moisture) * 1.5f);
  case BiomeId::Hills:
    return std::max(0.0f, (localHeightNorm - 0.55f) * 2.5f);
  case BiomeId::Forest:
    return std::max(0.0f, (moisture - 0.45f) * 2.0f) *
           std::max(0.0f, 1.0f - localHeightNorm);
  case BiomeId::Plains:
  default:
    return std::max(0.15f, 1.0f - std::fabs(temperature - 0.5f) -
                                std::fabs(moisture - 0.45f));
  }
}

int ApplyRiverCarve(int x, int z, int surfaceY, const BiomeWeightSet &weights,
                    const ProceduralSettings &settings)
{
  const float forest = weights.weights[BiomeIndex(BiomeId::Forest)];
  const float plains = weights.weights[BiomeIndex(BiomeId::Plains)];
  const float humid = forest + plains;
  if (humid < 0.35f)
  {
    return surfaceY;
  }
  const float riverNoise =
      FBM2D(static_cast<float>(x) * 0.008f, static_cast<float>(z) * 0.008f,
            settings.Seed + 8801, 2, 0.5f, 2.0f);
  if (riverNoise > 0.62f)
  {
    return std::max(1, settings.SeaLevel - 2);
  }
  return surfaceY;
}

int ApplyCoastShelf(int x, int z, int surfaceY, const ProceduralSettings &settings,
                    uint32_t seed)
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
      const float nh =
          FBM2D(static_cast<float>(x + dx) * 0.01f,
                static_cast<float>(z + dz) * 0.01f, seed, 4, 0.5f, 2.0f);
      const int ny = sea + static_cast<int>((nh - 0.5f) * 6.0f);
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

float SampleHeightGradient(int x, int z, int coarseY, uint32_t seed)
{
  const int hx =
      coarseY -
      static_cast<int>(FBM2D(static_cast<float>(x + 3) * 0.01f,
                             static_cast<float>(z) * 0.01f, seed + 91, 2, 0.5f,
                             2.0f) *
                       2.0f);
  const int hz =
      coarseY -
      static_cast<int>(FBM2D(static_cast<float>(x) * 0.01f,
                             static_cast<float>(z + 3) * 0.01f, seed + 92, 2,
                             0.5f, 2.0f) *
                       2.0f);
  return static_cast<float>(std::abs(hx - hz));
}

int ApplyErosionLite(int x, int z, int surfaceY, uint32_t seed,
                     const ProceduralSettings &settings)
{
  const float erosion = std::clamp(settings.Tuning.terrainErosion, 0.0f, 1.0f);
  if (erosion <= 0.0f)
  {
    return surfaceY;
  }
  const float gradient = SampleHeightGradient(x, z, surfaceY, seed);
  if (gradient > 2.0f * erosion)
  {
    return std::max(1, surfaceY - static_cast<int>(gradient * erosion));
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
    profile.baseOffsetBlocks = -2.f;
    profile.amplitudeMultiplier = 0.45f;
    profile.volatilityMultiplier = 0.5f;
    break;
  case BiomeId::Hills:
    profile.baseOffsetBlocks = 4.f;
    profile.amplitudeMultiplier = 1.6f;
    profile.volatilityMultiplier = 1.2f;
    break;
  case BiomeId::Forest:
    profile.baseOffsetBlocks = 0.f;
    profile.amplitudeMultiplier = 1.0f;
    profile.volatilityMultiplier = 1.0f;
    break;
  case BiomeId::Tundra:
    profile.baseOffsetBlocks = 1.f;
    profile.amplitudeMultiplier = 0.7f;
    profile.volatilityMultiplier = 0.8f;
    break;
  case BiomeId::Plains:
  default:
    profile.baseOffsetBlocks = 0.f;
    profile.amplitudeMultiplier = 0.85f;
    profile.volatilityMultiplier = 0.9f;
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
  const float tempRaw =
      FBM2D(static_cast<float>(x) * 0.002f, static_cast<float>(z) * 0.002f,
            seed + 1000, 3, 0.5f, 2.0f);
  const float moistRaw =
      FBM2D(static_cast<float>(x) * 0.002f, static_cast<float>(z) * 0.002f,
            seed + 2000, 3, 0.5f, 2.0f);
  temperature = (tempRaw + 1.0f) * 0.5f;
  moisture = (moistRaw + 1.0f) * 0.5f;
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
  if (localHeightNorm > 0.7f)
  {
    return BiomeId::Hills;
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
  case BiomeId::Plains:
  default:
    return tuning.biomePlainsWeight;
  }
}

BiomeWeightSet ComputeBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning)
{
  float temperature = 0.5f;
  float moisture = 0.5f;
  ComputeBiomeClimate(x, z, seed, temperature, moisture);
  const float denom = static_cast<float>(std::max(1, maxHeight - seaLevel));
  const float localHeightNorm =
      std::clamp(static_cast<float>(coarseY - seaLevel) / denom, 0.0f, 1.0f);
  const BiomeId primary =
      ClassifyBiome(temperature, moisture, localHeightNorm);

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
    float w = BiomeAffinity(biome, temperature, moisture, localHeightNorm) *
              tuningWeight;
    if (biome == primary)
    {
      w *= 2.0f;
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
    result.weights[BiomeIndex(primary)] = 1.0f;
  }
  return result;
}

BiomeWeightSet BlendedBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning)
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
      const BiomeWeightSet local = ComputeBiomeWeights(
          x + dx, z + dz, coarseY, seaLevel, maxHeight, seed, tuning);
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
  const uint32_t pick =
      BiomePickHash(x, z, 7711) % 1000;
  float cursor = static_cast<float>(pick) / 1000.0f;
  for (int i = 0; i < kBiomeCount; ++i)
  {
    if (weights.weights[i] <= 0.0f)
    {
      continue;
    }
    cursor -= weights.weights[i];
    if (cursor <= 0.0f)
    {
      return BiomeFromIndex(i);
    }
  }
  return DominantBiome(weights);
}

SubBiomeId SubBiomeFor(int x, int z, BiomeId biome, uint32_t seed)
{
  const float n =
      FBM2D(static_cast<float>(x) * 0.01f, static_cast<float>(z) * 0.01f,
            seed + 5500 + BiomeIndex(biome) * 17, 2, 0.5f, 2.0f);
  const float t = (n + 1.0f) * 0.5f;
  switch (biome)
  {
  case BiomeId::Forest:
    if (t < 0.33f)
    {
      return SubBiomeId::SparseForest;
    }
    if (t < 0.66f)
    {
      return SubBiomeId::Woodland;
    }
    return SubBiomeId::DenseForest;
  case BiomeId::Desert:
    return t < 0.5f ? SubBiomeId::ScrubDesert : SubBiomeId::Dunes;
  default:
    return SubBiomeId::Default;
  }
}

int RefineSurfaceYWithBiomes(int x, int z, int coarseY,
                             const ProceduralSettings &settings, uint32_t seed,
                             const WorldGenTuning &tuning)
{
  const BiomeWeightSet weights = BlendedBiomeWeights(
      x, z, coarseY, settings.SeaLevel, settings.MaxHeight, seed, tuning);
  float blendedDelta = 0.0f;
  for (int i = 0; i < kBiomeCount; ++i)
  {
    const BiomeHeightProfile profile = BiomeHeightProfileFor(BiomeFromIndex(i));
    const float delta = static_cast<float>(coarseY - settings.SeaLevel);
    blendedDelta += weights.weights[i] *
                    (delta * profile.amplitudeMultiplier +
                     profile.baseOffsetBlocks);
  }
  int y = settings.SeaLevel + static_cast<int>(std::lround(blendedDelta));
  y = ApplyRiverCarve(x, z, y, weights, settings);
  y = ApplyCoastShelf(x, z, y, settings, seed);
  y = ApplyErosionLite(x, z, y, seed, settings);
  return std::clamp(y, 1, settings.MaxHeight);
}

BiomeId PickWeightedBiome(int x, int z, uint32_t seed, float temperature,
                          float moisture, float localHeightNorm,
                          const WorldGenTuning &tuning)
{
  const BiomeId primary = ClassifyBiome(temperature, moisture, localHeightNorm);
  constexpr std::array<BiomeId, 5> kBiomes = {
      BiomeId::Plains, BiomeId::Forest, BiomeId::Desert, BiomeId::Hills,
      BiomeId::Tundra};

  float total = 0.0f;
  std::array<float, 5> weights{};
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

BiomeId UBiomeSampler::At(int x, int z, int surfaceY, int SeaLevel,
                          int MaxHeight) const
{
  return DominantBiome(WeightsAt(x, z, surfaceY, SeaLevel, MaxHeight));
}

BiomeWeightSet UBiomeSampler::WeightsAt(int x, int z, int surfaceY,
                                        int SeaLevel, int MaxHeight) const
{
  return BlendedBiomeWeights(x, z, surfaceY, SeaLevel, MaxHeight, Seed, Tuning);
}

int UBiomeSampler::RefineSurfaceY(int x, int z, int coarseY,
                                  const ProceduralSettings &settings) const
{
  return RefineSurfaceYWithBiomes(x, z, coarseY, settings, Seed, Tuning);
}

BiomeSurfaceRule UBiomeSampler::SurfaceRule(BiomeId biome,
                                            const WorldGenContext &ctx) const
{
  BiomeSurfaceRule rule;
  switch (biome)
  {
  case BiomeId::Desert:
    rule.surface = ctx.Sand;
    rule.subsurface = ctx.Sandstone != BLOCK_AIR ? ctx.Sandstone : ctx.Sand;
    break;
  case BiomeId::Tundra:
    rule.surface = ctx.Snow != BLOCK_AIR ? ctx.Snow : ctx.Stone;
    rule.subsurface = ctx.Dirt;
    break;
  case BiomeId::Hills:
    rule.surface = ctx.Stone;
    rule.subsurface = ctx.Gravel != BLOCK_AIR ? ctx.Gravel : ctx.Stone;
    break;
  case BiomeId::Forest:
  case BiomeId::Plains:
  default:
    rule.surface = ctx.Grass;
    rule.subsurface = ctx.Dirt;
    break;
  }
  return ApplyPackPalette(biome, rule, ctx);
}

BiomeSurfaceRule UBiomeSampler::BlendedSurfaceRule(
    int x, int z, const BiomeWeightSet &weights, const WorldGenContext &ctx,
    int surfaceY) const
{
  const BiomeId pick = PickSurfaceBiome(x, z, weights);
  BiomeSurfaceRule rule = SurfaceRule(pick, ctx);

  if (ctx.Settings.FillWater && surfaceY < ctx.Settings.SeaLevel)
  {
    if (rule.surface == ctx.Grass)
    {
      rule.surface = ctx.Sand != BLOCK_AIR ? ctx.Sand : ctx.Stone;
    }
    if (rule.subsurface == ctx.Grass || rule.subsurface == ctx.Dirt)
    {
      rule.subsurface =
          ctx.Gravel != BLOCK_AIR ? ctx.Gravel
                                  : (ctx.Sand != BLOCK_AIR ? ctx.Sand
                                                           : ctx.Stone);
    }
  }

  if (ctx.Settings.FillWater &&
      surfaceY <= ctx.Settings.SeaLevel + 2 &&
      surfaceY >= ctx.Settings.SeaLevel - 1)
  {
    const float land =
        weights.weights[BiomeIndex(BiomeId::Plains)] +
        weights.weights[BiomeIndex(BiomeId::Forest)] +
        weights.weights[BiomeIndex(BiomeId::Hills)];
    if (land > 0.2f)
    {
      rule.surface = ctx.Sand != BLOCK_AIR ? ctx.Sand : rule.surface;
      rule.subsurface =
          ctx.Gravel != BLOCK_AIR ? ctx.Gravel : rule.subsurface;
    }
  }

  const SubBiomeId sub = SubBiomeFor(x, z, pick, Seed);
  const BiomePackDefinition *packDef =
      UWorldGenPack::BiomeDefinitionFor(BiomeIdToString(pick));
  if (packDef)
  {
    const auto subIt = packDef->SubBiomes.find(SubBiomeIdToString(sub));
    if (subIt != packDef->SubBiomes.end() &&
        !subIt->second.SubsurfaceSlot.empty())
    {
      rule.subsurface = ResolveSlotBlock(ctx, subIt->second.SubsurfaceSlot,
                                         rule.subsurface);
    }
  }
  else if (pick == BiomeId::Desert && sub == SubBiomeId::Dunes)
  {
    rule.subsurface = ctx.Sandstone != BLOCK_AIR ? ctx.Sandstone : ctx.Sand;
  }
  else if (pick == BiomeId::Forest && sub == SubBiomeId::DenseForest)
  {
    rule.subsurface = ctx.Dirt;
  }

  const float erosion = std::clamp(ctx.Settings.Tuning.terrainErosion, 0.0f, 1.0f);
  if (erosion > 0.2f &&
      SampleHeightGradient(x, z, surfaceY, Seed) > 2.5f * erosion)
  {
    rule.surface = ctx.Stone;
    rule.subsurface = ctx.Gravel != BLOCK_AIR ? ctx.Gravel : ctx.Stone;
  }

  return rule;
}

} // namespace cutum
