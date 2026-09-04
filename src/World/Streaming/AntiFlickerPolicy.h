#pragma once

namespace cutum
{

/// Era27 I-A1: SoftDefer Capture witness pin length (frames).
constexpr int kSoftDeferCaptureWitnessPinFrames = 8;

/// I13-A2: after pin column heals (drawable), wait before hopping witness —
/// instant retarget caused frontier flicker (manual 150840: retarget every frame).
constexpr int kIngressCaptureRetargetCooldownFrames = 4;
/// I14b-C: minimum capture pin age on nh<=4 cruise (except visual_holes).
constexpr int kIngressCaptureWitnessPinMinAgeFrames = 48;
/// R4.6.2: hard expire — SLA > sticky pinned_still (shared with SoftDefer retarget).
constexpr int kIngressCaptureHardExpireFrames = 48;

/// I13-A1: allow witness hop only toward focus (smaller horiz), not lateral/back.
inline bool ShouldAllowFrontierWitnessAdvance(int cand_horiz, int pin_horiz)
{
  return cand_horiz >= 0 && pin_horiz >= 0 && cand_horiz < pin_horiz;
}

/// Era27 I-A1: retarget Capture witness only when pin invalid/expired, or
/// pinned column no longer SoftDefer-empty/miss. CheapRemesh C4: sticky —
/// better_horiz alone must not hop while pin age < SLA (hold_nh2 still wins
/// via ShouldRetargetRelightWitness).
/// I13-A2: healed pin requires cooldown unless visual_holes.
inline bool ShouldRetargetSoftDeferCaptureWitness(
    bool pin_valid, int pin_age_frames, int pin_T,
    bool new_witness_better_horiz, bool pinned_still_empty_or_miss,
    bool visual_holes = false,
    int healed_pin_cooldown_frames = kIngressCaptureRetargetCooldownFrames,
    int cand_horiz = -1, int pin_horiz = -1)
{
  (void)new_witness_better_horiz;
  if (!pin_valid)
  {
    return true;
  }
  if (pin_age_frames >= kIngressCaptureHardExpireFrames)
  {
    return true;
  }
  if (pin_age_frames >= pin_T)
  {
    return true;
  }
  if (!pinned_still_empty_or_miss)
  {
    if (visual_holes)
    {
      return true;
    }
    if (ShouldAllowFrontierWitnessAdvance(cand_horiz, pin_horiz))
    {
      return true;
    }
    return pin_age_frames >= healed_pin_cooldown_frames;
  }
  return false;
}

/// I14b-C: damp better_horiz retarget while drawable GPU apply in flight — not block.
inline bool ShouldDampWitnessRetargetOnIngressDrawable(
    bool pin_has_pending_or_inflight_gpu, bool pin_has_drawable, bool visual_holes,
    int pin_horiz)
{
  if (visual_holes || !pin_has_pending_or_inflight_gpu || !pin_has_drawable)
  {
    return false;
  }
  return pin_horiz >= 0 && pin_horiz <= 4;
}

/// I14b-C: block witness column swap until min pin SLA on ingress cruise.
inline bool ShouldBlockWitnessRetargetForPinSla(int pin_age_frames, int pin_horiz,
                                              bool visual_holes, bool moving)
{
  if (visual_holes || !moving || pin_horiz < 0 || pin_horiz > 4)
  {
    return false;
  }
  return pin_age_frames < kIngressCaptureWitnessPinMinAgeFrames;
}

/// I14b-D: damp seam remesh on cruise ingress (block-level opaque churn).
inline bool ShouldDampCruiseIngressSeamRemesh(bool moving, bool visual_holes,
                                              int miss_horiz)
{
  return moving && !visual_holes && miss_horiz >= 3;
}

/// I13-A3: hold witness pin while GPU mesh apply is in flight at frontier ingress.
/// I14b-C: drawable pins use damp path, not block (undrawn pins only).
/// I13-A1: bypass when FM schedule starved or witness advances toward focus.
inline bool ShouldBlockCaptureRetargetForIngressGpuPending(
    bool pin_has_pending_or_inflight_gpu, int pin_horiz, bool visual_holes,
    int cand_horiz = -1, bool fm_schedule_starved = false,
    bool pin_has_drawable = false)
{
  if (visual_holes || fm_schedule_starved)
  {
    return false;
  }
  if (!pin_has_pending_or_inflight_gpu)
  {
    return false;
  }
  if (pin_has_drawable)
  {
    return false;
  }
  if (ShouldAllowFrontierWitnessAdvance(cand_horiz, pin_horiz))
  {
    return false;
  }
  return pin_horiz >= 0 && pin_horiz <= 4;
}

/// FZ2.7-P15c: Capture pin MaxAge after retarget — never ratchet on prev.
/// Frontier/stuck may raise to pin_T (hard cap 24); otherwise decay to default 8.
inline int SoftDeferCapturePinMaxAgeAfterRetarget(
    int /*prev_max_age*/, int pin_T, bool land_frontier_or_stuck,
    int default_pin_frames = kSoftDeferCaptureWitnessPinFrames)
{
  if (!land_frontier_or_stuck)
  {
    return default_pin_frames;
  }
  const int raised = pin_T > default_pin_frames ? pin_T : default_pin_frames;
  return raised > 24 ? 24 : raised;
}

/// Era27 I-A2: SoftDefer empty age resets only when healed or real progress —
/// capped rim scan must not erase sticky ages for skipped empties.
inline bool SoftDeferEmptyAgeShouldReset(bool still_empty, bool had_progress)
{
  if (!still_empty)
  {
    return true;
  }
  return had_progress;
}

/// Era27 I-A3: MarkRelit must not RemeshSeam/Dirty-storm SoftDefer-empty
/// undrawn columns that already have FirstMesh / PendingReplace ownership.
/// Drawable lit→relit seam remesh stays KEEP unless cruise ingress damp.
inline bool ShouldDampMarkRelitRemeshOnSoftDeferEmpty(
    bool soft_defer_empty_owned, bool has_drawable,
    bool damp_cruise_ingress = false)
{
  if (damp_cruise_ingress)
  {
    return true;
  }
  if (has_drawable)
  {
    return false;
  }
  return soft_defer_empty_owned;
}

/// I15-C1: stand VB debt — damp seam remesh storm without cruise ingress.
inline bool ShouldDampMarkRelitRemeshOnStandVbDebt(bool moving,
                                                   int vb_no_ticket_n,
                                                   int vb_focus_n,
                                                   int focus_floor = 15)
{
  return !moving && vb_no_ticket_n > 0 && vb_focus_n >= focus_floor;
}

/// Era27 I-A4: under miss/undrawn, do not ForgetInflight / supersede a live
/// FirstMesh Inflight or PendingReplace path that would leave a hole frame.
inline bool ShouldHoldInflightSupersedeUnderMiss(bool miss_or_soft_undrawn,
                                                 bool has_inflight_or_pending,
                                                 bool has_drawable)
{
  return miss_or_soft_undrawn && has_inflight_or_pending && !has_drawable;
}

/// Era39: SoftDefer empty ownership sticks until healed (Drawable / left rim).
inline bool SoftDeferEmptyShouldKeepOwnership(bool still_empty,
                                              bool had_ownership)
{
  return still_empty && had_ownership;
}

/// Era39: after SoftDeferEmptyPublishAvoided, do not Dirty-storm while FM /
/// Inflight / Pending owns the column, or until min_frames elapse.
inline bool SoftDeferEmptyShouldMarkDirtyAfterAvoid(bool has_fm_or_pending,
                                                    int frames_since_avoid,
                                                    int min_frames = 4)
{
  if (has_fm_or_pending)
  {
    return false;
  }
  return frames_since_avoid >= min_frames;
}

/// Era39: recount SoftDefer empty every frame; apply MarkDirty / FirstMesh
/// ownership only when UndrawnForceCd is ready.
inline bool SoftDeferEmptyShouldApplyOwnership(bool cd_ready)
{
  return cd_ready;
}

/// Era39: remesh a drawable face-neighbor when SoftDefer-hidden neighbor
/// appears or heals (enter/leave empty set).
inline bool ShouldRemeshDrawableForHiddenNeighborSeam(bool has_drawable,
                                                      bool neighbor_hidden_now,
                                                      bool neighbor_hidden_prev)
{
  if (!has_drawable)
  {
    return false;
  }
  return neighbor_hidden_now != neighbor_hidden_prev;
}

} // namespace cutum
