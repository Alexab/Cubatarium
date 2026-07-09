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

int BlockEmissionLevel(const UBlockRegistry &registry, BlockId id)
{
  if (id == BLOCK_AIR)
  {
    return 0;
  }
  const std::string name = registry.GetTypeNameById(id);
  if (name.empty())
  {
    return 0;
  }
  if (name.find("torch") != std::string::npos ||
      name.find("lantern") != std::string::npos ||
      name.find("lamp") != std::string::npos)
  {
    return 13;
  }
  if (name.find("glow") != std::string::npos ||
      name.find("light") != std::string::npos)
  {
    return 12;
  }
  if (name.find("lava") != std::string::npos ||
      name.find("fire") != std::string::npos)
  {
    return 14;
  }
  if (registry.IsFireBlock(id))
  {
    return 14;
  }
  return 0;
}

} // namespace cutum
