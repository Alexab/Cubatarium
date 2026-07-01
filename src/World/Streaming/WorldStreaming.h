#ifndef WORLDSTREAMING_H
#define WORLDSTREAMING_H

#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Chunks/StreamingAltitudePolicy.h"
#include "WorldGen/Core/IUChunkPopulator.h"
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
                      uint32_t seed, int maxHeight);
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

  UChunkEmergeCoordinator &GetEmergeCoordinator() { return *EmergeCoordinator; }
  const UChunkEmergeCoordinator &GetEmergeCoordinator() const
  {
    return *EmergeCoordinator;
  }

  void EnsureCollisionChunks(const glm::ivec3 &feetBlock,
                             const glm::vec3 &forward);

  void ResetFrameTiming();
  double GetFrameStreamingGenMs() const { return FrameStreamingGenMs; }
  double GetFrameStreamingIoMs() const { return FrameStreamingIoMs; }
  const StreamingFrameStats *GetLastFrameStats() const;

  void SetStreamerMaxLoadOpsPerFrame(int value);

  void MarkPersistedColumnsFromWorld();

private:
  void InitChunkScheduler(UWorld &world);

  std::unique_ptr<UChunkStreamer> Streamer;
  std::unique_ptr<UChunkEmergeCoordinator> EmergeCoordinator;
  std::unique_ptr<UPipelineChunkPopulator> ChunkPopulator;
  std::unique_ptr<UChunkLoadScheduler> ChunkScheduler;
  UChunkGenerationRegistry ChunkGenTokens;
  bool StreamingEnabled{true};
  double FrameStreamingGenMs{0.0};
  double FrameStreamingIoMs{0.0};
};

} // namespace cutum

#endif // WORLDSTREAMING_H
