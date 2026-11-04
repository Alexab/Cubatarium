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
#include "World/Physics/PhysicsTelemetry.h"
#include "World/Streaming/WorldStreaming.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

using json = nlohmann::json;

namespace
{

json PopulateMetricToJson(const ChunkPopulateMetric &metric)
{
  return json{{"count", metric.count},
              {"total_ms", metric.totalMs},
              {"median_ms", metric.medianMs},
              {"p95_ms", metric.p95Ms},
              {"max_ms", metric.maxMs}};
}

const char *PopulateStageName(size_t index)
{
  static constexpr std::array<const char *, WorldGenStageIdCount> names = {
      "terrain",      "ravines",   "valleys",      "lakes",
      "caves",        "fluids",    "ores",         "vegetation",
      "ground_cover", "decoration", "structures", "lava_pools",
      "fire_patch",   "geology",   "aquifers",     "cave_features"};
  return names[index];
}

json PopulateSummaryToJson(const ChunkPopulateSummary &summary)
{
  json root{{"schema", "chunk_populate_summary.v1"},
            {"completed_count", summary.completedCount},
            {"discarded_count", summary.discardedCount},
            {"detail_count", summary.detailCount},
            {"total", PopulateMetricToJson(summary.total)},
            {"sample", PopulateMetricToJson(summary.sample)},
            {"terrain", PopulateMetricToJson(summary.terrain)},
            {"carve", PopulateMetricToJson(summary.carve)},
            {"post", PopulateMetricToJson(summary.post)},
            {"seal", PopulateMetricToJson(summary.seal)},
            {"mudflow", PopulateMetricToJson(summary.mudflow)},
            {"pocket_seal", PopulateMetricToJson(summary.pocketSeal)},
            {"permeable_seal", PopulateMetricToJson(summary.permeableSeal)},
            {"pocket_reseal", PopulateMetricToJson(summary.pocketReseal)},
            {"vegetation_prune", PopulateMetricToJson(summary.vegetationPrune)},
            {"work", {{"mudflow_columns", summary.mudflowColumns},
                       {"mudflow_voxels_moved", summary.mudflowVoxelsMoved},
                       {"pocket_voxels_filled", summary.pocketVoxelsFilled},
                       {"permeable_columns", summary.permeableColumns},
                       {"permeable_columns_changed", summary.permeableColumnsChanged},
                       {"reseal_voxels_filled", summary.resealVoxelsFilled},
                       {"prune_columns", summary.pruneColumns},
                       {"pruned_voxels", summary.prunedVoxels}}}};
  for (size_t i = 0; i < summary.postStages.size(); ++i)
  {
    root["post_stages"][PopulateStageName(i)] =
        PopulateMetricToJson(summary.postStages[i]);
  }
  return root;
}

json ObjectFeatureSummaryToJson()
{
  json root{{"schema", "object_feature_rules.v1"},
            {"rules", json::array()}};
  for (const ObjectFeatureRuleProfile &profile :
       ObjectFeatureDiagnostics::GetSummary())
  {
    root["rules"].push_back(
        {{"pool", profile.pool},
         {"rule_index", profile.ruleIndex},
         {"feature_key", profile.featureKey},
         {"evaluated", profile.evaluated},
         {"candidates", profile.candidates},
         {"placement_attempts", profile.placementAttempts},
         {"placed", profile.placed},
         {"evaluation_ms", profile.evaluationMs},
         {"placement_ms", profile.placementMs},
         {"prefab_voxel_visits", profile.prefabVoxelVisits},
         {"prefab_surface_scans", profile.prefabSurfaceScans}});
  }
  return root;
}

} // namespace

