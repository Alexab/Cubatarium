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
    std::cerr << "block_placement_raycast_test: " << message << std::endl;
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

static void BuildPit(cutum::UBlockWorld &world, glm::ivec3 center)
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
}

static void BuildWall(cutum::UBlockWorld &world, int x, int y0, int z, int height)
{
  constexpr cutum::BlockId kStone = 8;
  for (int dy = 0; dy < height; ++dy)
  {
    world.SetBlock(glm::ivec3(x, y0 + dy, z), kStone);
  }
}

int main()
{
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision collision(world);
  collision.SetBlockRegistry(&registry);
  const cutum::PlayerCapsule cap = cutum::PlayerCapsule::Standing();

  // T1: ray into wall 4 blocks ahead
  BuildWall(world, 5, 0, 0, 4);
  const glm::vec3 wall_eye(0.5f, 1.62f, 0.5f);
  const glm::vec3 wall_front(1.0f, 0.0f, 0.0f);
  const auto wall_resolved =
      collision.ResolveBlockPlacement(wall_eye, wall_front, cap, 8.0f);
  Expect(wall_resolved.break_hit.has_value(), "wall break hit");
  Expect(wall_resolved.place_block_pos.has_value(), "wall place target");
  const glm::ivec3 wall_normal =
      cutum::InferPlacementNormal(*wall_resolved.break_hit, wall_eye);
  Expect(*wall_resolved.place_block_pos ==
             wall_resolved.break_hit->blockPos + wall_normal,
         "wall place is hit+normal");

  // T2: identical resolve twice (UI == click)
  const auto wall_resolved_2 =
      collision.ResolveBlockPlacement(wall_eye, wall_front, cap, 8.0f);
  Expect(wall_resolved.place_block_pos == wall_resolved_2.place_block_pos,
         "resolve is deterministic");

  const auto free_pos =
      collision.FindNearestFreeCubePosition(wall_eye, wall_front, cap);
  Expect(free_pos.has_value(), "FNFCP matches resolve");
  Expect(cutum::WorldPosToBlock(*free_pos) == *wall_resolved.place_block_pos,
         "FNFCP block matches resolve");

  // T3: 1x1 dry pit, look down
  cutum::UBlockWorld pit_world;
  pit_world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision pit_collision(pit_world);
  pit_collision.SetBlockRegistry(&registry);
  const glm::ivec3 pit_center(1, 10, 1);
  BuildPit(pit_world, pit_center);
  const glm::vec3 pit_eye(1.5f, 11.62f, 1.5f);
  const glm::vec3 pit_down(0.0f, -1.0f, 0.0f);
  const auto pit_resolved =
      pit_collision.ResolveBlockPlacement(pit_eye, pit_down, cap, 8.0f);
  Expect(pit_resolved.place_block_pos.has_value(), "pit down placement");
  Expect(*pit_resolved.place_block_pos == pit_center, "pit center from above");
  const glm::ivec3 pit_normal =
      cutum::InferPlacementNormal(*pit_resolved.break_hit, pit_eye);
  Expect(*pit_resolved.place_block_pos ==
             pit_resolved.break_hit->blockPos + pit_normal,
         "pit place is hit+normal");

  // T4: air above floor along ray to wall must not steal standard placement
  cutum::UBlockWorld floor_world;
  floor_world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision floor_collision(floor_world);
  floor_collision.SetBlockRegistry(&registry);
  for (int x = 0; x <= 6; ++x)
  {
    floor_world.SetBlock(glm::ivec3(x, 0, 0), 8);
  }
  BuildWall(floor_world, 6, 1, 0, 3);
  const glm::vec3 floor_eye(0.5f, 1.62f, 0.5f);
  const glm::vec3 floor_front(1.0f, 0.0f, 0.0f);
  const auto floor_resolved =
      floor_collision.ResolveBlockPlacement(floor_eye, floor_front, cap, 8.0f);
  Expect(floor_resolved.place_block_pos.has_value(), "floor wall place");
  Expect(floor_resolved.break_hit->blockPos == glm::ivec3(6, 1, 0),
         "floor wall break on near face");
  Expect(*floor_resolved.place_block_pos == glm::ivec3(5, 1, 0),
         "floor wall place adjacent to hit");

  std::cout << "block_placement_raycast_test: OK" << std::endl;
  return 0;
}
