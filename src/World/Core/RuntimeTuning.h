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

  /// Mesh/relight soft forward bias in Chebyshev units (0 = off).
  float MeshForwardBiasK{0.75f};
  /// Async relight inflight = RelightThreadCount * mult.
  int RelightInflightMultHigh{4};
  int RelightInflightMultHoles{8};
  int MeshFlyCapYellow{10};
  int MeshFlyCapRed{8};
  /// Added to recover_n after pressure caps (iterate knob).
  int RecoverNBoost{0};

  static URuntimeTuning &Get();
  static void ResetToDefaults();
  /// Overlay knobs from bin/streaming_tune.json (flight_sim_iterate).
  static void LoadStreamingTuneFile(const char *path);
};

} // namespace cutum
