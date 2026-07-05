#include "World/Core/RuntimeTuning.h"

#include "World/Physics/FluidTuning.h"

namespace cutum
{

URuntimeTuning &URuntimeTuning::Get()
{
  static URuntimeTuning instance;
  return instance;
}

void URuntimeTuning::ResetToDefaults()
{
  Get() = URuntimeTuning{};
}

} // namespace cutum
