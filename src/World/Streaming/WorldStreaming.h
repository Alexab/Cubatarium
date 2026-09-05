#ifndef WORLDSTREAMING_H
#define WORLDSTREAMING_H

#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Chunks/StreamingAltitudePolicy.h"
#include "World/Streaming/StreamingPressure.h"
#include "World/Streaming/MemoryBudgetController.h"
#include "World/Streaming/StreamIngressPolicy.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "World/Chunks/ChunkManager.h"
#include <chrono>
#include <climits>
#include <cstdint>
#include <deque>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_set>

namespace cutum
{

class UWorld;
class UWorldMeshService;
class UChunkEmergeCoordinator;
struct ProceduralSettings;
struct RenderSettings;

class UWorldStreaming
{
public:
  UWorldStreaming();
  ~UWorldStreaming();

  void EnsureStreamer(class UBlockWorld &blockWorld, class UBlockRegistry &registry,
                      uint32_t seed, const ProceduralSettings &settings);
  bool HasStreamer() const { return Streamer != nullptr; }
  UChunkStreamer *GetStreamer() { return Streamer.get(); }
  const UChunkStreamer *GetStreamer() const { return Streamer.get(); }
  UChunkGenerationRegistry &GetChunkGenTokens() { return ChunkGenTokens; }
  const UChunkGenerationRegistry &GetChunkGenTokens() const
  {
    return ChunkGenTokens;
  }

  void SetRenderDistance(int distance);
  void SetStreamingEnabled(bool enabled) { StreamingEnabled = enabled; }
  bool IsStreamingEnabled() const { return StreamingEnabled; }

  void InitStreamerCallbacks(UWorld &world);
  void RefreshStreamerSettings(const ProceduralSettings &settings,
                               int maxLoadOpsPerFrame, int maxUnloadOpsPerFrame);

  void UpdateStreaming(UWorld &world, UWorldMeshService &meshService,
                       const RenderSettings &render, int renderDistanceChunks,
                       int &effectiveRenderDistance,
                       float &effectiveFogStartRatio,
                       StreamingAltitudePolicyParams &altitudeParams,
                       glm::vec3 &lastCameraPosition, float &lastMovementSpeed,
                       glm::vec2 &lastMovementDirXz);

  void TickAsyncChunkSystems(UWorld &world);
  void TickMeshEmerge(UWorld &world);
  void QuiesceBackgroundWork(UWorld &world,
                             std::chrono::milliseconds async_io_timeout =
                                 std::chrono::milliseconds(2000));
  void PauseChunkGeneration(
      std::chrono::milliseconds worker_wait =
          std::chrono::milliseconds(10000));
  /// Cancel queued gen + bump tokens without waiting for in-flight populate.
  void CancelChunkGeneration();
  /// Process exit: cancel gen, brief wait, then drop or leak workers so
  /// Join never blocks forever on late ChunkPopulate/carve.
  void AbandonWorkersForProcessExit(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(250));
  void ResumeStreamerAfterQuiesce();

  UChunkEmergeCoordinator &GetEmergeCoordinator() { return *EmergeCoordinator; }
  const UChunkEmergeCoordinator &GetEmergeCoordinator() const
  {
    return *EmergeCoordinator;
  }
  UChunkLoadScheduler *GetChunkScheduler() { return ChunkScheduler.get(); }
  const UChunkLoadScheduler *GetChunkScheduler() const
  {
    return ChunkScheduler.get();
  }

  void EnsureCollisionChunks(const glm::ivec3 &feetBlock,
                             const glm::vec3 &forward);

  void ResetFrameTiming();
  double GetFrameStreamingGenMs() const { return FrameStreamingGenMs; }
  double GetFrameStreamingIoMs() const { return FrameStreamingIoMs; }
  const StreamingFrameStats *GetLastFrameStats() const;

  void SetStreamerMaxLoadOpsPerFrame(int value);

  void WarmupSpawnAreaForEnterGame(UWorld &world);
  void PrepareEnterGameSession(UWorld &world);

  void MarkPersistedColumnsFromWorld();

  const StreamingPressureCaps &GetLastPressureCaps() const
  {
    return LastPressureCaps;
  }

private:
  void InitChunkScheduler(UWorld &world);
  void RefreshStreamingPressure(
      UWorld &world,
      std::chrono::high_resolution_clock::time_point stream_t0,
      double stream_budget_ms);

