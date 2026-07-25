#include "Navigation/WorldNavigationQueries.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Environment/CreatureTraverseQueries.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"

namespace cutum
{

NavigationStandNode NavigationStandNodeFromBody(const glm::vec3 &body_origin)
{
  NavigationStandNode node;
  node.x = WorldCoordToBlockIndex(body_origin.x);
  node.z = WorldCoordToBlockIndex(body_origin.z);
  node.ground_y = WorldCoordToBlockIndex(body_origin.y - 0.05f);
  return node;
}

UWorldNavigationQueries::UWorldNavigationQueries(const UWorld &world)
    : World(world)
{
}

bool UWorldNavigationQueries::IsTerrestrialStandNode(
    const NavigationStandNode &node, float body_height) const
{
  const UBlockRegistry &registry = World.GetBlockRegistry();
  const glm::ivec3 ground(node.x, node.ground_y, node.z);
  const BlockId ground_id = World.GetBlockWorld().GetBlock(ground);
  if (!registry.BlocksMovement(ground_id) || ground_id == BLOCK_AIR)
  {
    return false;
  }
  const float feet_y = BlockTopY(node.ground_y);
  const glm::vec3 size_blocks(0.6f, body_height, 0.6f);
  const glm::vec3 body_origin(static_cast<float>(node.x), feet_y,
                              static_cast<float>(node.z));
  // Nav-hot path: ground + block clearance only (no fluid volume probe).
  return CanCreatureStandAtNav(World, body_origin, size_blocks, 1.25f);
}

bool UWorldNavigationQueries::CanStepTerrestrial(
    const NavigationStandNode &from, const NavigationStandNode &to,
    float max_jump, float max_drop, float body_height) const
{
  const int dx = std::abs(to.x - from.x);
  const int dz = std::abs(to.z - from.z);
  if (dx + dz != 1)
  {
    return false;
  }
  if (!IsTerrestrialStandNode(to, body_height))
  {
    return false;
  }
  const float dy = static_cast<float>(to.ground_y - from.ground_y);
  if (dy > max_jump + 0.01f || dy < -max_drop - 0.01f)
  {
    return false;
  }
  // Climb-through cells only (not the landing solid at to.ground_y).
  // Old check used from.ground_y+1 at destination — for dy==1 that IS the
  // landing block, so every 1-block step-up was rejected (pits/stairs).
  if (dy > 0.01f)
  {
    for (int y = from.ground_y + 1; y < to.ground_y; ++y)
    {
      const glm::ivec3 climb_cell(to.x, y, to.z);
      if (World.GetBlockRegistry().BlocksMovement(
              World.GetBlockWorld().GetBlock(climb_cell)))
      {
        return false;
      }
    }
  }
  return true;
}

} // namespace cutum
