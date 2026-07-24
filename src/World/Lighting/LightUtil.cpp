#include "World/Lighting/LightUtil.h"

#include <string>

namespace cutum
{

bool IsLightTransparent(const UBlockRegistry &registry, BlockId id)
{
  if (id == BLOCK_AIR)
  {
    return true;
  }
  if (registry.IsLiquid(id))
  {
    return true;
  }
  if (registry.GetRenderStyle(id) == BlockRenderStyle::Cross)
  {
    return true;
  }
  return !registry.BlocksMovement(id);
}

} // namespace cutum
