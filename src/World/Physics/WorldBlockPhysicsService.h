#ifndef WORLDBLOCKPHYSICSSERVICE_H
#define WORLDBLOCKPHYSICSSERVICE_H

#include "World/Physics/IUBlockPhysicsService.h"
#include "World/Physics/BlockUpdateQueue.h"
#include "World/Physics/FallingBlocksSystem.h"
#include "World/Physics/LiquidSimulationSystem.h"
#include "World/Physics/LiquidUpdateQueue.h"
#include "World/Physics/PhysicsProfile.h"

namespace cutum
{

class UWorldBlockPhysicsService : public IUBlockPhysicsService
{
public:
  void SetBudgets(const PhysicsBudgets &budgets);
  void SetFeatureFlags(const PhysicsFeatureFlags &flags);

  void PublishBlockChanged(glm::ivec3 blockPos, glm::ivec3 chunkCoord,
                           uint64_t triggerTick, uint64_t localOrder);
  void PublishNeighborChanged(glm::ivec3 blockPos, glm::ivec3 chunkCoord,
                              uint64_t triggerTick, uint64_t localOrder);
  void PublishSupportLost(glm::ivec3 blockPos, glm::ivec3 chunkCoord,
                          uint64_t triggerTick, uint64_t localOrder);
  void PublishLiquid(glm::ivec3 blockPos);

  void TickBlockPhysics(UWorld &world) override;
  const BlockUpdateQueueStats &GetBlockQueueStats() const
  {
    return BlockQueue.GetStats();
  }
  const LiquidUpdateQueueStats &GetLiquidQueueStats() const
  {
    return LiquidQueue.GetStats();
  }

private:
  static bool ShouldCheckFalling(const BlockUpdateEvent &event);

  PhysicsFeatureFlags Flags;
  PhysicsBudgets Budgets;
  UBlockUpdateQueue BlockQueue;
  ULiquidUpdateQueue LiquidQueue;
  UFallingBlocksSystem FallingSystem;
  ULiquidSimulationSystem LiquidSystem;
};

} // namespace cutum

#endif // WORLDBLOCKPHYSICSSERVICE_H
