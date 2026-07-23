#include "World/Physics/FluidTuning.h"
#include "World/Core/RuntimeTuning.h"

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
  FluidTest::Expect(
      cutum::URuntimeTuning::Get().CoastalBandAboveSea ==
          cutum::FluidTuning::CoastalPermeableBandAboveSea,
      kTestName, "runtime coastal band matches default");
}

static void TestRuntimeTuningDefaults()
{
  cutum::URuntimeTuning::ResetToDefaults();
  const cutum::URuntimeTuning &t = cutum::URuntimeTuning::Get();
  FluidTest::Expect(t.WaterDropBoost == cutum::FluidTuning::WaterDropBoost,
                    kTestName, "runtime water drop boost default");
  FluidTest::Expect(t.FloodMaxPasses == cutum::FluidTuning::FloodMaxPassesDefault,
                    kTestName, "runtime flood passes default");
  FluidTest::Expect(t.FluidSurfaceScanUp == 32, kTestName,
                    "runtime surface scan up default");
  FluidTest::Expect(t.FluidSurfaceScanDown == 64, kTestName,
                    "runtime surface scan down default");
  FluidTest::Expect(t.FluidSurfaceWindowMoveThreshold == 32, kTestName,
                    "runtime window threshold default");
  FluidTest::Expect(
      std::abs(t.HillsVegetationHeightNormMax - 0.82f) < 1e-5f, kTestName,
      "runtime hills vegetation norm default");
}

} // namespace

int main()
{
  TestWaterDropBoost();
  TestFloodPasses();
  TestCoastalBand();
  TestRuntimeTuningDefaults();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
