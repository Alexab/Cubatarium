#ifndef IUMOVEMENTPHYSICSSERVICE_H
#define IUMOVEMENTPHYSICSSERVICE_H

namespace cutum
{

class UWorld;

class IUMovementPhysicsService
{
public:
  virtual ~IUMovementPhysicsService() = default;
  virtual void TickMovement(UWorld &world) = 0;
};

} // namespace cutum

#endif // IUMOVEMENTPHYSICSSERVICE_H
