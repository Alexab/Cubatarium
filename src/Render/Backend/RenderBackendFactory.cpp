#include "Render/Backend/RenderBackendFactory.h"
#include "Render/Engine/CpuStagingGpuStore.h"
#include "Render/Engine/MdiVertexPoolStore.h"
#include "Render/Mesh/AndroidGpuGreedyMesher.h"
#include "Render/Mesh/CpuFrustumCull.h"
#include "Render/Mesh/CpuGreedyMesher.h"
#include "Render/Mesh/GpuFrustumCull.h"
#include "Render/Mesh/GpuGreedyMesher.h"
#include "glog/logging.h"

namespace cutum
{

URenderBackendBundle::URenderBackendBundle() = default;
URenderBackendBundle::~URenderBackendBundle() = default;
URenderBackendBundle::URenderBackendBundle(URenderBackendBundle &&) noexcept =
    default;
URenderBackendBundle &
URenderBackendBundle::operator=(URenderBackendBundle &&) noexcept = default;

bool URenderBackendFactory::BindOnce(URenderBackendBundle &bundle,
                                     const RenderBackendCaps &caps)
{
  if (bundle.Selection.Bound)
  {
    LOG(WARNING) << "[RenderBackend] BindOnce ignored — already bound ("
                 << (bundle.Mesher ? bundle.Mesher->BackendName() : "?") << "/"
                 << (bundle.Store ? bundle.Store->BackendName() : "?") << "/"
                 << (bundle.Cull ? bundle.Cull->BackendName() : "?") << ")";
    return false;
  }

  bundle.Selection = Select(caps);

  if (bundle.Selection.Mesher == MesherBackendKind::AndroidHybridGpu &&
      caps.AllowAndroidGpu && !caps.ForceCpuBackends)
  {
    bundle.Mesher = std::make_unique<UAndroidGpuGreedyMesher>();
  }
  else if (caps.HasCompute && !caps.ForceCpuBackends &&
           caps.Platform == RenderPlatformKind::Desktop &&
           bundle.Selection.Mesher == MesherBackendKind::GpuGreedy)
  {
    bundle.Mesher = std::make_unique<UGpuGreedyMesher>();
  }
  else
  {
    bundle.Mesher = std::make_unique<UCpuGreedyMesher>();
    bundle.Selection.Mesher = MesherBackendKind::CpuGreedy;
  }

  if (bundle.Selection.Store == MeshStoreBackendKind::MdiVertexPool)
  {
    bundle.Store = std::make_unique<UMdiVertexPoolStore>();
  }
  else
  {
    bundle.Store = std::make_unique<UCpuStagingGpuStore>();
  }

  if (bundle.Selection.Cull == CullBackendKind::GpuFrustum)
  {
    bundle.Cull = std::make_unique<UGpuFrustumCull>();
  }
  else
  {
    bundle.Cull = std::make_unique<UCpuFrustumCull>();
  }

  LOG(INFO) << "[RenderBackend] Bound mesher=" << bundle.Mesher->BackendName()
            << " store=" << bundle.Store->BackendName()
            << " cull=" << bundle.Cull->BackendName();
  return true;
}

} // namespace cutum
