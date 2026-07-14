#include "World/Mesh/WorldMeshDirtyPolicy.h"

#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Physics/WorldChunkDirtyService.h"

namespace cutum::WorldMeshDirtyPolicy
{

void MarkRuntimeOverlayMeshDirty(UWorld & /*world*/, UBlockWorld &block_world,
                                 UWorldMeshService &mesh_service,
                                 const std::vector<BlockId> &block_ids)
{
  if (block_ids.empty())
  {
    mesh_service.MarkAllDirtyFromWorld(block_world);
    return;
  }
  mesh_service.MarkChunksContainingBlockIds(block_world, block_ids);
}

} // namespace cutum::WorldMeshDirtyPolicy
