#ifndef WORLDBLOCKPHYSICSSERVICE_H
#define WORLDBLOCKPHYSICSSERVICE_H

#include "World/Physics/BlockUpdateQueue.h"
#include "World/Physics/FallingBlocksSystem.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"
#include "World/Physics/IUBlockPhysicsService.h"
#include "World/Physics/MaterialReactionRules.h"
#include "World/Physics/PhysicsProfile.h"
#include "World/Chunks/ChunkManager.h"

#include <glm/glm.hpp>

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
  void PublishFluid(glm::ivec3 blockPos);
  void WakeNearbyFluids(const UBlockWorld &blockWorld,
                        const UBlockDefinitionStorage &definitions,
                        glm::ivec3 center, int radius_blocks);

  void TickBlockPhysics(UWorld &world) override;
  void ResetRuntimeState();
  void ClearFluidQueue();
  const BlockUpdateQueueStats &GetBlockQueueStats() const
  {
    return BlockQueue.GetStats();
  }
  const FluidUpdateSetStats &GetFluidQueueStats() const
  {
    return FluidQueue.GetStats();
  }

private:
  static bool ShouldCheckFalling(const BlockUpdateEvent &event);
  void ProcessFluidChange(UWorld &world, const FluidSpreadChange &change);

  PhysicsFeatureFlags Flags;
  PhysicsBudgets Budgets;
  UBlockUpdateQueue BlockQueue;
  UFluidUpdateSet FluidQueue;
  UFallingBlocksSystem FallingSystem;
  UFluidSpreadSystem FluidSystem;
  UMaterialReactionRules MaterialRules;
};

} // namespace cutum

#endif // WORLDBLOCKPHYSICSSERVICE_H
