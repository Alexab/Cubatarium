#include "WorldGen/Sampling/DensityRouter.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

DensityRouteParams RouteDensity(const ClimateSample &climate, int seaLevel,
                                int maxHeight, float terrainRoughness)
{
  DensityRouteParams params;
  const float erosion = std::clamp(climate.erosion, 0.0f, 1.0f);
  const float erosion_flat = 1.0f - erosion;
  const float roughness = std::max(0.25f, terrainRoughness);

  params.baseHeightBlocks =
      ContinentalHeightBlocks(climate.continentalness, seaLevel, maxHeight);
  params.noiseFactor =
      (6.0f + roughness * 10.0f) * ErosionAmplitudeMultiplier(erosion);
  params.jaggedness =
      (0.35f + PeaksAndValleys(climate.weirdness) * 0.65f) *
      (erosion_flat + 0.15f);
  return params;
}

} // namespace cutum
