#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include <cstdint>
#include <functional>

namespace cutum
{

struct WorldGenContext;

using RavineSurfaceYCallback = std::function<int(int x, int z)>;

void CarveColumnRavines(WorldGenContext &ctx, int x, int z, int surface_y,
                        uint32_t seed, const RavineParams &params, int sea_level,
                        const RavineSurfaceYCallback &get_surface_y = {});

// Deterministic carve for unit tests (skips hash/shape gates, uses maxDepth).
void CarveColumnRavinesDeterministic(WorldGenContext &ctx, int x, int z,
                                     int surface_y, const RavineParams &params,
                                     int sea_level,
                                     const RavineSurfaceYCallback &get_surface_y =
                                         {});

} // namespace cutum
