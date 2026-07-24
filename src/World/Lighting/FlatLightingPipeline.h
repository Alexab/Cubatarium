#pragma once

#include "World/Lighting/IULightingPipeline.h"

namespace cutum
{

/// Constant skylight lightmap (no CPU propagation). Used by Performance preset.
class UFlatLightingPipeline final : public IULightingPipeline
{
public:
  LightingMode GetMode() const override { return LightingMode::Flat; }
  bool RequiresLitGate() const override { return false; }
  bool AllowsAsyncRelight() const override { return false; }

  void FillChunkInitialLight(UChunk &chunk) override;
  void FillAllLoadedChunks(UBlockWorld &world) override;

  void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                    glm::ivec3 chunk_coord, bool include_block_light,
                    bool include_skylight) override;

  void RelightChunkBlockLight(UBlockWorld &world, UBlockRegistry &registry,
                              glm::ivec3 chunk_coord) override;

  void RelightColumnWithFrontier(UBlockWorld &world, UBlockRegistry &registry,
                                 int world_x, int world_z, int min_y, int max_y,
                                 bool include_block_light, bool include_skylight,
                                 std::vector<glm::ivec3> *out_relit_chunks) override;

  void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                     int world_z, int min_y, int max_y, bool include_block_light,
                     bool include_skylight) override;

  void RelightBlocksAroundEdit(
      UBlockWorld &world, UBlockRegistry &registry,
      const std::vector<glm::ivec3> &block_positions) override;

  RelightFrontierOutcome RelightBlocksAroundAllEx(
      UBlockWorld &world, UBlockRegistry &registry,
      const std::vector<glm::ivec3> &block_positions, int min_world_y,
      int max_world_y, bool include_block_light,
      int frontier_iterations) override;

  void RelightAllLoadedChunks(UBlockWorld &world,
                              UBlockRegistry &registry) override;

private:
  static void FillChunkFlat(UChunk &chunk);
};

} // namespace cutum
