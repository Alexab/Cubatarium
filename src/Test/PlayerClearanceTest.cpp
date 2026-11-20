#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "player_clearance_test: " << message << std::endl;
    std::exit(1);
  }
}

static void ExpectNear(float a, float b, float eps, const char *message)
{
  if (std::abs(a - b) > eps)
  {
    std::cerr << "player_clearance_test: " << message << " (got " << a
              << ", expected " << b << ")" << std::endl;
    std::exit(1);
  }
}

static void InstallSolid(cutum::UBlockDefinitionStorage &storage,
                         cutum::BlockId id, const std::string &name)
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

static void BuildCorridorFloor(cutum::UBlockWorld &block_world,
                               cutum::BlockId stone, int ceiling_y)
{
  for (int x = 0; x <= 5; ++x)
  {
    block_world.SetBlock(glm::ivec3(x, 0, 0), stone);
  }
  for (int x = 1; x <= 4; ++x)
  {
    block_world.SetBlock(glm::ivec3(x, ceiling_y, 0), stone);
  }
}

static void RunAabbInvariant(const cutum::PlayerCapsule &cap, float feet_y)
{
  const glm::vec3 eye(0.5f, feet_y + cap.eyeHeight, 0.5f);
  const cutum::CollisionVolume vol =
      cutum::CollisionVolumeFromEye(eye, cap);
  ExpectNear(vol.center.y - vol.halfExtents.y, feet_y, 1e-4f,
             "AABB bottom should match feet");
  ExpectNear(vol.center.y + vol.halfExtents.y, feet_y + cap.height, 1e-4f,
             "AABB top should be feet + height");
}

static void RunTwoAirCorridor(cutum::UWorldCollision &collision,
                              const cutum::PlayerCapsule &cap)
{
  const float feet_y = cutum::BlockTopY(0);
  const glm::vec3 eye(0.5f, feet_y + cap.eyeHeight, 0.5f);
  const glm::vec3 corridor_center(2.5f, eye.y, 0.5f);

  Expect(!collision.CheckCollision(corridor_center, cap, 0),
         "player should not collide in 2-air corridor center");

  const glm::vec3 resolved =
      collision.ResolveMovement(eye, glm::vec3(3.0f, 0.0f, 0.0f), cap, 0);
  Expect(resolved.x >= 3.0f,
         "player should pass through 2-air corridor horizontally");
}

static void RunOneAirBlocked(cutum::UBlockWorld &block_world,
                             cutum::UWorldCollision &collision,
                             cutum::BlockId stone,
                             const cutum::PlayerCapsule &cap)
{
  block_world.SetBlock(glm::ivec3(1, 2, 0), cutum::BLOCK_AIR);
  block_world.SetBlock(glm::ivec3(2, 2, 0), cutum::BLOCK_AIR);
  block_world.SetBlock(glm::ivec3(3, 2, 0), cutum::BLOCK_AIR);
  block_world.SetBlock(glm::ivec3(4, 2, 0), cutum::BLOCK_AIR);
  for (int x = 1; x <= 4; ++x)
  {
    block_world.SetBlock(glm::ivec3(x, 2, 0), stone);
  }

  const float feet_y = cutum::BlockTopY(0);
  const glm::vec3 eye(0.5f, feet_y + cap.eyeHeight, 0.5f);
  const glm::vec3 resolved =
      collision.ResolveMovement(eye, glm::vec3(3.0f, 0.0f, 0.0f), cap, 0);
  Expect(resolved.x < 3.0f - 0.05f,
         "player should not pass 1-air corridor");
}

