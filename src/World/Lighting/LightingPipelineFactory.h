#pragma once

#include "World/Lighting/IULightingPipeline.h"
#include <memory>

namespace cutum
{

class ULightingPipelineFactory
{
public:
  static std::unique_ptr<IULightingPipeline> Create(LightingMode mode);
};

} // namespace cutum
