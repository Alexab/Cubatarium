#pragma once

#include "Render/Camera/CullInputKey.h"
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

/// Resident geometry table vs visible command list. Upload each visible ref that
/// is not yet resident; overlap ratio must not suppress missing-ref uploads.
struct GpuPassVisibleDelta
{
  size_t visible_refs{0};
  size_t resident_refs{0};
  size_t missing_refs{0};
};

inline bool GpuPassHasMissingVisibleRefs(const GpuPassVisibleDelta &delta)
{
  return delta.missing_refs > 0;
}

/// Full rebuild only when the GPU pass has no resident geometry yet.
inline bool GpuPassVisibleSetNeedsFullRebuild(const GpuPassVisibleDelta &delta)
{
  return delta.visible_refs > 0 && delta.resident_refs == 0;
}

/// Sync visible command list with resident table (missing refs and/or empty GPU).
inline bool GpuPassVisibleSetNeedsSync(const GpuPassVisibleDelta &delta)
{
  return GpuPassVisibleSetNeedsFullRebuild(delta) ||
         GpuPassHasMissingVisibleRefs(delta);
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

/// Phase 5.7R3: cull skip blocked only for underfeet/near miss (nh≤1), not rim.
inline bool OpaqueCullUnderfeetMissBlocks(bool focus_missing, int miss_horiz)
{
  return focus_missing && miss_horiz <= 1;
}

/// Phase 5.7R4 / 5.7R5: vb_edge = meaningful VB transition or underfeet
/// no-ticket storm — not rim plateau and not micro ΔVB flicker (170947).
inline bool OpaqueCullVbFocusDeltaIsEdge(int vb_focus_n, int vb_focus_prev,
                                         int min_abs_delta = 4)
{
  const int d = vb_focus_n > vb_focus_prev ? vb_focus_n - vb_focus_prev
                                           : vb_focus_prev - vb_focus_n;
  return d >= min_abs_delta;
}

inline bool OpaqueCullVbEdgeBlocks(int vb_focus_n, int vb_focus_prev,
                                   int vb_stalled_n, int vb_stalled_prev,
                                   int vb_no_ticket_n, int miss_horiz,
                                   bool prev_valid, int delta_streak = 0,
                                   int streak_need = 2, int min_abs_delta = 4)
{
  const bool focus_changed = prev_valid && vb_focus_n != vb_focus_prev;
  const bool vb_transition =
      !prev_valid ||
      OpaqueCullVbFocusDeltaIsEdge(vb_focus_n, vb_focus_prev, min_abs_delta) ||
      (focus_changed && delta_streak >= streak_need) ||
      (miss_horiz <= 1 && vb_stalled_n > 0 && vb_stalled_prev <= 0);
  if (vb_transition)
  {
    return true;
  }
  return vb_no_ticket_n >= 8 && miss_horiz <= 1;
}

/// Phase 5.7R3: ±2% opaque_cmd_on hysteresis (micro-churn must not kill reuse).
inline bool OpaqueCullCmdOnStable(uint64_t opaque_cmd_on,
                                  uint64_t opaque_cmd_on_prev,
                                  double frac = 0.02)
{
  if (opaque_cmd_on == opaque_cmd_on_prev)
  {
    return true;
  }
  const uint64_t base = opaque_cmd_on_prev > 0 ? opaque_cmd_on_prev : 1u;
  const uint64_t diff = opaque_cmd_on > opaque_cmd_on_prev
                            ? opaque_cmd_on - opaque_cmd_on_prev
                            : opaque_cmd_on_prev - opaque_cmd_on;
  return static_cast<double>(diff) / static_cast<double>(base) <= frac;
}

/// Phase 5.7.4 / 5.7R2 / 5.7R3: skip GPU compact cull on light cruise when
/// cull inputs are unchanged and no underfeet miss / VB edge.
/// miss_horiz default 0: unknown miss treated as underfeet (fail-closed).
inline bool ShouldSkipOpaqueCullLightCruise(
    bool draw_set_stable, const CullInputKey &cached,
    const CullInputKey &current, bool indirect_ready,
    bool reverse_compact_active, float movement_speed, bool focus_missing,
    bool vb_edge, int miss_horiz = 0)
{
  if (OpaqueCullUnderfeetMissBlocks(focus_missing, miss_horiz) || vb_edge ||
      !draw_set_stable || !CullInputKeyAllowsCacheReuse(cached, current) ||
      !indirect_ready || !reverse_compact_active)
  {
    return false;
  }
  // Phase 5.7R2: widen near-stand to light cruise ≤1.5.
  return movement_speed <= 1.5f;
}

/// Phase 5.7.4 / 5.7R3: standing stable skip refuses underfeet miss / VB edge.
inline bool ShouldSkipOpaqueCullStable(bool draw_set_stable,
                                       const CullInputKey &cached,
                                       const CullInputKey &current,
                                       bool indirect_ready,
                                       bool reverse_compact_active,
                                       bool focus_missing, bool vb_edge,
                                       int miss_horiz = 0)
{
  return draw_set_stable &&
         CullInputKeyAllowsCacheReuse(cached, current) && indirect_ready &&
         reverse_compact_active &&
         !OpaqueCullUnderfeetMissBlocks(focus_missing, miss_horiz) && !vb_edge;
}

/// Phase 5.7R2 / 5.7R3: CullInputKey-gated compact reuse on alternate frames.
/// Never skip under underfeet miss (nh≤1) / VB edge; rim miss (nh≥2) is OK.
inline bool ShouldReuseOpaqueCullCompact(
    bool draw_set_stable, const CullInputKey &cached,
    const CullInputKey &current, bool reverse_compact_active,
    bool focus_unchanged, bool focus_missing, bool vb_edge,
    uint64_t opaque_cmd_on, uint64_t opaque_cmd_on_prev, uint32_t frame_parity,
    int miss_horiz = 0)
{
  if (OpaqueCullUnderfeetMissBlocks(focus_missing, miss_horiz) || vb_edge)
  {
    return false;
  }
  if (!draw_set_stable || !CullInputKeyAllowsCacheReuse(cached, current) ||
      !reverse_compact_active)
  {
    return false;
  }
  if (!focus_unchanged)
  {
    return false;
  }
  if (!OpaqueCullCmdOnStable(opaque_cmd_on, opaque_cmd_on_prev))
  {
    return false;
  }
  return (frame_parity & 1u) == 0u;
}

/// Phase 5.7.4b / 5.7R / 5.7R2: half-rate API now forwards to compact reuse
/// when yaw/cmd args are omitted (legacy tests keep false without extras).
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
  // Legacy 7-arg form stays disabled; GeometryEngine calls
  // ShouldReuseOpaqueCullCompact directly with yaw/cmd gates.
  return false;
}

