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
    if (caps.Platform == RenderPlatformKind::Android)
    {
      sel.Mesher = MesherBackendKind::AndroidHybridGpu;
      // Honest CPU cull until a dedicated GLES frustum path lands.
      sel.Cull = CullBackendKind::CpuFrustum;
    }
    else
    {
      sel.Mesher = MesherBackendKind::GpuGreedy;
      sel.Cull = CullBackendKind::GpuFrustum;
    }
  }

  if (caps.HasMultiDrawIndirect && !caps.ForceCpuBackends &&
      caps.Platform == RenderPlatformKind::Desktop)
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
