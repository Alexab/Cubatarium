#pragma once

namespace cutum
{

/// Soft-defer / first-mesh gate (V2 RenderReady).
/// While PendingLight: never mesh or remesh (holes > dark bake everywhere,
/// including outside focus). Player dig/place does not set PendingLight.
inline bool SoftDeferMeshUntilLitPolicy(bool underfeet, bool has_mesh,
                                        bool pending_light, bool in_focus,
                                        bool may_mesh_outside_focus)
{
  (void)has_mesh;
  if (pending_light)
  {
    return true;
  }
  if (underfeet || in_focus)
  {
    return false;
  }
  return !may_mesh_outside_focus;
}

/// Reject committing a mesh that has fully-dark faces when light is still
/// pending, or when it would replace an already-lit mesh (dig/async race).
/// Cave first-mesh with legitimate light=0 is allowed (no lit predecessor,
/// not deferred).
inline bool ShouldRejectDarkMeshCommit(bool new_has_dark_face,
                                       bool defer_until_lit,
                                       bool had_lit_mesh)
{
  if (!new_has_dark_face)
  {
    return false;
  }
  if (defer_until_lit)
  {
    return true;
  }
  return had_lit_mesh;
}

} // namespace cutum
