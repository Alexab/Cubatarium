#include "Navigation/WorldNavigationQueries.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
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
  const int clearance_cells =
      static_cast<int>(std::ceil(body_height + 0.15f));
  for (int dy = 1; dy <= clearance_cells; ++dy)
  {
    const BlockId above =
        World.GetBlockWorld().GetBlock(ground + glm::ivec3(0, dy, 0));
    if (registry.BlocksMovement(above))
    {
      return false;
    }
  }
  return true;
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
  if (dy > 0.01f)
  {
    const glm::ivec3 head_cell(to.x, from.ground_y + 1, to.z);
    if (World.GetBlockRegistry().BlocksMovement(
            World.GetBlockWorld().GetBlock(head_cell)))
    {
      return false;
    }
  }
  return true;
}

} // namespace cutum
