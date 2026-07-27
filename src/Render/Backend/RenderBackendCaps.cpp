#include "Render/Backend/RenderBackendCaps.h"

namespace cutum
{

RenderBackendCaps DetectRenderBackendCaps()
{
  RenderBackendCaps caps;
  caps.ForceCpuBackends = false;
  caps.AllowAndroidGpu = false;
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

} // namespace cutum
