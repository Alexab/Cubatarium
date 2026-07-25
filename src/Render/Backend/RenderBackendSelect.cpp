#include "Render/Backend/RenderBackendFactory.h"

namespace cutum
{

RenderBackendSelection
URenderBackendFactory::Select(const RenderBackendCaps &caps)
{
  RenderBackendSelection sel;
  sel.Mesher = MesherBackendKind::CpuGreedy;
  sel.Cull = CullBackendKind::CpuFrustum;

  if (!caps.ForceCpuBackends && caps.Platform == RenderPlatformKind::Desktop &&
      caps.HasCompute)
  {
    // Phase 3+: GPU mesher selectable; keep CPU until compute kernels land.
    // Cull may use GpuFrustum wrapper (delegates CPU rebuild today).
    sel.Cull = CullBackendKind::GpuFrustum;
  }

  if (caps.HasMultiDrawIndirect && !caps.ForceCpuBackends)
  {
    sel.Store = MeshStoreBackendKind::MdiVertexPool;
  }
  else
  {
    sel.Store = MeshStoreBackendKind::CpuStaging;
  }

  sel.Bound = true;
  return sel;
}

} // namespace cutum
