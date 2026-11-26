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

/// Desktop synchronous GPU transport after full-context geometry seeding.
/// Missing compute context/capacity/failure uses the same CPU solver. Async
/// jobs have independent CPU ownership and do not inherit this thread scope.
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
    // Section-local preview seeds cannot replace full-height vertical
    // propagation: their incoming sky is independent of roofs/water above.
    ScopedSyncRelightTransport transport(TryGpuTransportLight);
    Inner.RelightChunk(world, registry, chunk_coord, include_block_light,
                       include_skylight);
  }

  void RelightChunkBlockLight(UBlockWorld &world, UBlockRegistry &registry,
                              glm::ivec3 chunk_coord) override
  {
    ScopedSyncRelightTransport transport(TryGpuTransportLight);
    Inner.RelightChunkBlockLight(world, registry, chunk_coord);
  }

  void RelightColumnWithFrontier(
      UBlockWorld &world, UBlockRegistry &registry, int world_x, int world_z,
      int min_y, int max_y, bool include_block_light, bool include_skylight,
      std::vector<glm::ivec3> *out_relit_chunks) override
  {
    ScopedSyncRelightTransport transport(TryGpuTransportLight);
    Inner.RelightColumnWithFrontier(world, registry, world_x, world_z, min_y,
                                    max_y, include_block_light, include_skylight,
                                    out_relit_chunks);
  }

  void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                     int world_z, int min_y, int max_y,
                     bool include_block_light = true,
                     bool include_skylight = true) override
  {
    ScopedSyncRelightTransport transport(TryGpuTransportLight);
    Inner.RelightColumn(world, registry, world_x, world_z, min_y, max_y,
                        include_block_light, include_skylight);
  }

  void RelightBlocksAroundEdit(
      UBlockWorld &world, UBlockRegistry &registry,
      const std::vector<glm::ivec3> &block_positions) override
  {
    ScopedSyncRelightTransport transport(TryGpuTransportLight);
    Inner.RelightBlocksAroundEdit(world, registry, block_positions);
  }

  RelightFrontierOutcome RelightBlocksAroundAllEx(
      UBlockWorld &world, UBlockRegistry &registry,
      const std::vector<glm::ivec3> &block_positions, int min_world_y,
      int max_world_y, bool include_block_light,
      int frontier_iterations) override
  {
    ScopedSyncRelightTransport transport(TryGpuTransportLight);
    return Inner.RelightBlocksAroundAllEx(
        world, registry, block_positions, min_world_y, max_world_y,
        include_block_light, frontier_iterations);
  }

  void RelightAllLoadedChunks(UBlockWorld &world,
                              UBlockRegistry &registry) override
  {
    ScopedSyncRelightTransport transport(TryGpuTransportLight);
    Inner.RelightAllLoadedChunks(world, registry);
  }

  static size_t GetInnerInlineBytesForStorage() noexcept {return sizeof(Inner);}

  const char *BackendName() const { return "gpu_transport_with_cpu_fallback"; }
  uint64_t GetComputeDispatchCount() const
  {
    return GpuLightTransportDispatchCount();
  }

private:
  UFullLightingPipeline Inner;
};

} // namespace cutum
