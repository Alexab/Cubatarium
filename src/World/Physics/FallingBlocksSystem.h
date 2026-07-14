#ifndef FALLINGBLOCKSSYSTEM_H
#define FALLINGBLOCKSSYSTEM_H

#include "World/Physics/BlockUpdateEvent.h"
#include <cstdint>

namespace cutum
{

class UWorld;

struct FallingBlocksStats
{
  uint64_t Candidates{0};
  uint64_t Applied{0};
  uint64_t Deferred{0};
  uint64_t Dropped{0};
};

class UFallingBlocksSystem
{
public:
  bool ShadowMode{true};
  FallingBlocksStats Tick(UWorld &world, const BlockUpdateEvent &event);
};

} // namespace cutum

#endif // FALLINGBLOCKSSYSTEM_H
