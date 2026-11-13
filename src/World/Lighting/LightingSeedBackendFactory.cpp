#include "World/Lighting/LightingSeedBackendFactory.h"
#include "World/Lighting/CpuLightingSeedBackend.h"
#include "World/Lighting/GpuLightingSeedBackend.h"

namespace cutum
{

std::unique_ptr<ILightingSeedBackend>
SelectLightingSeedBackend(UWorld &world, int relight_min, int relight_max,
                          const RenderBackendCaps &caps)
{
  if (PreferGpuLightingSeed(caps))
  {
    return std::make_unique<UGpuLightingSeedBackend>(world, relight_min,
                                                     relight_max);
  }
  return std::make_unique<UCpuLightingSeedBackend>(world, relight_min,
                                                   relight_max);
}

} // namespace cutum
