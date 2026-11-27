#include "Render/Mesh/GpuFrustumCull.h"

namespace cutum
{

void UGpuFrustumCull::RebuildVisible(UChunkMeshCache &cache,
                                     const Frustum *frustum,
                                     const glm::vec3 *camera_pos,
                                     float max_cull_distance)
{
  // The MDI store owns opaque GPU compaction. Transparent and cross refs
  // use the same CPU frustum rebuild in both rendering backends.
  Delegate.RebuildVisible(cache, frustum, camera_pos, max_cull_distance);
}

} // namespace cutum
