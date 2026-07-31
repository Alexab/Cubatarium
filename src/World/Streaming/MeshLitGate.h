#pragma once

namespace cutum
{

/// Soft-defer / first-mesh gate (V2 RenderReady).
/// First-mesh in focus/underfeet is never deferred (UnlitFirstMesh SoT) —
/// SoftDefer only blocks remesh while PendingLight. land_fix miss=1 sticky
/// runs came from deferring first-mesh until Capture cleared the gate.
/// Player dig/place does not set PendingLight.
inline bool SoftDeferMeshUntilLitPolicy(bool underfeet, bool has_mesh,
                                        bool pending_light, bool in_focus,
                                        bool may_mesh_outside_focus,
                                        bool allow_unlit_first_mesh = false)
{
  // Missing mesh: always allow in focus / underfeet / explicit unlit allow.
  if (!has_mesh &&
      (underfeet || in_focus || allow_unlit_first_mesh))
  {
    return false;
  }
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
/// Cave / UnlitFirstMesh first-mesh with light=0 is allowed (no lit predecessor).
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
