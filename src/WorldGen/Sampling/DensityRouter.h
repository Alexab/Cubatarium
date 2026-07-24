#pragma once

#include "WorldGen/Sampling/ClimateSampler.h"
#include <cstdint>

namespace cutum
{

struct DensityRouteParams
{
  float baseHeightBlocks{0.0f};
  float noiseFactor{1.0f};
  float jaggedness{1.0f};
};

DensityRouteParams RouteDensity(const ClimateSample &climate, int seaLevel,
                                int maxHeight, float terrainRoughness);

} // namespace cutum
