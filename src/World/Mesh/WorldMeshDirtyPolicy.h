#ifndef WORLDMESHDIRTYPOLICY_H
#define WORLDMESHDIRTYPOLICY_H

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockWorld;
class UWorld;
class UWorldChunkDirtyService;
class UWorldMeshService;

namespace WorldMeshDirtyPolicy
{

void MarkRuntimeOverlayMeshDirty(UWorld &world, UBlockWorld &block_world,
                                 UWorldMeshService &mesh_service,
                                 const std::vector<BlockId> &block_ids);

} // namespace WorldMeshDirtyPolicy

} // namespace cutum

#endif // WORLDMESHDIRTYPOLICY_H
