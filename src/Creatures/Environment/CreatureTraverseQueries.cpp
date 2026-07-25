#include "Creatures/Environment/CreatureTraverseQueries.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/CreatureBounds.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace cutum
{
namespace
{

std::optional<float> ResolveGroundFeetY(const UWorld &world, int gx, int gz,
                                        float reference_feet_y)
{
  if (const std::optional<float> feet_y =
          world.QueryGroundFeetYUnder(gx, gz, reference_feet_y))
  {
    return feet_y;
  }
  return world.QueryGroundFeetYColumn(gx, gz);
}

bool IsFluidBlockId(const UBlockRegistry &registry, BlockId id)
{
  if (id == BLOCK_AIR || registry.BlocksMovement(id))
  {
    return false;
  }
  if (registry.GetRenderStyle(id) == BlockRenderStyle::Fluid)
  {
    return true;
  }
  const std::string &name = registry.GetTypeNameById(id);
  return name.find("water") != std::string::npos ||
         name.find("lava") != std::string::npos;
}

/// O(height) cell samples — not full AABB fluid volume scan.
bool BodyColumnTouchesFluid(const UWorld &world, const glm::vec3 &feet_origin,
                            float body_height)
{
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const UBlockWorld &blocks = world.GetBlockWorld();
  const int gx = WorldCoordToBlockIndex(feet_origin.x);
  const int gz = WorldCoordToBlockIndex(feet_origin.z);
  const int y0 = WorldCoordToBlockIndex(feet_origin.y);
  const int y1 =
      WorldCoordToBlockIndex(feet_origin.y + std::max(0.5f, body_height) - 0.05f);
  for (int y = y0; y <= y1; ++y)
  {
    if (IsFluidBlockId(registry, blocks.GetBlock(glm::ivec3(gx, y, gz))))
    {
      return true;
    }
  }
  return false;
}

} // namespace

bool AreBlocksClearAt(const UWorld &world, const glm::vec3 &body_origin,
                      const glm::vec3 &size_blocks)
{
  glm::vec3 collide_origin = body_origin;
  collide_origin.y += kCreatureStandCollisionSkin;
  const CollisionVolume vol =
      CollisionVolumeFromBody(collide_origin, size_blocks);
  return !world.CheckBlockCollisionVolume(vol);
}

bool CanCreatureStandAtNav(const UWorld &world, const glm::vec3 &body_origin,
                           const glm::vec3 &size_blocks,
                           float max_climb_drop_blocks)
{
  const float max_step = std::max(0.25f, max_climb_drop_blocks);
  const int gx = WorldCoordToBlockIndex(body_origin.x);
  const int gz = WorldCoordToBlockIndex(body_origin.z);
  const std::optional<float> feet_y =
      ResolveGroundFeetY(world, gx, gz, body_origin.y);
  if (!feet_y)
  {
    return false;
  }
  const float climb = *feet_y - body_origin.y;
  const float drop = body_origin.y - *feet_y;
  if (climb > max_step || drop > max_step)
  {
    return false;
  }
  glm::vec3 feet_origin = body_origin;
  feet_origin.y = *feet_y;
  return AreBlocksClearAt(world, feet_origin, size_blocks);
}

bool CanCreatureStandAt(const UWorld &world, const glm::vec3 &body_origin,
                        const glm::vec3 &size_blocks,
                        float max_climb_drop_blocks)
{
  if (!CanCreatureStandAtNav(world, body_origin, size_blocks,
                             max_climb_drop_blocks))
  {
    return false;
  }
  const int gx = WorldCoordToBlockIndex(body_origin.x);
  const int gz = WorldCoordToBlockIndex(body_origin.z);
  const std::optional<float> feet_y =
      ResolveGroundFeetY(world, gx, gz, body_origin.y);
  if (!feet_y)
  {
    return false;
  }
  glm::vec3 feet_origin = body_origin;
  feet_origin.y = *feet_y;
  return !BodyColumnTouchesFluid(world, feet_origin, size_blocks.y);
}

bool CanCreatureStep(const UWorld &world, const glm::vec3 &from_body,
                     const glm::vec3 &to_body, const glm::vec3 &size_blocks,
                     float max_jump, float max_drop)
{
  const float dx = to_body.x - from_body.x;
  const float dz = to_body.z - from_body.z;
  const float horiz = std::sqrt(dx * dx + dz * dz);
  if (horiz < 1e-4f || horiz > 1.55f)
  {
    return false;
  }
  if (!CanCreatureStandAtNav(world, to_body, size_blocks,
                             std::max(max_jump, max_drop)))
  {
    return false;
  }
  const float dy = to_body.y - from_body.y;
  if (dy > max_jump + 0.01f || dy < -max_drop - 0.01f)
  {
    return false;
  }
  // Climb-through only — do not treat the destination ground cell as headroom.
  if (dy > 0.01f)
  {
    const int to_gx = WorldCoordToBlockIndex(to_body.x);
    const int to_gz = WorldCoordToBlockIndex(to_body.z);
    const int from_ground = WorldCoordToBlockIndex(from_body.y - 0.05f);
    const int to_ground = WorldCoordToBlockIndex(to_body.y - 0.05f);
    for (int y = from_ground + 1; y < to_ground; ++y)
    {
      const glm::ivec3 climb_cell(to_gx, y, to_gz);
      if (world.GetBlockRegistry().BlocksMovement(
              world.GetBlockWorld().GetBlock(climb_cell)))
      {
        return false;
      }
    }
  }
  return true;
}

bool CanCreatureMoveDirectlyXZ(const UWorld &world, const glm::vec3 &from_body,
                               const glm::vec3 &to_body,
                               const glm::vec3 &size_blocks,
                               float max_climb_drop_blocks, float sample_step)
{
  glm::vec3 delta = to_body - from_body;
  delta.y = 0.0f;
  const float dist = glm::length(delta);
  if (dist < 1e-4f)
  {
    return CanCreatureStandAtNav(world, from_body, size_blocks,
                                 max_climb_drop_blocks);
  }
  const glm::vec3 dir = delta / dist;
  const float step = std::max(0.45f, sample_step);
  const int samples =
      std::min(6, std::max(1, static_cast<int>(std::ceil(dist / step))));
  for (int i = 1; i <= samples; ++i)
  {
    const float t = dist * (static_cast<float>(i) / static_cast<float>(samples));
    glm::vec3 sample = from_body + dir * t;
    sample.y = from_body.y;
    if (!CanCreatureStandAtNav(world, sample, size_blocks, max_climb_drop_blocks))
    {
      return false;
    }
  }
  return true;
}

} // namespace cutum
