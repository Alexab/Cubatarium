#ifndef WORLDMOVEMENTPHYSICSSERVICE_H
#define WORLDMOVEMENTPHYSICSSERVICE_H

#include "World/Physics/IUMovementPhysicsService.h"

namespace cutum
{

class UWorldMovementPhysicsService : public IUMovementPhysicsService
{
public:
  void TickMovement(UWorld &world) override;
};

} // namespace cutum

#endif // WORLDMOVEMENTPHYSICSSERVICE_H
