#include "World/Physics/WorldBlockPhysicsService.h"
#include "World/Core/World.h"
#include <algorithm>

namespace cutum
{

void UWorldBlockPhysicsService::SetBudgets(const PhysicsBudgets &budgets)
{
  Budgets = budgets;
  BlockQueue.SetBudgets(budgets);
  LiquidQueue.SetBudgets(budgets);
}

void UWorldBlockPhysicsService::SetFeatureFlags(const PhysicsFeatureFlags &flags)
{
  Flags = flags;
  FallingSystem.ShadowMode = !flags.EnableFalling || flags.FallingShadowMode;
  LiquidSystem.ShadowMode = !flags.EnableFluids || flags.LiquidShadowMode;
}

bool UWorldBlockPhysicsService::ShouldCheckFalling(const BlockUpdateEvent &event)
{
  switch (event.Type)
  {
  case BlockUpdateEventType::SupportLost:
  case BlockUpdateEventType::NeighborChanged:
    return true;
  case BlockUpdateEventType::BlockChanged:
    return true;
  case BlockUpdateEventType::LiquidCheck:
    return false;
  }
  return false;
}

void UWorldBlockPhysicsService::PublishBlockChanged(glm::ivec3 blockPos,
                                                    glm::ivec3 chunkCoord,
                                                    uint64_t triggerTick,
                                                    uint64_t localOrder)
{
  if (!Flags.EnableBlockEvents)
  {
    return;
  }
  BlockUpdateEvent ev;
  ev.Type = BlockUpdateEventType::BlockChanged;
  ev.Priority = BlockUpdatePriority::Normal;
  ev.BlockPos = blockPos;
  ev.ChunkCoord = chunkCoord;
  ev.TriggerTick = triggerTick;
  ev.LocalOrder = localOrder;
  BlockQueue.Enqueue(ev);
}

void UWorldBlockPhysicsService::PublishNeighborChanged(glm::ivec3 blockPos,
                                                       glm::ivec3 chunkCoord,
                                                       uint64_t triggerTick,
                                                       uint64_t localOrder)
{
  if (!Flags.EnableBlockEvents)
  {
    return;
  }
  BlockUpdateEvent ev;
  ev.Type = BlockUpdateEventType::NeighborChanged;
  ev.Priority = BlockUpdatePriority::High;
  ev.BlockPos = blockPos;
  ev.ChunkCoord = chunkCoord;
  ev.TriggerTick = triggerTick;
  ev.LocalOrder = localOrder;
  BlockQueue.Enqueue(ev);
}

void UWorldBlockPhysicsService::PublishSupportLost(glm::ivec3 blockPos,
                                                   glm::ivec3 chunkCoord,
                                                   uint64_t triggerTick,
                                                   uint64_t localOrder)
{
  if (!Flags.EnableBlockEvents)
  {
    return;
  }
  BlockUpdateEvent ev;
  ev.Type = BlockUpdateEventType::SupportLost;
  ev.Priority = BlockUpdatePriority::Critical;
  ev.BlockPos = blockPos;
  ev.ChunkCoord = chunkCoord;
  ev.TriggerTick = triggerTick;
  ev.LocalOrder = localOrder;
  BlockQueue.Enqueue(ev);
}

void UWorldBlockPhysicsService::PublishLiquid(glm::ivec3 blockPos)
{
  if (!Flags.EnableFluids)
  {
    return;
  }
  LiquidQueue.Enqueue(blockPos);
}

void UWorldBlockPhysicsService::TickBlockPhysics(UWorld &world)
{
  int fallingBudget = std::max(0, Budgets.FallingEventsPerTickMax);
  const std::vector<BlockUpdateEvent> events = BlockQueue.PopBudgeted();
  for (const BlockUpdateEvent &event : events)
  {
    if (!Flags.EnableFalling || fallingBudget <= 0 || !ShouldCheckFalling(event))
    {
      continue;
    }
    const FallingBlocksStats stats = FallingSystem.Tick(world, event);
    world.AccumulateFallingStats(stats);
    if (stats.Applied > 0)
    {
      --fallingBudget;
      const glm::ivec3 below(event.BlockPos.x, event.BlockPos.y - 1,
                             event.BlockPos.z);
      world.MarkBlockChunkDirtyFromPhysics(event.BlockPos);
      world.MarkBlockChunkDirtyFromPhysics(below);
      world.PublishBlockPhysicsEvent(below);
      world.PublishNeighborPhysicsEvents(below);
    }
  }

  const std::vector<glm::ivec3> liquidEvents = LiquidQueue.PopBudgeted();
  for (glm::ivec3 pos : liquidEvents)
  {
    if (Flags.EnableFluids)
    {
      const LiquidSimulationStats stats = LiquidSystem.Tick(world, pos);
      world.AccumulateLiquidStats(stats);
      if (stats.Applied > 0 && stats.HasAppliedDest)
      {
        const glm::ivec3 dest = stats.AppliedDest;
        world.MarkBlockChunkDirtyFromPhysics(pos);
        world.MarkBlockChunkDirtyFromPhysics(dest);
        if (stats.SourceCleared)
        {
          world.PublishBlockPhysicsEvent(dest);
          world.PublishNeighborPhysicsEvents(dest);
          if (world.GetBlockWorld().IsAir(pos))
          {
            world.PublishNeighborPhysicsEvents(pos);
          }
        }
        else
        {
          PublishLiquid(dest);
        }
      }
    }
  }

  world.UpdatePhysicsQueueStats(BlockQueue.GetStats(), LiquidQueue.GetStats());
}

} // namespace cutum
