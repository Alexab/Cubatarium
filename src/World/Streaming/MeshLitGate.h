#pragma once

namespace cutum
{

/// Soft-defer / first-mesh gate (V2 RenderReady / Era28 Visual Stage).
/// Near FOV (AllowUnlitFirstMesh=false): hide-until-lit while PendingLight —
/// do not publish Unlit dark/bright preview. Far FOV may Unlit via allow flag.
/// Remesh while PendingLight always deferred. Player dig/place does not set
/// PendingLight.
inline bool SoftDeferMeshUntilLitPolicy(bool underfeet, bool has_mesh,
                                        bool pending_light, bool in_focus,
                                        bool may_mesh_outside_focus,
                                        bool allow_unlit_first_mesh = false)
{
  if (!has_mesh)
  {
    // Era28 I-V1: Unlit preview only when explicitly allowed (far rim).
    if (allow_unlit_first_mesh)
    {
      return false;
    }
    // Light debt → defer (Relight-before-draw); Unlit allow bypasses above.
    if (pending_light)
    {
      return true;
    }
    if (underfeet || in_focus)
    {
      return false; // lit gate open → schedule FirstMesh
    }
    return !may_mesh_outside_focus;
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
/// Cave / far UnlitFirstMesh first-mesh with light=0 is allowed (no lit predecessor).
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

/// Era32: after SoftDefer-reject of a dark remesh, do not MarkDirty when a
/// drawable already exists — MarkRelit owns the single requeue. Dirty-on-reject
/// stormed discarded_late/opaque_churn (manual 195432: 55 / 290) and starved
/// FirstMesh that fills empty ocean surface holes.
inline bool ShouldMarkDirtyAfterDarkSoftDeferReject(bool remesh_after_apply,
                                                    bool had_mesh)
{
  if (remesh_after_apply)
  {
    return true;
  }
  return !had_mesh;
}

} // namespace cutum
