#ifndef IUREPLAYWORLD_H
#define IUREPLAYWORLD_H

#include <cstdint>

namespace cutum
{

class UBlockWorld;
class UBlockRegistry;

class IUReplayWorld
{
public:
  virtual ~IUReplayWorld() = default;
  virtual UBlockWorld &GetBlockWorld() = 0;
  virtual const UBlockRegistry &GetBlockRegistry() const = 0;
  virtual uint64_t GetPhysicsTickCounter() const = 0;
};

} // namespace cutum

#endif // IUREPLAYWORLD_H
