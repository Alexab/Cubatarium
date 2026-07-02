#include "World/Physics/Replay/ReplayInputLog.h"

namespace cutum
{

void UReplayInputLog::SetBudgets(const PhysicsBudgets &budgets)
{
  Budgets = budgets;
  BlockQueue.SetBudgets(budgets);
  FluidQueue.SetBudgets(budgets);
  VisualQueue.SetLimits(budgets.VisualRemeshPerTickMax,
                        budgets.VisualRemeshQueueSoftLimit,
                        budgets.VisualRemeshQueueHardLimit);
  CollisionQueue.SetLimits(budgets.CollisionRebuildPerTickMax,
                           budgets.CollisionRebuildQueueSoftLimit,
                           budgets.CollisionRebuildQueueHardLimit);
}

void UReplayInputLog::Reset()
{
  BlockQueue.Clear();
  FluidQueue.Clear();
  FluidQueue.SetBudgets(Budgets);
  VisualQueue.Clear();
  CollisionQueue.Clear();
  VisualQueue.SetLimits(Budgets.VisualRemeshPerTickMax,
                        Budgets.VisualRemeshQueueSoftLimit,
                        Budgets.VisualRemeshQueueHardLimit);
  CollisionQueue.SetLimits(Budgets.CollisionRebuildPerTickMax,
                           Budgets.CollisionRebuildQueueSoftLimit,
                           Budgets.CollisionRebuildQueueHardLimit);
  Actions.clear();
  TickCounter = 0;
  LocalOrder = 0;
}

void UReplayInputLog::Enqueue(const ReplayAction &action)
{
  Actions.push_back(action);
}

void UReplayInputLog::LoadGoldenScenario()
{
  Reset();
  PhysicsBudgets budgets;
  budgets.BlockEventsPerTickMax = 4;
  budgets.LiquidEventsPerTickMax = 4;
  budgets.BlockQueueSoftLimit = 8;
  budgets.BlockQueueHardLimit = 16;
  budgets.LiquidQueueSoftLimit = 6;
  budgets.LiquidQueueHardLimit = 12;
  SetBudgets(budgets);

  ReplayAction event;
  event.Type = ReplayActionType::EnqueueBlockEvent;
  event.BlockEvent.Type = BlockUpdateEventType::NeighborChanged;
  event.BlockEvent.Priority = BlockUpdatePriority::High;
  event.BlockEvent.BlockPos = glm::ivec3(1, 4, 1);
  event.BlockEvent.ChunkCoord = glm::ivec3(0, 0, 0);
  Enqueue(event);

  event.BlockEvent.Priority = BlockUpdatePriority::Critical;
  event.BlockEvent.Type = BlockUpdateEventType::SupportLost;
  event.BlockEvent.BlockPos = glm::ivec3(2, 5, 1);
  Enqueue(event);

  event.Type = ReplayActionType::EnqueueLiquid;
  event.BlockPos = glm::ivec3(0, 3, 0);
  Enqueue(event);

  event.BlockPos = glm::ivec3(1, 3, 0);
  Enqueue(event);

  ReplayAction tick;
  tick.Type = ReplayActionType::TickQueues;
  for (int i = 0; i < 6; ++i)
  {
    Enqueue(tick);
  }

  event.Type = ReplayActionType::EnqueueBlockEvent;
  event.BlockEvent.Type = BlockUpdateEventType::BlockChanged;
  event.BlockEvent.Priority = BlockUpdatePriority::Low;
  event.BlockEvent.BlockPos = glm::ivec3(3, 2, 3);
  Enqueue(event);

  for (int i = 0; i < 4; ++i)
  {
    Enqueue(tick);
  }

  event.Type = ReplayActionType::EnqueueVisualRemesh;
  event.BlockPos = glm::ivec3(4, 4, 4);
  event.ChunkPriority = 0;
  Enqueue(event);

  event.Type = ReplayActionType::EnqueueCollisionRebuild;
  event.BlockPos = glm::ivec3(4, 4, 4);
  Enqueue(event);

  for (int i = 0; i < 3; ++i)
  {
    Enqueue(tick);
  }
}

uint64_t UReplayInputLog::TickQueuesAndHash(const UBlockWorld *world,
                                            glm::ivec3 world_min,
                                            glm::ivec3 world_max)
{
  ++TickCounter;
  BlockQueue.PopBudgeted();
  FluidQueue.PopBudgeted();
  VisualQueue.PopBudgeted();
  CollisionQueue.PopBudgeted();

  PhysicsReplayState state;
  state.Tick = TickCounter;
  state.BlockQueueStats = BlockQueue.GetStats();
  state.FluidQueueStats = FluidQueue.GetStats();
  state.VisualQueueStats = VisualQueue.GetStats();
  state.CollisionQueueStats = CollisionQueue.GetStats();
  return UWorldStateHasher::HashPhysicsReplayState(state) ^
         (world != nullptr
              ? UWorldStateHasher::HashBlockWorldRegion(*world, world_min,
                                                        world_max)
              : 0ULL);
}

uint64_t UReplayInputLog::TickQueuesAndHash(IUReplayWorld *world,
                                            glm::ivec3 world_min,
                                            glm::ivec3 world_max)
{
  return TickQueuesAndHash(world != nullptr ? &world->GetBlockWorld() : nullptr,
                           world_min, world_max);
}

std::vector<ReplayTickHash> UReplayInputLog::Run(const UBlockWorld *world,
                                                 glm::ivec3 world_min,
                                                 glm::ivec3 world_max)
{
  std::vector<ReplayTickHash> hashes;
  for (const ReplayAction &action : Actions)
  {
    switch (action.Type)
    {
    case ReplayActionType::EnqueueBlockEvent:
    {
      BlockUpdateEvent event = action.BlockEvent;
      event.TriggerTick = TickCounter;
      event.LocalOrder = ++LocalOrder;
      BlockQueue.Enqueue(event);
      break;
    }
    case ReplayActionType::EnqueueLiquid:
      FluidQueue.Enqueue(action.BlockPos);
      break;
    case ReplayActionType::EnqueueVisualRemesh:
      VisualQueue.Enqueue(glm::ivec3(0, 0, 0), action.ChunkPriority, ++LocalOrder);
      VisualQueue.Enqueue(glm::ivec3(0, 0, 1), action.ChunkPriority + 1,
                         ++LocalOrder);
      break;
    case ReplayActionType::EnqueueCollisionRebuild:
      CollisionQueue.Enqueue(glm::ivec3(0, 0, 0), 0, ++LocalOrder);
      break;
    case ReplayActionType::TickQueues:
    {
      ReplayTickHash entry;
      entry.Tick = TickCounter + 1;
      entry.StateHash = TickQueuesAndHash(world, world_min, world_max);
      entry.WorldHash =
          world != nullptr
              ? UWorldStateHasher::HashBlockWorldRegion(*world, world_min,
                                                         world_max)
              : 0ULL;
      hashes.push_back(entry);
      break;
    }
    }
  }
  return hashes;
}

std::vector<ReplayTickHash> UReplayInputLog::Run(IUReplayWorld *world,
                                                 glm::ivec3 world_min,
                                                 glm::ivec3 world_max)
{
  return Run(world != nullptr ? &world->GetBlockWorld() : nullptr, world_min,
             world_max);
}

} // namespace cutum
