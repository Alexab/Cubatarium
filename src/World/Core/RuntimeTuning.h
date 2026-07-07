#pragma once

namespace cutum
{

struct URuntimeTuning
{
  int FluidSurfaceScanUp{32};
  int FluidSurfaceScanDown{64};
  int FluidSurfaceWindowMoveThreshold{8};
  float HillsVegetationHeightNormMax{0.82f};
  int WaterDropBoost{4};
  int FloodMaxPasses{8};
  int CoastalBandAboveSea{8};
  /// TD-FL-034: gated rollout for shore/seafloor fog policy tweaks.
  bool BelowSurfaceFogV2{false};

  static URuntimeTuning &Get();
  static void ResetToDefaults();
};

} // namespace cutum