/// Phase 5.7.4: throttle fail-open CPU AABB — need N consecutive triggers.
inline bool ShouldThrottleFailOpenGpuCompact(int consecutive_fail_open,
                                             int threshold = 3)
{
  return consecutive_fail_open >= threshold;
}

/// Phase 5.7R7: fail-open AABB probe cadence. Always probe when compact is
/// inactive, fail-open streak is live, or caller forces (underfeet / VB edge).
/// Healthy cruise uses period 6 (was hard-coded 3) to cut CPU AABB wall.
inline bool ShouldProbeFailOpenAabb(int tick, bool gpu_compact_active,
                                    int consecutive_fail_open, int period = 6,
                                    bool force_probe = false)
{
  if (force_probe || !gpu_compact_active || consecutive_fail_open > 0)
  {
    return true;
  }
  if (period <= 1)
  {
    return true;
  }
  return (tick % period) == 0;
}

/// Sysreset v4 hitch C: defer full opaque GPU compact cull when frame leftover
/// is below cost-class and a prior compact mask can be reused. Never defer on
/// underfeet miss / VB edge (must refresh visibility).
inline bool ShouldDeferOpaqueCompactCullForDeadline(
    double remaining_ms, bool gpu_compact_active, bool focus_missing,
    bool vb_edge, int miss_horiz = 0, double cost_class_ms = 8.0)
{
  if (OpaqueCullUnderfeetMissBlocks(focus_missing, miss_horiz) || vb_edge)
  {
    return false;
  }
  if (!gpu_compact_active)
  {
    return false;
  }
  return remaining_ms < cost_class_ms;
}

/// Sysreset v4 hitch C: skip transparent full resort when mesh/ref set is
/// stable and the previous frame did not need a command reorder.
inline bool ShouldSkipTransparentFullResort(bool mesh_and_refs_stable,
                                            bool has_cached_sorted_refs,
                                            int prev_cmd_reorder_n)
{
  return mesh_and_refs_stable && has_cached_sorted_refs &&
         prev_cmd_reorder_n == 0;
}

} // namespace cutum
