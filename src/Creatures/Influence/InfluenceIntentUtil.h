#ifndef INFLUENCE_INTENT_UTIL_H
#define INFLUENCE_INTENT_UTIL_H

#include "Creatures/Core/CreatureIntent.h"

namespace cutum
{

inline void SyncInfluenceFromAttackTarget(CreatureIntent &intent)
{
  if (intent.attackTargetId == 0 && intent.Influence.TargetId == 0)
  {
    return;
  }
  if (intent.Influence.TargetId == 0)
  {
    intent.Influence.TargetId = intent.attackTargetId;
  }
  if (intent.attackTargetId == 0)
  {
    intent.attackTargetId = intent.Influence.TargetId;
  }
  if (intent.Influence.Channel == InfluenceChannel::None)
  {
    intent.Influence.Channel = InfluenceChannel::Melee;
  }
}

} // namespace cutum

#endif
