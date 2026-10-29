#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/CreatureBounds.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_map>

namespace
{

void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_terrestrial_gate_test: " << message << std::endl;
    std::exit(1);
  }
}

void InstallSolid(cutum::UBlockDefinitionStorage &storage, cutum::BlockId id,
                  const std::string &name)
{
  cutum::BlockDefinition def;
  def.Name = name;
  def.Id = id;
  def.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[id] = def;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id[name] = id;
  storage.ReplaceAll(std::move(by_id), std::move(name_to_id));
}

/// Soft post-motor terrestrial criteria (mirrors CanCreatureStandAt /
/// HabitatAllowsMovementAt): ground under feet within step + no block
/// collision. Does NOT require HasGroundSupportVolume multi-sample footprint.
bool SoftTerrestrialOk(const cutum::UWorldCollision &collision, float feet_x,
                       float feet_z, float feet_y_ref,
                       const glm::vec3 &size_blocks, float max_step)
{
  const int gx = cutum::WorldCoordToBlockIndex(feet_x);
  const int gz = cutum::WorldCoordToBlockIndex(feet_z);
  const std::optional<float> ground =
      collision.QueryGroundFeetYUnder(gx, gz, feet_y_ref);
  if (!ground)
  {
    return false;
  }
  const float climb = *ground - feet_y_ref;
  const float drop = feet_y_ref - *ground;
  if (climb > max_step || drop > max_step)
  {
    return false;
  }
  const cutum::CollisionVolume vol = cutum::CollisionVolumeFromBody(
      glm::vec3(feet_x, *ground + 0.01f, feet_z), size_blocks);
  return !collision.CheckBlockCollisionVolume(vol);
}

} // namespace

int main()
{
  constexpr cutum::BlockId kStone = 12;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  InstallSolid(*definitions, kStone, "stone");
  cutum::UBlockRegistry registry(nullptr, definitions);

  // Flat 5x5 floor at y=53 — zombie-sized biped must accept small chase steps.
  cutum::UBlockWorld floor;
  for (int x = -2; x <= 2; ++x)
  {
    for (int z = -2; z <= 2; ++z)
    {
      floor.SetBlock(glm::ivec3(x, 53, z), kStone);
    }
  }
  cutum::UWorldCollision collision(floor);
  collision.SetBlockRegistry(&registry);
  collision.SetEntityCollisionEnabled(false);

  const glm::vec3 zombie_size(0.6f, 1.85f, 0.6f);
  const float feet_y = cutum::BlockTopY(53);
  const float max_step = 1.0f;

  Expect(SoftTerrestrialOk(collision, 0.0f, 0.0f, feet_y, zombie_size, max_step),
         "zombie size on flat BlockTopY should pass soft gate");

  // Fractional body like live diag (-7641.70 style offsets on cell edge).
  Expect(SoftTerrestrialOk(collision, 0.30f, -0.30f, feet_y, zombie_size,
                           max_step),
         "soft gate must accept near cell-edge feet");
  Expect(SoftTerrestrialOk(collision, 0.30f, -0.30f, feet_y + 0.02f, zombie_size,
                           max_step),
         "soft gate must heal tiny float above BlockTopY");

  // Small chase step (diag showed ~0.014 before reject).
  Expect(SoftTerrestrialOk(collision, 0.014f, -0.010f, feet_y, zombie_size,
                           max_step),
         "soft gate must accept motor-scale XZ step on flat");

  // Single solid column under feet: soft gate uses column ground query (not
  // multi-sample footprint). Clearance AABB with stand skin must still pass.
  cutum::UBlockWorld ledge;
  ledge.SetBlock(glm::ivec3(0, 10, 0), kStone);
  cutum::UWorldCollision ledge_collision(ledge);
  ledge_collision.SetBlockRegistry(&registry);
  ledge_collision.SetEntityCollisionEnabled(false);
  const float ledge_feet = cutum::BlockTopY(10);
  Expect(SoftTerrestrialOk(ledge_collision, 0.0f, 0.0f, ledge_feet, zombie_size,
                           max_step),
         "single-column under feet: soft gate must pass");
  Expect(!SoftTerrestrialOk(ledge_collision, 1.0f, 0.0f, ledge_feet, zombie_size,
                            max_step),
         "adjacent air column: soft gate must reject (no ground)");

  // Body support (creatures): the box rests on blocks under ANY part of it.
  // 5x5 floor at y=53 with a 1x1 hole at the center column.
  cutum::UBlockWorld holed;
  for (int x = -2; x <= 2; ++x)
  {
    for (int z = -2; z <= 2; ++z)
    {
      if (x != 0 || z != 0)
      {
        holed.SetBlock(glm::ivec3(x, 53, z), kStone);
      }
    }
  }
  cutum::UWorldCollision holed_collision(holed);
  holed_collision.SetBlockRegistry(&registry);
  holed_collision.SetEntityCollisionEnabled(false);
  const glm::vec3 cow_size(1.1f, 1.3f, 1.8f);
  const cutum::CollisionVolume cow_over_hole = cutum::CollisionVolumeFromBody(
      glm::vec3(0.0f, feet_y, 0.0f), cow_size);
  Expect(holed_collision.HasBodySupport(cow_over_hole),
         "wide body spanning a 1x1 hole rests on its rim");
  Expect(!holed_collision.HasGroundSupportVolume(cow_over_hole, feet_y),
         "center-footprint rule (player) calls the same body unsupported");

  // Zombie center just past a ledge edge, box still overlapping the ledge
  // top: it cannot fall, so it is supported (the stall that hung mobs in
  // "Fall" with gravity growing every frame).
  const cutum::CollisionVolume on_rim = cutum::CollisionVolumeFromBody(
      glm::vec3(0.75f, ledge_feet, 0.0f), zombie_size);
  Expect(ledge_collision.HasBodySupport(on_rim),
         "body overlapping the ledge top by its edge is supported");
  const cutum::CollisionVolume off_rim = cutum::CollisionVolumeFromBody(
      glm::vec3(0.85f, ledge_feet, 0.0f), zombie_size);
  Expect(!ledge_collision.HasBodySupport(off_rim),
         "body past the ledge edge is not supported");
  const cutum::CollisionVolume floating = cutum::CollisionVolumeFromBody(
      glm::vec3(0.0f, ledge_feet + 0.2f, 0.0f), zombie_size);
  Expect(!ledge_collision.HasBodySupport(floating),
         "body 0.2 above the ground is not supported");

  // A wall touching the side of the box is not ground.
  cutum::UBlockWorld wall;
  wall.SetBlock(glm::ivec3(1, 10, 0), kStone);
  wall.SetBlock(glm::ivec3(1, 11, 0), kStone);
  cutum::UWorldCollision wall_collision(wall);
  wall_collision.SetBlockRegistry(&registry);
  wall_collision.SetEntityCollisionEnabled(false);
  const cutum::CollisionVolume beside_wall = cutum::CollisionVolumeFromBody(
      glm::vec3(0.5f - 0.3f, cutum::BlockTopY(10), 0.0f), zombie_size);
  Expect(!wall_collision.HasBodySupport(beside_wall),
         "a wall beside the body does not count as support");

  std::cout << "creature_terrestrial_gate_test: OK" << std::endl;
  return 0;
}
