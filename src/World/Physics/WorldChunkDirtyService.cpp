#include "World/Physics/WorldChunkDirtyService.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <chrono>

namespace cutum
{

void UWorldChunkDirtyService::SetBudgets(const PhysicsBudgets &budgets)
{
  Budgets = budgets;
  VisualQueue.SetLimits(budgets.VisualRemeshPerTickMax,
                        budgets.VisualRemeshQueueSoftLimit,
                        budgets.VisualRemeshQueueHardLimit);
  VisualQueue.SetProtectLowPriorities(false);
  CollisionQueue.SetLimits(budgets.CollisionRebuildPerTickMax,
                           budgets.CollisionRebuildQueueSoftLimit,
                           budgets.CollisionRebuildQueueHardLimit);
  CollisionQueue.SetProtectLowPriorities(true);
}

void UWorldChunkDirtyService::ClearPendingQueues()
{
  VisualQueue.Clear();
  CollisionQueue.Clear();
}

void UWorldChunkDirtyService::EnqueueAffectedChunks(UWorld &world,
                                                    glm::ivec3 blockPos,
                                                    UChunkRebuildQueue &queue)
{
  const glm::ivec3 focus = world.MovementDiag.feetChunk;
  const glm::ivec3 center = UChunkManager::WorldToChunk(blockPos);
  const uint64_t order = ++EnqueueOrderCounter;

  const auto enqueue_coord = [&](glm::ivec3 chunk_coord)
  {
    const int priority = std::max(
        {std::abs(chunk_coord.x - focus.x), std::abs(chunk_coord.y - focus.y),
         std::abs(chunk_coord.z - focus.z)});
    queue.Enqueue(chunk_coord, priority, order);
  };

  enqueue_coord(center);
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    enqueue_coord(UChunkManager::WorldToChunk(blockPos + offset));
  }
}

void UWorldChunkDirtyService::MarkVisualRemesh(UWorld &world, glm::ivec3 blockPos)
{
  EnqueueAffectedChunks(world, blockPos, VisualQueue);
}

void UWorldChunkDirtyService::MarkCollisionRebuild(UWorld &world,
                                                    glm::ivec3 blockPos)
{
  const glm::ivec3 focus = world.MovementDiag.feetChunk;
  const glm::ivec3 center = UChunkManager::WorldToChunk(blockPos);
  const uint64_t order = ++EnqueueOrderCounter;

  const auto enqueue_coord = [&](glm::ivec3 chunk_coord)
  {
    world.Collision.InvalidateChunkMovementSolid(chunk_coord);
    const int priority = std::max(
        {std::abs(chunk_coord.x - focus.x), std::abs(chunk_coord.y - focus.y),
         std::abs(chunk_coord.z - focus.z)});
    CollisionQueue.Enqueue(chunk_coord, priority, order);
  };

  enqueue_coord(center);
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    enqueue_coord(UChunkManager::WorldToChunk(blockPos + offset));
  }
}

void UWorldChunkDirtyService::DrainRebuildQueues(UWorld &world)
{
  const glm::ivec3 focus = world.MovementDiag.feetChunk;
  VisualQueue.SetFocusChunk(focus);
  CollisionQueue.SetFocusChunk(focus);

  const std::vector<glm::ivec3> collisionChunks = CollisionQueue.PopBudgeted();
  for (glm::ivec3 chunk_coord : collisionChunks)
  {
    world.Collision.RebuildChunkMovementSolid(chunk_coord);
  }

  const std::vector<glm::ivec3> visualChunks = VisualQueue.PopBudgeted();
  const float movement_speed = world.GetLastMovementSpeed();
  const bool moving =
      movement_speed > world.GetProceduralSettings().MovementPrefetchThreshold;
  const size_t pending_dirty =
      world.MeshService ? world.MeshService->GetDirtyCount() : 0;
  const bool drain_time_cap = moving && pending_dirty > 200;
  const auto drain_t0 = std::chrono::high_resolution_clock::now();
  for (glm::ivec3 chunk_coord : visualChunks)
  {
    if (drain_time_cap)
    {
      const double drain_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - drain_t0)
              .count();
      if (drain_ms > 15.0)
      {
        break;
      }
    }
    world.ModifiedChunks.insert(chunk_coord);
    if (world.BlockRegistry)
    {
      world.MeshService->MarkDirty(chunk_coord);
    }
  }

  const ChunkRebuildQueueStats &visualStats = VisualQueue.GetStats();
  const ChunkRebuildQueueStats &collisionStats = CollisionQueue.GetStats();
  world.PhysicsTelemetryData.VisualRemeshBacklog = VisualQueue.Size();
  world.PhysicsTelemetryData.CollisionRebuildBacklog = CollisionQueue.Size();
  world.PhysicsTelemetryData.DeferredUpdates +=
      visualStats.Deferred + collisionStats.Deferred;
  world.PhysicsTelemetryData.DroppedUpdates +=
      visualStats.Dropped + collisionStats.Dropped;
  world.PhysicsTelemetryData.PurgedUpdates +=
      visualStats.Purged + collisionStats.Purged;
}

} // namespace cutum
