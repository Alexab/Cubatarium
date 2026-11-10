#include "World/Lighting/LightingPipelineFactory.h"
#include "World/Lighting/FlatLightingPipeline.h"
#include "World/Lighting/FullLightingPipeline.h"
#include "World/Lighting/GpuFullLightingPipeline.h"

namespace cutum
{

std::unique_ptr<IULightingPipeline>
ULightingPipelineFactory::Create(LightingMode mode)
{
  if (mode == LightingMode::Flat)
  {
    return std::make_unique<UFlatLightingPipeline>();
  }
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  // Desktop: GPU lighting pipeline (delegates to Full until compute flood).
  return std::make_unique<UGpuFullLightingPipeline>();
#else
  return std::make_unique<UFullLightingPipeline>();
#endif
}

} // namespace cutum
