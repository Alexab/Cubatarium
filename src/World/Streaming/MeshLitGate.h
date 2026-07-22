#pragma once

namespace cutum
{

/// Soft-defer / first-mesh gate (V2 RenderReady): no visible mesh while
/// PendingLight except underfeet (horiz <= 1).
inline bool SoftDeferMeshUntilLitPolicy(bool underfeet, bool has_mesh,
                                        bool pending_light, bool in_focus,
                                        bool may_mesh_outside_focus)
{
  if (underfeet)
  {
    return false;
  }
  if (has_mesh)
  {
    return pending_light;
  }
  if (in_focus)
  {
    // V2a: holes preferred over dark preview in focus ring.
    return pending_light;
  }
  return !may_mesh_outside_focus;
}

} // namespace cutum
