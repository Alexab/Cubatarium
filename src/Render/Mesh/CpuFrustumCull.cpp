#include "Render/Mesh/CpuFrustumCull.h"
#include "Render/Mesh/ChunkMeshCache.h"

namespace cutum
{

void UCpuFrustumCull::RebuildVisible(UChunkMeshCache &cache,
                                     const Frustum *frustum,
                                     const glm::vec3 *camera_pos,
                                     float max_cull_distance)
{
  cache.RebuildGreedyVisibleForCull(frustum, camera_pos, max_cull_distance);
}

} // namespace cutum
