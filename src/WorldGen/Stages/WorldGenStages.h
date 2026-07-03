#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include <glm/glm.hpp>

namespace cutum
{

struct ColumnLayerRule
{
  BlockId surfaceBlock{BLOCK_AIR};
  BlockId subsurfaceBlock{BLOCK_AIR};
  BlockId fillerBlock{BLOCK_AIR};
  int dirtDepth{1};
  int stoneDepthBelowDirt{3};
};

void FillTerrainColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                       const ColumnLayerRule &rule);
void FillFluidColumn(WorldGenContext &ctx, int x, int z, int surfaceY);
void SealFluidPocketsInChunk(WorldGenContext &ctx, int base_x, int base_z);

int AdjustSurfaceYForSpawnIsland(int worldX, int worldZ, int naturalSurfaceY,
                                 const ProceduralSettings &settings,
                                 int centerX = 0, int centerZ = 0);

int LegacyHashSurfaceY(int x, int z, const ProceduralSettings &settings);
void FillLegacyHashColumn(WorldGenContext &ctx, int x, int z);

void FillFlatColumn(WorldGenContext &ctx, int x, int z);

} // namespace cutum
