#ifndef IUPHYSICSSCHEDULER_H
#define IUPHYSICSSCHEDULER_H

namespace cutum
{

class UWorld;

class IUPhysicsScheduler
{
public:
  virtual ~IUPhysicsScheduler() = default;
  virtual void Tick(UWorld &world) = 0;
};

} // namespace cutum

#endif // IUPHYSICSSCHEDULER_H
