#include "Blocks/BlockDefinitionStorage.h"
#include "Test/FluidTestHelpers.h"

#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"
#include "World/Physics/LiquidDebugTrace.h"

#include <iostream>
#include <memory>

namespace
{

constexpr const char *kTestName = "fluid_queue_drain_integration_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;
constexpr cutum::BlockId kGrass = 10;

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

static int CountSpreadTargets(const cutum::UBlockWorld &world,
                              const cutum::UBlockDefinitionStorage &defs,
                              int floor_size)
{
  const int wall = floor_size + 2;
  int count = 0;
  for (int x = 0; x < wall; ++x)
  {
    for (int z = 0; z < wall; ++z)
    {
      const glm::ivec3 pos(x, 10, z);
      if (cutum::UFluidSpreadSystem::HasSpreadTarget(world, defs, pos))
      {
        ++count;
      }
    }
  }
  return count;
}

static void TestLandSpillDrainsQueue(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
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
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 800);

  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "5x5 land spill drains LiqQ after spread");

  cutum::ULiquidDebugTrace::Instance().Clear();
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 120);
  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "land spill queue stays empty during idle ticks");

  std::vector<cutum::LiquidDebugEntry> trace;
  cutum::ULiquidDebugTrace::Instance().CopyRecent(trace);
  FluidTest::Expect(trace.empty(), kTestName,
                    "no fluid changes after land spill stabilizes");

  FluidTest::Expect(CountSpreadTargets(world, *definitions, kFloor) == 0,
                    kTestName,
                    "no spread-target cells remain in contained basin after stabilization");
}

static int CountWater(const cutum::UBlockWorld &world, int y, int min_x,
                      int max_x, int min_z, int max_z)
{
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

static void TestPitFillDrainsQueue(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  for (int x = 0; x < 4; ++x)
  {
    for (int z = 0; z < 4; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
      world.SetBlock(glm::ivec3(x, 10, z), kStone);
      world.SetBlock(glm::ivec3(x, 11, z), kStone);
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
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 800);

  FluidTest::Expect(CountWater(world, 10, 1, 2, 1, 2) == 4, kTestName,
                    "2x2 pit fills via fluid update queue");
  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "2x2 pit fill drains LiqQ after spread");
}

static void TestGrassEdgeSpillDrainsQueue(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  constexpr int kFloor = 3;
  BuildContainedBasin(world, kFloor);
  const glm::ivec3 grass_pos = BasinCenter(kFloor);
  world.SetBlock(grass_pos, kGrass);
  const glm::ivec3 source(grass_pos.x - 1, grass_pos.y, grass_pos.z);
  world.SetBlock(source, kWater);
  world.SetFluidState(source, cutum::FluidCellState::Source());

  cutum::UFluidUpdateSet queue;
  cutum::UFluidSpreadSystem fluid;
  FluidTest::EnqueueFluidFrontier(queue, world, *definitions, source);
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 800);

  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "grass-edge spill drains LiqQ after spread");
}

} // namespace

int main()
{
  const auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
  TestLandSpillDrainsQueue(definitions);
  TestPitFillDrainsQueue(definitions);
  TestGrassEdgeSpillDrainsQueue(definitions);
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
