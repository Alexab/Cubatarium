#include "World/Lighting/FlatLightingPipeline.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/LightUtil.h"
#include "World/Math/BlockTypes.h"
#include <unordered_set>
#include <vector>

namespace cutum
{

void UFlatLightingPipeline::FillChunkFlat(UChunk &chunk)
{
  const uint8_t packed = PackLight(kMaxLightLevel, 0);
  chunk.GetLightDataMutable().fill(packed);
}

void UFlatLightingPipeline::FillChunkInitialLight(UChunk &chunk)
{
  FillChunkFlat(chunk);
}

void UFlatLightingPipeline::FillAllLoadedChunks(UBlockWorld &world)
{
  std::vector<glm::ivec3> coords;
  world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk) { coords.push_back(chunk.GetCoord()); });
  for (const glm::ivec3 &coord : coords)
  {
    if (UChunk *chunk = world.GetChunkManager().GetChunk(coord))
    {
      FillChunkFlat(*chunk);
    }
  }
}

void UFlatLightingPipeline::RelightChunk(UBlockWorld &world,
                                         UBlockRegistry &registry,
                                         glm::ivec3 chunk_coord,
                                         bool include_block_light,
                                         bool include_skylight)
{
  (void)registry;
  (void)include_block_light;
  (void)include_skylight;
  if (UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord))
  {
    FillChunkFlat(*chunk);
  }
}

void UFlatLightingPipeline::RelightChunkBlockLight(UBlockWorld &world,
                                                   UBlockRegistry &registry,
                                                   glm::ivec3 chunk_coord)
{
  RelightChunk(world, registry, chunk_coord, true, false);
}

void UFlatLightingPipeline::RelightColumnWithFrontier(
    UBlockWorld &world, UBlockRegistry &registry, int world_x, int world_z,
    int min_y, int max_y, bool include_block_light, bool include_skylight,
    std::vector<glm::ivec3> *out_relit_chunks)
{
  (void)registry;
  (void)include_block_light;
  (void)include_skylight;
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  const int min_cy = UChunkManager::WorldToChunk(glm::ivec3(0, min_y, 0)).y;
  const int max_cy = UChunkManager::WorldToChunk(glm::ivec3(0, max_y, 0)).y;
  const glm::ivec3 base =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, 0, world_z));
  for (int cy = min_cy; cy <= max_cy; ++cy)
  {
    const glm::ivec3 coord(base.x, cy, base.z);
    if (UChunk *chunk = world.GetChunkManager().GetChunk(coord))
    {
      FillChunkFlat(*chunk);
      coords.insert(coord);
    }
  }
  if (out_relit_chunks)
  {
    out_relit_chunks->assign(coords.begin(), coords.end());
  }
}

void UFlatLightingPipeline::RelightColumn(UBlockWorld &world,
                                          UBlockRegistry &registry, int world_x,
                                          int world_z, int min_y, int max_y,
                                          bool include_block_light,
                                          bool include_skylight)
{
  RelightColumnWithFrontier(world, registry, world_x, world_z, min_y, max_y,
                            include_block_light, include_skylight, nullptr);
}

void UFlatLightingPipeline::RelightBlocksAroundEdit(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::vector<glm::ivec3> &block_positions)
{
  (void)registry;
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  for (const glm::ivec3 &pos : block_positions)
  {
    coords.insert(UChunkManager::WorldToChunk(pos));
  }
  for (const glm::ivec3 &coord : coords)
  {
    if (UChunk *chunk = world.GetChunkManager().GetChunk(coord))
    {
      FillChunkFlat(*chunk);
    }
  }
}

RelightFrontierOutcome UFlatLightingPipeline::RelightBlocksAroundAllEx(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::vector<glm::ivec3> &block_positions, int min_world_y,
    int max_world_y, bool include_block_light, int frontier_iterations)
{
  (void)min_world_y;
  (void)max_world_y;
  (void)include_block_light;
  (void)frontier_iterations;
  RelightFrontierOutcome outcome;
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  for (const glm::ivec3 &pos : block_positions)
  {
    coords.insert(UChunkManager::WorldToChunk(pos));
  }
  for (const glm::ivec3 &coord : coords)
  {
    RelightChunk(world, registry, coord, true, true);
    outcome.relit_chunks.push_back(coord);
  }
  outcome.frontier_unfinished = false;
  return outcome;
}

void UFlatLightingPipeline::RelightAllLoadedChunks(UBlockWorld &world,
                                                   UBlockRegistry &registry)
{
  (void)registry;
  FillAllLoadedChunks(world);
}

} // namespace cutum
