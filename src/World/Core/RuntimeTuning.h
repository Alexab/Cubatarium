#pragma once

#include <cstdint>

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

  /// Memory budget (med tier defaults). Soft < ExpandKeep < Budget.
  int MemoryBudgetMb{1536};
  int MemorySoftMb{1152};
  int MemoryExpandKeepMb{768};
  /// 0 → workers * pipeline slots (mesh×6 / relight×8).
  int MeshCompletedSlots{0};
  int RelightCompletedSlots{0};
  int DirtySoftCap{1200};
  int PendingLightSoftCap{80};
  int RelightFifoSoftCap{96};
  int GpuVertexPoolReserveMb{64};
  int GpuVertexPoolMaxMb{256};
  int MaxKeepPrefetchMargin{4};
  int MemoryExpandMaxRd{6};
  bool CompletedExpandEnabled{true};
  /// Cumulative buffer expand events (Completed rings / GPU Reserve).
  uint64_t BufferExpandEvents{0};

  static URuntimeTuning &Get();
  static void ResetToDefaults();
  /// Apply low|med|high preset (keeps other knobs unless tier sets them).
  static void ApplyMemoryTier(const char *tier);
  /// Overlay knobs from bin/streaming_tune.json (flight_sim_iterate).
  static void LoadStreamingTuneFile(const char *path);
};

} // namespace cutum
