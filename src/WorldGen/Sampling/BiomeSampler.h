#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include <cstdint>

namespace cutum
{

enum class BiomeId
{
  Plains,
  Forest,
  Desert,
  Hills,
  Tundra
};

struct BiomeSurfaceRule
{
  BlockId surface{BLOCK_AIR};
  BlockId subsurface{BLOCK_AIR};
};

class UBiomeSampler
{
public:
  explicit UBiomeSampler(uint32_t Seed);

  BiomeId At(int x, int z, int surfaceY, int SeaLevel, int MaxHeight) const;
  BiomeSurfaceRule SurfaceRule(BiomeId biome, const WorldGenContext &ctx) const;

private:
  uint32_t Seed;
};

BiomeId ClassifyBiome(float temperature, float moisture, float localHeightNorm);

} // namespace cutum
