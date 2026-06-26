#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Features/PrefabFeatureConfig.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include <string>
#include <vector>

namespace cutum
{

bool CanPlacePrefabAt(const WorldGenContext &ctx, const std::string &prefabName,
                      glm::ivec3 anchorWorldPos);
bool PlacePrefabAt(WorldGenContext &ctx, const std::string &prefabName,
                   glm::ivec3 anchorWorldPos, int surfaceY = -1);

bool TryPlaceVegetationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome);
bool TryPlaceGroundCoverFeatures(WorldGenContext &ctx, int x, int z, int surfaceY,
                                 BiomeId biome, bool skipIfTreeNearby);
bool TryPlaceDecorationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome);
bool TryPlaceStructureFeatures(WorldGenContext &ctx, int x, int z,
                               int surfaceY, BiomeId biome);

bool TryPlaceLavaPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                      BiomeId biome);
bool TryPlaceFirePatch(WorldGenContext &ctx, int x, int z, int surfaceY,
                       BiomeId biome, BlockId grassId);

void ResetScatterChunkCounts();

} // namespace cutum
