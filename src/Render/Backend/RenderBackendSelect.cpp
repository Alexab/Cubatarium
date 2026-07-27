#include "Render/Backend/RenderBackendFactory.h"

namespace cutum
{

RenderBackendSelection
URenderBackendFactory::Select(const RenderBackendCaps &caps)
{
  RenderBackendSelection sel;
  sel.Mesher = MesherBackendKind::CpuGreedy;
  sel.Cull = CullBackendKind::CpuFrustum;

  const bool want_gpu =
      !caps.ForceCpuBackends && caps.HasCompute && caps.HasSsbo &&
      (caps.Platform == RenderPlatformKind::Desktop || caps.AllowAndroidGpu);

  if (want_gpu)
  {
    sel.Mesher = MesherBackendKind::GpuGreedy;
    sel.Cull = CullBackendKind::GpuFrustum;
  }

  if (caps.HasMultiDrawIndirect && !caps.ForceCpuBackends &&
      (caps.Platform == RenderPlatformKind::Desktop || caps.AllowAndroidGpu))
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
