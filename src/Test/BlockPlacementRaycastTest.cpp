#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include "World/Raycast/BlockRaycast.h"

#include <cmath>
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
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kTallGrass = 10;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  cutum::BlockDefinition tall_grass;
  tall_grass.Name = "tall_grass";
  tall_grass.Physics = cutum::BlockPhysicsProfile::Solid();
  tall_grass.Physics.Movement.Occupancy = 0.0f;
  tall_grass.Render.Style = cutum::BlockRenderStyle::Cross;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  by_id[kTallGrass] = tall_grass;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  name_to_id["tall_grass"] = kTallGrass;
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

static void BuildDryRavine7x7(cutum::UBlockWorld &world, glm::ivec3 center)
{
  constexpr cutum::BlockId kStone = 8;
  for (int dx = -3; dx <= 3; ++dx)
  {
    for (int dz = -3; dz <= 3; ++dz)
    {
      world.SetBlock(center + glm::ivec3(dx, -1, dz), kStone);
      if (std::abs(dx) == 3 || std::abs(dz) == 3)
      {
        world.SetBlock(center + glm::ivec3(dx, 0, dz), kStone);
      }
    }
  }
}

static void BuildWorldgenRavineColumn(cutum::UBlockWorld &world,
                                      glm::ivec3 surface_center, int depth)
{
  constexpr cutum::BlockId kStone = 8;
  for (int dx = -4; dx <= 4; ++dx)
  {
    for (int dz = -4; dz <= 4; ++dz)
    {
      const int wx = surface_center.x + dx;
      const int wz = surface_center.z + dz;
      const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
      const int carve_depth =
          dist <= 3.5f ? depth : (dist <= 4.5f ? depth / 2 : 0);
      for (int y = surface_center.y - depth; y <= surface_center.y; ++y)
      {
        const bool air_cell =
            y > surface_center.y - carve_depth && y <= surface_center.y;
        if (!air_cell)
        {
          world.SetBlock(glm::ivec3(wx, y, wz), kStone);
        }
      }
    }
  }
}

