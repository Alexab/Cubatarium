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
    // Desktop: GPU mesher + GPU cull (compute / parity wrappers).
    sel.Mesher = MesherBackendKind::GpuGreedy;
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
