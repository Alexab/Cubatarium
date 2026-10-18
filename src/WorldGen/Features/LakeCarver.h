#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include <cstdint>
#include <functional>

namespace cutum
{

struct WorldGenContext;

struct LakeParams
{
  bool enabled{true};
  int cellSize{128};
  int chancePerCell{5};
  int radiusMin{7};
  int radiusMax{15};
  int minElevationAboveSea{2};
  int maxElevationAboveSea{8};
  int maxExcavationDepth{14};
  int basinDepthMin{3};
  int basinDepthMax{7};
  float minMoisture{0.32f};
};

using LakeSurfaceYCallback = std::function<int(int, int)>;

void CarveLakeColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                     uint32_t seed, const LakeParams &params, int seaLevel,
                     const LakeSurfaceYCallback &get_surface_y);

} // namespace cutum
