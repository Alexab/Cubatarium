#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Backend/RenderBackendFactory.h"

#include <cstdlib>
#include <iostream>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

int main()
{
  using cutum::CullBackendKind;
  using cutum::MesherBackendKind;
  using cutum::MeshStoreBackendKind;
  using cutum::RenderBackendCaps;
  using cutum::RenderBackendSelection;
  using cutum::RenderPlatformKind;
  using cutum::URenderBackendFactory;

  {
    RenderBackendCaps caps;
    caps.ForceCpuBackends = true;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Mesher == MesherBackendKind::CpuGreedy, "force-cpu mesher");
    Expect(sel.Cull == CullBackendKind::CpuFrustum, "force-cpu cull");
    Expect(sel.Store == MeshStoreBackendKind::CpuStaging,
           "force-cpu staging store (P0)");
    Expect(sel.Bound, "select marks bound");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Android;
    caps.HasCompute = true;
    caps.HasMultiDrawIndirect = true;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Mesher == MesherBackendKind::CpuGreedy,
           "android keeps cpu mesher");
    Expect(sel.Cull == CullBackendKind::CpuFrustum, "android keeps cpu cull");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Desktop;
    caps.HasMultiDrawIndirect = true;
    caps.ForceCpuBackends = false;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Store == MeshStoreBackendKind::MdiVertexPool,
           "desktop MDI selects mdi store");
  }

  {
    RenderBackendCaps caps;
    caps.ForceCpuBackends = true;
    caps.HasMultiDrawIndirect = true;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Store == MeshStoreBackendKind::CpuStaging,
           "force-cpu keeps staging");
  }

  if (gFails != 0)
  {
    std::cerr << "render_backend_factory_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "render_backend_factory_test: ok\n";
  return 0;
}
