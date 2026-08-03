#ifndef INFLUENCE_INTENT_UTIL_H
#define INFLUENCE_INTENT_UTIL_H

#include "Creatures/Core/CreatureIntent.h"

namespace cutum
{

/// Deprecated shim kept for transitional call sites; prefer writing
/// Influence.Channel/TargetId directly (or PlayerInteractionRouter).
inline void SyncInfluenceFromAttackTarget(CreatureIntent &intent)
{
  if (intent.Influence.TargetId == 0 && intent.attackTargetId != 0)
  {
    intent.Influence.TargetId = intent.attackTargetId;
  }
  intent.attackTargetId = 0;
  if (intent.Influence.Channel == InfluenceChannel::None &&
      intent.Influence.TargetId != 0)
  {
    intent.Influence.Channel = InfluenceChannel::Melee;
  }
}

inline void SetMeleeInfluenceIntent(CreatureIntent &intent, uint64_t targetId)
{
  intent.attackTargetId = 0;
  intent.Influence = InfluenceIntent{};
  intent.Influence.Channel = InfluenceChannel::Melee;
  intent.Influence.TargetId = targetId;
}

} // namespace cutum

#endif
