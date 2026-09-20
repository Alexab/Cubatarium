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
                                        bool allow_unlit_first_mesh = false,
                                        bool allow_unlit_hole_preview = false)
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
    // Era37 P0: controlled unlit hole preview in LitDrawable ring under debt.
    if (allow_unlit_hole_preview)
    {
      return false;
    }
    return true;
  }
  if (underfeet || in_focus)
  {
    return false;
  }
  return !may_mesh_outside_focus;
}

/// Prior-lit hold: never let an unlit/FullyDark candidate become sole image
/// when a lit CPU or live lit GPU predecessor exists (zero-in-frame / R05).
/// Sysreset I3t: after converge_deadline_frames expire hold → allow publish
/// or PublishedEmpty (no infinite prior_lit plateau).
inline constexpr int kPriorLitConvergeDeadlineFrames = 90;

inline bool ShouldExpirePriorLitHold(int hold_age_frames,
                                     int converge_deadline_frames =
                                         kPriorLitConvergeDeadlineFrames)
{
  return hold_age_frames >= converge_deadline_frames;
}

inline bool ShouldRetainPriorLitOverUnlitCandidate(bool had_lit_mesh,
                                                  bool had_live_lit_gpu,
                                                  bool candidate_dark_or_unlit,
                                                  int hold_age_frames = 0,
                                                  int converge_deadline_frames =
                                                      kPriorLitConvergeDeadlineFrames)
{
  if (!candidate_dark_or_unlit)
  {
    return false;
  }
  if (ShouldExpirePriorLitHold(hold_age_frames, converge_deadline_frames))
  {
    return false;
  }
  return had_lit_mesh || had_live_lit_gpu;
}

/// SoftDefer intentional empty must not erase / replace live lit GPU.
/// Sysreset: expire prior-lit empty-avoid after converge deadline.
inline bool ShouldAvoidEmptyPublishOverPriorLit(bool had_live_lit_gpu,
                                               bool had_lit_mesh,
                                               bool had_gpu_resident,
                                               int hold_age_frames = 0,
                                               int converge_deadline_frames =
                                                   kPriorLitConvergeDeadlineFrames)
{
  if (ShouldExpirePriorLitHold(hold_age_frames, converge_deadline_frames))
  {
    return false;
  }
  if (had_live_lit_gpu)
  {
    return true;
  }
  return had_gpu_resident && had_lit_mesh;
}

/// Reject committing a mesh that has fully-dark faces when light is still
/// pending, or when it would replace an already-lit mesh (dig/async race).
/// Also reject dark over a live lit GPU SSBO (PendingReplace / SoftDefer empty
/// with GpuResident lit — had_lit_mesh alone can miss that case).
/// Cave / far UnlitFirstMesh first-mesh with light=0 is allowed (no lit predecessor).
inline bool ShouldRejectDarkMeshCommit(bool new_has_dark_face,
                                       bool defer_until_lit,
                                       bool had_lit_mesh,
                                       bool had_live_lit_gpu = false,
                                       int hold_age_frames = 0)
{
  if (!new_has_dark_face)
  {
    return false;
  }
  if (defer_until_lit)
  {
    return true;
  }
  return ShouldRetainPriorLitOverUnlitCandidate(had_lit_mesh, had_live_lit_gpu,
                                               true, hold_age_frames);
}

/// After keeping lit SSBO under dark CPU replace: do not PreferKick a pending
/// dark/unknown GPU job over the live lit draw (ring thrash).
inline bool ShouldPreferKickPendingGpuAfterLitKeep(bool kept_lit_gpu,
                                                   bool new_mesh_fully_dark)
{
  if (!kept_lit_gpu)
  {
    return true;
  }
  return !new_mesh_fully_dark;
}

/// Era32: after SoftDefer-reject of a dark remesh, do not MarkDirty when a
/// drawable already exists — MarkRelit owns the single requeue (ColPipe P5).
inline bool ShouldMarkDirtyAfterDarkSoftDeferReject(bool remesh_after_apply,
                                                    bool had_mesh)
{
  if (had_mesh)
  {
    return false;
  }
  (void)remesh_after_apply;
  return true;
}

} // namespace cutum
