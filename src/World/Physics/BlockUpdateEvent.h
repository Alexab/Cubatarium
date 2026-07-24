#ifndef BLOCKUPDATEEVENT_H
#define BLOCKUPDATEEVENT_H

#include "World/Physics/BlockUpdatePriority.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace cutum
{

enum class BlockUpdateEventType
{
  BlockChanged,
  NeighborChanged,
  SupportLost,
  LiquidCheck
};

struct BlockUpdateEvent
{
  BlockUpdateEventType Type{BlockUpdateEventType::BlockChanged};
  BlockUpdatePriority Priority{BlockUpdatePriority::Normal};
  glm::ivec3 BlockPos{0};
  glm::ivec3 ChunkCoord{0};
  uint64_t TriggerTick{0};
  uint64_t LocalOrder{0};
};

} // namespace cutum

#endif // BLOCKUPDATEEVENT_H
