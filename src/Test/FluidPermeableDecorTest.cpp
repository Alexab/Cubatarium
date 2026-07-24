#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"

#include <iostream>
#include <memory>

namespace
{

constexpr const char *kTestName = "fluid_permeable_decor_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;
constexpr cutum::BlockId kTallGrass = 10;

static void BuildPitWithGrass(cutum::UBlockWorld &world)
{
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
      world.SetBlock(glm::ivec3(x, 11, z), cutum::BLOCK_AIR);
    }
  }
  world.SetBlock(glm::ivec3(2, 10, 2), kTallGrass);
}

static void TestPitSpreadPreservesGrass(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry, cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  BuildPitWithGrass(world);
  world.SetBlock(glm::ivec3(1, 10, 1), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(glm::ivec3(1, 10, 1));
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 500);

  FluidTest::Expect(world.GetBlock(glm::ivec3(2, 10, 2)) == kTallGrass,
                    kTestName, "grass block preserved after pit flood");
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(2, 10, 2))) != 0,
      kTestName, "grass cell waterlogged after pit flood");
  FluidTest::Expect(world.GetBlock(glm::ivec3(2, 10, 1)) == kWater, kTestName,
                    "adjacent air cell becomes water block");
}

static void TestDirectWaterlog(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kStone);
  world.SetBlock(glm::ivec3(0, 11, 0), kTallGrass);
  world.SetFluidState(glm::ivec3(0, 11, 0), cutum::FluidCellState::Flowing(1));
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 11, 0)) == kTallGrass, kTestName,
                    "direct waterlog keeps grass block");
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(0, 11, 0))) != 0,
      kTestName, "direct waterlog sets fluid_data");
}

static void TestStrandedWaterOnSaturatedPermeableDrainsQueue(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kStone);
  world.SetBlock(glm::ivec3(0, 11, 0), kTallGrass);
  world.SetFluidState(glm::ivec3(0, 11, 0), cutum::FluidCellState::Flowing(1));
  world.SetBlock(glm::ivec3(1, 12, 0), kStone);
  world.SetBlock(glm::ivec3(-1, 12, 0), kStone);
  world.SetBlock(glm::ivec3(0, 12, 1), kStone);
  world.SetBlock(glm::ivec3(0, 12, -1), kStone);
  world.SetBlock(glm::ivec3(0, 13, 0), kStone);
  world.SetBlock(glm::ivec3(0, 12, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 12, 0), cutum::FluidCellState::Source());

  cutum::UFluidUpdateSet queue;
  FluidTest::EnqueueFluidFrontier(queue, world, *definitions, glm::ivec3(0, 12, 0));
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 64);

  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "queue drains when water sits on saturated permeable decor");
  FluidTest::Expect(
      !cutum::UFluidSpreadSystem::HasSpreadTarget(world, *definitions,
                                                  glm::ivec3(0, 12, 0)),
      kTestName, "stranded source has no spread target over saturated decor");
}

static void TestFlowingWaterOverPermeableDoesNotChurn(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kStone);
  world.SetBlock(glm::ivec3(0, 11, 0), kTallGrass);
  world.SetFluidState(glm::ivec3(0, 11, 0), cutum::FluidCellState::Flowing(1));
  world.SetBlock(glm::ivec3(1, 12, 0), kStone);
  world.SetBlock(glm::ivec3(-1, 12, 0), kStone);
  world.SetBlock(glm::ivec3(0, 12, 1), kStone);
  world.SetBlock(glm::ivec3(0, 12, -1), kStone);
  world.SetBlock(glm::ivec3(0, 13, 0), kStone);
  world.SetBlock(glm::ivec3(0, 12, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 12, 0), cutum::FluidCellState::Flowing(4));

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(glm::ivec3(0, 12, 0));
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 32);
  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "flowing water queue drains over saturated decor");

  int extra_changes = 0;
  for (uint64_t tick = 32; tick < 64; ++tick)
  {
    const cutum::FluidSpreadStats stats =
        fluid.TickBlock(world, *definitions, tick, glm::ivec3(0, 12, 0));
    extra_changes += static_cast<int>(stats.Changes.size());
  }
  FluidTest::Expect(extra_changes == 0, kTestName,
                    "flowing water over permeable decor stops transform churn");
}

static void TestSourceNearOpenAirDoesNotStayInLiquidQueue(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      world.SetBlock(glm::ivec3(dx, 11, dz), kStone);
    }
  }
  world.SetBlock(glm::ivec3(0, 12, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 12, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 12, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(2, 12, 0), kStone);
  world.SetBlock(glm::ivec3(0, 13, 0), kStone);
  world.SetBlock(glm::ivec3(1, 13, 0), kStone);

  cutum::UFluidUpdateSet queue;
  FluidTest::EnqueueFluidFrontier(queue, world, *definitions, glm::ivec3(0, 12, 0));
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 96);

  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "source near open air drains the liquid queue after settle");
  FluidTest::Expect(
      !cutum::UFluidSpreadSystem::HasSpreadTarget(world, *definitions,
                                                  glm::ivec3(0, 12, 0)),
      kTestName,
      "settled source block is not kept as a spread target because of nearby air");
}

static void TestCanopyGridDrainsQueue(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  for (int x = -1; x <= 1; ++x)
  {
    for (int z = -1; z <= 1; ++z)
    {
      world.SetBlock(glm::ivec3(x, 10, z), kStone);
      world.SetBlock(glm::ivec3(x, 11, z), kTallGrass);
      world.SetFluidState(glm::ivec3(x, 11, z), cutum::FluidCellState::Flowing(1));
      world.SetBlock(glm::ivec3(x, 12, z), kWater);
      world.SetFluidState(glm::ivec3(x, 12, z), cutum::FluidCellState::Source());
    }
  }
  world.SetBlock(glm::ivec3(0, 13, 0), kStone);

  cutum::UFluidUpdateSet queue;
  for (int x = -1; x <= 1; ++x)
  {
    for (int z = -1; z <= 1; ++z)
    {
      FluidTest::EnqueueFluidFrontier(queue, world, *definitions,
                                      glm::ivec3(x, 12, z));
    }
  }
  FluidTest::RunPhysicsFluidQueueTicks(world, *definitions, queue, fluid, 128);

  FluidTest::Expect(queue.GetStats().Depth == 0, kTestName,
                    "multi-block canopy drains the liquid queue after settle");
}

} // namespace

int main()
{
  const auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UFluidSpreadSystem fluid;

  TestDirectWaterlog(definitions, registry);
  TestPitSpreadPreservesGrass(definitions, registry, fluid);
  TestStrandedWaterOnSaturatedPermeableDrainsQueue(definitions, fluid);
  TestFlowingWaterOverPermeableDoesNotChurn(definitions, fluid);
  TestSourceNearOpenAirDoesNotStayInLiquidQueue(definitions, fluid);
  TestCanopyGridDrainsQueue(definitions, fluid);

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
