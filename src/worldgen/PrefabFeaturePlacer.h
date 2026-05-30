#pragma once

#include "WorldGenContext.h"
#include "BiomeSampler.h"
#include <string>

namespace cutum {

struct FeatureParams {
 const char* treePrefabName{"tree_small"};
 int treeSpacingMod{40};
 uint32_t treeSeedOffset{4000};
};

bool CanPlacePrefabAt(const WorldGenContext& ctx, const std::string& prefabName, glm::ivec3 anchorWorldPos);
bool PlacePrefabAt(WorldGenContext& ctx, const std::string& prefabName, glm::ivec3 anchorWorldPos);

bool TryPlaceTree(WorldGenContext& ctx, int x, int z, int surfaceY, BiomeId biome, const FeatureParams& params);

} // namespace cutum
