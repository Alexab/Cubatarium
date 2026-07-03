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

} // namespace

int main()
{
  const auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UFluidSpreadSystem fluid;

  TestDirectWaterlog(definitions, registry);
  TestPitSpreadPreservesGrass(definitions, registry, fluid);

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
