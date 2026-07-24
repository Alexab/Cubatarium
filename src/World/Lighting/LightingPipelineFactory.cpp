#include "World/Lighting/LightingPipelineFactory.h"
#include "World/Lighting/FlatLightingPipeline.h"
#include "World/Lighting/FullLightingPipeline.h"

namespace cutum
{

std::unique_ptr<IULightingPipeline>
ULightingPipelineFactory::Create(LightingMode mode)
{
  if (mode == LightingMode::Flat)
  {
    return std::make_unique<UFlatLightingPipeline>();
  }
  return std::make_unique<UFullLightingPipeline>();
}

} // namespace cutum
