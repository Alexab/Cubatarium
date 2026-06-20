#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include <cstdint>

namespace cutum
{

void FillOreVeins(WorldGenContext &ctx, int x, int z, int surfaceY, uint32_t seed,
                  float oreDensity);

} // namespace cutum
