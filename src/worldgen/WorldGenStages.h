#pragma once

#include "WorldGenContext.h"
#include <glm/glm.hpp>

namespace cutum {

struct ColumnLayerRule {
 BlockId surfaceBlock{BLOCK_AIR};
 BlockId subsurfaceBlock{BLOCK_AIR};
 BlockId fillerBlock{BLOCK_AIR};
 int dirtDepth{1};
 int stoneDepthBelowDirt{3};
};

void FillTerrainColumn(WorldGenContext& ctx, int x, int z, int surfaceY, const ColumnLayerRule& rule);

int LegacyHashSurfaceY(int x, int z, const ProceduralSettings& settings);
void FillLegacyHashColumn(WorldGenContext& ctx, int x, int z);

void FillFlatColumn(WorldGenContext& ctx, int x, int z);

} // namespace cutum
