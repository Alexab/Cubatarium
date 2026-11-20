#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Backend/RenderBackendFactory.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

int gFails = 0;

struct CpuOverrideScope
{
  bool present{std::getenv("CUBATARIUM_FORCE_CPU") != nullptr};
  std::string previous{present ? std::getenv("CUBATARIUM_FORCE_CPU") : ""};
  static void Set(const char *value)
  {
#if defined(_WIN32)
    _putenv_s("CUBATARIUM_FORCE_CPU", value ? value : "");
#else
    if (value) setenv("CUBATARIUM_FORCE_CPU", value, 1);
    else unsetenv("CUBATARIUM_FORCE_CPU");
#endif
  }
  ~CpuOverrideScope() { Set(present ? previous.c_str() : nullptr); }
};

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
    CpuOverrideScope restore;
    const auto hardware_defaults=cutum::DetectRenderBackendCaps();
    CpuOverrideScope::Set("1");
    const auto forced=cutum::DetectRenderBackendCaps();
    const auto selected=URenderBackendFactory::Select(forced);
    Expect(forced.ForceCpuBackends && forced.HasCompute==hardware_defaults.HasCompute &&
               forced.HasSsbo==hardware_defaults.HasSsbo,
           "CPU override selects policy without inventing missing hardware capabilities");
    Expect(selected.Mesher==MesherBackendKind::CpuGreedy &&
               selected.Store==MeshStoreBackendKind::CpuStaging && selected.Cull==CullBackendKind::CpuFrustum,
           "documented CPU environment override reaches all backend selections");
    for(const char *value : {"0", "1invalid", ""})
    {
      CpuOverrideScope::Set(value);
      Expect(!cutum::DetectRenderBackendCaps().ForceCpuBackends,
             "only explicit CPU override value 1 enables the policy");
    }
  }

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
    caps.HasSsbo = true;
    caps.HasMultiDrawIndirect = true;
    caps.AllowAndroidGpu = false;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Mesher == MesherBackendKind::CpuGreedy,
           "android keeps cpu mesher without AllowAndroidGpu");
    Expect(sel.Cull == CullBackendKind::CpuFrustum, "android keeps cpu cull");
    Expect(sel.Store == MeshStoreBackendKind::CpuStaging,
           "android keeps staging without AllowAndroidGpu");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Android;
    caps.HasCompute = true;
    caps.HasSsbo = true;
    caps.HasMultiDrawIndirect = false;
    caps.AllowAndroidGpu = true;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Mesher == MesherBackendKind::AndroidHybridGpu,
           "android AllowAndroidGpu selects android hybrid mesher");
    Expect(sel.Cull == CullBackendKind::CpuFrustum,
           "android hybrid keeps cpu cull");
    Expect(sel.Store == MeshStoreBackendKind::CpuStaging,
           "android without MDI keeps staging");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Android;
    caps.HasCompute = true;
    caps.HasSsbo = true;
    caps.AllowAndroidGpu = false;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Mesher == MesherBackendKind::CpuGreedy,
           "android opt-out keeps cpu mesher");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Desktop;
    caps.HasCompute = true;
    caps.HasSsbo = true;
    caps.HasMultiDrawIndirect = true;
    caps.ForceCpuBackends = false;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Store == MeshStoreBackendKind::MdiVertexPool,
           "desktop MDI selects mdi store");
    Expect(sel.Mesher == MesherBackendKind::GpuGreedy,
           "desktop compute selects gpu mesher");
    Expect(sel.Cull == CullBackendKind::GpuFrustum,
           "desktop compute selects gpu cull");
  }

  {
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Desktop;
    caps.HasCompute = true;
    caps.HasSsbo = false;
    caps.HasMultiDrawIndirect = true;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Mesher == MesherBackendKind::CpuGreedy,
           "desktop without SSBO keeps cpu mesher");
  }

  {
    RenderBackendCaps caps;
    caps.ForceCpuBackends = true;
    caps.HasMultiDrawIndirect = true;
    const RenderBackendSelection sel = URenderBackendFactory::Select(caps);
    Expect(sel.Store == MeshStoreBackendKind::CpuStaging,
           "force-cpu keeps staging");
  }

  {
    RenderBackendCaps caps;
    caps.ForceCpuBackends = true;
    caps.HasMultiDrawIndirect = true;
    const RenderBackendSelection a = URenderBackendFactory::Select(caps);
    const RenderBackendSelection b = URenderBackendFactory::Select(caps);
    Expect(a.Mesher == b.Mesher && a.Store == b.Store && a.Cull == b.Cull,
           "Select stable for same caps (BindOnce matrix parity)");
    Expect(a.Bound && b.Bound, "Select marks bound");
  }

  {
    using cutum::PreferGpuLightingSeed;
    RenderBackendCaps caps;
    caps.Platform = RenderPlatformKind::Desktop;
    caps.HasCompute = true;
    caps.HasSsbo = true;
    Expect(PreferGpuLightingSeed(caps), "desktop compute+ssbo prefers gpu seed");
    caps.ForceCpuBackends = true;
    Expect(!PreferGpuLightingSeed(caps), "force-cpu seed");
    caps.ForceCpuBackends = false;
    caps.HasSsbo = false;
    Expect(!PreferGpuLightingSeed(caps), "no ssbo → cpu seed");
  }

  if (gFails != 0)
  {
    std::cerr << "render_backend_factory_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "render_backend_factory_test: ok\n";
  return 0;
}
