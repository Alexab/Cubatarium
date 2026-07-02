#include "World/Physics/Replay/WorldStateHasher.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

uint64_t UWorldStateHasher::HashBytes(const void *data, size_t size)
{
  const auto *bytes = static_cast<const uint8_t *>(data);
  uint64_t hash = 14695981039346656037ULL;
  for (size_t i = 0; i < size; ++i)
  {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint64_t UWorldStateHasher::HashCombine(uint64_t seed, uint64_t value)
{
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
  return seed;
}

uint64_t UWorldStateHasher::HashBlockWorldRegion(const UBlockWorld &world,
                                                 glm::ivec3 min_pos,
                                                 glm::ivec3 max_pos)
{
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (int y = min_pos.y; y <= max_pos.y; ++y)
  {
    for (int z = min_pos.z; z <= max_pos.z; ++z)
    {
      for (int x = min_pos.x; x <= max_pos.x; ++x)
      {
        const BlockId id = world.GetBlock(glm::ivec3(x, y, z));
        hash = HashCombine(hash, static_cast<uint64_t>(id));
      }
    }
  }
  return hash;
}

uint64_t UWorldStateHasher::HashPhysicsReplayState(const PhysicsReplayState &state)
{
  uint64_t hash = HashCombine(0, state.Tick);
  hash = HashCombine(hash, state.BlockQueueStats.Enqueued);
  hash = HashCombine(hash, state.BlockQueueStats.Processed);
  hash = HashCombine(hash, state.BlockQueueStats.Deferred);
  hash = HashCombine(hash, state.BlockQueueStats.Dropped);
  hash = HashCombine(hash, state.BlockQueueStats.Depth);
  hash = HashCombine(hash, state.LiquidQueueStats.Enqueued);
  hash = HashCombine(hash, state.LiquidQueueStats.Processed);
  hash = HashCombine(hash, state.LiquidQueueStats.Deferred);
  hash = HashCombine(hash, state.LiquidQueueStats.Dropped);
  hash = HashCombine(hash, state.LiquidQueueStats.Depth);
  hash = HashCombine(hash, state.VisualQueueStats.Depth);
  hash = HashCombine(hash, state.CollisionQueueStats.Depth);
  return hash;
}

} // namespace cutum
