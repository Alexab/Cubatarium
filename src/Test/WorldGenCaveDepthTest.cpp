#include "Test/FluidTestHelpers.h"

#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/CaveDepthBand.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "worldgen_cave_depth_test";

static void TestDeepBandValidAtSeaLevel()
{
  cutum::CaveParams params;
  params.minY = 4;
  params.maxDepthBelowSurface = 48;
  params.minDepthBelowSurface = 2;
  const cutum::CaveDepthBand band = cutum::ComputeCaveDepthBand(51, params);
  FluidTest::Expect(band.valid, kTestName,
                    "cave depth band valid near sea level");
  FluidTest::Expect(band.y_bottom == 4, kTestName,
                    "cave band bottom clamped to minY");
  FluidTest::Expect(band.y_top == 49, kTestName,
                    "cave band top is surface minus min depth");
}

static void TestShallowBandStillValid()
{
  cutum::CaveParams params;
  params.minY = 4;
  params.maxDepthBelowSurface = 6;
  params.minDepthBelowSurface = 2;
  const cutum::CaveDepthBand band = cutum::ComputeCaveDepthBand(51, params);
  FluidTest::Expect(band.valid, kTestName, "legacy shallow depth still valid");
  FluidTest::Expect(band.y_bottom == 45, kTestName,
                    "shallow band bottom follows max depth");
  FluidTest::Expect(band.y_top == 49, kTestName,
                    "shallow band top follows min depth");
}

} // namespace

int main()
{
  TestDeepBandValidAtSeaLevel();
  TestShallowBandStillValid();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
