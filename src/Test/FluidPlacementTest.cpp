#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include "World/Raycast/BlockRaycast.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "fluid_placement_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kStone = 8;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static void BuildPit(cutum::UBlockWorld &world, int floor_y, glm::ivec3 center)
{
  constexpr cutum::BlockId kStone = 8;
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      world.SetBlock(center + glm::ivec3(dx, 0, dz), kStone);
    }
  }
  world.SetBlock(center + glm::ivec3(0, -1, 0), kStone);
  (void)floor_y;
}

int main()
{
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision collision(world);
  collision.SetBlockRegistry(&registry);

  const glm::ivec3 pit_center(1, 10, 1);
  BuildPit(world, 10, pit_center);

  const glm::vec3 feet(1.5f, 11.5f, 1.5f);
  const glm::vec3 front(0.0f, -1.0f, 0.0f);
  const cutum::PlayerCapsule cap = cutum::PlayerCapsule::Standing();
  const auto resolved =
      collision.ResolveBlockPlacement(feet, front, cap, 8.0f);
  Expect(resolved.break_hit.has_value(), "pit break hit from above");
  Expect(resolved.place_block_pos.has_value(), "1x1 pit placement position found");
  Expect(*resolved.place_block_pos == pit_center, "pit center resolved");
  const glm::ivec3 normal =
      cutum::InferPlacementNormal(*resolved.break_hit, feet);
  Expect(*resolved.place_block_pos == resolved.break_hit->blockPos + normal,
         "classic place is hit+normal");

  const glm::ivec3 old_pit(20, 8, 20);
  BuildPit(world, 8, old_pit);
  world.SetBlock(old_pit + glm::ivec3(0, 1, 0), 8);
  const glm::vec3 old_eye(20.5f, 12.0f, 20.5f);
  const auto old_resolved = collision.ResolveBlockPlacement(
      old_eye, glm::vec3(0.0f, -1.0f, 0.0f), cap, 8.0f);
  Expect(old_resolved.place_block_pos.has_value(), "old pit placement hit");

  const auto capsule_free =
      collision.FindNearestFreeCubePosition(feet, front, cap);
  Expect(capsule_free.has_value(),
         "capsule overlap pit below feet still allows placement");

  std::cout << "fluid_placement_test: OK" << std::endl;
  return 0;
}
