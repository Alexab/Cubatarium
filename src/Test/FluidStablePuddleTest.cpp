#include "Blocks/BlockDefinitionStorage.h"
#include "Test/FluidTestHelpers.h"

#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"
#include "World/Physics/LiquidDebugTrace.h"

#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

constexpr const char *kTestName = "fluid_stable_puddle_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
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

static void BuildContainedBasin(cutum::UBlockWorld &world, int floor_size)
{
  const int wall = floor_size + 2;
  for (int x = 0; x < wall; ++x)
  {
    for (int z = 0; z < wall; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
      const bool perimeter =
          x == 0 || z == 0 || x == wall - 1 || z == wall - 1;
      if (perimeter)
      {
        world.SetBlock(glm::ivec3(x, 10, z), kStone);
      }
    }
  }
}

static glm::ivec3 BasinCenter(int floor_size)
{
  return glm::ivec3(1 + floor_size / 2, 10, 1 + floor_size / 2);
}

} // namespace

int main()
{
  const auto definitions = MakeDefinitions();
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  constexpr int kFloor = 3;
  BuildContainedBasin(world, kFloor);
  const glm::ivec3 source = BasinCenter(kFloor);
  world.SetBlock(source, kWater);
  world.SetFluidState(source, cutum::FluidCellState::Source());

  cutum::UFluidUpdateSet queue;
  cutum::UFluidSpreadSystem fluid;
  FluidTest::EnqueueFluidFrontier(queue, world, *definitions, source);
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 300);

  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "stable land puddle drains fluid queue");

  cutum::ULiquidDebugTrace::Instance().Clear();
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 50);
  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "queue stays empty after additional idle ticks");
  std::vector<cutum::LiquidDebugEntry> trace;
  cutum::ULiquidDebugTrace::Instance().CopyRecent(trace);
  FluidTest::Expect(trace.empty(), kTestName,
                    "no further fluid changes after stabilization");

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
