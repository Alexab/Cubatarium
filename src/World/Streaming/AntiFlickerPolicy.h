#pragma once

namespace cutum
{

/// Era27 I-A1: SoftDefer Capture witness pin length (frames).
constexpr int kSoftDeferCaptureWitnessPinFrames = 8;

/// Era27 I-A1: retarget Capture witness only when pin invalid/expired, horiz
/// improved, or pinned column no longer SoftDefer-empty/miss.
inline bool ShouldRetargetSoftDeferCaptureWitness(
    bool pin_valid, int pin_age_frames, int pin_T,
    bool new_witness_better_horiz, bool pinned_still_empty_or_miss)
{
  if (!pin_valid)
  {
    return true;
  }
  if (pin_age_frames >= pin_T)
  {
    return true;
  }
  if (new_witness_better_horiz)
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

} // namespace cutum
