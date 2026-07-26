#pragma once

#include "Render/Mesh/CpuFrustumCull.h"
#include <memory>

namespace cutum
{

/// GPU frustum cull backend. Desktop runs a compute AABB pass then delegates
/// visible-list rebuild to CPU (full compaction of flat refs is follow-up).
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
  bool Warmed{false};
};

} // namespace cutum
