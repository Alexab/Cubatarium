#include "Test/FluidTestHelpers.h"

#include "World/Core/BlockWorld.h"
#include "World/Physics/FluidBlockResolver.h"
#include "World/Physics/FluidSpreadSystem.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "fluid_kind_resolver_test";
constexpr cutum::BlockId kWater = 9;
constexpr cutum::BlockId kLava = 11;
constexpr cutum::BlockId kTallGrass = 10;

static void TestResolverMatchesSpreadSystemForWater()
{
  cutum::UBlockWorld world;
  auto definitions = FluidTest::MakeTestFluidDefinitions();
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
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
  FluidTest::Expect(via_resolver == kWater, kTestName, "resolved water block id");
}

static void TestWaterloggedDecorPrefersWaterOverLava()
{
  auto definitions = FluidTest::MakeTestWaterLavaDecorDefinitions();
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(0, 63, 0), kLava);
  world.SetFluidState(glm::ivec3(0, 63, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 63, 0), kWater);
  world.SetFluidState(glm::ivec3(1, 63, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(2, 63, 0), kTallGrass);
  world.SetFluidState(glm::ivec3(2, 63, 0), cutum::FluidCellState::Flowing(1));

  const cutum::BlockId via_resolver =
      cutum::UFluidBlockResolver::ResolveFluidKind(
          world, *definitions, glm::ivec3(2, 63, 0), kTallGrass);
  const cutum::BlockId via_spread = cutum::UFluidSpreadSystem::ResolveFluidKind(
      world, *definitions, glm::ivec3(2, 63, 0), kTallGrass);
  FluidTest::Expect(via_resolver == kWater, kTestName,
                    "waterlogged decor resolves water over lava");
  FluidTest::Expect(via_resolver == via_spread, kTestName,
                    "resolver matches spread for waterlogged kind");
}

static void TestFluidKindEnumFromNeighborAwareBlock()
{
  auto definitions = FluidTest::MakeTestWaterLavaDecorDefinitions();
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(2, 63, 0), kTallGrass);
  world.SetFluidState(glm::ivec3(2, 63, 0), cutum::FluidCellState::Flowing(1));
  world.SetBlock(glm::ivec3(1, 63, 0), kWater);
  world.SetFluidState(glm::ivec3(1, 63, 0), cutum::FluidCellState::Source());

  cutum::UFluidBlockResolver resolver(*definitions);
  const cutum::FluidKind kind = resolver.ResolveFluidKind(
      world, glm::ivec3(2, 63, 0), kTallGrass);
  FluidTest::Expect(kind == cutum::FluidKind::Water, kTestName,
                    "IU resolver returns water kind for wet decor");
}

} // namespace

int main()
{
  TestResolverMatchesSpreadSystemForWater();
  TestWaterloggedDecorPrefersWaterOverLava();
  TestFluidKindEnumFromNeighborAwareBlock();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
