#include "Creatures/Influence/BareHandToolInfluenceProvider.h"
#include "Creatures/Core/Creature.h"
#include <algorithm>
#include <cmath>

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
  const int strength = source.GetAttributes().strength;
  const int fleshy =
      std::max(1, static_cast<int>(std::lround(
                      8.f * (0.5f + static_cast<float>(strength) / 20.f))));
  out = InfluenceCapability::DefaultBareHand(fleshy);
  return true;
}

} // namespace cutum
