#pragma once

#include "WorldGen/Features/PrefabFeatureConfig.h"
#include "WorldGen/Core/WorldGenContext.h"

namespace cutum
{

bool SatisfiesSurfaceConstraint(const WorldGenContext &ctx,
                                const SurfaceConstraint &constraint,
                                const std::string &prefab_name, int x, int z,
                                int surface_y);

bool FindWaterSurfaceAnchorForPlacement(const WorldGenContext &ctx, int x,
                                        int z, glm::ivec3 &anchor_out);

} // namespace cutum
