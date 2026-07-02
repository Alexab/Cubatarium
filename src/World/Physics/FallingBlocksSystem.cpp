#include "World/Physics/FallingBlocksSystem.h"
#include "World/Core/World.h"
#include "World/Physics/FallingBlockRules.h"

namespace cutum
{

FallingBlocksStats UFallingBlocksSystem::Tick(UWorld &world,
                                              const BlockUpdateEvent &event)
{
  FallingBlocksStats stats;
  ++stats.Candidates;
  if (ShadowMode)
  {
    ++stats.Deferred;
    return stats;
  }
  if (UFallingBlockRules::TryApplyFall(world.GetBlockRegistry(), world.GetBlockWorld(),
                                       event.BlockPos))
  {
    ++stats.Applied;
  }
  return stats;
}

} // namespace cutum
