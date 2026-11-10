#pragma once

#include "Render/Mesh/IUChunkCull.h"

namespace cutum
{

/// CPU frustum cull — delegates to ChunkMeshCache flat greedy rebuild.
class UCpuFrustumCull final : public IUChunkCull
{
public:
  const char *BackendName() const override { return "cpu_frustum"; }

  void RebuildVisible(UChunkMeshCache &cache, const Frustum *frustum,
                      const glm::vec3 *camera_pos,
                      float max_cull_distance) override;
};

} // namespace cutum
