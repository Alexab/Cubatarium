#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/WorldGenPlacementTuning.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "worldgen_hills_vegetation_gate_test";
constexpr int kSea = 48;
constexpr int kMaxHeight = 128;

static void TestLowCoastalColumnAllowsVegetation()
{
  const int topSolid = kSea + 3;
  FluidTest::Expect(
      cutum::HillsVegetationAllowed(topSolid, kSea, kMaxHeight), kTestName,
      "low coastal topSolid below hills vegetation cutoff");
}

static void TestHighHillColumnBlocksVegetation()
{
  const int topSolid = kSea + 70;
  FluidTest::Expect(
      !cutum::HillsVegetationAllowed(topSolid, kSea, kMaxHeight), kTestName,
      "high hill topSolid above hills vegetation cutoff");
}

static void TestGateUsesTopSolidNotHeightmap()
{
  const int heightmapSurface = kSea + 70;
  const int resolvedTopSolid = kSea + 5;
  FluidTest::Expect(
      !cutum::HillsVegetationAllowed(heightmapSurface, kSea, kMaxHeight),
      kTestName, "heightmap alone would block vegetation");
  FluidTest::Expect(
      cutum::HillsVegetationAllowed(resolvedTopSolid, kSea, kMaxHeight),
      kTestName, "resolved topSolid allows vegetation on coastal shelf");
}

} // namespace

int main()
{
  TestLowCoastalColumnAllowsVegetation();
  TestHighHillColumnBlocksVegetation();
  TestGateUsesTopSolidNotHeightmap();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
