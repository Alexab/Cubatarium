#ifndef REPLAYWORLDFIXTURE_H
#define REPLAYWORLDFIXTURE_H

#include "World/Core/BlockWorld.h"

namespace cutum
{

class UReplayWorldFixture
{
public:
  UBlockWorld &GetBlockWorld() { return BlockWorld; }
  const UBlockWorld &GetBlockWorld() const { return BlockWorld; }

private:
  UBlockWorld BlockWorld;
};

} // namespace cutum

#endif // REPLAYWORLDFIXTURE_H
