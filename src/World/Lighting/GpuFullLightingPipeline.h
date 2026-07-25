#pragma once

#include "World/Lighting/FullLightingPipeline.h"

namespace cutum
{

using UCpuFullLightingPipeline = UFullLightingPipeline;

/// GPU full lighting pipeline. Compute flood lands later; delegates to Full for
/// parity. LightingPipelineFactory does not bind this until compute is ready.
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

private:
  UFullLightingPipeline Inner;
};

} // namespace cutum
