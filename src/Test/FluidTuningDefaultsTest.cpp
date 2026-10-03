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
  // Phase B streaming budget defaults (wall↓ without SoT changes).
  FluidTest::Expect(t.MeshFlyCapYellow == 8, kTestName, "mesh fly yellow");
  FluidTest::Expect(t.MeshFlyCapRed == 6, kTestName, "mesh fly red");
  FluidTest::Expect(t.DirtyThrashSoftCap == 320, kTestName, "dirty thrash soft");
  FluidTest::Expect(t.MeshFlyCapWallHot == 6, kTestName, "mesh fly wall hot");
  FluidTest::Expect(t.MeshFlyCapWallMid == 10, kTestName, "mesh fly wall mid");
  FluidTest::Expect(t.ImmediateBudgetHotMs == 3.0f, kTestName, "imm hot ms");
  FluidTest::Expect(t.CaptureDrainMovingMs == 3.0f, kTestName, "capture moving");
  FluidTest::Expect(t.CaptureDrainHolesMovingMs == 5.0f, kTestName,
                    "capture holes moving");
  FluidTest::Expect(t.CaptureDrainHighPendingMovingMs == 6.0f, kTestName,
                    "capture high pending moving");
  FluidTest::Expect(t.CaptureHotFrameMult == 4.0f, kTestName, "capture hot mult");
  FluidTest::Expect(t.CaptureMovingBgCap == 1, kTestName, "capture moving bg");
  FluidTest::Expect(t.MemoryHitchCaptureWallMs == 400.0f, kTestName,
                    "memory hitch wall");
  FluidTest::Expect(t.FogPullInExpandSec == 2.5f, kTestName, "fog expand sec");
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
