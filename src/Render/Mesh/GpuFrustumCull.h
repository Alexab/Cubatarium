#pragma once

#include "Render/Mesh/CpuFrustumCull.h"
#include <memory>

namespace cutum
{

/// Desktop GPU frustum cull backend. Opaque MDI path uses GPU compact→indirect
/// instanceCount (P2 via MdiVertexPoolStore); flat greedy refs for
/// transparent/Cross rebuild via CPU Delegate.
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
