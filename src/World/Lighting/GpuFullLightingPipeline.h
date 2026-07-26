#pragma once

#include "World/Lighting/FullLightingPipeline.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "Blocks/BlockRegistry.h"
#include <array>

namespace cutum
{

using UCpuFullLightingPipeline = UFullLightingPipeline;

/// Desktop GPU lighting: column skylight seed via compute when GL is available;
/// full BFS/flood still runs on CPU (Inner) to preserve LitReady semantics.
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
    // Authoritative lightmap: Full CPU. Skylight column-seed compute is warmed
    // once (not per Relight) so LitReady / autofly wall are not tanked by
    // sync SSBO readback on the streaming hot path.
    WarmSkylightSeedOnce(world, registry, chunk_coord);
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
  void WarmSkylightSeedOnce(UBlockWorld &world, UBlockRegistry &registry,
                            glm::ivec3 chunk_coord)
  {
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
    (void)world;
    (void)registry;
    (void)chunk_coord;
#else
    if (SeedWarmed)
    {
      return;
    }
    SeedWarmed = true;
    UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
    if (!chunk)
    {
      return;
    }
    std::array<uint8_t, CHUNK_VOLUME> occ{};
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int z = 0; z < CHUNK_SIZE; ++z)
      {
        for (int x = 0; x < CHUNK_SIZE; ++x)
        {
          const glm::ivec3 local(x, y, z);
          const BlockId id = chunk->GetBlockLocal(local);
          const int li = (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
          occ[static_cast<size_t>(li)] =
              (id != 0 && !registry.IsTransparent(id) && registry.IsSolid(id))
                  ? 1u
                  : 0u;
        }
      }
    }
    std::array<uint8_t, CHUNK_VOLUME> sky{};
    (void)TryGpuSeedSkylightColumns(occ, sky);
#endif
  }

  UFullLightingPipeline Inner;
  bool SeedWarmed{false};
};

} // namespace cutum
