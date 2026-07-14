#ifndef IUBLOCKPHYSICSSERVICE_H
#define IUBLOCKPHYSICSSERVICE_H

namespace cutum
{

class UWorld;

class IUBlockPhysicsService
{
public:
  virtual ~IUBlockPhysicsService() = default;
  virtual void TickBlockPhysics(UWorld &world) = 0;
};

} // namespace cutum

#endif // IUBLOCKPHYSICSSERVICE_H
