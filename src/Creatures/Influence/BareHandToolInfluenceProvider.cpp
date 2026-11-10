#include "Creatures/Influence/BareHandToolInfluenceProvider.h"
#include "Creatures/Core/Creature.h"
#include "Items/ToolCapabilities.h"
#include <algorithm>

namespace cutum
{

bool UBareHandToolInfluenceProvider::TryGetCapability(
    const UCreature &source, InfluenceChannel channel,
    InfluenceCapability &out) const
{
  if (channel != InfluenceChannel::Melee && channel != InfluenceChannel::None)
  {
    return false;
  }
  const int fleshy = source.GetBareHandFleshyOverride() > 0
                         ? source.GetBareHandFleshyOverride()
                         : BareHandFleshyDamage(source.GetAttributes());
  out = InfluenceCapability::DefaultBareHand(fleshy);
  if (source.GetBareHandIntervalOverride() > 0.f)
  {
    out.FullIntervalSec = source.GetBareHandIntervalOverride();
  }
  return true;
}

} // namespace cutum