void UMovementDiagnosticsRecorder::SaveToFile(const UWorld &world,
                                              const std::string &file_name)
{
  json root;
  root["schema"] = "movement_diagnostics.v2";
  root["world_name"] = world.WorldName;
  root["sample_count"] = world.MovementDiagHistory.size();
  root["populate_summary"] =
      PopulateSummaryToJson(ChunkPopulateDiagnostics::GetSummary());
  root["object_feature_summary"] = ObjectFeatureSummaryToJson();
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
        {"gen_queue_pending", sample.genQueuePending},
        {"gen_inflight", sample.genInFlight},
        {"populate_ms_last", sample.populateMsLast},
        {"populate_ms_ema", sample.populateMsEma},
        {"populate_sample_ms", sample.populateSampleMs},
        {"populate_terrain_ms", sample.populateTerrainMs},
        {"populate_carve_ms", sample.populateCarveMs},
        {"populate_post_ms", sample.populatePostMs},
        {"async_meshing_enabled", sample.asyncMeshingEnabled},
        {"greedy_cache_entries", sample.greedyCacheEntries},
        {"frames_since_load", sample.framesSinceLoad},
        {"mesh_backlog_cleared", sample.meshBacklogCleared},
        {"hitch_detected", sample.hitchDetected},
        {"fall_through_suspected", sample.fallThroughSuspected},
        {"physics_step_ms", sample.physicsStepMs},
        {"physics_movement_ms", sample.physicsMovementMs},
        {"physics_block_ms", sample.physicsBlockMs},
        {"physics_drain_ms", sample.physicsDrainMs},
        {"wall_frame_ms", sample.wallFrameMs},
        {"swap_wait_ms", sample.swapWaitMs},
        {"sim_ms", sample.simMs},
        {"unaccounted_ms", sample.unaccountedMs},
        {"physics_simulation_steps", sample.physicsSimulationSteps},
        {"physics_block_queue_depth", sample.physicsBlockQueueDepth},
        {"physics_liquid_queue_depth", sample.physicsLiquidQueueDepth},
        {"physics_deferred_updates", sample.physicsDeferredUpdates},
        {"physics_dropped_updates", sample.physicsDroppedUpdates},
        {"physics_purged_updates", sample.physicsPurgedUpdates},
        {"physics_collision_broadphase_rejects",
         sample.physicsCollisionBroadphaseRejects},
        {"physics_collision_broadphase_fallbacks",
         sample.physicsCollisionBroadphaseFallbacks},
        {"physics_collision_ready_wait_ms", sample.physicsCollisionReadyWaitMs},
        {"physics_collision_ready_transitions",
         sample.physicsCollisionReadyTransitions},
        {"physics_visual_remesh_backlog", sample.physicsVisualRemeshBacklog},
        {"physics_collision_rebuild_backlog",
         sample.physicsCollisionRebuildBacklog},
        {"pending_player_relights", sample.pendingPlayerRelights},
        {"pending_bg_relights", sample.pendingBgRelights},
        {"async_relight_inflight", sample.asyncRelightInflight},
        {"relight_discarded_late", sample.relightDiscardedLate},
        {"mesh_discarded_late", sample.meshDiscardedLate},
        {"relight_completed_per_sec", sample.relightCompletedPerSec},
    });
  }
  root["samples"] = json(samples);
  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << root.dump(2);
  }
}

