#pragma once

#include <cstdint>

namespace cutum
{

struct URuntimeTuning
{
  int FluidSurfaceScanUp{32};
  int FluidSurfaceScanDown{64};
  int FluidSurfaceWindowMoveThreshold{32};
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
  /// StreamingPressure Yellow/Red mesh schedule caps (ApplyPressureCap).
  /// Phase B defaults tightened vs historical 10/8 to cut emerge wall.
  int MeshFlyCapYellow{8};
  int MeshFlyCapRed{6};
  /// Added to recover_n after pressure caps (iterate knob).
  int RecoverNBoost{0};

  // --- Phase B: main-thread Capture / Immediate / fly / hitch budgets ---
  /// DrainRelightQueues Capture wall budgets (ms). SoftDefer hole exception
  /// semantics stay in WorldPersistence; these only bound Capture time.
  float CaptureDrainMovingMs{3.0f};
  float CaptureDrainIdleMs{8.0f};
  float CaptureDrainHolesMovingMs{5.0f};
  float CaptureDrainHolesIdleMs{10.0f};
  float CaptureDrainHighPendingMovingMs{6.0f};
  float CaptureDrainHighPendingIdleMs{12.0f};
  /// Skip further Capture when frame_ms >= budget * mult (unless SoftDefer hole).
  float CaptureHotFrameMult{4.0f};
  float CaptureSyncSkipWallMs{110.0f};
  float CaptureIdlePendingMaxWallMs{160.0f};
  /// Cruise max Captures/frame (TD-ARCH-015: worker Capture still backlog).
  int CaptureMovingBgCap{1};

  /// Era14.1 B: hard wall budget for TickWorldStreamingPhase (ms). Miss / UV
  /// carve-out skips this — FirstMesh/emerge heal always runs. Cuts EnterGame
  /// burst when stream already spent the budget on a clean frame.
  float StreamingPhaseBudgetMs{24.0f};

  /// RebuildChunkImmediate hard budget (idle only; moving sync_cap=0).
  float ImmediateBudgetHotMs{3.0f};
  float ImmediateBudgetOkMs{5.0f};
  float ImmediateHotWallMs{22.0f};

  /// Moving mesh schedule baselines before StreamingPressure fly_cap.
  float MeshFlyWallHotMs{22.0f};
  float MeshFlyWallMidMs{16.0f};
  int MeshFlyCapWallHot{6};
  int MeshFlyCapWallMid{10};
  int MeshFlyCapWallOk{12};
  int MeshFlyCapHolesHot{8};
  int MeshFlyCapHolesOk{12};

  /// MemoryBudgetController Green expand / hitch Capture gates.
  float MemoryGreenMaxWallMs{28.0f};
  float MemoryHitchCaptureWallMs{400.0f};
  float MemoryUrgentEvalWallMs{100.0f};

  /// Fog pull-in timing only (not SoT / unfinished predicates).
  float FogPullInExpandSec{2.5f};
  float FogPullInShrinkSec{0.45f};
  float FogPullInSevereWallMs{100.0f};

  /// Memory budget (med tier defaults). Soft < ExpandKeep < Budget.
  int MemoryBudgetMb{1536};
  int MemorySoftMb{1152};
  int MemoryExpandKeepMb{768};
  /// 0 → workers * pipeline slots (mesh×6 / relight×8).
  int MeshCompletedSlots{0};
  int RelightCompletedSlots{0};
  int DirtySoftCap{1200};
  /// When mesh_async >= DirtyThrashAsyncMin, use this lower SoftCap (thrash).
  /// Phase B: 320 (was 400) — engage remesh drop sooner under Yellow/async.
  int DirtyThrashSoftCap{320};
  int DirtyThrashAsyncMin{12};
  int PendingLightSoftCap{80};
  int RelightFifoSoftCap{96};
  /// Era19 kill-switch: SoftDefer Capture floor while VisibleBlack (Era18).
  /// Default true = current Era18 behavior until miss-first budget owns it.
  bool Era18VbCaptureFloor{true};
  /// Era19 kill-switch: bg_budget floor while VisibleBlack (Era18).
  bool Era18VbBgBudgetFloor{true};
  /// Era19 P1: unified miss-first FrameStreamingBudget (default on).
  bool MissFirstFrameBudget{true};
  int GpuVertexPoolReserveMb{64};
  int GpuVertexPoolMaxMb{256};
  int MaxKeepPrefetchMargin{4};
  int MemoryExpandMaxRd{6};
  /// Cap recycled UChunk free-list (0 → auto from Keep footprint / 4).
  int MaxResidentChunks{0};
  /// Max vertical chunk layers per main-thread Capture (top-down bands).
  /// 0 = full column. SoftDefer keeps PendingLight until final band.
  int RelightCaptureBandCy{4};
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
