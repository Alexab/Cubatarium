#include "BiomeSampler.h"
#include "Noise.h"
#include <algorithm>

namespace cutum {

BiomeId ClassifyBiome(float temperature, float moisture, float localHeightNorm)
{
 if (temperature > 0.65f && moisture < 0.35f) {
  return BiomeId::Desert;
 }
 if (temperature < 0.25f && moisture < 0.6f) {
  return BiomeId::Tundra;
 }
 if (localHeightNorm > 0.7f) {
  return BiomeId::Hills;
 }
 if (moisture > 0.55f) {
  return BiomeId::Forest;
 }
 return BiomeId::Plains;
}

UBiomeSampler::UBiomeSampler(uint32_t seed)
 : seed_(seed)
{
}

BiomeId UBiomeSampler::At(int x, int z, int surfaceY, int seaLevel, int maxHeight) const
{
 const float tempRaw = FBM2D(static_cast<float>(x) * 0.002f, static_cast<float>(z) * 0.002f,
     seed_ + 1000, 3, 0.5f, 2.0f);
 const float moistRaw = FBM2D(static_cast<float>(x) * 0.002f, static_cast<float>(z) * 0.002f,
     seed_ + 2000, 3, 0.5f, 2.0f);
 const float temperature = (tempRaw + 1.0f) * 0.5f;
 const float moisture = (moistRaw + 1.0f) * 0.5f;
 const float denom = static_cast<float>(std::max(1, maxHeight - seaLevel));
 const float localHeightNorm = std::clamp(
     static_cast<float>(surfaceY - seaLevel) / denom, 0.0f, 1.0f);
 return ClassifyBiome(temperature, moisture, localHeightNorm);
}

BiomeSurfaceRule UBiomeSampler::SurfaceRule(BiomeId biome, const WorldGenContext& ctx) const
{
 BiomeSurfaceRule rule;
 switch (biome) {
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
