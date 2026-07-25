#pragma once

#include "Render/Mesh/CpuFrustumCull.h"

namespace cutum
{

/// GPU frustum cull backend (compute compaction later). Currently delegates to
/// CPU frustum rebuild; SupportsGpuCull() reports true for telemetry/bind.
class UGpuFrustumCull final : public IUChunkCull
{
public:
  const char *BackendName() const override { return "gpu_frustum"; }
  bool SupportsGpuCull() const override { return true; }

  void RebuildVisible(UChunkMeshCache &cache, const Frustum *frustum,
                      const glm::vec3 *camera_pos,
                      float max_cull_distance) override
  {
    Delegate.RebuildVisible(cache, frustum, camera_pos, max_cull_distance);
  }

private:
  UCpuFrustumCull Delegate;
};

} // namespace cutum
