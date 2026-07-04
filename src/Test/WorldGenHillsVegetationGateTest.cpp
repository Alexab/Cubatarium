#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPlacementTuning.h"

#include <algorithm>
#include <iostream>

namespace
{

constexpr const char *kTestName = "worldgen_hills_vegetation_gate_test";
constexpr int kSea = 48;
constexpr int kMaxHeight = 128;

static float HillsHeightNorm(int topSolidY)
{
  const int range = std::max(1, kMaxHeight - kSea);
  return std::clamp(static_cast<float>(topSolidY - kSea) /
                        static_cast<float>(range),
                    0.0f, 1.0f);
}

static void TestLowCoastalColumnAllowsVegetation()
{
  const int topSolid = kSea + 3;
  const float heightNorm = HillsHeightNorm(topSolid);
  FluidTest::Expect(
      heightNorm <= cutum::WorldGenPlacementTuning::HillsVegetationHeightNormMax,
      kTestName, "low coastal topSolid below hills vegetation cutoff");
}

static void TestHighHillColumnBlocksVegetation()
{
  const int topSolid = kSea + 70;
  const float heightNorm = HillsHeightNorm(topSolid);
  FluidTest::Expect(
      heightNorm > cutum::WorldGenPlacementTuning::HillsVegetationHeightNormMax,
      kTestName, "high hill topSolid above hills vegetation cutoff");
}

static void TestGateUsesTopSolidNotHeightmap()
{
  const int heightmapSurface = kSea + 70;
  const int resolvedTopSolid = kSea + 5;
  FluidTest::Expect(HillsHeightNorm(heightmapSurface) >
                        cutum::WorldGenPlacementTuning::HillsVegetationHeightNormMax,
                    kTestName, "heightmap alone would block vegetation");
  FluidTest::Expect(
      HillsHeightNorm(resolvedTopSolid) <=
          cutum::WorldGenPlacementTuning::HillsVegetationHeightNormMax,
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
