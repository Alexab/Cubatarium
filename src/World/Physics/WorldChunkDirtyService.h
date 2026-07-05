#ifndef WORLDCHUNKDIRTYSERVICE_H
#define WORLDCHUNKDIRTYSERVICE_H

#include "World/Physics/ChunkRebuildQueue.h"
#include "World/Physics/IUChunkDirtyService.h"
#include "World/Physics/PhysicsProfile.h"

namespace cutum
{

class UWorldChunkDirtyService : public IUChunkDirtyService
{
public:
  void SetBudgets(const PhysicsBudgets &budgets);

  void MarkVisualRemesh(UWorld &world, glm::ivec3 blockPos) override;
  void MarkCollisionRebuild(UWorld &world, glm::ivec3 blockPos) override;
  void DrainRebuildQueues(UWorld &world) override;
  void ClearPendingQueues();

private:
  void EnqueueAffectedChunks(UWorld &world, glm::ivec3 blockPos,
                             UChunkRebuildQueue &queue);

  PhysicsBudgets Budgets;
  UChunkRebuildQueue VisualQueue;
  UChunkRebuildQueue CollisionQueue;
  uint64_t EnqueueOrderCounter{0};
};

} // namespace cutum

#endif // WORLDCHUNKDIRTYSERVICE_H
