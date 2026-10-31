#ifndef CREATURE_FEET_SNAP_H
#define CREATURE_FEET_SNAP_H

namespace cutum
{

/// Whether a walking creature's feet snap from `refFeetY` (where the motor
/// left them) to the footprint ground `groundY`. Grounded (anchored) bodies
/// always follow the ground, others heal brief float/sink states within
/// `maxSnap` so the habitat gate cannot trap them above air. Never to a height
/// where the box overlaps blocks (`clearAtGround` false): the footprint is
/// sampled inside its edges, so a body whose edge still hung over a ledge
/// snapped a block down into the ledge and was then shoved out sideways.
inline bool ShouldSnapFeetToGround(float refFeetY, float groundY, float maxSnap,
                                   bool anchoredGround, bool clearAtGround)
{
  if (!clearAtGround)
  {
    return false;
  }
  const float drop = refFeetY - groundY;
  const float climb = groundY - refFeetY;
  return anchoredGround || (drop >= 0.0f && drop <= maxSnap) ||
         (climb >= 0.0f && climb <= maxSnap);
}

} // namespace cutum

#endif