void UMovementDiagnosticsRecorder::Update(
    UWorld &world, const std::shared_ptr<UCamera> &camera, float prev_player_y)
{
  world.MovementDiag = UWorld::MovementDiagnostics{};
  if (!camera)
  {
    return;
  }

  const glm::vec3 playerPos = camera->GetPosition();
  const PlayerCapsule cap = camera->GetPlayerCapsule();
  world.MovementDiag.feetBlock = WorldPosToBlock(
      glm::vec3(playerPos.x, cap.feetY(playerPos) + 0.01f, playerPos.z));
  world.MovementDiag.feetChunk =
      UChunkManager::WorldToChunk(world.MovementDiag.feetBlock);
  const glm::ivec3 feetGround(world.MovementDiag.feetChunk.x, 0,
                              world.MovementDiag.feetChunk.z);
  world.MovementDiag.feetChunkLoaded =
      world.BlockWorld.GetChunkManager().HasChunk(feetGround);
  world.MovementDiag.feetIsAir =
      world.BlockWorld.IsAir(world.MovementDiag.feetBlock);
  world.MovementDiag.meshDrawCount = world.GetRenderInstanceCount();
  world.MovementDiag.deltaTime = camera->GetDeltaTime();
  world.MovementDiag.streamingGenMs = world.Streaming->GetFrameStreamingGenMs();
  world.MovementDiag.streamingIoMs = world.Streaming->GetFrameStreamingIoMs();
  world.MovementDiag.dirtyChunksPending =
      static_cast<int>(world.MeshService->GetDirtyCount());
  world.MovementDiag.flatRebuildMs = world.MeshService->GetLastFlatRebuildMs();
  world.MovementDiag.asyncMeshInFlight =
      world.MeshService->GetAsyncInFlightCount();
  if (world.Streaming)
  {
    if (const UChunkLoadScheduler *sched = world.Streaming->GetChunkScheduler())
    {
      world.MovementDiag.genQueuePending = sched->GetPendingQueueCount();
      world.MovementDiag.genInFlight = sched->GetGenInFlightCount();
      world.MovementDiag.genBacklogTotal = sched->GetGenBacklogTotal();
    }
  }
  {
    const ChunkPopulateTiming populate = ChunkPopulateDiagnostics::GetLast();
    world.MovementDiag.populateMsLast = populate.totalMs;
    world.MovementDiag.populateSampleMs = populate.sampleMs;
    world.MovementDiag.populateTerrainMs = populate.terrainMs;
    world.MovementDiag.populateCarveMs = populate.carveMs;
    world.MovementDiag.populatePostMs = populate.postMs;
    constexpr double kEmaAlpha = 0.15;
    const double prev_ema =
        world.MovementDiagHistory.empty()
            ? 0.0
            : world.MovementDiagHistory.back().populateMsEma;
    if (prev_ema <= 0.0)
    {
      world.MovementDiag.populateMsEma = populate.totalMs;
    }
    else if (populate.totalMs > 0.0)
    {
      world.MovementDiag.populateMsEma =
          kEmaAlpha * populate.totalMs + (1.0 - kEmaAlpha) * prev_ema;
    }
    else
    {
      world.MovementDiag.populateMsEma = prev_ema;
    }
  }
  world.MovementDiag.asyncMeshingEnabled = world.Render.AsyncMeshing;
  world.MovementDiag.greedyCacheEntries =
      static_cast<int>(world.MeshService->GetGreedyCacheSize());
  world.MovementDiag.framesSinceLoad = world.FramesSinceLoad;
  world.MovementDiag.meshBacklogCleared = world.MeshBacklogClearedLatch;
  const PhysicsTelemetry &physicsTelemetry = world.GetPhysicsTelemetry();
  world.MovementDiag.physicsStepMs = physicsTelemetry.PhysicsStepMs;
  world.MovementDiag.physicsMovementMs = physicsTelemetry.MovementStepMs;
  world.MovementDiag.physicsBlockMs = physicsTelemetry.BlockStepMs;
  world.MovementDiag.physicsDrainMs = physicsTelemetry.DrainStepMs;
  world.MovementDiag.wallFrameMs = world.WallFrameDeltaSec * 1000.0;
  world.MovementDiag.swapWaitMs = world.LastSwapWaitMs;
  world.MovementDiag.simMs =
      world.MovementDiag.physicsStepMs +
      (world.GetDurationViewUpdateMks() / 1000.0) +
      (world.GetDurationDrawSceneMks() / 1000.0);
  world.MovementDiag.unaccountedMs = world.MovementDiag.wallFrameMs -
                                     world.MovementDiag.simMs -
                                     world.MovementDiag.swapWaitMs;
  world.MovementDiag.physicsSimulationSteps =
      physicsTelemetry.SimulationStepsThisFrame;
  world.MovementDiag.physicsBlockQueueDepth = physicsTelemetry.BlockQueueDepth;
  world.MovementDiag.physicsLiquidQueueDepth =
      physicsTelemetry.LiquidQueueDepth;
  world.MovementDiag.physicsDeferredUpdates = physicsTelemetry.DeferredUpdates;
  world.MovementDiag.physicsDroppedUpdates = physicsTelemetry.DroppedUpdates;
  world.MovementDiag.physicsPurgedUpdates = physicsTelemetry.PurgedUpdates;
  world.MovementDiag.physicsCollisionBroadphaseRejects =
      physicsTelemetry.CollisionBroadphaseRejects;
  world.MovementDiag.physicsCollisionBroadphaseFallbacks =
      physicsTelemetry.CollisionBroadphaseFallbacks;
  world.MovementDiag.physicsCollisionReadyWaitMs =
      physicsTelemetry.CollisionReadyWaitMs;
  world.MovementDiag.physicsCollisionReadyTransitions =
      physicsTelemetry.CollisionReadyTransitions;
  world.MovementDiag.physicsVisualRemeshBacklog =
      physicsTelemetry.VisualRemeshBacklog;
  world.MovementDiag.physicsCollisionRebuildBacklog =
      physicsTelemetry.CollisionRebuildBacklog;
  world.MovementDiag.pendingPlayerRelights =
      static_cast<int>(physicsTelemetry.PendingPlayerRelights);
  world.MovementDiag.pendingBgRelights =
      static_cast<int>(physicsTelemetry.PendingBackgroundRelights);
  world.MovementDiag.asyncRelightInflight =
      static_cast<int>(physicsTelemetry.AsyncRelightInflight);
  world.MovementDiag.relightDiscardedLate =
      physicsTelemetry.RelightDiscardedLate;
  world.MovementDiag.meshDiscardedLate = physicsTelemetry.MeshDiscardedLate;
  world.MovementDiag.relightCompletedPerSec =
      physicsTelemetry.RelightCompletedPerSec;
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

  const double sim_ms =
      (world.DurationDoMovementMks +
       (world.GetViewEngine() ? world.GetViewEngine()->GetDurationUpdateMks()
                              : 0.0)) /
      1000.0;
  const double wall_ms =
      world.WallFrameDeltaSec > 0.0 ? world.WallFrameDeltaSec * 1000.0 : sim_ms;
  const double frameMs = std::max(sim_ms, wall_ms);
  world.MovementDiag.hitchDetected = frameMs > 50.0 ||
                                     world.MovementDiag.physicsStepMs > 50.0 ||
                                     world.MovementDiag.deltaTime > 0.1f;
  world.MovementDiag.fallThroughSuspected =
      world.MovementDiag.playerYDrop > 2.0f &&
      (world.MovementDiag.feetIsAir || !world.MovementDiag.feetChunkLoaded) &&
      world.MovementDiag.meshDrawCount > 0;

#ifdef CUBATARIUM_DEBUG
  if (world.MovementDiag.hitchDetected ||
      world.MovementDiag.fallThroughSuspected ||
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
    world.MovementDiagHistory.erase(world.MovementDiagHistory.begin(),
                                    world.MovementDiagHistory.begin() +
                                        static_cast<std::ptrdiff_t>(trim));
  }
}

} // namespace cutum
