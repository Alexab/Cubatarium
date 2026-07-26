#pragma once

#include "Render/Mesh/CpuFrustumCull.h"
#include <memory>

namespace cutum
{

/// Desktop GPU frustum cull backend. Opaque MDI path uses instanceCount cull
/// (P2); flat greedy refs for transparent/Cross rebuild via CPU Delegate —
/// no sync SSBO GetBufferSubData on the cruise hot path.
class UGpuFrustumCull final : public IUChunkCull
{
public:
  UGpuFrustumCull();
  ~UGpuFrustumCull() override;

  const char *BackendName() const override { return "gpu_frustum"; }
  bool SupportsGpuCull() const override { return true; }

  void RebuildVisible(UChunkMeshCache &cache, const Frustum *frustum,
                      const glm::vec3 *camera_pos,
                      float max_cull_distance) override;

private:
  struct GpuState;
  bool EnsureGpu();
  UCpuFrustumCull Delegate;
  std::unique_ptr<GpuState> State;
};

} // namespace cutum
