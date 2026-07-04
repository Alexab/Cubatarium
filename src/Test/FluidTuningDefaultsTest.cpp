#include "World/Physics/FluidTuning.h"

#include "Test/FluidTestHelpers.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "fluid_tuning_defaults_test";

static void TestWaterDropBoost()
{
  FluidTest::Expect(cutum::FluidTuning::WaterDropBoost == 4, kTestName,
                    "water drop boost default");
}

static void TestFloodPasses()
{
  FluidTest::Expect(cutum::FluidTuning::FloodMaxPassesDefault == 8, kTestName,
                    "flood max passes default");
}

static void TestCoastalBand()
{
  FluidTest::Expect(cutum::FluidTuning::CoastalPermeableBandAboveSea == 8,
                    kTestName, "coastal permeable band");
}

} // namespace

int main()
{
  TestWaterDropBoost();
  TestFloodPasses();
  TestCoastalBand();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
