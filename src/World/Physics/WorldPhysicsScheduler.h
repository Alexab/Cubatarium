#ifndef WORLDPHYSICSSCHEDULER_H
#define WORLDPHYSICSSCHEDULER_H

#include "World/Physics/IUPhysicsScheduler.h"
#include <memory>

namespace cutum
{

class IUMovementPhysicsService;
class IUBlockPhysicsService;
class IUChunkDirtyService;

class UWorldPhysicsScheduler : public IUPhysicsScheduler
{
public:
  UWorldPhysicsScheduler(IUMovementPhysicsService *movementService,
                         IUBlockPhysicsService *blockService,
                         IUChunkDirtyService *chunkDirtyService);

  void Tick(UWorld &world) override;

private:
  IUMovementPhysicsService *MovementService{nullptr};
  IUBlockPhysicsService *BlockService{nullptr};
  IUChunkDirtyService *ChunkDirtyService{nullptr};
};

} // namespace cutum

#endif // WORLDPHYSICSSCHEDULER_H
