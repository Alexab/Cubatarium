#pragma once

#include "World/Lighting/FullLightingPipeline.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "Blocks/BlockRegistry.h"

namespace cutum
{

using UCpuFullLightingPipeline = UFullLightingPipeline;

/// Desktop GPU lighting: column skylight seed via compute when GL is available;
/// horizontal BFS / blocklight flood stay on CPU to preserve LitReady semantics.
/// Android never binds this class.
class UGpuFullLightingPipeline final : public IULightingPipeline
{
public:
  LightingMode GetMode() const override { return Inner.GetMode(); }
  bool RequiresLitGate() const override { return Inner.RequiresLitGate(); }
  bool AllowsAsyncRelight() const override
  {
    return Inner.AllowsAsyncRelight();
  }

  void FillChunkInitialLight(UChunk &chunk) override
  {
    Inner.FillChunkInitialLight(chunk);
  }
  void FillAllLoadedChunks(UBlockWorld &world) override
  {
    Inner.FillAllLoadedChunks(world);
  }

  void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                    glm::ivec3 chunk_coord, bool include_block_light = true,
                    bool include_skylight = true) override
  {
    if (include_skylight)
    {
      UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
      if (chunk && ApplyGpuSkylightSeedToChunk(*chunk, registry))
      {
        // Clear blocklight if needed, then horizontal sky + blocklight.
        // Sky columns already applied — skip PropagateSkylightColumn.
        if (include_block_light)
        {
          for (int ly = 0; ly < CHUNK_SIZE; ++ly)
          {
            for (int lz = 0; lz < CHUNK_SIZE; ++lz)
            {
              for (int lx = 0; lx < CHUNK_SIZE; ++lx)
              {
                const glm::ivec3 local(lx, ly, lz);
                const int sky = chunk->GetSkyLightLocal(local);
                chunk->SetLightLocal(local, sky, 0);
              }
            }
          }
        }
        RelightChunkAfterGpuSkySeed(world, registry, chunk_coord,
                                    include_block_light);
        return;
      }
    }
    Inner.RelightChunk(world, registry, chunk_coord, include_block_light,
                       include_skylight);
  }

  void RelightChunkBlockLight(UBlockWorld &world, UBlockRegistry &registry,
                              glm::ivec3 chunk_coord) override
  {
    Inner.RelightChunkBlockLight(world, registry, chunk_coord);
  }

  void RelightColumnWithFrontier(
      UBlockWorld &world, UBlockRegistry &registry, int world_x, int world_z,
      int min_y, int max_y, bool include_block_light, bool include_skylight,
      std::vector<glm::ivec3> *out_relit_chunks) override
  {
    Inner.RelightColumnWithFrontier(world, registry, world_x, world_z, min_y,
                                    max_y, include_block_light, include_skylight,
                                    out_relit_chunks);
  }

  void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                     int world_z, int min_y, int max_y,
                     bool include_block_light = true,
                     bool include_skylight = true) override
  {
    Inner.RelightColumn(world, registry, world_x, world_z, min_y, max_y,
                        include_block_light, include_skylight);
  }

  void RelightBlocksAroundEdit(
      UBlockWorld &world, UBlockRegistry &registry,
      const std::vector<glm::ivec3> &block_positions) override
  {
    Inner.RelightBlocksAroundEdit(world, registry, block_positions);
  }

  RelightFrontierOutcome RelightBlocksAroundAllEx(
      UBlockWorld &world, UBlockRegistry &registry,
      const std::vector<glm::ivec3> &block_positions, int min_world_y,
      int max_world_y, bool include_block_light,
      int frontier_iterations) override
  {
    return Inner.RelightBlocksAroundAllEx(
        world, registry, block_positions, min_world_y, max_world_y,
        include_block_light, frontier_iterations);
  }

  void RelightAllLoadedChunks(UBlockWorld &world,
                              UBlockRegistry &registry) override
  {
    Inner.RelightAllLoadedChunks(world, registry);
  }

  const char *BackendName() const { return "gpu_full_light"; }
  uint64_t GetComputeDispatchCount() const
  {
    return GpuSkylightSeedDispatchCount();
  }

private:
  UFullLightingPipeline Inner;
};

} // namespace cutum
