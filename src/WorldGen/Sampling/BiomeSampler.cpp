#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace cutum
{

namespace
{

uint32_t BiomePickHash(int x, int z, uint32_t seed)
{
  return static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ seed;
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

} // namespace

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
  const float tempRaw =
      FBM2D(static_cast<float>(x) * 0.002f, static_cast<float>(z) * 0.002f,
            Seed + 1000, 3, 0.5f, 2.0f);
  const float moistRaw =
      FBM2D(static_cast<float>(x) * 0.002f, static_cast<float>(z) * 0.002f,
            Seed + 2000, 3, 0.5f, 2.0f);
  const float temperature = (tempRaw + 1.0f) * 0.5f;
  const float moisture = (moistRaw + 1.0f) * 0.5f;
  const float denom = static_cast<float>(std::max(1, MaxHeight - SeaLevel));
  const float localHeightNorm =
      std::clamp(static_cast<float>(surfaceY - SeaLevel) / denom, 0.0f, 1.0f);
  return PickWeightedBiome(x, z, Seed, temperature, moisture, localHeightNorm,
                           Tuning);
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
  return rule;
}

} // namespace cutum
