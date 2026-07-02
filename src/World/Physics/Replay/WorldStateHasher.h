#ifndef WORLDSTATEHASHER_H
#define WORLDSTATEHASHER_H

#include "World/Physics/BlockUpdateQueue.h"
#include "World/Physics/ChunkRebuildQueue.h"
#include "World/Physics/FluidUpdateSet.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

struct PhysicsReplayState
{
  uint64_t Tick{0};
  BlockUpdateQueueStats BlockQueueStats;
  FluidUpdateSetStats FluidQueueStats;
  ChunkRebuildQueueStats VisualQueueStats;
  ChunkRebuildQueueStats CollisionQueueStats;
};

class UWorldStateHasher
{
public:
  static uint64_t HashCombine(uint64_t seed, uint64_t value);
  static uint64_t HashBlockWorldRegion(const UBlockWorld &world, glm::ivec3 min_pos,
                                       glm::ivec3 max_pos);
  static uint64_t HashPhysicsReplayState(const PhysicsReplayState &state);
  static uint64_t HashBytes(const void *data, size_t size);
};

} // namespace cutum

#endif // WORLDSTATEHASHER_H
