#ifndef REPLAYINPUTLOG_H
#define REPLAYINPUTLOG_H

#include "World/Physics/BlockUpdateEvent.h"
#include "World/Physics/BlockUpdateQueue.h"
#include "World/Physics/ChunkRebuildQueue.h"
#include "World/Physics/LiquidUpdateQueue.h"
#include "World/Physics/PhysicsProfile.h"
#include "World/Physics/Replay/WorldStateHasher.h"
#include <cstddef>
#include <vector>

namespace cutum
{

class UBlockWorld;

enum class ReplayActionType : uint8_t
{
  EnqueueBlockEvent,
  EnqueueLiquid,
  EnqueueVisualRemesh,
  EnqueueCollisionRebuild,
  TickQueues
};

struct ReplayAction
{
  ReplayActionType Type{ReplayActionType::TickQueues};
  BlockUpdateEvent BlockEvent;
  glm::ivec3 BlockPos{0};
  int ChunkPriority{0};
};

struct ReplayTickHash
{
  uint64_t Tick{0};
  uint64_t StateHash{0};
  uint64_t WorldHash{0};
};

class UReplayInputLog
{
public:
  void SetBudgets(const PhysicsBudgets &budgets);
  void Reset();

  void Enqueue(const ReplayAction &action);
  void LoadGoldenScenario();

  std::vector<ReplayTickHash> Run(const UBlockWorld *world, glm::ivec3 world_min,
                                  glm::ivec3 world_max);

  const UBlockUpdateQueue &GetBlockQueue() const { return BlockQueue; }
  const ULiquidUpdateQueue &GetLiquidQueue() const { return LiquidQueue; }

private:
  uint64_t TickQueuesAndHash(const UBlockWorld *world, glm::ivec3 world_min,
                             glm::ivec3 world_max);

  PhysicsBudgets Budgets;
  UBlockUpdateQueue BlockQueue;
  ULiquidUpdateQueue LiquidQueue;
  UChunkRebuildQueue VisualQueue;
  UChunkRebuildQueue CollisionQueue;
  std::vector<ReplayAction> Actions;
  uint64_t TickCounter{0};
  uint64_t LocalOrder{0};
};

} // namespace cutum

#endif // REPLAYINPUTLOG_H
