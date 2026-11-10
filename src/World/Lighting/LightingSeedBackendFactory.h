#pragma once

#include "World/Lighting/ILightingSeedBackend.h"
#include "Render/Backend/RenderBackendCaps.h"

#include <memory>

namespace cutum
{

class UWorld;

/// Select Cpu vs Gpu lighting seed backend from capability caps (E4).
/// PreferGpuLightingSeed lives in RenderBackendCaps.h.
std::unique_ptr<ILightingSeedBackend>
SelectLightingSeedBackend(UWorld &world, int relight_min, int relight_max,
                          const RenderBackendCaps &caps);

} // namespace cutum
