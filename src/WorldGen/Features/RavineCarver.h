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

void CarveColumnRavinesDeterministic(WorldGenContext &ctx, int x, int z,
                                     int surface_y, const RavineParams &params,
                                     int sea_level,
                                     const RavineSurfaceYCallback &get_surface_y =
                                         {});

void CarveChunkRavines(WorldGenContext &ctx, int base_x, int base_z,
                       uint32_t seed, const RavineParams &params, int sea_level,
                       const RavineSurfaceYCallback &get_surface_y = {});

void CarveChunkRavinesDeterministic(WorldGenContext &ctx, int base_x, int base_z,
                                    int trigger_x, int trigger_z,
                                    const RavineParams &params, int sea_level,
                                    const RavineSurfaceYCallback &get_surface_y =
                                        {});

} // namespace cutum
