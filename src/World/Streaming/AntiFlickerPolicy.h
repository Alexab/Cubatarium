#pragma once

namespace cutum
{

/// Era27 I-A1: SoftDefer Capture witness pin length (frames).
constexpr int kSoftDeferCaptureWitnessPinFrames = 8;

/// Era27 I-A1: retarget Capture witness only when pin invalid/expired, or
/// pinned column no longer SoftDefer-empty/miss. CheapRemesh C4: sticky —
/// better_horiz alone must not hop while pin age < SLA (hold_nh2 still wins
/// via ShouldRetargetRelightWitness).
inline bool ShouldRetargetSoftDeferCaptureWitness(
    bool pin_valid, int pin_age_frames, int pin_T,
    bool new_witness_better_horiz, bool pinned_still_empty_or_miss)
{
  (void)new_witness_better_horiz;
  if (!pin_valid)
  {
    return true;
  }
  if (pin_age_frames >= pin_T)
  {
    return true;
  }
  if (!pinned_still_empty_or_miss)
  {
    return true;
  }
  return false;
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
/// Drawable lit→relit seam remesh stays KEEP.
inline bool ShouldDampMarkRelitRemeshOnSoftDeferEmpty(
    bool soft_defer_empty_owned, bool has_drawable)
{
  if (has_drawable)
  {
    return false;
  }
  return soft_defer_empty_owned;
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
