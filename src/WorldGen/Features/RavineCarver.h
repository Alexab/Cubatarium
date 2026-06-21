#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include <cstdint>

namespace cutum
{

struct WorldGenContext;

void CarveColumnRavines(WorldGenContext &ctx, int x, int z, int surfaceY,
                        uint32_t seed, const RavineParams &params);

} // namespace cutum
