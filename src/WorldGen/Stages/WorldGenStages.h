#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include <glm/glm.hpp>

namespace cutum
{

class UDensityFieldSampler;

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
void FillTerrainColumnFromDensity(WorldGenContext &ctx, int x, int z,
                                  int surfaceY, const ColumnLayerRule &rule,
                                  const UDensityFieldSampler &sampler);
void FillFluidColumn(WorldGenContext &ctx, int x, int z, int surfaceY);
bool SealFluidPocketsInChunk(WorldGenContext &ctx, int base_x, int base_z);
bool SealFluidPermeableDecorInChunk(WorldGenContext &ctx, int base_x, int base_z);
bool SealFluidShoreOnChunkCommitted(UBlockWorld &world, UBlockRegistry &registry,
                                    const ProceduralSettings &settings,
                                    const std::string &worldgen_owner_pack_id,
                                    glm::ivec3 chunk_coord,
                                    bool include_shore_air = true);
bool SealFluidShoreAirOnChunkCommitted(
    UBlockWorld &world, UBlockRegistry &registry,
    const ProceduralSettings &settings, const std::string &worldgen_owner_pack_id,
    glm::ivec3 chunk_coord);

int PruneFloatingVegetationInChunk(WorldGenContext &ctx, int base_x, int base_z);

int AdjustSurfaceYForSpawnIsland(int worldX, int worldZ, int naturalSurfaceY,
                                 const ProceduralSettings &settings,
                                 int centerX = 0, int centerZ = 0);

int LegacyHashSurfaceY(int x, int z, const ProceduralSettings &settings);
void FillLegacyHashColumn(WorldGenContext &ctx, int x, int z);

void FillFlatColumn(WorldGenContext &ctx, int x, int z);

} // namespace cutum