  std::unique_ptr<UChunkStreamer> Streamer;
  std::unique_ptr<UChunkEmergeCoordinator> EmergeCoordinator;
  std::unique_ptr<UPipelineChunkPopulator> ChunkPopulator;
  std::unique_ptr<UChunkLoadScheduler> ChunkScheduler;
  UChunkGenerationRegistry ChunkGenTokens;
  bool StreamingEnabled{true};
  double FrameStreamingGenMs{0.0};
  double FrameStreamingIoMs{0.0};
  std::deque<glm::ivec3> DeferredPhysicsSeedQueue;
  std::deque<glm::ivec3> DeferredShoreSealQueue;
  /// Columns that still need IntraChunk seal (never sync on commit — CB hitch).
  std::unordered_set<glm::ivec3, IVec3Hash> DeferredIntraChunkSealNeeded;
  /// Sync GenerateColumn path: one CoarseHeightCache per ground chunk.
  glm::ivec3 SyncCoarseCacheGround{INT32_MAX, 0, INT32_MAX};
  int AdaptiveEffectiveRd{-1};
  double PhysMsEma{0.0};
  std::chrono::steady_clock::time_point AdaptiveRdLastAdjust{};
  int FogPullInRd{-1};
  std::chrono::steady_clock::time_point FogPullInLastAdjust{};
  std::chrono::steady_clock::time_point FogPullInLastShrink{};
  /// Hole-debt latch: hold pull-in after holes clear so fog End / start_ratio
  /// do not thrash every period (manual 084551 opaque 1037↔223).
  int FogPullInHoleHoldFrames{0};
  int FogPullInMarginHeld{-1};
  float FogPullInStartRatioHeld{-1.0f};
  StreamingPressureState PressureState{};
  StreamingPressureCaps LastPressureCaps{};
  /// Cached once per RefreshStreamingPressure — safe for commit callback.
  int LastPendingLightFocus{0};
  UMemoryBudgetController MemoryBudget{};
  int StreamingFrameCounter{0};
  int LastUnderfeetSyncFrame{-1};
  MemoryBudgetDecision LastMemoryDecision{};
  uint64_t LastMeshCompletedDiscarded{0};
  uint64_t LastRelightCompletedDiscarded{0};
  int LastCompletedExpandFrame{-10000};
  /// Era27 I-A1: SoftDefer Capture witness pin (cx, cz, cy) for T frames.
  bool SoftDeferCapturePinValid{false};
  int SoftDeferCapturePinCx{0};
  int SoftDeferCapturePinCz{0};
  int SoftDeferCapturePinCy{-1};
  int SoftDeferCapturePinHoriz{0};
  int SoftDeferCapturePinAge{0};
  /// Pin length; Era29 enter sets EnterSpawnCapturePinFrames(), else Era27 T=8.
  int SoftDeferCapturePinMaxAge{8};
  /// I18-D1: hold prior column drawable briefly on witness column swap.
  WitnessSwapGrace WitnessColumnGrace{};
  /// R4.5.1: same-frame camera-column complete (UpdateStreaming → Refresh).
  glm::ivec3 LastCameraTerrainCompleteGround{INT32_MAX, 0, INT32_MAX};
  bool LastCameraTerrainComplete{false};
  int LastCameraTerrainCompleteFrame{-1};

  /// Perf-root P3: explicit cadence state (was function-static locals).
  struct RefreshProbeState
  {
    int miss_probe_cd{0};
    bool last_missing_near{false};
    int miss_positive_hold{0};
    glm::ivec2 last_sticky_focus_xz{INT_MAX, INT_MAX};
    int last_sticky_keep_cols{-1};
    int unfinished_reuse_age{0};
    int prev_focus_pressure{0};
    int focus_dirty_sample_cd{0};
    int last_focus_dirty{0};
    glm::ivec3 last_dirty_focus{0};
    int last_dirty_radius{-1};
    int unfinished_sample_cd{0};
    int last_unfinished_visual{0};
    glm::ivec3 last_unfinished_focus{0};
    int last_unfinished_radius{-1};
    int visible_black_sample_cd{0};
    int vb_published{0};
    int vb_pending_raw{0};
    int vb_pending_stable{0};
    int last_visible_black_no_ticket{0};
    int last_visible_black_progress{0};
    int last_visible_black_stalled{0};
    int vb_focus_stable_frames{0};
    int facing_sample_cd{0};
    int last_ahead{0};
    int last_behind{0};
    bool uf_predicted_latched{false};
    int uf_predicted_hold{0};
    /// SoftDefer witness retarget baseline (was function-static).
    uint64_t last_softdefer_witness_retarget{0};
  } RefreshProbe{};
};

} // namespace cutum

#endif // WORLDSTREAMING_H
