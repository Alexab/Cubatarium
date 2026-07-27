#pragma once

namespace cutum
{

/// Soft-defer / first-mesh gate (V2 RenderReady): no visible mesh while
/// PendingLight except underfeet first-mesh (horiz <= 1).
/// Remesh of an *existing* mesh while streaming PendingLight is deferred
/// (including underfeet) so cold Clear→0 remesh does not overwrite a lit mesh.
/// Player dig/place does not set PendingLight — incremental edit relight runs
/// before Immediate, so SoftDefer is not an edit-lock.
inline bool SoftDeferMeshUntilLitPolicy(bool underfeet, bool has_mesh,
                                        bool pending_light, bool in_focus,
                                        bool may_mesh_outside_focus)
{
  if (has_mesh)
  {
    return pending_light;
  }
  if (underfeet)
  {
    return false;
  }
  if (in_focus)
  {
    // V2a: holes preferred over dark preview in focus ring.
    return pending_light;
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
