#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/FluidBlockResolver.h"
#include "World/Physics/FluidSpreadSystem.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "fluid_block_resolver_test";

static void TestResolverMatchesSpreadSystem()
{
  cutum::UBlockWorld world;
  auto definitions = FluidTest::MakeTestFluidDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  const cutum::BlockId water = registry.GetIdByTypeName("water");
  const cutum::BlockId stone = registry.GetIdByTypeName("stone");
  world.SetBlock(glm::ivec3(0, 10, 0), water);
  world.SetFluidState(glm::ivec3(0, 10, 0),
                       cutum::FluidCellState::Source().WithKind(
                           cutum::FluidKind::Water));

  cutum::UFluidBlockResolver resolver(*definitions);
  const cutum::BlockId via_resolver =
      resolver.ResolveFluidBlockId(world, glm::ivec3(0, 10, 0));
  const cutum::BlockId via_spread = cutum::UFluidSpreadSystem::ResolveFluidBlockId(
      world, *definitions, glm::ivec3(0, 10, 0));
  FluidTest::Expect(via_resolver == via_spread, kTestName,
                    "resolver matches spread system for water cell");
  FluidTest::Expect(via_resolver == water, kTestName, "resolved water block id");
  std::cout << kTestName << ": OK" << std::endl;
}

} // namespace

int main()
{
  TestResolverMatchesSpreadSystem();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
