#include "Blocks/BlockDefinitionStorage.h"
#include "Test/FluidTestHelpers.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"
#include "World/Physics/PhysicsProfile.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "fluid_queue_integration_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static int CountWater(const cutum::UBlockWorld &world, int y, int min_x,
                      int max_x, int min_z, int max_z)
{
  constexpr cutum::BlockId kWater = 9;
  int count = 0;
  for (int x = min_x; x <= max_x; ++x)
  {
    for (int z = min_z; z <= max_z; ++z)
    {
      if (world.GetBlock(glm::ivec3(x, y, z)) == kWater)
      {
        ++count;
      }
    }
  }
  return count;
}

static void
EnqueueFluidFrontier(cutum::UFluidUpdateSet &queue, cutum::UBlockWorld &world,
                     const cutum::UBlockDefinitionStorage &definitions,
                     glm::ivec3 block_pos)
{
  FluidTest::EnqueueFluidFrontier(queue, world, definitions, block_pos);
}

int main()
{
  const auto definitions = MakeDefinitions();
  constexpr cutum::BlockId kWater = 9;
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

  for (int x = 0; x < 4; ++x)
  {
    for (int z = 0; z < 4; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), 8);
      world.SetBlock(glm::ivec3(x, 10, z), 8);
      world.SetBlock(glm::ivec3(x, 11, z), 8);
    }
  }
  for (int x = 1; x <= 2; ++x)
  {
    for (int z = 1; z <= 2; ++z)
    {
      world.SetBlock(glm::ivec3(x, 10, z), cutum::BLOCK_AIR);
    }
  }
  world.SetBlock(glm::ivec3(1, 10, 1), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());

  cutum::UFluidUpdateSet queue;
  cutum::UFluidSpreadSystem fluid;
  FluidTest::EnqueueFluidFrontier(queue, world, *definitions,
                                  glm::ivec3(1, 10, 1));
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 500);

  Expect(CountWater(world, 10, 1, 2, 1, 2) == 4,
         "2x2 pit fills via fluid update queue");
  Expect(world.GetFluidState(glm::ivec3(1, 10, 1)).IsSource(),
         "source cell remains source after queue-driven spread");
  Expect(queue.GetStats().Depth == 0,
         "pit fill drains fluid queue after spread settles");

  cutum::UBlockWorld shore_world;
  shore_world.SetFluidDefinitions(definitions.get());
  shore_world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  shore_world.SetFluidState(glm::ivec3(0, 10, 0),
                            cutum::FluidCellState::Source());
  shore_world.SetBlock(glm::ivec3(1, 10, 0), 8);
  shore_world.SetBlock(glm::ivec3(1, 10, 0), cutum::BLOCK_AIR);

  cutum::UFluidUpdateSet shore_queue;
  shore_queue.Enqueue(glm::ivec3(0, 10, 0));
  shore_queue.Enqueue(glm::ivec3(1, 10, 0));
  FluidTest::RunQueueTicks(shore_world, *definitions, shore_queue, fluid, 200);

  Expect(shore_world.GetBlock(glm::ivec3(1, 10, 0)) == kWater,
         "shore air next to sea fills via floodable queue tick");

  cutum::UBlockWorld stair_world;
  stair_world.SetFluidDefinitions(definitions.get());
  stair_world.SetBlock(glm::ivec3(0, 11, 0), kWater);
  stair_world.SetFluidState(glm::ivec3(0, 11, 0),
                            cutum::FluidCellState::Source());
  stair_world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  stair_world.SetFluidState(glm::ivec3(0, 10, 0),
                            cutum::FluidCellState::Source());
  stair_world.SetBlock(glm::ivec3(1, 10, 0), cutum::BLOCK_AIR);
  stair_world.SetBlock(glm::ivec3(0, 9, 0), cutum::BLOCK_AIR);
  stair_world.SetBlock(glm::ivec3(1, 9, 0), cutum::BLOCK_AIR);
  stair_world.SetBlock(glm::ivec3(0, 8, 0), 8);
  stair_world.SetBlock(glm::ivec3(1, 8, 0), 8);

  cutum::UFluidUpdateSet stair_queue;
  EnqueueFluidFrontier(stair_queue, stair_world, *definitions,
                       glm::ivec3(1, 10, 0));
  FluidTest::RunQueueTicks(stair_world, *definitions, stair_queue, fluid, 200);

  Expect(stair_world.GetBlock(glm::ivec3(1, 10, 0)) == kWater,
         "stair-step shore cavity fills from horizontal sea water");

  cutum::UBlockWorld land_world;
  land_world.SetFluidDefinitions(definitions.get());
  for (int x = 0; x < 5; ++x)
  {
    for (int z = 0; z < 5; ++z)
    {
      land_world.SetBlock(glm::ivec3(x, 9, z), 8);
      const bool perimeter = x == 0 || z == 0 || x == 4 || z == 4;
      if (perimeter)
      {
        land_world.SetBlock(glm::ivec3(x, 10, z), 8);
      }
    }
  }
  land_world.SetBlock(glm::ivec3(2, 10, 2), kWater);
  land_world.SetFluidState(glm::ivec3(2, 10, 2),
                           cutum::FluidCellState::Source());

  cutum::UFluidUpdateSet land_queue;
  EnqueueFluidFrontier(land_queue, land_world, *definitions,
                       glm::ivec3(2, 10, 2));
  FluidTest::RunQueueTicks(land_world, *definitions, land_queue, fluid, 400);

  Expect(land_world.GetBlock(glm::ivec3(3, 10, 2)) == kWater,
         "placed source spreads to horizontal air on land");
  Expect(land_world.GetBlock(glm::ivec3(2, 10, 3)) == kWater,
         "placed source spreads to second horizontal neighbor");
  Expect(land_queue.GetStats().Depth == 0,
         "land puddle drains fluid queue after spread settles");

  std::cout << "fluid_queue_integration_test: OK" << std::endl;
  return 0;
}