int main()
{
  constexpr cutum::BlockId kStone = 12;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  InstallSolid(*definitions, kStone, "stone");
  cutum::UBlockRegistry registry(nullptr, definitions);

  const cutum::PlayerCapsule cap = cutum::PlayerCapsule::Standing();
  const float feet_y = cutum::BlockTopY(0);

  RunAabbInvariant(cap, feet_y);

  cutum::UBlockWorld block_world_a;
  BuildCorridorFloor(block_world_a, kStone, 3);
  cutum::UWorldCollision collision_a(block_world_a);
  collision_a.SetBlockRegistry(&registry);
  collision_a.SetEntityCollisionEnabled(false);
  RunTwoAirCorridor(collision_a, cap);

  cutum::UBlockWorld block_world_b;
  BuildCorridorFloor(block_world_b, kStone, 2);
  cutum::UWorldCollision collision_b(block_world_b);
  collision_b.SetBlockRegistry(&registry);
  collision_b.SetEntityCollisionEnabled(false);
  RunOneAirBlocked(block_world_b, collision_b, kStone, cap);

  // Actual cold-source-removal counterexample: the saved eye is still at its
  // former standing height, one floor cell is AIR, and the cave roof remains.
  // Clearance, unlike ground support or highest-column height, preserves this
  // valid airborne position and lets ordinary vertical movement settle it.
  cutum::UBlockWorld cave;
  const glm::vec3 saved(22.0f, -46.88f, 51.0f);
  const auto saved_volume = cutum::CollisionVolumeFromEye(saved, cap);
  cutum::UWorldCollision cave_collision(cave);
  cave_collision.SetBlockRegistry(&registry);
  Expect(
      !cave_collision.QueryKnownBlockCollisionVolume(saved_volume).has_value(),
      "unloaded saved body is unknown even when occupancy broadphase is empty");
  for (int y = -50; y <= -33; ++y)
  {
    cave.SetBlock(glm::ivec3(22, y, 51), cutum::BLOCK_AIR);
  }
  // SetBlock(AIR) alone does not allocate previously absent terrain.
  cave.SetBlock(glm::ivec3(23, -48, 51), kStone);
  cave.SetBlock(glm::ivec3(22, -50, 51), kStone);
  cave.SetBlock(glm::ivec3(22, -33, 51), kStone);
  Expect(!cave_collision.HasGroundSupport(saved, cap),
         "removed floor leaves the saved eye without immediate ground support");
  const auto clear =
      cave_collision.QueryKnownBlockCollisionVolume(saved_volume);
  Expect(clear.has_value() && !*clear,
         "known AIR body above removed floor remains valid below the roof");
  Expect(cave_collision.FindHighestSolidY(22, 51) == -33,
         "highest-column fallback would incorrectly choose the roof");
  const auto settled = cave_collision.ResolveMovement(
      saved, glm::vec3(0.0f, -2.0f, 0.0f), cap, 0);
  Expect(
      settled.y < saved.y - 0.5f && settled.y > -48.0f,
      "normal movement settles the saved body onto the block below the hole");
  cave.SetBlock(glm::ivec3(22, -49, 51), kStone);
  const auto standing =
      cave_collision.QueryKnownBlockCollisionVolume(saved_volume);
  Expect(standing.has_value() && !*standing,
         "touching the restored floor is not a penetrating body overlap");
  cave.SetBlock(glm::ivec3(22, -47, 51), kStone);
  const auto blocked =
      cave_collision.QueryKnownBlockCollisionVolume(saved_volume);
  Expect(blocked.has_value() && *blocked,
         "known solid overlap still requires position repair");
  cutum::UWorldCollision no_registry(cave);
  Expect(!no_registry.QueryKnownBlockCollisionVolume(saved_volume).has_value(),
         "missing catalog cannot prove saved-body clearance");
  for (float boundary_x : {15.5f, -0.5f})
  {
    cutum::UBlockWorld seam;
    cutum::UWorldCollision seam_collision(seam);
    seam_collision.SetBlockRegistry(&registry);
    const int left = static_cast<int>(std::floor(boundary_x));
    seam.SetBlock(glm::ivec3(left, -47, 55), kStone);
    const auto body = cutum::CollisionVolumeFromEye(
        glm::vec3(boundary_x, saved.y, saved.z), cap);
    Expect(
        !seam_collision.QueryKnownBlockCollisionVolume(body).has_value(),
        "partially loaded body across signed section seam must await terrain");
    seam.SetBlock(glm::ivec3(left + 1, -47, 55), kStone);
    const auto seam_clear = seam_collision.QueryKnownBlockCollisionVolume(body);
    Expect(
        seam_clear.has_value() && !*seam_clear,
        "both known AIR sides establish clearance across signed section seam");
    seam.SetBlock(glm::ivec3(left + 1, -47, 51), kStone);
    const auto seam_blocked =
        seam_collision.QueryKnownBlockCollisionVolume(body);
    Expect(seam_blocked.has_value() && *seam_blocked,
           "solid on either side of seam prevents clear restoration");
  }

  std::cout << "player_clearance_test: OK" << std::endl;
  return 0;
}
