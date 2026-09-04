#pragma once
// BUDGET_MS: 0.0  // perf-root P4: measure via Tracy; kill-switch required for new heuristics

namespace cutum
{

/// Era28 I-V1: near FOV radius for underfeet / enter pin / far-Unlit damp.
constexpr int kVisualStageNearFovHoriz = 2;

/// Era32 I-L1: LitDrawable publication ring (land+ocean). Unlit FirstMesh only
/// hinterland (horiz > ring); fully-dark drawable in ring is unfinished.
constexpr int kVisualStageLitDrawableHoriz = 4;

/// Ocean / cruise protect ring (FIFO trim + live-GPU keep). LitDrawable+4.
constexpr int kVisualStageProtectHoriz = kVisualStageLitDrawableHoriz + 4;

/// Era28 I-V1: UnlitFirstMesh allowed only outside near FOV (hide-until-lit near).
/// has_mesh or !in_focus → never; near horiz ≤ near_r → false; far → true.
/// Era32: pass kVisualStageLitDrawableHoriz as near_r for FOV publication.
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

/// Era28 I-V1/V3 / Era32 I-L1: FOV draw publish — LitDrawable or keep-prior GPU
/// only (no Unlit / fully-dark preview into MDI). Wired in SoftDefer prod path.
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

/// Era32 I-L1: hide fully-dark drawable in LitDrawable ring until lit binds.
/// Universal (land+ocean) — not gated on ocean_heal.
/// pending_replace_lit is ignored: PendingLight/PendingGpu keep-prior was
/// drawing Unlit black plugs for the whole Relight→Remesh window (manual
/// 183525 eye). Hole until lit bind > black surface in FOV.
inline bool ShouldHideFullyDarkUntilLitInRing(int horiz, bool fully_dark,
                                             bool pending_replace_lit,
                                             int ring = kVisualStageLitDrawableHoriz)
{
  (void)pending_replace_lit;
  if (horiz > ring || !fully_dark)
  {
    return false;
  }
  return true;
}

/// LitRing: FullyDark in LitDrawable/underfeet → hole until lit or true-dark.
/// Published Satisfying dark plugs are hidden (stable hole > blank↔dark flicker).
inline bool ShouldHideUncomputedFullyDarkInRing(
    int horiz, bool fully_dark, bool pending_light, bool stale_field,
    int ring = kVisualStageLitDrawableHoriz, bool true_dark = false,
    bool has_lit_drawable = false, bool has_published_mesh = false)
{
  (void)has_published_mesh;
  (void)pending_light;
  (void)stale_field;
  if (horiz > ring || !fully_dark)
  {
    return false;
  }
  if (has_lit_drawable || true_dark)
  {
    return false;
  }
  return true;
}

/// FirstMesh Dirty prune must keep the LitDrawable ring (and not the nh≤2
/// shell). 183918 keep_h=2 dropped cruise-frontier FM → opaque 882→472.
inline int FirstMeshPruneKeepHoriz(int focus_radius,
                                   int lit_ring = kVisualStageLitDrawableHoriz)
{
  const int ring = lit_ring < 1 ? 1 : lit_ring;
  const int focus = focus_radius < 1 ? 1 : focus_radius;
  return ring < focus ? ring : focus;
}

/// Era28 I-V4: near void/VB needs Relight before first draw (not Unlit preview).
/// Era32: near_fov means inside LitDrawable ring.
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

/// FZ2.7-P3: coalesce in-flight remesh. Never skip FirstMesh holes. Near
/// FullyDark (nh≤2) may re-Dirty immediately.
inline bool ShouldSkipInFlightDirtyReschedule(bool inflight, bool fully_dark,
                                              int horiz,
                                              bool first_mesh_missing)
{
  if (!inflight)
  {
    return false;
  }
  if (first_mesh_missing)
  {
    return false;
  }
  if (fully_dark && horiz >= 0 && horiz <= kVisualStageNearFovHoriz)
  {
    return false;
  }
  return true;
}

/// Era32 P2: live drawable remesh → RemeshAfterApply only (land+ocean), not
/// MarkDirty storm. pressure = suppress seam / void|VB / idle drawable.
inline bool ShouldRemeshAfterApplyOnlyOnLiveDrawable(bool has_drawable,
                                                     bool suppress_or_pressure,
                                                     bool building_or_pending)
{
  if (!has_drawable)
  {
    return false;
  }
  return suppress_or_pressure || building_or_pending;
}

/// Era28 I-V2/P2: PreferKick SoftDefer empty only after age SLA (not every scan).
inline bool SoftDeferEmptyPreferKickAfterAgeOnly(bool age_sla,
                                                 bool soft_defer_empty,
                                                 bool missing_visible_mesh,
                                                 bool queued_or_kicked_stuck)
{
  if (!age_sla)
  {
    return false;
  }
  return soft_defer_empty && missing_visible_mesh && queued_or_kicked_stuck;
}

} // namespace cutum
