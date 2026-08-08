#pragma once

namespace cutum
{

/// Era20: SoftDefer empty placeholder → FirstMesh ticket (pure policy).
/// HasGreedy && !Drawable && solid && !in-flight work.
inline bool IsSoftDeferEmptyPlaceholder(bool has_greedy, bool has_drawable,
                                        bool is_dirty, bool pending_gpu,
                                        bool inflight, bool any_solid)
{
  if (has_drawable || pending_gpu || inflight || is_dirty)
  {
    return false;
  }
  if (!has_greedy || !any_solid)
  {
    return false;
  }
  return true;
}

/// Stuck SoftDefer empty → ColumnFlow FirstMesh only while FOV miss
/// (idle PreferKick without miss caused remesh churn; MarkDirty still heals).
inline bool ShouldEnqueueSoftDeferEmptyFirstMesh(bool empty_placeholder,
                                                 int horiz,
                                                 bool missing_visible_mesh)
{
  (void)horiz;
  if (!empty_placeholder || !missing_visible_mesh)
  {
    return false;
  }
  return true;
}

/// Era20 I-M2: Imm/Force Dirty escape when miss and async dead (ignore wall).
inline bool ShouldColdAsyncImmEscape(bool missing_visible_mesh, int mesh_async)
{
  return missing_visible_mesh && mesh_async < 2;
}

} // namespace cutum
