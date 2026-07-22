#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Locomotion/CreatureLocomotionController.h"
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

static void RunCrouchEyeSync()
{
  cutum::UCreatureLocomotionController locomotion;
  locomotion.SetCollisionProfile(glm::vec3(0.6f, 1.8f, 0.6f), 1.62f);
  locomotion.SetStanceBlendForView(1.0f);
  const cutum::PlayerCapsule collision_cap = locomotion.GetCollisionCapsule();
  ExpectNear(collision_cap.eyeHeight, locomotion.GetViewEyeHeight(), 1e-4f,
             "crouch collision and view eye height should match");
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

  RunCrouchEyeSync();

  cutum::UBlockWorld block_world_b;
  BuildCorridorFloor(block_world_b, kStone, 2);
  cutum::UWorldCollision collision_b(block_world_b);
  collision_b.SetBlockRegistry(&registry);
  collision_b.SetEntityCollisionEnabled(false);
  RunOneAirBlocked(block_world_b, collision_b, kStone, cap);

  std::cout << "player_clearance_test: OK" << std::endl;
  return 0;
}
