#ifndef WORLDSTREAMING_H
#define WORLDSTREAMING_H

#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Chunks/StreamingAltitudePolicy.h"
#include "World/Streaming/StreamingPressure.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include <chrono>
#include <deque>
#include <glm/glm.hpp>
#include <memory>

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
                       glm::vec3 &lastCameraPosition, float &lastMovementSpeed);

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
  void RefreshStreamingPressure(UWorld &world);

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
  /// Sync GenerateColumn path: one CoarseHeightCache per ground chunk.
  glm::ivec3 SyncCoarseCacheGround{INT32_MAX, 0, INT32_MAX};
  int AdaptiveEffectiveRd{-1};
  double PhysMsEma{0.0};
  std::chrono::steady_clock::time_point AdaptiveRdLastAdjust{};
  StreamingPressureState PressureState{};
  StreamingPressureCaps LastPressureCaps{};
};

} // namespace cutum

#endif // WORLDSTREAMING_H
