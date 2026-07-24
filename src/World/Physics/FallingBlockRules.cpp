#include "World/Physics/FallingBlockRules.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

namespace
{

bool IsFallingBlock(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (id == BLOCK_AIR)
  {
    return false;
  }
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Falling;
  }
  return false;
}

} // namespace

bool UFallingBlockRules::CanFall(const UBlockDefinitionStorage &definitions,
                                 const UBlockWorld &world, glm::ivec3 blockPos)
{
  const BlockId id = world.GetBlock(blockPos);
  if (!IsFallingBlock(definitions, id))
  {
    return false;
  }
  const glm::ivec3 below(blockPos.x, blockPos.y - 1, blockPos.z);
  return below.y >= 0 && world.IsAir(below);
}

bool UFallingBlockRules::TryApplyFall(const UBlockDefinitionStorage &definitions,
                                      UBlockWorld &world, glm::ivec3 blockPos)
{
  if (!CanFall(definitions, world, blockPos))
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
