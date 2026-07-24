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

} // namespace cutum
