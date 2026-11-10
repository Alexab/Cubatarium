#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

class UDensityFieldSampler;
class UAquiferSampler;

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
                                  int surfaceY, int rawSurfaceY,
                                  const ColumnLayerRule &rule,
                                  const UDensityFieldSampler &sampler,
                                  const UAquiferSampler *aquiferSampler = nullptr);
bool ApplyGeologyColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                        uint32_t seed);
bool FillAquiferColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                       const UAquiferSampler &sampler);
void FillFluidColumn(WorldGenContext &ctx, int x, int z, int surfaceY);
/// `max_passes`: 16 on worker populate threads (deep pocket chains, W5.4);
/// main-thread committed drains pass 8 — each seal runs to completion on
/// the frame budget and 16 doubled the main-thread stall (load-bar
/// oscillation, user report).
bool SealFluidPocketsInChunk(WorldGenContext &ctx, int base_x, int base_z,
                             int *out_voxels_filled = nullptr,
                             int max_passes = 16);
struct FluidPermeableSealStats
{
  double floorScanMs{0.0};
  double restoreMs{0.0};
  double waterlogMs{0.0};
  uint64_t floorGroundChecks{0};
  uint64_t floorSlotChecks{0};
  uint64_t restoreCells{0};
  uint64_t waterlogCells{0};
};

bool SealFluidPermeableDecorInChunk(WorldGenContext &ctx, int base_x, int base_z,
                                    int *out_columns_changed = nullptr,
                                    FluidPermeableSealStats *out_stats = nullptr);
bool SealFluidShoreOnChunkCommitted(UBlockWorld &world, UBlockRegistry &registry,
                                    const ProceduralSettings &settings,
                                    const std::string &worldgen_owner_pack_id,
                                    glm::ivec3 chunk_coord,
                                    bool include_shore_air = true);
/// Intra-chunk pockets+permeable only (V_fluid IntraChunkSeal). Prefer this name
/// over SealFluidShoreOnChunkCommitted(..., false) at commit sites.
inline bool SealFluidIntraChunkOnCommitted(
    UBlockWorld &world, UBlockRegistry &registry,
    const ProceduralSettings &settings, const std::string &worldgen_owner_pack_id,
    glm::ivec3 chunk_coord)
{
  return SealFluidShoreOnChunkCommitted(world, registry, settings,
                                        worldgen_owner_pack_id, chunk_coord,
                                        /*include_shore_air=*/false);
}
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
