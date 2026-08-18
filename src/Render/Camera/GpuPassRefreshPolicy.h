#pragma once

#include "Render/Camera/Frustum.h"

#include <cstddef>
#include <cstdint>

namespace cutum
{

inline bool BatchCullAabbDegenerate(const float *bmin, const float *bmax)
{
  return bmin[0] == bmax[0] && bmin[1] == bmax[1] && bmin[2] == bmax[2];
}

inline void FillChunkCullFields(glm::ivec3 chunk_coord, float *sphere,
                                float *aabb_min, float *aabb_max)
{
  const glm::vec3 bmin = ChunkAABBMin(chunk_coord);
  const glm::vec3 bmax = ChunkAABBMax(chunk_coord);
  const glm::vec3 center = (bmin + bmax) * 0.5f;
  const float radius = glm::length(bmax - center);
  sphere[0] = center.x;
  sphere[1] = center.y;
  sphere[2] = center.z;
  sphere[3] = radius > 0.0f ? radius : 0.5f;
  aabb_min[0] = bmin.x;
  aabb_min[1] = bmin.y;
  aabb_min[2] = bmin.z;
  aabb_max[0] = bmax.x;
  aabb_max[1] = bmax.y;
  aabb_max[2] = bmax.z;
}

/// GPU pass holds the last uploaded visible set. Teleport / first-paint after
/// warmup can keep mesh_revision while refs are disjoint — force rebuild.
inline bool GpuPassVisibleSetNeedsRebuild(size_t overlap, size_t visible_refs,
                                          size_t gpu_batches)
{
  if (visible_refs == 0)
  {
    return false;
  }
  if (gpu_batches == 0)
  {
    return true;
  }
  return overlap == 0 || overlap * 10 < visible_refs;
}

/// All eligible batches CPU-culled: AABB are wrong or camera jumped. Draw
/// rather than a blue screen (compact would write vis=0).
/// Fail-open only when AABB were never filled (warmup). Valid far AABB after
/// teleport must stay culled — drawing them inflates opaque_on (false green).
inline bool ShouldFailOpenGpuCompactCull(uint64_t aabb_on, uint64_t eligible,
                                         bool any_degenerate)
{
  return eligible > 0 && aabb_on == 0 && any_degenerate;
}

} // namespace cutum
