#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
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
  UBiomeSampler(uint32_t Seed, const WorldGenTuning &tuning);

  BiomeId At(int x, int z, int surfaceY, int SeaLevel, int MaxHeight) const;
  BiomeSurfaceRule SurfaceRule(BiomeId biome, const WorldGenContext &ctx) const;

private:
  uint32_t Seed;
  WorldGenTuning Tuning;
};

BiomeId ClassifyBiome(float temperature, float moisture, float localHeightNorm);
BiomeId PickWeightedBiome(int x, int z, uint32_t seed,
                          float temperature, float moisture,
                          float localHeightNorm, const WorldGenTuning &tuning);

float BiomeTuningWeight(BiomeId biome, const WorldGenTuning &tuning);

} // namespace cutum
