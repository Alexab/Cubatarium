#pragma once

#include "App/Settings/GraphicsQualityProfile.h"
#include "World/Lighting/ChunkLighting.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UChunk;

/// Pluggable CPU lighting backend. Full = skylight/blocklight propagation;
/// Flat = constant lightmap, no SoftDefer lit gate.
class IULightingPipeline
{
public:
  virtual ~IULightingPipeline() = default;

  virtual LightingMode GetMode() const = 0;
  virtual bool RequiresLitGate() const = 0;
  virtual bool AllowsAsyncRelight() const = 0;

  virtual void FillChunkInitialLight(UChunk &chunk) = 0;
  virtual void FillAllLoadedChunks(UBlockWorld &world) = 0;

  virtual void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                            glm::ivec3 chunk_coord,
                            bool include_block_light = true,
                            bool include_skylight = true) = 0;

  virtual void RelightChunkBlockLight(UBlockWorld &world,
                                      UBlockRegistry &registry,
                                      glm::ivec3 chunk_coord) = 0;

  virtual void RelightColumnWithFrontier(
      UBlockWorld &world, UBlockRegistry &registry, int world_x, int world_z,
      int min_y, int max_y, bool include_block_light, bool include_skylight,
      std::vector<glm::ivec3> *out_relit_chunks) = 0;

  virtual void RelightColumn(UBlockWorld &world, UBlockRegistry &registry,
                             int world_x, int world_z, int min_y, int max_y,
                             bool include_block_light = true,
                             bool include_skylight = true) = 0;

  virtual void RelightBlocksAroundEdit(
      UBlockWorld &world, UBlockRegistry &registry,
      const std::vector<glm::ivec3> &block_positions) = 0;

  virtual RelightFrontierOutcome RelightBlocksAroundAllEx(
      UBlockWorld &world, UBlockRegistry &registry,
      const std::vector<glm::ivec3> &block_positions, int min_world_y,
      int max_world_y, bool include_block_light, int frontier_iterations) = 0;

  virtual void RelightAllLoadedChunks(UBlockWorld &world,
                                      UBlockRegistry &registry) = 0;
};

} // namespace cutum
