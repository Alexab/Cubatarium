#include "World/Lighting/FullLightingPipeline.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

void UFullLightingPipeline::FillChunkInitialLight(UChunk &chunk)
{
  // Full lighting leaves zero until Relight*; SoftDefer holds mesh until lit.
  (void)chunk;
}

void UFullLightingPipeline::FillAllLoadedChunks(UBlockWorld &world)
{
  (void)world;
}

void UFullLightingPipeline::RelightChunk(UBlockWorld &world,
                                         UBlockRegistry &registry,
                                         glm::ivec3 chunk_coord,
                                         bool include_block_light,
                                         bool include_skylight)
{
  cutum::RelightChunk(world, registry, chunk_coord, include_block_light,
                      include_skylight);
}

void UFullLightingPipeline::RelightChunkBlockLight(UBlockWorld &world,
                                                   UBlockRegistry &registry,
                                                   glm::ivec3 chunk_coord)
{
  cutum::RelightChunkBlockLight(world, registry, chunk_coord);
}

void UFullLightingPipeline::RelightColumnWithFrontier(
    UBlockWorld &world, UBlockRegistry &registry, int world_x, int world_z,
    int min_y, int max_y, bool include_block_light, bool include_skylight,
    std::vector<glm::ivec3> *out_relit_chunks)
{
  cutum::RelightColumnWithFrontier(world, registry, world_x, world_z, min_y,
                                   max_y, include_block_light, include_skylight,
                                   out_relit_chunks);
}

void UFullLightingPipeline::RelightColumn(UBlockWorld &world,
                                          UBlockRegistry &registry, int world_x,
                                          int world_z, int min_y, int max_y,
                                          bool include_block_light,
                                          bool include_skylight)
{
  cutum::RelightColumn(world, registry, world_x, world_z, min_y, max_y,
                       include_block_light, include_skylight);
}

void UFullLightingPipeline::RelightBlocksAroundEdit(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::vector<glm::ivec3> &block_positions)
{
  cutum::RelightBlocksAroundEdit(world, registry, block_positions);
}

RelightFrontierOutcome UFullLightingPipeline::RelightBlocksAroundAllEx(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::vector<glm::ivec3> &block_positions, int min_world_y,
    int max_world_y, bool include_block_light, int frontier_iterations)
{
  return cutum::RelightBlocksAroundAllEx(world, registry, block_positions,
                                         min_world_y, max_world_y,
                                         include_block_light,
                                         frontier_iterations);
}

void UFullLightingPipeline::RelightAllLoadedChunks(UBlockWorld &world,
                                                   UBlockRegistry &registry)
{
  cutum::RelightAllLoadedChunks(world, registry);
}

} // namespace cutum
