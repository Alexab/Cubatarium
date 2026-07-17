#pragma once

namespace cutum
{

struct URuntimeTuning
{
  int FluidSurfaceScanUp{32};
  int FluidSurfaceScanDown{64};
  int FluidSurfaceWindowMoveThreshold{16};
  float HillsVegetationHeightNormMax{0.82f};
  int WaterDropBoost{4};
  int FloodMaxPasses{8};
  int CoastalBandAboveSea{8};
  /// Extra keep ring beyond visual RD (voxels without mandatory mesh).
  int KeepPrefetchMargin{2};
  /// Max keep-shell async gen requests per frame when backpressure allows.
  int MaxKeepPrefetchOpsPerFrame{2};

  static URuntimeTuning &Get();
  static void ResetToDefaults();
};

} // namespace cutum
