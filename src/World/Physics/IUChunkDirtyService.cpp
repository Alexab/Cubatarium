#include "World/Physics/IUChunkDirtyService.h"
#include "World/Core/World.h"

namespace cutum
{

void IUChunkDirtyService::MarkDirty(UWorld &world, glm::ivec3 blockPos)
{
  MarkVisualRemesh(world, blockPos);
  MarkCollisionRebuild(world, blockPos);
}

} // namespace cutum
