#include "World/Diagnostics/MovementDiagnosticsRecorder.h"

#include "Creatures/Player/PlayerCapsule.h"
#include "Render/Camera/Camera.h"
#include "Render/Engine/ViewEngine.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Core/World.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Streaming/WorldStreaming.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

using json = nlohmann::json;

void UMovementDiagnosticsRecorder::SaveToFile(const UWorld &world,
                                            const std::string &file_name)
{
  json root;
  root["schema"] = "movement_diagnostics.v2";
  root["world_name"] = world.WorldName;
  root["sample_count"] = world.MovementDiagHistory.size();
  std::vector<json> samples;
  samples.reserve(world.MovementDiagHistory.size());
  for (const UWorld::MovementDiagnostics &sample : world.MovementDiagHistory)
  {
    samples.push_back({
        {"delta_time", sample.deltaTime},
        {"player_y_drop", sample.playerYDrop},
        {"streaming_loads", sample.streamingLoads},
        {"streaming_unloads", sample.streamingUnloads},
        {"streaming_gen_ms", sample.streamingGenMs},
        {"streaming_io_ms", sample.streamingIoMs},
        {"mesh_rebuild_ms", sample.meshRebuildMs},
        {"dirty_chunks_pending", sample.dirtyChunksPending},
        {"mesh_rebuilds_this_frame", sample.meshRebuildsThisFrame},
        {"flat_rebuild_ms", sample.flatRebuildMs},
        {"count_non_air_ms", sample.countNonAirMs},
        {"async_mesh_in_flight", sample.asyncMeshInFlight},
        {"async_meshing_enabled", sample.asyncMeshingEnabled},
        {"greedy_cache_entries", sample.greedyCacheEntries},
        {"frames_since_load", sample.framesSinceLoad},
        {"mesh_backlog_cleared", sample.meshBacklogCleared},
        {"hitch_detected", sample.hitchDetected},
        {"fall_through_suspected", sample.fallThroughSuspected},
    });
  }
  root["samples"] = json(samples);
  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << root.dump(2);
  }
}

void UMovementDiagnosticsRecorder::Update(UWorld &world,
                                          const std::shared_ptr<UCamera> &camera,
                                          float prev_player_y)
{
  world.MovementDiag = UWorld::MovementDiagnostics{};
  if (!camera)
  {
    return;
  }

  const glm::vec3 playerPos = camera->GetPosition();
  const PlayerCapsule cap = camera->GetPlayerCapsule();
  world.MovementDiag.feetBlock = WorldPosToBlock(glm::vec3(
      playerPos.x, cap.feetY(playerPos) + 0.01f, playerPos.z));
  world.MovementDiag.feetChunk =
      UChunkManager::WorldToChunk(world.MovementDiag.feetBlock);
  const glm::ivec3 feetGround(world.MovementDiag.feetChunk.x, 0,
                              world.MovementDiag.feetChunk.z);
  world.MovementDiag.feetChunkLoaded =
      world.BlockWorld.GetChunkManager().HasChunk(feetGround);
  world.MovementDiag.feetIsAir = world.BlockWorld.IsAir(world.MovementDiag.feetBlock);
  world.MovementDiag.meshDrawCount = world.GetRenderInstanceCount();
  world.MovementDiag.deltaTime = camera->GetDeltaTime();
  world.MovementDiag.streamingGenMs = world.Streaming->GetFrameStreamingGenMs();
  world.MovementDiag.streamingIoMs = world.Streaming->GetFrameStreamingIoMs();
  world.MovementDiag.dirtyChunksPending =
      static_cast<int>(world.MeshService->GetDirtyCount());
  world.MovementDiag.flatRebuildMs = world.MeshService->GetLastFlatRebuildMs();
  world.MovementDiag.asyncMeshInFlight = world.MeshService->GetAsyncInFlightCount();
  world.MovementDiag.asyncMeshingEnabled = world.Render.AsyncMeshing;
  world.MovementDiag.greedyCacheEntries =
      static_cast<int>(world.MeshService->GetGreedyCacheSize());
  world.MovementDiag.framesSinceLoad = world.FramesSinceLoad;
  world.MovementDiag.meshBacklogCleared = world.MeshBacklogClearedLatch;
  world.TickMeshLoadDiagnostics();

  if (world.HasLastPlayerY)
  {
    world.MovementDiag.playerYDrop = prev_player_y - playerPos.y;
  }
  else
  {
    world.MovementDiag.playerYDrop = 0.0f;
  }
  world.HasLastPlayerY = true;
  world.LastPlayerY = playerPos.y;

  if (const StreamingFrameStats *stats = world.Streaming->GetLastFrameStats())
  {
    world.MovementDiag.streamingLoads = stats->loadsThisFrame;
    world.MovementDiag.streamingUnloads = stats->unloadsThisFrame;
    for (const glm::ivec3 &coord : stats->unloadedCoords)
    {
      if (coord.x == feetGround.x && coord.z == feetGround.z)
      {
        world.MovementDiag.feetInUnloadList = true;
        break;
      }
    }
  }

  const double frameMs =
      (world.DurationDoMovementMks +
       (world.ViewInstance ? world.ViewInstance->GetDurationUpdateMks() : 0.0)) /
      1000.0;
  world.MovementDiag.hitchDetected =
      frameMs > 50.0 || world.MovementDiag.deltaTime > 0.1f;
  world.MovementDiag.fallThroughSuspected =
      world.MovementDiag.playerYDrop > 2.0f &&
      (world.MovementDiag.feetIsAir || !world.MovementDiag.feetChunkLoaded) &&
      world.MovementDiag.meshDrawCount > 0;

#ifdef CUBATARIUM_DEBUG
  if (world.MovementDiag.hitchDetected || world.MovementDiag.fallThroughSuspected ||
      world.MovementDiag.playerYDrop > 2.0f)
  {
    std::cerr << "[Movement-debug] cameraDt=" << world.MovementDiag.deltaTime
              << " frameMs=" << frameMs
              << " yDrop=" << world.MovementDiag.playerYDrop << " feetChunk=("
              << world.MovementDiag.feetChunk.x << ","
              << world.MovementDiag.feetChunk.y << ","
              << world.MovementDiag.feetChunk.z << ")"
              << " hasChunk=" << world.MovementDiag.feetChunkLoaded
              << " feetAir=" << world.MovementDiag.feetIsAir
              << " meshDraw=" << world.MovementDiag.meshDrawCount
              << " loads=" << world.MovementDiag.streamingLoads
              << " unloads=" << world.MovementDiag.streamingUnloads
              << " feetUnloaded=" << world.MovementDiag.feetInUnloadList
              << std::endl;
  }
#endif
  constexpr size_t kMaxSamples = 4096;
  world.MovementDiagHistory.push_back(world.MovementDiag);
  if (world.MovementDiagHistory.size() > kMaxSamples)
  {
    const size_t trim = world.MovementDiagHistory.size() - kMaxSamples;
    world.MovementDiagHistory.erase(
        world.MovementDiagHistory.begin(),
        world.MovementDiagHistory.begin() + static_cast<std::ptrdiff_t>(trim));
  }
}

} // namespace cutum