static void BuildWaterPit(cutum::UBlockWorld &world, glm::ivec3 center)
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  BuildPit(world, center);
  world.SetBlock(center, kWater);
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

  // T1: ray into wall 4 blocks ahead (wall z=1 aligns with WorldPosToBlock eye z)
  BuildWall(world, 5, 0, 1, 4);
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
  const glm::vec3 pit_eye(1.0f, 11.62f, 1.0f);
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

  // T5: 7x7 dry ravine (worldgen-style), look down from outer rim
  cutum::UBlockWorld ravine_world;
  ravine_world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision ravine_collision(ravine_world);
  ravine_collision.SetBlockRegistry(&registry);
  const glm::ivec3 ravine_center(0, 10, 0);
  BuildDryRavine7x7(ravine_world, ravine_center);
  const glm::vec3 ravine_eye(4.5f, 11.62f, 0.0f);
  const glm::vec3 ravine_front =
      glm::normalize(glm::vec3(-4.5f, -2.12f, 0.0f));
  const auto ravine_resolved =
      ravine_collision.ResolveBlockPlacement(ravine_eye, ravine_front, cap, 8.0f);
  Expect(ravine_resolved.place_block_pos.has_value(), "ravine rim placement");
  Expect(*ravine_resolved.place_block_pos == ravine_center,
         "ravine center from rim");
  const glm::ivec3 ravine_normal =
      cutum::InferPlacementNormal(*ravine_resolved.break_hit, ravine_eye);
  Expect(*ravine_resolved.place_block_pos ==
             ravine_resolved.break_hit->blockPos + ravine_normal,
         "ravine place is hit+normal");

  // T7: worldgen-style deep ravine (20 blocks), rim reach beyond 8m gate
  cutum::UBlockWorld deep_ravine_world;
  deep_ravine_world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision deep_ravine_collision(deep_ravine_world);
  deep_ravine_collision.SetBlockRegistry(&registry);
  const glm::ivec3 deep_surface(40, 70, 40);
  constexpr int kDeepRavineDepth = 20;
  BuildWorldgenRavineColumn(deep_ravine_world, deep_surface, kDeepRavineDepth);
  const glm::vec3 deep_eye(40.0f, 75.62f, 40.0f);
  const glm::vec3 deep_down(0.0f, -1.0f, 0.0f);
  const auto deep_resolved = deep_ravine_collision.ResolveBlockPlacement(
      deep_eye, deep_down, cap, 8.0f);
  Expect(deep_resolved.break_hit.has_value(), "deep ravine break hit");
  Expect(deep_resolved.break_hit->distance > 8.0f,
         "deep ravine hit beyond classic gate");
  Expect(deep_resolved.place_block_pos.has_value(),
         "deep ravine placement beyond 8m gate");
  Expect(*deep_resolved.place_block_pos == glm::ivec3(40, 51, 40),
         "deep ravine floor air cell");

  // T6: 1x1 water-filled pit (SealFluidPockets-style), solid replace
  cutum::UBlockWorld water_pit_world;
  water_pit_world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision water_pit_collision(water_pit_world);
  water_pit_collision.SetBlockRegistry(&registry);
  const glm::ivec3 water_pit_center(20, 10, 20);
  BuildWaterPit(water_pit_world, water_pit_center);
  const glm::vec3 water_pit_eye(20.0f, 11.62f, 20.0f);
  const auto water_pit_resolved = water_pit_collision.ResolveBlockPlacement(
      water_pit_eye, pit_down, cap, 8.0f);
  Expect(water_pit_resolved.place_block_pos.has_value(),
         "water pit placement preview");
  Expect(*water_pit_resolved.place_block_pos == water_pit_center,
         "water pit center target");
  Expect(water_pit_world.GetBlock(water_pit_center) == 9, "pit cell is water");

  // T4: air above floor along ray to wall must not steal standard placement
  cutum::UBlockWorld floor_world;
  floor_world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision floor_collision(floor_world);
  floor_collision.SetBlockRegistry(&registry);
  for (int x = 0; x <= 6; ++x)
  {
    floor_world.SetBlock(glm::ivec3(x, 0, 0), 8);
  }
  BuildWall(floor_world, 6, 1, 1, 3);
  const glm::vec3 floor_eye(0.5f, 1.0f, 0.5f);
  const glm::vec3 floor_front(1.0f, 0.0f, 0.0f);
  const auto floor_resolved =
      floor_collision.ResolveBlockPlacement(floor_eye, floor_front, cap, 8.0f);
  Expect(floor_resolved.place_block_pos.has_value(), "floor wall place");
  Expect(floor_resolved.break_hit->blockPos == glm::ivec3(6, 1, 1),
         "floor wall break on near face");
  Expect(*floor_resolved.place_block_pos == glm::ivec3(5, 1, 1),
         "floor wall place adjacent to hit");

  // T8: worldgen scatter (occupancy 0) in pit center blocks placement without this fix
  cutum::UBlockWorld scatter_world;
  scatter_world.SetFluidDefinitions(definitions.get());
  cutum::UWorldCollision scatter_collision(scatter_world);
  scatter_collision.SetBlockRegistry(&registry);
  const glm::ivec3 scatter_pit_center(60, 10, 60);
  BuildPit(scatter_world, scatter_pit_center);
  constexpr cutum::BlockId kTallGrass = 10;
  scatter_world.SetBlock(scatter_pit_center, kTallGrass);
  const glm::vec3 scatter_eye(60.0f, 11.62f, 60.0f);
  const glm::vec3 scatter_down(0.0f, -1.0f, 0.0f);
  const auto scatter_resolved = scatter_collision.ResolveBlockPlacement(
      scatter_eye, scatter_down, cap, 8.0f);
  Expect(scatter_resolved.place_block_pos.has_value(),
         "scatter-filled pit placement");
  Expect(*scatter_resolved.place_block_pos == scatter_pit_center,
         "scatter pit center target");

  std::cout << "block_placement_raycast_test: OK" << std::endl;
  return 0;
}
