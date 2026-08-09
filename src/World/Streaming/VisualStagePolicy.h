#pragma once

namespace cutum
{

/// Era28 I-V1: near FOV radius for hide-until-lit (underfeet + rim ≤2).
constexpr int kVisualStageNearFovHoriz = 2;

/// Era28 I-V1: UnlitFirstMesh allowed only outside near FOV (hide-until-lit near).
/// has_mesh or !in_focus → never; near horiz ≤ near_r → false; far → true.
inline bool ShouldAllowUnlitFirstMeshNearFov(bool has_mesh, bool in_focus,
                                             int horiz, int near_r = 2)
{
  if (has_mesh || !in_focus)
  {
    return false;
  }
  if (horiz <= near_r)
  {
    return false;
  }
  return true;
}

/// Era28 I-V2: SoftDefer empty MarkDirty only when ownership path is dead —
/// FirstMesh ticket / Inflight / PendingGpu must not Dirty-storm every scan.
inline bool SoftDeferEmptyShouldMarkDirty(bool empty_or_held, bool has_fm_ticket,
                                          bool inflight_or_pending_gpu)
{
  if (!empty_or_held)
  {
    return false;
  }
  if (has_fm_ticket || inflight_or_pending_gpu)
  {
    return false;
  }
  return true;
}

/// Era28 I-V1/V3: near FOV draw publish — LitDrawable or keep-prior GPU only
/// (no Unlit preview into MDI).
inline bool ShouldPublishMeshToDraw(bool lit_drawable, bool keep_prior_gpu_live,
                                    bool unlit_preview)
{
  if (lit_drawable || keep_prior_gpu_live)
  {
    return true;
  }
  (void)unlit_preview;
  return false;
}

/// Era28 I-V4: near void/VB needs Relight before first draw (not Unlit preview).
inline bool ShouldRelightBeforeDrawNear(bool near_fov, bool void_or_vb,
                                        bool has_lit_drawable)
{
  if (has_lit_drawable)
  {
    return false;
  }
  return near_fov && void_or_vb;
}

/// Era28 I-V3: while Building (FM/Inflight/Pending), remesh via RemeshAfterApply
/// only — no Dirty bump that restarts Inflight.
inline bool ShouldRemeshAfterApplyOnlyWhileBuilding(bool building_owned,
                                                    bool has_drawable_lit)
{
  if (has_drawable_lit)
  {
    return false;
  }
  return building_owned;
}

} // namespace cutum
