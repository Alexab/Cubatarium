#ifndef INFLUENCE_INTENT_UTIL_H
#define INFLUENCE_INTENT_UTIL_H

#include "Creatures/Core/CreatureIntent.h"

namespace cutum
{

inline void SetMeleeInfluenceIntent(CreatureIntent &intent, uint64_t targetId)
{
  intent.attackTargetId = 0;
  intent.Influence = InfluenceIntent{};
  intent.Influence.Channel = InfluenceChannel::Melee;
  intent.Influence.TargetId = targetId;
}

} // namespace cutum

#endif
