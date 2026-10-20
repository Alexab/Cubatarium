#include "World/Physics/FallingBlockRules.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

bool UFallingBlockRules::CanFall(const UBlockRegistry &registry,
                                 const UBlockWorld &world,
                                 glm::ivec3 blockPos)
{
  return CanFall(registry, world, blockPos,
                 world.GetVerticalProfile().MinY);
}

bool UFallingBlockRules::CanFall(const UBlockRegistry &registry,
                                 const UBlockWorld &world, glm::ivec3 blockPos,
                                 int min_world_y)
{
  const BlockId id = world.GetBlock(blockPos);
  if (id == BLOCK_AIR || !registry.IsFallingBlock(id))
  {
    return false;
  }
  const glm::ivec3 below(blockPos.x, blockPos.y - 1, blockPos.z);
  return below.y >= min_world_y && world.IsAir(below);
}

bool UFallingBlockRules::TryApplyFall(const UBlockRegistry &registry,
                                      UBlockWorld &world,
                                      glm::ivec3 blockPos)
{
  return TryApplyFall(registry, world, blockPos,
                      world.GetVerticalProfile().MinY);
}

bool UFallingBlockRules::TryApplyFall(const UBlockRegistry &registry,
                                      UBlockWorld &world, glm::ivec3 blockPos,
                                      int min_world_y)
{
  if (!CanFall(registry, world, blockPos, min_world_y))
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
