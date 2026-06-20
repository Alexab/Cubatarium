#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Features/PrefabFeatureConfig.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include <string>
#include <vector>

namespace cutum
{

struct FeatureParams
{
  const char *treeSmallPrefabName{"tree_small"};
  const char *treeLargePrefabName{"tree_large"};
  int treeSmallSpacingModPlains{40};
  int treeSmallSpacingModForest{25};
  int treeLargeSpacingModForest{100};
  uint32_t treeSeedOffset{4000};
  uint32_t treeLargeSeedOffset{5000};
};

bool CanPlacePrefabAt(const WorldGenContext &ctx, const std::string &prefabName,
                      glm::ivec3 anchorWorldPos);
bool PlacePrefabAt(WorldGenContext &ctx, const std::string &prefabName,
                   glm::ivec3 anchorWorldPos, int surfaceY = -1);

bool TryPlaceTree(WorldGenContext &ctx, int x, int z, int surfaceY,
                  BiomeId biome, const FeatureParams &params);

bool TryPlaceVegetationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome);
bool TryPlaceDecorationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome);
bool TryPlaceStructureFeatures(WorldGenContext &ctx, int x, int z,
                               int surfaceY, BiomeId biome);

bool TryPlaceLavaPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                      BiomeId biome);
bool TryPlaceFirePatch(WorldGenContext &ctx, int x, int z, int surfaceY,
                       BiomeId biome, BlockId grassId);

} // namespace cutum
