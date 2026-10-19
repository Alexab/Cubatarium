#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include <cstdint>
#include <functional>

namespace cutum
{

struct WorldGenContext;

struct ValleyParams
{
  bool enabled{true};
  int maxDepth{12};
  float widthSigma{2.5f};
  float aquaticDepthScale{0.4f};
  float riverNoiseScale{0.008f};
  bool fillWater{false};
  float waterMinStrength{0.65f};
  float waterMinMoisture{0.42f};
  int waterSurfaceDepth{0};
  int maxWaterElevationAboveSea{24};
};

using ValleySurfaceYCallback = std::function<int(int x, int z)>;

void CarveColumnValleys(WorldGenContext &ctx, int x, int z, int surface_y,
                        uint32_t seed, const ValleyParams &params, int sea_level,
                        float river_width, const ValleySurfaceYCallback &get_surface_y);

void CarveChunkValleys(WorldGenContext &ctx, int base_x, int base_z,
                       uint32_t seed, const ValleyParams &params, int sea_level,
                       float river_width,
                       const ValleySurfaceYCallback &get_surface_y);

} // namespace cutum
