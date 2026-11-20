#include "Render/Backend/RenderBackendCaps.h"
#include <cstdlib>

namespace cutum
{
namespace
{

RenderBackendCaps gActiveCaps = DetectRenderBackendCaps();
bool gActiveInitialized = false;

} // namespace

RenderBackendCaps DetectRenderBackendCaps()
{
  RenderBackendCaps caps;
  const char *force_cpu = std::getenv("CUBATARIUM_FORCE_CPU");
  caps.ForceCpuBackends = force_cpu && force_cpu[0] == '1' && force_cpu[1] == '\0';
  caps.AllowAndroidGpu = false;
  caps.ProbeCompleted = false;
  caps.AndroidGpuDenyReason = "n/a";
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  caps.Platform = RenderPlatformKind::Android;
  caps.HasCompute = false;
  caps.HasMultiDrawIndirect = false;
  caps.HasSsbo = false;
  caps.HasGlMapBufferRange = false;
  caps.PreferSinglePassTransparent = true;
#else
  caps.Platform = RenderPlatformKind::Desktop;
  caps.HasCompute = true;
  caps.HasMultiDrawIndirect = true;
  caps.HasSsbo = true;
  caps.HasGlMapBufferRange = true;
  caps.PreferSinglePassTransparent = false;
#endif
  return caps;
}

void SetActiveRenderBackendCaps(const RenderBackendCaps &caps)
{
  gActiveCaps = caps;
  gActiveInitialized = true;
}

const RenderBackendCaps &GetActiveRenderBackendCaps()
{
  if (!gActiveInitialized)
  {
    gActiveCaps = DetectRenderBackendCaps();
    gActiveInitialized = true;
  }
  return gActiveCaps;
}

void InvalidateRenderBackendCapsCache()
{
  gActiveCaps = DetectRenderBackendCaps();
  gActiveInitialized = false;
}

} // namespace cutum
