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

/// Phase 5.7.4: skip GPU compact cull on light cruise when camera stable and
/// no FocusMissing / VB edge (never skip under miss/holes).
inline bool ShouldSkipOpaqueCullLightCruise(bool draw_set_stable,
                                            bool rev_match, float cam_move2,
                                            float cam_eps2, bool indirect_ready,
                                            bool reverse_compact_active,
                                            float movement_speed,
                                            bool focus_missing,
                                            bool vb_edge)
{
  if (focus_missing || vb_edge || !draw_set_stable || !rev_match ||
      !indirect_ready || !reverse_compact_active)
  {
    return false;
  }
  if (cam_move2 > cam_eps2)
  {
    return false;
  }
  // Near-stand only; land/fly keep GPU compact every moving frame.
  return movement_speed <= 0.5f;
}

/// Phase 5.7.4: standing stable skip must also refuse under miss/VB edge.
inline bool ShouldSkipOpaqueCullStable(bool cull_stable, bool focus_missing,
                                       bool vb_edge)
{
  return cull_stable && !focus_missing && !vb_edge;
}

/// Phase 5.7.4b / 5.7R: half-rate cruise skip disabled (latent overdraw on
/// healthy focus). Keep API for tests; always returns false.
inline bool ShouldSkipOpaqueCullHalfRate(bool draw_set_stable, bool rev_match,
                                         bool reverse_compact_active,
                                         bool focus_unchanged,
                                         bool focus_missing, bool vb_edge,
                                         uint32_t frame_parity)
{
  (void)draw_set_stable;
  (void)rev_match;
  (void)reverse_compact_active;
  (void)focus_unchanged;
  (void)focus_missing;
  (void)vb_edge;
  (void)frame_parity;
  return false;
}

/// Phase 5.7.4: throttle fail-open CPU AABB — need N consecutive triggers.
inline bool ShouldThrottleFailOpenGpuCompact(int consecutive_fail_open,
                                             int threshold = 3)
{
  return consecutive_fail_open >= threshold;
}

} // namespace cutum
