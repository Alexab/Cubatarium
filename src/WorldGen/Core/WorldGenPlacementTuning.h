#pragma once

#include <algorithm>

#include "World/Core/RuntimeTuning.h"

namespace cutum
{

struct WorldGenPlacementTuning
{
  static constexpr int LeafNearSolidRadius = 5;
  static constexpr int SurfaceScanAboveHeightmap = 24;
  static constexpr int SurfaceScanMinAboveSea = 4;
  static constexpr int MinLandAboveSea = 1;
  static constexpr int SpawnIslandMinLandAboveSea = 3;
  static constexpr int PruneMaxAboveSea = 32;
  static constexpr int SpawnIslandFlatRadius = 48;
  static constexpr int SpawnIslandBlendRadius = 16;
  static constexpr float HillsVegetationHeightNormMax = 0.82f;
};

inline float HillsVegetationHeightNorm(int top_solid_y, int sea_level,
                                       int max_height)
{
  const int range = std::max(1, max_height - sea_level);
  return std::clamp(static_cast<float>(top_solid_y - sea_level) /
                        static_cast<float>(range),
                    0.0f, 1.0f);
}

inline bool HillsVegetationAllowed(int top_solid_y, int sea_level,
                                   int max_height)
{
  return HillsVegetationHeightNorm(top_solid_y, sea_level, max_height) <=
         URuntimeTuning::Get().HillsVegetationHeightNormMax;
}

inline int ComputeMaxScanY(int heightmap_surface_y, int sea_level,
                           int max_height)
{
  if (heightmap_surface_y >= 0)
  {
    return std::min(max_height - 1,
                    std::max(heightmap_surface_y +
                                 WorldGenPlacementTuning::SurfaceScanAboveHeightmap,
                             sea_level +
                                 WorldGenPlacementTuning::SurfaceScanMinAboveSea));
  }
  return std::min(max_height - 1,
                  sea_level + WorldGenPlacementTuning::SurfaceScanMinAboveSea);
}

} // namespace cutum
