#include "World/Physics/FallingBlockRules.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

bool UFallingBlockRules::CanFall(const UBlockRegistry &registry,
                                 const UBlockWorld &world, glm::ivec3 blockPos)
{
  const BlockId id = world.GetBlock(blockPos);
  if (id == BLOCK_AIR || !registry.IsFallingBlock(id))
  {
    return false;
  }
  const glm::ivec3 below(blockPos.x, blockPos.y - 1, blockPos.z);
  return below.y >= 0 && world.IsAir(below);
}

bool UFallingBlockRules::TryApplyFall(const UBlockRegistry &registry,
                                      UBlockWorld &world, glm::ivec3 blockPos)
{
  if (!CanFall(registry, world, blockPos))
  {
    return false;
  }
  const BlockId id = world.GetBlock(blockPos);
  const glm::ivec3 below(blockPos.x, blockPos.y - 1, blockPos.z);
  world.SetBlock(blockPos, BLOCK_AIR);
  world.SetBlock(below, id);
  return true;
}

} // namespace cutum
