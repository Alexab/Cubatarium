#include "Test/FluidTestHelpers.h"

#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidFillPolicy.h"
#include "World/Physics/FluidSpreadSystem.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "fluid_fill_policy_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;
constexpr cutum::BlockId kTallGrass = 10;

static void TestCanReceiveFluidAirAndFloodable()
{
  auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
  cutum::UBlockWorld world;
  FluidTest::Expect(
      cutum::UFluidFillPolicy::CanReceiveFluid(world, *definitions,
                                               glm::ivec3(0, 10, 0)),
      kTestName, "air can receive fluid");
  world.SetBlock(glm::ivec3(1, 10, 0), kTallGrass);
  FluidTest::Expect(
      cutum::UFluidFillPolicy::CanReceiveFluid(world, *definitions,
                                               glm::ivec3(1, 10, 0)),
      kTestName, "permeable decor can receive fluid");
  world.SetBlock(glm::ivec3(2, 10, 0), kStone);
  FluidTest::Expect(
      !cutum::UFluidFillPolicy::CanReceiveFluid(world, *definitions,
                                                glm::ivec3(2, 10, 0)),
      kTestName, "solid cannot receive fluid");
}

static void TestShouldReplaceBlockWithFluid()
{
  auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
  cutum::UBlockWorld world;
  FluidTest::Expect(
      cutum::UFluidFillPolicy::ShouldReplaceBlockWithFluid(
          world, *definitions, glm::ivec3(0, 10, 0)),
      kTestName, "air should be replaced");
  world.SetBlock(glm::ivec3(1, 10, 0), kTallGrass);
  FluidTest::Expect(
      !cutum::UFluidFillPolicy::ShouldReplaceBlockWithFluid(
          world, *definitions, glm::ivec3(1, 10, 0)),
      kTestName, "permeable decor should not be replaced");
}

static void TestApplyFluidFillMatchesSpreadSystem()
{
  auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
  cutum::UBlockWorld via_policy;
  cutum::UBlockWorld via_spread;
  via_policy.SetBlock(glm::ivec3(0, 10, 0), kTallGrass);
  via_spread.SetBlock(glm::ivec3(0, 10, 0), kTallGrass);
  const cutum::FluidCellState state =
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Water);

  cutum::UFluidFillPolicy::ApplyFluidFill(via_policy, *definitions,
                                          glm::ivec3(0, 10, 0), kWater, state);
  cutum::UFluidSpreadSystem::ApplyFluidFill(via_spread, *definitions,
                                            glm::ivec3(0, 10, 0), kWater, state);

  FluidTest::Expect(via_policy.GetBlock(glm::ivec3(0, 10, 0)) == kTallGrass,
                    kTestName, "fill preserves permeable block");
  FluidTest::Expect(
      via_policy.GetFluidState(glm::ivec3(0, 10, 0)).GetKind() ==
          cutum::FluidKind::Water,
      kTestName, "fill writes explicit kind on decor");
  FluidTest::Expect(
      via_policy.GetBlock(glm::ivec3(0, 10, 0)) ==
          via_spread.GetBlock(glm::ivec3(0, 10, 0)),
      kTestName, "policy fill matches spread for block");
  FluidTest::Expect(
      cutum::PackFluidCellState(via_policy.GetFluidState(glm::ivec3(0, 10, 0))) ==
          cutum::PackFluidCellState(
              via_spread.GetFluidState(glm::ivec3(0, 10, 0))),
      kTestName, "policy fill matches spread for fluid state");
}

static void TestStoredFluidStateDowngradesSourceOnDecor()
{
  auto definitions = FluidTest::MakeTestFluidDecorDefinitions();
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(0, 10, 0), kTallGrass);
  const cutum::FluidCellState source =
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Water);
  const cutum::FluidCellState stored =
      cutum::UFluidFillPolicy::StoredFluidStateForCell(
          world, *definitions, glm::ivec3(0, 10, 0), source);
  FluidTest::Expect(!stored.IsSource(), kTestName,
                    "source downgraded to flowing on decor");
  FluidTest::Expect(stored.Level == 1, kTestName,
                    "decor stored state is flowing level 1");
}

} // namespace

int main()
{
  TestCanReceiveFluidAirAndFloodable();
  TestShouldReplaceBlockWithFluid();
  TestApplyFluidFillMatchesSpreadSystem();
  TestStoredFluidStateDowngradesSourceOnDecor();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
