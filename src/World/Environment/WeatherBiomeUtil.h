#ifndef WEATHERBIOMEUTIL_H
#define WEATHERBIOMEUTIL_H

#include "WorldGen/Sampling/BiomeSampler.h"

namespace cutum
{

class UWorld;

enum class WeatherClimateGroup
{
  Temperate = 0,
  Cold = 1,
  Arid = 2,
};

WeatherClimateGroup MapBiomeToClimateGroup(BiomeId biome);
bool ResolvePlayerBiome(const UWorld &world, BiomeId &out_biome);

} // namespace cutum

#endif // WEATHERBIOMEUTIL_H
